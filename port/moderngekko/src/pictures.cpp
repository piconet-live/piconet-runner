// When a picture goes to the screen, and the times between the pictures.
// The game's loop copies a finished picture out of its frame buffer right after the field's start (GXCopyDisp, then
// the pad, the step, the drawing, the wait for the next field). A console puts that picture's first line out 58
// half-lines (1.84 ms) after the field's start: the game's 448-line picture (measured: the present of "console"
// sits 2.40 ms after the field's record, which the pad poll's 18 half-lines come before).
// The launch file's "present" names three ways:
//   "console"  every player's, and a file without the key: at the first line's own time on the host's clock. The
//              emulator works that time out for every field (VideoBackendBase.cpp, Video_OutputXFB: the next swap's
//              estimated time, by the same map of emulated to real time its waits use) and gives it to the swap as
//              its intended time. The runner sets the emulator's own "Immediately Present XFB", which hands the
//              picture over from the copy itself (VideoCommon/BPStructs.cpp, ImmediateSwap), and gives the
//              emulator the picture's time (Presenter::SetImmediateSwapHold, our patch of RecompCore): the picture
//              is held, and the GPU thread's loop presents it once that time has come, between its passes over the
//              game's commands or from its idle wait, which the time bounds and new commands end at once
//              (VideoCommon/Fifo.cpp, RunGpuLoop). A copy that comes after that time (a long frame) is presented
//              at once: nothing is skipped, nothing is held longer than the limit (fzgx/present.h holds the three
//              ways and the limit, where they are tested). A game that runs faster than real time (a playback as a
//              check) is given "now" as that time by the emulator, and nothing is held.
//              No thread waits for the time. A GPU thread that waits for it, at the copy inside its pass over the
//              game's commands or after that pass, races the game's switch of its two command buffers with the
//              GPU's lag, which then reads garbage ("FIFO: Unknown Opcode", every picture broken: 0.2.2's race
//              m23).
//   "field"    the emulator hands the copy to the screen when its emulated video output
//              reaches that line; the game's thread is by then deep in the next pass, and emulated time runs slower
//              than real time while it works, so the moment comes about 4.5 to 6 ms of real time after the field's
//              start, and moves with the pass's work. Kept for comparison, and a single-core start's way
//              (present::WayOf: there the wait would hold the game's own thread).
//   "copy"     at once, when the copy is made: about 0.1 ms into the field, sooner than a console. For comparison.
// Each is a setting of when the host shows a picture: the copy, the game's memory and the emulated clock are as
// before, and the emulated CPU's thread still waits for real time at its two pad polls a field, the second of which
// is the last wait before the game's pass, so the time sync's clock reads fall where they fell.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "adapter_internal.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <optional>
#include <string>

#include "Common/Config/Config.h"
#include "Common/HookableEvent.h"
#include "Core/Config/GraphicsSettings.h"
#include "VideoCommon/Present.h"
#include "VideoCommon/VideoEvents.h"

#include "fzgx/hfr.h"
#include "fzgx/present.h"
#include "threads.h"

