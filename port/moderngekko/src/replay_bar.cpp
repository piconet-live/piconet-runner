// The replay scrubber's bar (fzgx/host.h, ScrubBar), in a playback with the scrubber and in a practice: drawn OVER
// the game's picture along its bottom (overlay.h), so showing it never changes the window's or the picture's size.
// It fades in (120 ms, the motion rule) when the mouse comes near the picture's bottom, and out when the mouse
// leaves or stands still a while; it stays while the mouse button is held on it, and in a practice while the
// picture stands (paused or counting down). From the left: the race's time at the picture (and its length once the
// replay's end is known), figures on one pitch; the track: played in the accent, buffered a tone up, not yet the
// raised tone, a gap at each lap the leader completed; the play mark, an upright bar while it plays and a round
// handle while the picture stands, red (danger) while a scrub's pointer is beyond what can be shown; at the right
// the picture's frame. Under the pointer the track thickens, a thin line marks the pointer and a soft-cornered box
// above it says the time and frame there. The palette and the type are the launcher's (overlay.h).
//   - Pressing the mouse button on the track begins a scrub: while it is held (the game's window keeps the mouse)
//     the core is told the step under the pointer each frame; a press let go at once is a jump there.
//   - What it shows comes from the core once a frame (ShowReplayBar, the game's thread); it is drawn before every
//     present (DrawReplayBar, the thread that presents); the mouse comes from the game window's messages
//     (ReplayBarMouse, the window's thread, in the picture's pixels). SetCapture and ReleaseCapture send
//     WM_CAPTURECHANGED to the window at once, on this thread, and it comes back here: they are called with no lock
//     held (a second lock of the bar's std::mutex on one thread throws, and the game aborted: crash-233532).
//   - The sound gate (replay_audio.cpp) ticks on a timer of a hidden child window of the game's, made here.
// Nothing here reads or writes the game.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "adapter_internal.h"
#include "overlay.h"
#include "replay_bar.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <mutex>

namespace fzgx_port
{
namespace
{
namespace ov = overlay;
using Clock = std::chrono::steady_clock;
constexpr fzgx::u32 NONE = 0xFFFFFFFF;
constexpr float NEAR_PX = 140;    // the mouse this near the picture's bottom (at 720 lines) brings the bar
constexpr float IDLE_S = 2.0f;    // ... until it stands still this long

struct Bar
{
  std::mutex lock;          // all below but the atomics
  fzgx::ScrubBar told;
  bool told_once = false;
  std::atomic<fzgx::u32> pointer{NONE}, pressed{NONE};  // the step under the pointer while held; a press not yet told
  std::atomic<bool> held{false};
  float width = 0, height = 0;    // the picture as last drawn
  float mouse_x = -1, mouse_y = -1;
  Clock::time_point moved{};
  float lanes = 0;                 // the timeline lanes' height above the bar (replay_lanes.cpp), as last drawn
  ov::Eased shown, open;           // the bar's opacity; the track's thickness under the pointer
#ifdef _WIN32
  HWND ticks = nullptr;            // the hidden child window of the sound gate's timer
#endif
};
Bar s_bar;

bool BarOn()
{
  return s_state.options.replay_scrubber || s_state.options.practice;
}

// Where the bar is, in a picture `w` by `h`.
struct Layout
{
  float s = 1;
  ov::Box bar, track;
  float centre = 0;
  bool narrow = false, no_frame = false;
};

Layout Lay(float w, float h)
{
  Layout l;
  l.s = ov::Scale(h);
  const float s = l.s, height = 44 * s, side = 16 * s;
  l.narrow = w < 560 * s;
  l.no_frame = w < 640 * s;
  l.bar = {0, h - height, w, h};
  const float left = side + (l.narrow ? 72 : 140) * s;
  const float right = w - side - (l.no_frame ? 0 : 104 * s + side);
  l.track = {left, l.bar.y0, std::max(left + 1, right), h};
  l.centre = l.bar.y0 + 27 * s;
  return l;
}

float PlaceOf(const fzgx::ScrubBar& bar, fzgx::u32 step, const ov::Box& track)
{
  const fzgx::u32 span = std::max<fzgx::u32>(bar.last - bar.first, 1);
  const fzgx::u32 at = std::clamp(step, bar.first, bar.first + span) - bar.first;
  return track.x0 + (track.x1 - track.x0) * static_cast<float>(at) / static_cast<float>(span);
}

fzgx::u32 StepAt(const fzgx::ScrubBar& bar, float x, const ov::Box& track)
{
  const fzgx::u32 span = std::max<fzgx::u32>(bar.last - bar.first, 1);
  const float p = std::clamp((x - track.x0) / std::max(track.x1 - track.x0, 1.0f), 0.0f, 1.0f);
  return bar.first + static_cast<fzgx::u32>(p * static_cast<float>(span) + 0.5f);
}

// "1:02.3": minutes, seconds, tenths, of a frame count at 60 a second.
void Clock10(char* out, std::size_t size, unsigned frame)
{
  const unsigned tenths = frame / 6;
  std::snprintf(out, size, "%u:%02u.%u", tenths / 600, tenths / 10 % 60, tenths % 10);
}

// The bar is wanted: held, the mouse near the bottom and moving, or a practice's picture standing.
bool Wanted(const Layout& l, Clock::time_point now)
{
  if (s_bar.held.load() || (s_state.options.practice && s_bar.told.standing))
    return true;
  if (s_bar.mouse_y >= l.bar.y0 - s_bar.lanes && s_bar.mouse_y <= s_bar.height && s_bar.mouse_x >= 0)
    return true;  // (over the bar or its lanes it stays)
  const float near_y = l.bar.y0 - s_bar.lanes - NEAR_PX * l.s;
  return s_bar.mouse_y >= near_y && s_bar.mouse_y <= s_bar.height &&
         std::chrono::duration<float>(now - s_bar.moved).count() < IDLE_S;
}

#ifdef _WIN32
LRESULT CALLBACK TicksProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
  if (message == WM_TIMER)
  {
    ReplaySoundTick(window, wparam);
    return 0;
  }
  return DefWindowProcW(window, message, wparam, lparam);
}
#endif
}  // namespace

