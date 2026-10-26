// The replay scrubber's budget for front steps (scrub.cpp, More): how many steps ahead of the picture a displayed
// frame may run while the replay buffers, by the whole frame's time.
//
// A frame that buffers runs, after its first hook, some front steps (2.7 to 3.6 ms each on course 37), then its own
// play step and its drawing. A rule that gives the front steps 40 % of a field measured from the frame's start
// and always runs the first, whatever the frame's own work costs, is too slow: at course 37's heavy places (own work
// 7 to 10 ms) frames took 16.5 to 21 ms, and the replay played at 45 to 56 fields a second until it was buffered
// So the frame's own work is measured and counted:
//   - one more front step is given only while the time since the frame's start, plus the frame's own work, plus
//     the steps already set to run but not run yet, plus 1.25 front steps fits in WHOLE_SHARE of a field; the same
//     in emulated time (the frame's own ticks and all its front steps' ticks within the share of a field);
//   - the share of a field (Debug::buffer_share, 40 %; HELD_SHARE while the picture stands) stays the upper bound,
//     the rule as it was, for every step after the first;
//   - a frame may run no front step at all: a frame whose own work leaves no room goes straight to its play step,
//     while the front edge is at least LEAST_LEAD steps ahead of the play position. Below that the first step runs
//     whatever the time, as it always did: the play position, one step a frame, never passes the front edge (a
//     PC too busy for even one front step a frame lets it catch up: met on course 37). Light frames
//     run more, so the buffer still fills.
// The frame's own work is the time from the end of its front steps (the moments exchanged back) to the next frame's
// start, less what the game's thread waited there for the speed limit (the emulator's throttle sleeps where the
// emulated clock runs ahead of the host's: in a paced playback most of a frame's spare time). In emulated time it
// is the processor's ticks over the same stretch less those it idled. Both are smoothed as a front step's time is.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "net_internal.h"

#include <algorithm>
#include <utility>

namespace fzgx::net
{
namespace
{
constexpr double STEP_MARGIN = 1.25;     // a front step counted with a quarter more, for its spread
constexpr double SMOOTH = 0.2;           // of a new measurement (a front step's time is smoothed the same way)
constexpr double OWN_MOST_FIELDS = 2.0;  // a measurement of the own work counts at most this many fields (a hitch)
constexpr double CPU_TICKS_PER_TB = 12;  // the emulated processor's ticks to a time base tick

double Smoothed(double kept, double value)
{
  return kept == 0 ? value : kept * (1 - SMOOTH) + value * SMOOTH;
}
}  // namespace

bool FrontStepFits(const FrontBudget& b)
{
  const bool first = b.committed == 0;
  if (first && b.lead < LEAST_LEAD)
    return true;  // (one step a frame keeps the front edge ahead: a frame with the play step and one front step fits)
  const double pending = first ? 0 : 1;  // (after the first: the step set to run before this one has not run yet)
  // the host's clock: the whole frame, and (after the first) the share as the upper bound
  bool host = true;
  if (b.step_ms == 0)
    host = first;  // (the first step a frame until a step's time is known)
  else
  {
    host = b.since_ms + b.own_ms + (pending + STEP_MARGIN) * b.step_ms <= FRAME_MS * WHOLE_SHARE;
    if (!first)
      host = host && b.since_ms + b.step_ms * STEP_MARGIN <= FRAME_MS * b.share;
  }
  // emulated time: every front step of the frame and its own work (until that is known, its play step taken as a
  // front step), within a field; the share as the upper bound, as ever
  const double own_ticks = b.own_ticks != 0 ? b.own_ticks : b.step_ticks;
  bool emulated = (b.committed + STEP_MARGIN) * b.step_ticks + own_ticks <= FIELD_TICKS * WHOLE_SHARE;
  if (!first && b.step_ticks != 0)
    emulated = emulated && (b.committed + 2) * b.step_ticks <= FIELD_TICKS * b.share;
  return host && emulated;
}

void OwnWorkBegins(ScrubState& a)
{
  a.own_open = true;
  a.own_at = std::chrono::steady_clock::now();
  a.own_clocked = TheHost().ThreadClocks(&a.own_clocks);
}

void OwnWorkEnds(ScrubState& a, std::chrono::steady_clock::time_point now)
{
  if (!std::exchange(a.own_open, false))
    return;
  GameClocks clocks;
  const bool clocked = a.own_clocked && TheHost().ThreadClocks(&clocks);
  std::chrono::steady_clock::duration took = now - a.own_at;
  if (clocked)
    took -= std::chrono::duration_cast<std::chrono::steady_clock::duration>(clocks.waited - a.own_clocks.waited);
  const double ms =
      std::clamp(std::chrono::duration<double, std::milli>(took).count(), 0.0, FRAME_MS * OWN_MOST_FIELDS);
  a.own_ms = Smoothed(a.own_ms, ms);
  if (!clocked || clocks.ticks < a.own_clocks.ticks)
    return;
  const u64 idle = clocks.idle_ticks - a.own_clocks.idle_ticks;
  const u64 ticks = clocks.ticks - a.own_clocks.ticks;
  const double busy = static_cast<double>(ticks - std::min(ticks, idle)) / CPU_TICKS_PER_TB;
  if (busy > 0)
    a.own_ticks = Smoothed(a.own_ticks, std::min(busy, FIELD_TICKS * OWN_MOST_FIELDS));
}
}  // namespace fzgx::net