namespace fzgx_port
{
namespace
{
// The last pictures' times, kept by the thread that presents and taken by the game's thread once a displayed frame:
// a field has one picture, so a few places are room for a long frame's; when they are full the oldest goes.
std::mutex s_pictures_mutex;
std::array<fzgx::u32, 16> s_intervals{};
fzgx::u32 s_interval_count = 0;
std::chrono::steady_clock::time_point s_last_picture{};
Common::EventHook s_picture_hook, s_moment_hook, s_screen_hook;

// "console": pictures held for the console's moment, that came after it and were shown at once, and whose intended
// time was further off than the limit (shown at once); for the log's line when the game stops
std::atomic<fzgx::u64> s_held{0}, s_after{0}, s_far{0};
bool s_holds = false;  // this start's way is "console"

// "console", at the copy on the GPU thread: the picture is held for its first line's time, or shown at once
// (fzgx/present.h: the three ways). Nothing waits here: the emulator's GPU loop presents it when the time has come.
// The high refresh rate (fzgx/hfr.h, drawn_frames.cpp): the last true picture's console moment, for the ones after it
TimePoint s_last_console{};
bool s_drawn = false;  // this start shows pictures between steps
// How late each picture came to the presenter against its moment (a true one: its console moment; one between: the
// last true one's moment and its fraction of a field), in 1 ms bins from -20 ms, for the log's line at the stop
constexpr int LATE_BINS = 64, LATE_FROM_MS = -20;
std::array<fzgx::u64, LATE_BINS> s_late_true{}, s_late_between{};

void CountLate(std::array<fzgx::u64, LATE_BINS>& bins, TimePoint moment)
{
  const long long us = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - moment).count();
  const long long bin = (us >= 0 ? us / 1000 : -((-us + 999) / 1000)) - LATE_FROM_MS;
  ++bins[static_cast<size_t>(std::clamp<long long>(bin, 0, LATE_BINS - 1))];
}

// "p50 a, p90 b, p99 c, p99.9 d, most e" in ms (each the bin's upper edge), and how many
std::string LateLine(const std::array<fzgx::u64, LATE_BINS>& bins)
{
  fzgx::u64 n = 0;
  for (fzgx::u64 b : bins)
    n += b;
  if (n == 0)
    return "none";
  std::string line;
  const double shares[] = {0.5, 0.9, 0.99, 0.999, 1.0};
  const char* names[] = {"p50", "p90", "p99", "p99.9", "most"};
  for (int k = 0; k < 5; ++k)
  {
    fzgx::u64 seen = 0;
    int bin = 0;
    for (; bin < LATE_BINS; ++bin)
    {
      seen += bins[bin];
      if (static_cast<double>(seen) >= shares[k] * static_cast<double>(n))
        break;
    }
    line += std::string(k ? ", " : "") + names[k] + " " + std::to_string(bin + LATE_FROM_MS + 1);
  }
  return line + " ms of " + std::to_string(n);
}

std::optional<TimePoint> TheMoment(const PresentInfo& picture)
{
  TimePoint due = picture.intended_present_time;
  long long most = fzgx::present::MOST_WAIT_US;
  if (s_drawn)
  {
    // the step's own picture is held for its console moment as at 60 Hz; the pictures after it are its frame drawn
    // again right after its copy (VideoCommon/FrameRedraw.h), each a guess for its fraction of a field after that
    // moment, and held for that time
    if (picture.in_between >= 0 && s_last_console != TimePoint{})
    {
      most = fzgx::present::MOST_WAIT_DRAWN_US;
      due = s_last_console +
            std::chrono::microseconds(static_cast<long long>(picture.in_between * fzgx::present::FIELD_US));
      CountLate(s_late_between, due);
    }
    else
    {
      CountLate(s_late_true, due);
      s_last_console = due;
    }
  }
  const auto left = std::chrono::duration_cast<std::chrono::microseconds>(due - Clock::now());
  switch (fzgx::present::At(left.count(), most))
  {
  case fzgx::present::Moment::After: ++s_after; return std::nullopt;
  case fzgx::present::Moment::Far: ++s_far; return std::nullopt;
  case fzgx::present::Moment::Wait: ++s_held; return due;
  }
  return std::nullopt;
}
}  // namespace