void ShowReplayBar(const fzgx::ScrubBar& bar)
{
  {
    std::lock_guard<std::mutex> hold(s_bar.lock);
    s_bar.told = bar;
    s_bar.told_once = true;
  }
  ReplaySoundHeard(bar.heard);
}

fzgx::u32 TakeReplayScrub()
{
  const fzgx::u32 pressed = s_bar.pressed.exchange(NONE);
  return s_bar.held.load() ? s_bar.pointer.load() : pressed;
}

void SetReplayLanesHeight(float height)
{
  std::lock_guard<std::mutex> hold(s_bar.lock);
  s_bar.lanes = height;
}

float ReplayLanesHeight()
{
  std::lock_guard<std::mutex> hold(s_bar.lock);
  return s_bar.lanes;
}

float DrawReplayBar(BarPlace* place)
{
  if (!BarOn() || ImGui::GetCurrentContext() == nullptr)
    return 0;
  const ImVec2 size = ImGui::GetIO().DisplaySize;
  const Clock::time_point now = Clock::now();
  std::lock_guard<std::mutex> hold(s_bar.lock);
  if (!s_bar.told_once)
    return 0;
  s_bar.width = size.x;
  s_bar.height = size.y;
  const Layout l = Lay(size.x, size.y);
  s_bar.shown.Send(Wanted(l, now) ? 1.0f : 0.0f, now);
  const float a = s_bar.shown.At(now);
  if (place)
  {
    place->x0 = l.track.x0;
    place->x1 = l.track.x1;
    place->top = l.bar.y0;
    place->width = size.x;
    place->s = l.s;
    place->first = s_bar.told.first;
    place->last = s_bar.told.last;
  }
  if (a <= 0.004f)
    return 0;
  const fzgx::ScrubBar& bar = s_bar.told;
  const float s = l.s;
  ImDrawList* list = ImGui::GetForegroundDrawList();
  list->AddRectFilled(ImVec2(l.bar.x0, l.bar.y0), ImVec2(l.bar.x1, l.bar.y1), ov::Colour(ov::BAR, 0.84f * a));
  // the track: not yet buffered, buffered, played; the leader's laps as gaps
  const bool over = s_bar.mouse_y >= l.bar.y0 && s_bar.mouse_x >= l.track.x0 - 8 * s && s_bar.mouse_x <= l.track.x1 + 8 * s;
  s_bar.open.Send(over || s_bar.held.load() ? 1.0f : 0.0f, now);
  const float thick = (4 + 4 * s_bar.open.At(now)) * s;
  const float y0 = l.centre - thick / 2, y1 = l.centre + thick / 2;
  const float played = PlaceOf(bar, bar.played, l.track), buffered = PlaceOf(bar, bar.buffered, l.track);
  list->AddRectFilled(ImVec2(l.track.x0, y0), ImVec2(l.track.x1, y1), ov::Colour(ov::RAISED, a));
  list->AddRectFilled(ImVec2(l.track.x0, y0), ImVec2(buffered, y1), ov::Colour(ov::SELECT, a));
  list->AddRectFilled(ImVec2(l.track.x0, y0), ImVec2(played, y1), ov::Colour(ov::ACCENT, a));
  for (fzgx::u32 i = 0; i < std::min<fzgx::u32>(bar.laps, 16); ++i)
  {
    const float x = PlaceOf(bar, bar.lap_at[i], l.track);
    list->AddRectFilled(ImVec2(x - 1.5f * s, y0 - 1), ImVec2(x + 1.5f * s, y1 + 1), ov::Colour(ov::BAR, a));
  }
  // the play mark: the handle round (the launcher's knobs), the playing bar upright
  const ImU32 mark = ov::Colour(bar.beyond ? ov::DANGER : ov::TEXT, a);
  if (bar.standing)
    list->AddRectFilled(ImVec2(played - 6 * s, l.centre - 6 * s), ImVec2(played + 6 * s, l.centre + 6 * s), mark,
                        ov::CORNER_SMALL * s);
  else
    list->AddRectFilled(ImVec2(played - 3 * s, l.centre - 8 * s), ImVec2(played + 3 * s, l.centre + 8 * s), mark);
  // the readouts
  char words[64], length[24];
  const unsigned frame = bar.played > bar.first ? bar.played - 1 - bar.first : 0;
  const unsigned frames = bar.last > bar.first ? bar.last - bar.first : 0;
  Clock10(words, sizeof(words), frame);
  const ov::Type big = ov::Figure(17 * s), little = ov::Figure(13 * s), label = ov::Body(13 * s);
  const float text_y = l.centre - big.size / 2 - 1 * s;
  ov::Draw(list, big, 16 * s, text_y, ov::Colour(ov::TEXT, a), words);
  if (!l.narrow && bar.end_known)
  {
    Clock10(length, sizeof(length), frames);
    char of[32];
    std::snprintf(of, sizeof(of), "/ %s", length);
    ov::Draw(list, little, 16 * s + ov::Width(big, words) + 6 * s, text_y + 3 * s, ov::Colour(ov::FAINT, a), of);
  }
  if (!l.no_frame)
  {
    std::snprintf(words, sizeof(words), "%u", frame);
    const float right = l.bar.x1 - 16 * s, wide = ov::Width(big, words);
    ov::Draw(list, big, right - wide, text_y, ov::Colour(ov::TEXT, a), words);
    ov::Draw(list, label, right - wide - 6 * s - ov::Width(label, "frame"), text_y + 3 * s, ov::Colour(ov::FAINT, a),
             "frame");
  }
  // under the pointer: a thin line and the time there
  if (over || s_bar.held.load())
  {
    const float x = std::clamp(s_bar.mouse_x, l.track.x0, l.track.x1);
    const fzgx::u32 step = StepAt(bar, x, l.track);
    const unsigned at = step > bar.first ? step - 1 - bar.first : 0;
    list->AddLine(ImVec2(x, l.centre - 10 * s), ImVec2(x, l.centre + 10 * s), ov::Colour(ov::TEXT, 0.8f * a), 1);
    char tip[48];
    Clock10(words, sizeof(words), at);
    std::snprintf(tip, sizeof(tip), "%s  frame %u", words, at);
    const ov::Type read = ov::Body(13 * s, true);
    const float tw = ov::Width(read, tip), bx = std::clamp(x - tw / 2 - 6 * s, 0.0f, size.x - tw - 12 * s);
    const float by = l.bar.y0 - 26 * s - s_bar.lanes;
    list->AddRectFilled(ImVec2(bx, by), ImVec2(bx + tw + 12 * s, by + 22 * s), ov::Colour(ov::SURFACE, 0.92f * a),
                        ov::CORNER_SMALL * s);
    ov::Draw(list, read, bx + 6 * s, by + 4 * s, ov::Colour(step < bar.buffered ? ov::TEXT : ov::FAINT, a), tip);
  }
  return a;
}

