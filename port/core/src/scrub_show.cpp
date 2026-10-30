// The replay scrubber (scrub.cpp runs the frame): what the play position shows beside its step. The racers as the
// front edge's steps found them (the leader's laps, where each racer finished or went out, read only), the finish
// banner's words for the racer watched, and the bar the host is given.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "net_internal.h"
#include "out_test.h"

#include <algorithm>

namespace fzgx::net
{
namespace
{
constexpr u32 PROGRESS = game::MACHINE_PROGRESS;  // a machine's progress record pointer
constexpr u32 LAPS_DONE = game::PROGRESS_LAPS;     // ... its laps completed
}  // namespace

// The racers after the step just run at the front edge, read only, at the boundary before step `t`: the leader's laps
// (a mark where the most laps any racer has completed grows, at step t - 1) and where each racer finished or went out.
void ReadRacers(Guest& g, Session& s, u32 t)
{
  LapMarks& laps = s.scrub.laps;
  RacerEnds& ends = s.scrub.ends;
  const u32 module = g.Read32(MODULE_POINTER);
  const u32 machines = g.Read32(module + MACHINES);
  const u32 count = std::min<u32>(g.Read8(module + MACHINE_COUNT), 30);
  u32 most = 0;
  for (u32 i = 0; machines != 0 && i < count; ++i)
  {
    if (const u32 progress = g.Read32(machines + i * game::MACHINE_SIZE + PROGRESS); progress != 0)
      most = std::max(most, g.Read32(progress + LAPS_DONE));
    const u32 flags = g.Read32(machines + i * game::MACHINE_SIZE);
    if ((flags & game::MACHINE_FLAG_FINISHED) != 0 && ends.finish[i] == NONE_STEP)
      ends.finish[i] = t;
    // out by the game's own verdict, as the image's finish watch and spectator count it (never finished)
    if (ends.out[i] == NONE_STEP && out_test::Out(g, module, machines + i * game::MACHINE_SIZE))
      ends.out[i] = t;
  }
  const u32 step = t - 1;
  for (; laps.laps < most && laps.laps < 99; ++laps.laps)
  {
    if (laps.count < MOST_LAPS)
      laps.at[laps.count++] = step;
  }
}

// The finish banner of the racer watched (the image's `finwatch`, frame_hud.py) for the play position: what its words
// would say had the play position's steps alone passed its boundaries. The image looks at every step boundary of a
// frame, the front's too, and the words are the session's, never exchanged: left alone, the front's boundaries set
// the countdown's end by the front's step, the buffer's distance late, or took it back. So before the play step the
// kind and the end are the play position's own, from where the front saw the racer finish or go out: the boundary
// after the play step then changes the kind exactly when that step finishes the racer, as the image's own would.
void FinishBanner(Guest& g, Session& s, u32 p)
{
  const RacerEnds& ends = s.scrub.ends;
  const u32 seat = g.Read32(SEAT_SLOT + 4);
  if (seat >= 30)
    return;
  if (ends.finish[seat] <= p)
  {
    g.Write32(layout::FINISH_KIND, 1);
    g.Write32(layout::FINISH_END, ends.finish[seat] + 480);
  }
  else if (ends.out[seat] <= p)
  {
    g.Write32(layout::FINISH_KIND, 2);
    g.Write32(layout::FINISH_END, ends.out[seat] + 300);
  }
  else
    g.Write32(layout::FINISH_KIND, 0);
}

// The bar's picture of the frame.
void ShowBar(const Session& s)
{
  const ScrubState& a = s.scrub;
  ScrubBar bar;
  bar.first = a.first;
  bar.last = std::max(a.front_done ? a.end : static_cast<u32>(s.official.size()) - 1, a.first + 1);
  bar.played = a.play_step + 1;
  bar.buffered = a.front;
  bar.standing = a.held || a.wait_for != NONE_STEP;
  bar.beyond = a.clamped;
  bar.end_known = a.front_done;
  // heard: a frame that runs its next step once, for its picture, with its sounds (not: paused, held on the bar,
  // waiting for the front edge, or running steps again from a key)
  bar.heard = !a.held && !a.play_silent && !a.paused && !a.scrubbing && a.wait_for == NONE_STEP && a.catch_left == 0;
  bar.laps = a.laps.count;
  std::copy(a.laps.at.begin(), a.laps.at.end(), bar.lap_at.begin());
  TheHost().ReplayBar(bar);
}
void ScrubBarOutside(Session& s, u32 step)
{
  if (!s.playback || !TheOptions().replay_scrubber)
    return;
  // (outside the race: before it the bar is empty)
  ScrubBar bar;
  bar.first = step;
  bar.last = step + 1;
  bar.played = bar.buffered = step;
  TheHost().ReplayBar(bar);
}
}  // namespace fzgx::net
