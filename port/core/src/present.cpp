// A late frame's drawing skipped. In a race frame whose steps
// leave no room in the field for its drawing, the image draws nothing (sites_present.py: the draw half of
// fn_1_37AA8, the main loop's drawing tail, its depth read-back) and the next frame's begin copies no picture: the
// picture before stays on the screen one field more, as a late frame loses one today. Nothing a step reads is
// touched: the drawing's writes are presentation's own (the study's section 4: a whole race on memory no
// presentation ever wrote matches its recording at every step), so a game that skips and one that does not agree on
// every checksum, and recordings play the same either way.
//
// The moments, all on the game's thread: the frame tick (the mode machine's entry, before the frame's steps) starts
// the frame's clock and takes the last answer back; the network hook's NormalStep says whether the frame is a race
// frame that may skip (the rollback path: not lockstep, not the frame that settles or checks the race, not the
// replay scrubber's); the image's two stores into DRAW_ASK are the drawing's start (we answer in DRAW_SKIP before the
// image reads it) and its end (after the GPU's draw-done wait and the depth read-back: the drawing's whole cost on
// this thread), which times the drawing of a frame that drew.
//
// The rule: skip when the time since the tick, plus the drawing as it has cost lately, plus a margin for the frame's
// head and tail outside that window, passes a field; never in two frames running, so a picture of every second frame
// is drawn however busy the game is; never on course 41 (the drawing there places scenery with a collider: the
// study's 4.4). Tests: "debug.skipDraws" N skips the drawing of every Nth race frame whatever its time.
//
// Never while the high refresh rate draws pictures between steps (fzgx/hfr.h On: a refresh rate above 60, or the
// test's drawnFrames): there the frame's own drawing is what the re-draw takes, and a frame that drew nothing would
// leave it no frame (review-036 made the two exclusive until a window run proves them together). Later: a frame held
// this way is where the presenter could draw the held picture again with the corrected camera; Host::PictureHeld is
// that hook, called for every skipped frame.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "net_internal.h"

#include "fzgx/hfr.h"

#include <algorithm>
#include <chrono>

namespace fzgx::net
{
namespace
{
constexpr u32 DRAW_ASK = layout::DRAW_ASK;    // the image's: 1 the drawing begins, 2 it has ended
constexpr u32 DRAW_SKIP = layout::DRAW_SKIP;  // ours: not 0, this frame draws nothing (cleared at the next tick)
constexpr u32 ASK_BEGIN = 1, ASK_END = 2;
constexpr double FIELD_MS = 1000.0 * 1001.0 / 60000.0;  // 16.683 ms
constexpr double MARGIN_MS = 0.8;   // the frame's head before the tick and its tail after the read-back
constexpr double FIRST_DRAW_MS = 2.5;  // the drawing's cost before one was timed (the study: 1.2 to 3 ms)
constexpr u32 NO_SKIP_COURSE = 41;
constexpr u32 REPORT_EVERY = 1800;  // race frames between two log lines

struct Present
{
  Clock::time_point tick{}, began{};
  bool race_frame = false;   // NormalStep: this frame may skip
  bool skipping = false;     // this frame's answer
  bool skipped_last = false; // the frame before skipped
  bool timed = false;        // a drawing has been timed
  double draw_ms = FIRST_DRAW_MS;
  u32 race_frames = 0, skipped = 0, forced = 0, late = 0;
};
Present s_present;

double Ms(std::chrono::steady_clock::duration d)
{
  return std::chrono::duration<double, std::milli>(d).count();
}

void Report()
{
  const Present& p = s_present;
  Notice("fzgx: drawing: skipped in {} of {} race frames ({} forced; {} would have been late), a drawing {:.2f} ms",
         p.skipped, p.race_frames, p.forced, p.late, p.draw_ms);
}

bool Decide(Guest& g, Clock::time_point now)
{
  Present& p = s_present;
  if (!p.race_frame)
    return false;
  ++p.race_frames;
  const Debug& d = TheOptions().debug;
  if (hfr::On() || g.Read32(layout::RACE + 4) == NO_SKIP_COURSE)
    return false;
  if (d.skip_draws != 0 && p.race_frames % d.skip_draws == 0)
  {
    ++p.forced;
    return true;
  }
  const bool would_be_late = Ms(now - p.tick) + p.draw_ms + MARGIN_MS > FIELD_MS;
  if (!would_be_late)
    return false;
  ++p.late;
  return !p.skipped_last;
}
}  // namespace

void PresentTick(Guest& g)
{
  Present& p = s_present;
  p.tick = Clock::now();
  p.skipped_last = p.skipping;
  if (p.skipping)
    g.Write32(DRAW_SKIP, 0);
  p.skipping = false;
  p.race_frame = false;
}

void PresentRaceFrame()
{
  s_present.race_frame = true;
}

void PresentAsked(Guest& g, u32 value)
{
  Present& p = s_present;
  const auto now = Clock::now();
  if (value == ASK_BEGIN)
  {
    p.began = now;
    p.skipping = Decide(g, now);
    g.Write32(DRAW_SKIP, p.skipping ? 1 : 0);
    if (p.skipping)
    {
      ++p.skipped;
      TheHost().PictureHeld();
    }
    if (p.race_frame && p.race_frames % REPORT_EVERY == 0)
      Report();
  }
  else if (value == ASK_END && p.race_frame && !p.skipping && p.began != Clock::time_point{})
  {
    // the drawing as it has cost lately: an eighth of each new one
    const double ms = Ms(now - p.began);
    p.draw_ms = p.timed ? p.draw_ms + (ms - p.draw_ms) / 8 : ms;
    p.timed = true;
  }
}

void PresentEnd()
{
  if (s_present.race_frames != 0)
    Report();
  s_present = Present{};
}
}  // namespace fzgx::net