bool ReplayBarMouse(void* window, unsigned message, float x, float y)
{
  if (!BarOn())
    return false;
#ifdef _WIN32
  // the press ends (no lock: `held` is atomic, and ReleaseCapture sends WM_CAPTURECHANGED back here at once)
  if (message == WM_LBUTTONUP || message == WM_CAPTURECHANGED)
  {
    const bool was = s_bar.held.exchange(false);
    if (was && message == WM_LBUTTONUP)
      ReleaseCapture();
    return was;
  }
#endif
  bool capture = false;
  {
    std::lock_guard<std::mutex> hold(s_bar.lock);
    const Clock::time_point now = Clock::now();
    if (message == 0x0200)  // WM_MOUSEMOVE
    {
      if (x != s_bar.mouse_x || y != s_bar.mouse_y)
        s_bar.moved = now;
      s_bar.mouse_x = x;
      s_bar.mouse_y = y;
      if (s_bar.held.load())
        s_bar.pointer = StepAt(s_bar.told, x, Lay(s_bar.width, s_bar.height).track);
      return s_bar.held.load();
    }
    if (message == 0x02A3)  // WM_MOUSELEAVE
    {
      s_bar.mouse_x = s_bar.mouse_y = -1;
      return false;
    }
    const Layout l = Lay(s_bar.width, s_bar.height);
#ifdef _WIN32
    if (message != WM_LBUTTONDOWN)
      return y >= l.bar.y0 && s_bar.shown.to >= 1;  // (other presses on the bar are the bar's)
    // a press counts on the track and the room around it, and only while the bar is (coming) up
    if (!s_bar.told_once || s_bar.shown.to < 1 || y < l.bar.y0 || x < l.track.x0 - 8 * l.s || x > l.track.x1 + 8 * l.s)
      return false;
    const fzgx::u32 step = StepAt(s_bar.told, x, l.track);
    s_bar.pointer = step;
    s_bar.pressed = step;
    capture = true;
#else
    (void)window, (void)l, (void)capture;
    return false;
#endif
  }
#ifdef _WIN32
  if (capture)
  {
    SetCapture(static_cast<HWND>(window));  // (after the lock, and before `held`: it may send WM_CAPTURECHANGED)
    s_bar.held = true;
  }
#endif
  return capture;
}

