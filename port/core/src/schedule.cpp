// The time sync of a race's frames (NormalStep, frame.cpp): how far ahead of its schedule the game is, what its
// host's pacing still owes it, and whether the frame adds a step to catch up or runs no new step to fall back.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "net_internal.h"

#include <algorithm>
#include <chrono>

namespace fzgx::net
{
namespace
{
// The time sync's view of the host's pacing (NormalStep). A host that keeps the game at real time, as the emulator
// does, makes up by itself the time a long displayed frame lost: it runs the next frames short, one step each, until
// the game's time is real time again. The steps a long frame cost are therefore not lost, and a catch-up step
// added for them comes on top of the host's own: the game ends up ahead of its schedule by as many steps as it
// caught up. This keeps the account from when the race's frames begin: a long frame adds what it took over a
// frame's length, a short frame takes off what it made up, and a frame of the usual length halves what is left
// (the host is not making up anything: the time is gone, or was never the host's to make up, as when the game's
// own frame took two fields).
// Only a host that holds the pace is trusted so. A long frame, or a few together (a deep rollback's often come in
// twos), among frames of the usual length is a moment the host makes up. More than PACE_BURST_FRAMES long frames
// with no PACE_CALM_FRAMES frames in a row between them that were not long is a host too slow for the pace: it
// makes up nothing, the account is emptied and stays empty until the host holds the pace again, and every step
// lost is caught up at once, as before there was an account. (With every long frame counted, a computer at 55
// frames a second was always owed the most the account gives, caught up only from 2.5 steps behind, and its
// inputs reached the relay as their frames closed; at 40 two in three came late.)
// Beside the account: how far the long frames since the host last held the pace have set the game back, which is
// what they took over a frame's length less what the short frames between them made up (pace_burst_ms), and the
// most of that lately (pace_worst_ms): the lead this host's stalls need, for the hold in NormalStep. (With nothing
// taken off for a short frame, a host whose frames are long and short in turn, 19 and 14.33 ms, or one of 20 or 30
// ms in four, never had four frames without a long one: every over-length was added, the sum stood at its most,
// 100 ms, for as long as the host went on so, and a game five steps ahead of its schedule was never held.)
// Returns the time still owed, in ms: never more than PACE_OWED_MAX_MS.
double PaceOwed(Session& s, Clock::time_point now)
{
  if (s.pace_last != Clock::time_point{})
  {
    const double over = std::chrono::duration<double, std::milli>(now - s.pace_last).count() - FRAME_MS;
    if (over > PACE_SLACK_MS)
    {
      s.pace_calm = 0;
      s.pace_burst_ms = std::min(s.pace_burst_ms + over, PACE_OWED_MAX_MS);
      if (s.pace_long < PACE_BURST_FRAMES)
      {
        ++s.pace_long;
        s.pace_owed_ms = std::min(s.pace_owed_ms + over, PACE_OWED_MAX_MS);
      }
      else
      {
        s.pace_long = PACE_BURST_FRAMES + 1;  // too slow for the pace
        s.pace_owed_ms = 0;
      }
    }
    else
    {
      if (over < -PACE_SLACK_MS)
      {
        s.pace_owed_ms = std::max(0.0, s.pace_owed_ms + over);
        s.pace_burst_ms = std::max(0.0, s.pace_burst_ms + over);
      }
      else
        s.pace_owed_ms /= 2;
      if (++s.pace_calm >= PACE_CALM_FRAMES)
      {
        s.pace_long = 0;
        s.pace_burst_ms = 0;
      }
    }
    s.pace_worst_ms = std::max(s.pace_worst_ms - PACE_WORST_FADE_MS, s.pace_burst_ms);
  }
  s.pace_last = now;
  return s.pace_owed_ms;
}
}  // namespace

// How many steps the game is ahead of the schedule the time sync aims at (a playback: 0).
double StepsAhead(const Session& s, u32 step)
{
  // time sync: run far enough ahead of the relay's clock that the local inputs reach it before it closes their
  // frames; add a step to catch up, or run no new step to fall back. An input for step f is sent when this game is at
  // f - delay and reaches the relay half a round trip later, so the relay's clock plus half the round trip, less the
  // input delay, plus LEAD keeps the local inputs LEAD frames (and the relay's close window) ahead of their frames'
  // close. (With half the round trip less, they arrived about half a frame early at 150 ms: a jitter made them late,
  // the relay used the previous input, short presses such as a spin attack were lost and the local machine was put
  // back.) The relay's clock comes from its pongs: by this game's own clock alone, a relay clock 3.7% slow (WSL's
  // against Windows') left the game pinned MAX_AHEAD steps past the official frames, catching up one step and then
  // waiting for two official frames every displayed frame: 30 fps.
  // A playback has no relay and no clock to follow: one new step a displayed frame.
  const double ahead = s.playback ? 0.0 : static_cast<double>(step) - ScheduledStep(s);
  return ahead;
}

// The step the time sync aims at now (StepsAhead). A watching game sends its inputs by it (spectate.cpp): a racer's
// game is held between 1 step behind it and 2 ahead.
double ScheduledStep(const Session& s)
{
  const double since_go = std::chrono::duration<double, std::milli>(Clock::now() - s.go_local).count();
  const double relay_ms = since_go + (s.relay_offsets.empty() ? s.rtt_ms / 2 : s.relay_offset_ms);
  return (relay_ms + s.rtt_ms / 2) / FRAME_MS - static_cast<double>(s.delay) + LEAD;
}

// The time sync's decision for the frame: a step more to catch up, no new step to fall back (the memory then goes
// back one step), or neither. `check`: the frame checks the steps the race's end rests on (frame.cpp,
// BeginRollbackFrame) and runs no new step whatever the schedule says: it has just waited for official frames, so
// it never catches up, and it is the same frame as one held for the schedule (a frame that is both is one frame
// without a new step, and is the end's, not counted among the holds); the account of the host's pace takes its
// wait as any long frame's.
FrameSteps CatchUpOrHold(Guest& g, Session& s, u32 step, bool settle, bool check, Clock::time_point frame_now,
                         double ahead, FrameSteps steps)
{
  u32 run_from = steps.run_from;
  // The game is meant to sit between 1 step behind that schedule and 2 ahead of it. It sat 2 to 5 ahead, a
  // rollback that many steps deeper in every frame, for two reasons, both about the host's pacing (PaceOwed):
  // - Behind after a long frame (a deep rollback's), it added a catch-up step while the host was making the same
  //   time up with short frames. Now the steps the host still owes count as run: a catch-up step is for what is
  //   lost beyond them. Not more than MAX_OWED_STEPS are counted so, and none on a host that does not hold the
  //   pace (PaceOwed): what such a host owes it never makes up, and a game left behind sends its inputs late.
  // - Ahead, it waited 4 to 8 ms in this hook, and the host made that up too: the frame after came that much
  //   sooner, and the game never fell back. Now the frame runs no new step: the memory goes back one step and
  //   that step runs again, alone, with the inputs it has now (a rollback of depth 1 whose new step is left to
  //   the next frame). The picture stands for one frame and the game is one step less ahead, whatever the host
  //   does. Steps are neither skipped nor changed: step `step` runs in the next frame.
  //   A host with long frames keeps the lead they need: the game is held only when it is further ahead than the
  //   worst of them lately leaves room for (HOLD_SPARE_STEPS). After a long frame the game's inputs are that much
  //   later at the relay, and its lead is what keeps them in time. (Held back to 2 steps whatever the host, a game
  //   whose frames took 30 ms half the time sent one input in five too late for its frame; before there was a
  //   hold, none.)
  // A game drifting behind live (spectate.cpp) runs one step a frame, slower, and neither catches up nor is held
  // for the schedule; but for the frame that checks the race's end, as any race's.
  if (s.watch == Watch::Drifting && !check)
  {
    s.work.sched = ahead;
    return steps;
  }
  const double owed = s.playback ? 0.0 : std::min(PaceOwed(s, frame_now) / FRAME_MS, MAX_OWED_STEPS);
  s.work.sched = ahead;
  s.work.owed = owed;
  u32 catch_up = 0;
  bool held = false;
  // (not on top of a deep rollback: an overloaded machine would fall only further behind. What is deep follows the
  // input delay: a game runs one step further ahead of the relay's clock for each frame of delay its player gave up
  // (StepsAhead), and its everyday rollbacks are as much deeper, so the depth a catch-up step is still added at
  // grows by as much: 6 at the delay of 2, 7 at 1, 8 at 0. Left at 6, a game at delay 0 whose every frame had a
  // rollback of 7 added no step until it had fallen a step behind its schedule, and kept that much less time for
  // its inputs to reach the relay: tests/test_pace_delay.cpp.)
  const u32 deep = CATCH_UP_DEPTH + (PLAYERS_INPUT_DELAY - std::min(s.delay, PLAYERS_INPUT_DELAY));
  if (!settle && !check && CatchingUp(s, step) && Have(s) > step + 1 && step - run_from <= deep)
  {
    // a game come back into its race (rejoin.cpp): as many steps as the official frames in hand allow, up to
    // RETURN_CATCH_UP a frame, every one of them on official rows
    catch_up = std::min<u32>(RETURN_CATCH_UP, Have(s) - step - 1);
    s.catch_ups += catch_up;
  }
  else if (!settle && !check && ahead + owed < -1 && step - run_from <= deep)
  {
    catch_up = std::min<u32>(MAX_CATCH_UP, static_cast<u32>(-(ahead + owed)));
    // never into the run-ahead limit: the next frame would only wait for the official frames it ran past
    const u32 limit = static_cast<u32>(s.official.size()) + MAX_AHEAD;
    catch_up = std::min(catch_up, limit > step + 1 ? limit - step - 2 : 0);
    s.catch_ups += catch_up;
  }
  else if (check || (!settle && ahead > 2 + std::max(0.0, s.pace_worst_ms / FRAME_MS - HOLD_SPARE_STEPS) &&
                     step > s.checked))  // (the step before is not final: its undo entry is there)
  {
    held = true;
    if (check)
      Notice("fzgx: the race looks over before step {}: checked against the official inputs, {} steps run again",
             step, step - std::min(run_from, step - 1));
    else
      ++s.holds;
    if (run_from == step)
    {
      run_from = step - 1;
      s.rollback.RestoreTo(g, step, run_from);
    }
  }
  return {steps.known, run_from, catch_up, held};
}
}  // namespace fzgx::net