// After PinSettings, which sets the emulator's own values (Immediately Present XFB off, vertical sync as the launch
// file says) unless the launch file leaves the settings to the user folder's files.
void ApplyPresentation()
{
  const fzgx::Options& o = s_state.options;
  // (a start whose settings are the files': the launch file's vertical sync still holds when it is on)
  if (o.vertical_sync)
    Config::SetCurrent(Config::GFX_VSYNC, true);
  // what the way needs of the emulator (fzgx/present.h, SetupOf, where the table is tested; the gates' tests hold
  // these lines to it): its own "Immediately Present XFB" for "console" and "copy", the hold for "console"
  const fzgx::present::Setup setup = fzgx::present::SetupOf(fzgx::present::WayOf(o.present, o.debug.single_core));
  if (setup.immediate_xfb)
    Config::SetCurrent(Config::GFX_HACK_IMMEDIATE_XFB, true);
  s_holds = setup.holds;
  s_held = s_after = s_far = 0;
  s_last_console = {};
  s_late_true.fill(0);
  s_late_between.fill(0);
  s_drawn = setup.holds && fzgx::hfr::On();
  if (setup.holds)
    VideoCommon::Presenter::SetImmediateSwapHold(TheMoment);
  // before every present, on the thread that presents: the panel, then the window's full screen as the backend's own
  // (screen.cpp), asked right before the present that acts on it
  s_moment_hook = GetVideoEvents().before_present_event.Register([](const PresentInfo&) {
    DrawPanel();  // (panel.cpp: before a race only; one load of a flag otherwise)
    DrawOverlays();  // (spectator_overlay.cpp: the course map, the replay bar, its lanes, the standings)
    FollowFullscreen();
  });
}

void WatchPictures()
{
  {
    std::lock_guard<std::mutex> guard(s_pictures_mutex);
    s_interval_count = 0;
    s_last_picture = {};
  }
  s_picture_hook = GetVideoEvents().after_present_event.Register([](const PresentInfo&) {
    threads::KnowThisThread(threads::Which::Picture);  // (for the freeze report: one flag read after the first)
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> guard(s_pictures_mutex);
    if (s_last_picture != std::chrono::steady_clock::time_point{})
    {
      if (s_interval_count == s_intervals.size())
      {
        std::copy(s_intervals.begin() + 1, s_intervals.end(), s_intervals.begin());
        --s_interval_count;
      }
      const auto us = std::chrono::duration_cast<std::chrono::microseconds>(now - s_last_picture).count();
      s_intervals[s_interval_count++] = static_cast<fzgx::u32>(std::min<long long>(us, 0xFFFFFFFFll));
    }
    s_last_picture = now;
  });
  // (outside the lock: what the swap chain is, into the log when it changed)
  s_screen_hook = GetVideoEvents().after_present_event.Register([](const PresentInfo&) { SayTheScreen(); });
}

void StopWatchingPictures()
{
  s_picture_hook.reset();
  s_screen_hook.reset();
  s_moment_hook.reset();
  VideoCommon::Presenter::SetImmediateSwapHold({});
  ForgetTheScreen();
  if (!s_holds)
    return;
  s_holds = false;
  char text[240];
  std::snprintf(text, sizeof(text),
                "fzgx: presentation \"console\": %llu pictures held for their first line's time, %llu came after it and "
                "were shown at once, %llu had a time more than 4 ms off and were shown at once",
                static_cast<unsigned long long>(s_held.load()), static_cast<unsigned long long>(s_after.load()),
                static_cast<unsigned long long>(s_far.load()));
  s_core_host.Log(fzgx::LogLevel::Notice, text);
  if (s_drawn)
  {
    // (how late against their moments: a picture that came after its own was shown late)
    const std::string line = "fzgx: pictures between steps: the true pictures came " + LateLine(s_late_true) +
                             " after their console moment, the pictures between " + LateLine(s_late_between) +
                             " after the last true one's moment and their fraction of a field";
    s_core_host.Log(fzgx::LogLevel::Notice, line);
  }
}

fzgx::u32 TakePictureIntervals(fzgx::u32* out, fzgx::u32 most)
{
  std::lock_guard<std::mutex> guard(s_pictures_mutex);
  const fzgx::u32 n = std::min(s_interval_count, most);
  std::copy(s_intervals.begin(), s_intervals.begin() + n, out);
  std::copy(s_intervals.begin() + n, s_intervals.begin() + s_interval_count, s_intervals.begin());
  s_interval_count -= n;
  return n;
}
}  // namespace fzgx_port