int ReplayBarHeight(int)
{
  return 0;  // (drawn over the picture: it takes no room of the window)
}

void MakeReplayBar(void* parent)
{
#ifdef _WIN32
  if (!BarOn() || s_bar.ticks)
    return;
  if (s_state.options.practice)
    ReplaySoundHeard(true);  // (a practice is heard from its start: its gate shuts only while it stands)
  WNDCLASSEXW ticks{};
  ticks.cbSize = sizeof(ticks);
  ticks.lpfnWndProc = TicksProc;
  ticks.hInstance = GetModuleHandleW(nullptr);
  ticks.lpszClassName = L"FzgxReplayTicks";
  RegisterClassExW(&ticks);
  // A hidden child of the game's window, not a message-only window: the emulator's window loop takes only that
  // window's messages (PlatformWin32::ProcessEvents, PeekMessage with its handle, which takes its children's too),
  // so a message-only window's timer never ticked and the gate's volume stayed at the 0 the load's end set
  // (ReplaySoundVolumeReset): practice and replays were silent from 9a9b4de2 (review-031) on.
  s_bar.ticks = CreateWindowExW(0, ticks.lpszClassName, L"", WS_CHILD, 0, 0, 0, 0, static_cast<HWND>(parent),
                                nullptr, GetModuleHandleW(nullptr), nullptr);
  if (s_bar.ticks)
    SetTimer(s_bar.ticks, 0x4252, 33, nullptr);
#endif
}

void LayReplayBar(int, int, int)
{
}
}  // namespace fzgx_port
