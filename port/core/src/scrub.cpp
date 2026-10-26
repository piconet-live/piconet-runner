// The replay scrubber (Options::replay_scrubber, on in a player's playback): while a replay plays, the rest of its
// race is simulated ahead of the picture in the same game, so that anything buffered can be shown at once: a jump,
// a pause, or the picture following the pointer held on the bar (scrub_move.cpp). In the game's replay mode and as
// the race alike.
//
// Two moments of one race share one memory: the play position, whose step every displayed frame runs last and
// draws, and the front edge, whose steps run before it in the same frame without a picture (the image's extra
// steps, which leave the presentation's task lists out and are told to be silent). The memory holds one moment at
// a time; the other's content of every page either has seen change is kept beside it (Rollback::Moment), and the
// two change places at a hook between two steps. Each moment has an undo log of its own, both started from the
// race's first frame:
//   - the front's log (Session::front) is kept for the whole race, packed by units (undo_pack.cpp), with a whole
//     copy of the front's moment every KEY_EVERY steps (keys): a jump takes the nearest key's memory, or the
//     front's, and puts the front's entries back down to the step asked for, then makes that the play position's
//     memory. Over the replay's memory budget (Debug::replay_memory, 512 MiB) the packed units farthest from the
//     play position are dropped; a step of theirs is then run again from the key below before it is shown;
//   - the play log (Session::rollback) takes the play position back over the last PLAY_LOG_STEPS steps played,
//     picture state included, as a rollback does.
// So a jump forwards, or back beyond the play log, lands on memory in which no presentation task has run since the
// race's first frame; a jump back within the play log lands on memory as it was shown.
//
// A displayed frame, with K front steps (K by the budget, scrub_budget.cpp; a frame whose own work leaves no room
// runs none and goes straight to its play step): the first hook (ScrubFrame) closes the play log's entry, takes the
// keys, moves the play position if asked, exchanges the moments and lets the frame routine run front step 1; each hook
// after a front step (ScrubExtra) closes the front log's entry for it, checks it against the recording, and says
// whether another follows; the hook after the last exchanges the moments back, and the play step runs last, with
// the presentation, and is drawn. The frame routine decides before a hook whether the step after it is the last
// (frame_entry.py, `extra_clock`), so the count it reads (control block +8) is kept one ahead.
//
// The image ends a frame at the step that makes the race look over (`extra_cut`), with no hook: the play step is
// then not run and the memory is the front's. The one moment the host has there is the image's own store of
// "the extra steps are done" (WindowWritten, StepsDone): the moments are exchanged back, the front edge stands
// for good, and what that frame changed in the play position's memory (it drew, and counted the race clock) is
// put back at the next frame's start, which shows the picture before once more.
//
// What a step takes from outside both exchanged sets is not exchanged: memory never rolled back (the sound
// system's, the face board's), low memory, the data window. The session writes the step word, the input rows of a
// step and of the step before it, and the mute word for each moment. Every step at the front edge and every step
// run for a picture is compared with the recording; nothing is ever written into a step's memory that the
// recording's race did not have.
//
// Before the race the game runs unseen under LOADING (fzgx/panel.h, a start held to the race), with no speed limit
// and no sound: the load, the course intro cut short as the game's own skip does (core.cpp, CutTheIntro), and the
// countdown's steps from the recording. The race's first step (Fork) is the bar's first and the first picture; the
// panel comes down and the pace and sound are given back there. The bar's sound gate is the host's
// (ScrubBar::heard).
// SPDX-License-Identifier: GPL-3.0-or-later

#include "net_internal.h"

#include "fzgx/panel.h"

#include <algorithm>
#include <chrono>
#include <utility>

namespace fzgx::net
{
namespace
{
using Steady = std::chrono::steady_clock;
constexpr u32 SOUNDS_FROM = CONTROL + 492;  // the image's mute word (mute.py): a step below it is silent
constexpr u32 SILENT = 0xFFFFFFFF;
constexpr u32 MOST_FRONT_STEPS = 64;        // in one frame
constexpr double HELD_SHARE = 0.85;         // of a frame, for front steps while the picture stands

double Ms(Steady::duration d)
{
  return std::chrono::duration<double, std::milli>(d).count();
}

void Rows(Guest& g, const Session& s, u32 t)
{
  if (t > 0 && t - 1 < s.official.size())
    WriteRow(g, s, t - 1, s.official[t - 1]);
  if (t < s.official.size())
    WriteRow(g, s, t, s.official[t]);
}

// The two moments change places.
void Exchange(Guest& g, Session& s)
{
  ScrubState& a = s.scrub;
  const auto started = Steady::now();
  s.rollback.Exchange(g, s.front, &a.other);
  a.in_front = !a.in_front;
  const double us = Ms(Steady::now() - started) * 1000;
  a.swap_us += us;
  a.swap_most_us = std::max(a.swap_most_us, us);
  ++a.swaps;
}

// The race's first frame: the two moments are one, and both logs start from it.
void Fork(Guest& g, Session& s, u32 step)
{
  ScrubState& a = s.scrub;
  if (!s.rollback.Active())
    s.rollback.Start(g);
  s.front.Start(g);
  s.rollback.KeepWatching();
  s.front.KeepWatching();
  s.rollback.Keep(g, &a.base);
  a.other = a.base;
  s.front.KeepKey(g, step, a.base);  // (the race's first moment is a key too: steps run again may start there)
  a.forked = true;
  a.first = a.front = a.pure_at = step;
  a.race_at = Steady::now();
  // the first picture: LOADING goes, and the speed limit and the sound come back (the bar's gate opens the sound)
  if (panel::RaceBegins(panel::TheBoard()))
    TheHost().LoadPace(false);
  if (TheOptions().debug.buffer_steps != 0)
    Notice("fzgx: scrubber: the race begins at step {}: from here its steps are simulated ahead of the picture, {} "
           "in every frame (a test)", step, TheOptions().debug.buffer_steps);
  else
    Notice("fzgx: scrubber: the race begins at step {}: from here its steps are simulated ahead of the picture, for "
           "{} % of a frame's time while it plays", step, TheOptions().debug.buffer_share);
}

// The frame before, by its kind: its length, and whether it ran over a field.
void TallyFrame(ScrubState& a, Steady::time_point now)
{
  if (a.kind < 0)
    return;
  const double ms = Ms(now - a.frame_at);
  ++a.kind_frames[a.kind];
  a.kind_ms[a.kind] += ms;
  a.kind_steps[a.kind] += a.front_steps;
  a.kind_longest_ms[a.kind] = std::max(a.kind_longest_ms[a.kind], ms);
  if (ms > FRAME_MS * 1.1)
    ++a.kind_long[a.kind];
  if (ms > FRAME_MS * 1.1 && a.front_steps != 0)
    ++a.long_with;
  if (!a.front_done && a.front_steps == 0)
    ++a.frames_without;
}

// Whether front step `next` may run in this frame after the `a.front_steps` set to run already (0: whether the
// frame runs any): by the count asked for (tests), else by the frame's time, its own work counted
// (scrub_budget.cpp): the host's clock, and the emulated one (a frame whose steps take more emulated time than a
// field is shown a field late).
bool More(const Session& s, u32 next)
{
  const ScrubState& a = s.scrub;
  if (next >= s.official.size() || a.front_steps >= MOST_FRONT_STEPS)
    return false;
  if (const u32 fixed = TheOptions().debug.buffer_steps; fixed != 0)
    return a.front_steps < fixed;
  FrontBudget b;
  b.committed = a.front_steps;
  b.since_ms = Ms(Steady::now() - a.frame_at);
  b.step_ms = a.step_ms;
  b.own_ms = a.own_ms;
  b.step_ticks = a.step_ticks;
  b.own_ticks = a.own_ticks;
  b.lead = a.front > a.play_step ? a.front - a.play_step : 0;
  // (a scrub's picture stands too, but must follow the pointer at the display's rate: the share of a playing frame)
  b.share = a.held && !a.scrubbing ? HELD_SHARE : TheOptions().debug.buffer_share / 100.0;
  return FrontStepFits(b);
}

// Over the budget, what lies farthest from the play position goes: first the front's packed units, then keys, both
// only beyond Debug::replay_near steps either side of it (what a scrub there needs at once). A key every KEY_KEPT steps
// from the race's first stays, so a step whose log is gone is run again from at most that far below it. What cannot
// go (the near part, those keys, the fixed copies) may keep the replay over its budget: the line at the end says.
void KeepToBudget(Session& s)
{
  ScrubState& a = s.scrub;
  const std::size_t budget = std::size_t{TheOptions().debug.replay_memory} << 20;
  const u32 near = TheOptions().debug.replay_near;
  std::size_t bytes = ScrubBytes(s);
  while (bytes > budget)
  {
    std::size_t freed = s.front.DropFarthest(a.play_step, near);
    if (freed == 0)
      freed = s.front.DropFarKey(a.play_step, near, a.first, KEY_KEPT);
    if (freed == 0)
      break;
    bytes -= std::min(bytes, freed);
  }
  a.replay_most = std::max(a.replay_most, bytes);
  a.over_budget += bytes > budget ? 1 : 0;
}

// The play step is about to run: its step word, its rows, its sounds or none, the finish banner's words.
void PlayStep(Guest& g, Session& s)
{
  ScrubState& a = s.scrub;
  FinishBanner(g, s, a.play_step);
  g.Write32(STEP, a.play_step);
  Rows(g, s, a.play_step);
  g.Write32(SOUNDS_FROM, a.play_silent ? SILENT : a.play_step);
  a.play_ran = true;
}

// A front step has run: the front's log takes it, and its checksum is compared with the recording's.
void TookFrontStep(Guest& g, Session& s, u32 t)
{
  ScrubState& a = s.scrub;
  s.front.SaveStep(g, t, true);
  a.log_us += s.front.TakeWork().track_us;
  if (t > 0)
  {
    ReadRacers(g, s, t);
    TimelineStep(g, s, t - 1);  // (the racer timeline: what the step just run changed, read only)
  }
  a.front = t;
  ++a.front_total;
  a.front_bytes = std::max(a.front_bytes, s.front.LogBytes());
  if (t % KEY_EVERY == 0)
  {
    s.front.KeepKey(g, t, a.base);
    a.key_bytes = s.front.KeyBytes();
  }
  if (t > 0)
    s.front.PackStep(g, t - 1);
  if (t == 0 || t - 1 >= s.replay_checksums.size())
    return;
  s.verified = std::max(s.verified, t);
  const u32 value = Rollback::KeptChecksum(g, t - 1);
  if (value == s.replay_checksums[t - 1])
    return;
  ++s.desyncs;
  a.front_differed.push_back(t - 1);
  if (a.front_differed.size() <= 20)
  {
    Warn("fzgx: scrubber: step {} at the front edge differs from the recording ({:08x}, recorded {:08x})", t - 1,
         value, s.replay_checksums[t - 1]);
  }
}

// The front's memory in, and its first step of this frame set up (More said it may run).
void EnterFront(Guest& g, Session& s)
{
  ScrubState& a = s.scrub;
  Exchange(g, s);
  g.Write32(STEP, a.front);
  Rows(g, s, a.front);
  g.Write32(SOUNDS_FROM, SILENT);
  a.front_steps = 1;
  const bool more = More(s, a.front + 1);
  Rollback::Resimulate(g, more || a.catch_left > 0 ? 2 : 1);
  a.play_next = !more;
  a.hook_left = Steady::now();
}

// A step of the play position's run again from a key (its log was dropped), without a picture, before the frame's
// own. `first`: it is the frame's first step. The count the frame routine reads is kept one ahead (the head).
void CatchStep(Guest& g, Session& s, bool first)
{
  ScrubState& a = s.scrub;
  g.Write32(STEP, a.catch_at);
  Rows(g, s, a.catch_at);
  g.Write32(SOUNDS_FROM, SILENT);
  ++a.catch_at;
  --a.catch_left;
  ++a.catch_steps;
  const bool more = a.catch_left > 0;  // (another step before the play step)
  Rollback::Resimulate(g, first ? (more ? 2 : 1) : (more ? 3 : 2));
}

// A step's checksum at the play position against the recording (a step run again from a key, or a picture's).
void CheckPlayStep(Guest& g, Session& s, u32 t, const char* what)
{
  ScrubState& a = s.scrub;
  if (t >= s.replay_checksums.size())
    return;
  ++a.play_checked;
  const u32 value = Rollback::KeptChecksum(g, t);
  if (value == s.replay_checksums[t])
    return;
  ++s.desyncs;
  a.play_differed.push_back(t);
  if (a.play_differed.size() <= 20)
    Warn("fzgx: scrubber: step {} {} differs from the recording ({:08x}, recorded {:08x}; the front edge is at {})", t,
         what, value, s.replay_checksums[t], a.front);
}

}  // namespace

bool ScrubOn(const Session& s)
{
  return s.playback && TheOptions().replay_scrubber && !s.scrub.over;
}

u32 ScrubKeeps(const Session& s)
{
  return s.checked > PLAY_LOG_STEPS ? s.checked - PLAY_LOG_STEPS : 0;
}

void ScrubVerify(Guest& g, Session& s)
{
  ScrubState& a = s.scrub;
  if (!a.forked || !std::exchange(a.play_ran, false))
    return;
  s.verified = std::max(s.verified, a.play_step + 1);
  CheckPlayStep(g, s, a.play_step, "at the play position");
  // the racer watched has finished by the step just shown: what the banner's countdown reads, against what the game's
  // own reads that many steps after the finish (480 at the finish's boundary), once a racer
  const u32 seat = g.Read32(SEAT_SLOT + 4);
  if (seat < 30 && a.ends.finish[seat] <= a.play_step + 1 && (a.banner_said & (1u << seat)) == 0)
  {
    a.banner_said |= 1u << seat;
    Notice("fzgx: scrubber: racer {} finished at step {}; at the play position, after step {}, its banner's countdown "
           "reads {} (the game's own: {}; the front edge is at {})", seat, a.ends.finish[seat] - 1, a.play_step,
           g.Read32(layout::FINISH_COUNT), 480 - (a.play_step + 1 - a.ends.finish[seat]), a.front);
  }
}

void ScrubFrame(Guest& g, Session& s, u32 step)
{
  ScrubState& a = s.scrub;
  const auto now = Steady::now();
  if (!a.forked)
    Fork(g, s, step);
  else
    TallyFrame(a, now);
  OwnWorkEnds(a, now);
  a.frame_at = now;
  ++a.frames;
  // the emulated time the frame before's steps took, from the image's count over its extra steps
  if (a.front_steps != 0 && !a.cut && g.Read32(RESIM_TICKS) != 0)
  {
    const double ticks = static_cast<double>(g.Read32(RESIM_TICKS)) / a.front_steps;
    a.step_ticks = a.step_ticks == 0 ? ticks : a.step_ticks * 0.9 + ticks * 0.1;
  }
  // the play log: what the frame before changed, its step and its picture; a frame that ran no play step changed
  // only what must not stay
  if (std::exchange(a.cut, false))
    s.rollback.Discard(g);
  else
    s.rollback.SaveStep(g, step, true);
  a.play_bytes = std::max(a.play_bytes, s.rollback.LogBytes());
  s.work_open = true;
  // a recording that holds no settled race (its player closed the game in the race) ends at its last step
  if (!a.front_done && a.front >= s.official.size())
  {
    a.front_done = true;
    a.end = a.front - 1;
    Notice("fzgx: scrubber: the front edge stands at step {}, the recording's end: the race did not settle in it; "
           "the replay ends before step {} (buffered in {:.1f} s)", a.front, a.end, Ms(now - a.race_at) / 1000);
  }
  a.play_step = ScrubMove(g, s, step);
  a.front_steps = 0;
  ShowBar(s);
  TimelineSend(s);
  a.kind = (a.held ? 2 : 0) + (a.front_done ? 1 : 0);
  // front steps first while the budget gives one, else the frame's own steps at once
  if (!a.front_done && More(s, a.front))
    EnterFront(g, s);
  else
  {
    OwnWorkBegins(a);
    if (a.catch_left > 0)
      CatchStep(g, s, true);
    else
    {
      PlayStep(g, s);
      Rollback::Resimulate(g, 0);
    }
  }
  // what the rest of the session keeps of a race's frame: nothing waits to be checked, and the play log is kept
  // PLAY_LOG_STEPS deep
  g.Write32(END_OFFICIAL, Have(s));
  s.last_extra = 0;
  s.used.clear();
  s.checked = a.play_step + 1;
  s.rollback.Final(ScrubKeeps(s));
  KeepToBudget(s);
  // A check's playback (Debug::replay_end_quits), once its script, if any, has nothing more to do: it plays on to the
  // end, and stops at the end's picture, whether it paused there by itself or was paused by the script.
  if (!TheOptions().debug.replay_end_quits || a.script_next < a.script.size())
    return;
  if (a.paused && !(a.front_done && a.play_step + 1 >= a.end) && !std::exchange(a.played_on, true))
  {
    a.paused = false;
    Notice("fzgx: scrubber: the script is done: the check's playback plays on to the replay's end from step {}",
           a.play_step);
  }
  if (a.front_done && a.play_step + 1 >= a.end && !std::exchange(s.replay_ended, true))
  {
    Notice("fzgx: scrubber: the replay is at its end; the check's playback stops here");
    PlaybackSummary(g, s);
    TheHost().RequestStop();
  }
}

void ScrubExtra(Guest& g, Session& s)
{
  ScrubState& a = s.scrub;
  const u32 t = g.Read32(STEP);
  if (!a.in_front)
  {
    // a step run again at the play position just ran (the play step is a frame's last: no hook comes after it)
    s.rollback.SaveStep(g, t, true);
    if (t > 0)
      CheckPlayStep(g, s, t - 1, "run again from a key");
    if (a.catch_left > 0)
      return CatchStep(g, s, false);
    return PlayStep(g, s);
  }
  const double ms = Ms(Steady::now() - a.hook_left);
  a.step_ms = a.step_ms == 0 ? ms : a.step_ms * 0.8 + ms * 0.2;
  TookFrontStep(g, s, t);
  if (a.play_next)
  {
    Exchange(g, s);
    OwnWorkBegins(a);
    if (a.catch_left > 0)
      return CatchStep(g, s, false);
    PlayStep(g, s);
    return;
  }
  Rows(g, s, t);
  ++a.front_steps;
  const bool more = More(s, t + 1);
  Rollback::Resimulate(g, more || a.catch_left > 0 ? 3 : 2);
  a.play_next = !more;
  a.hook_left = Steady::now();
}

// The image ended the frame's steps. With the front's memory still in, it did so at the step that made the front's
// race look over: that step is taken, the front edge stands, and the play position's memory comes back.
void StepsDone(Guest& g)
{
  if (!s_session || !s_session->scrub.in_front)
    return;
  Session& s = *s_session;
  ScrubState& a = s.scrub;
  const u32 t = g.Read32(STEP);
  if (t > 0)
    Rollback::KeepChecksum(g, t - 1, Rollback::Checksum(g));
  TookFrontStep(g, s, t);
  a.front_done = true;
  a.end = t - 1;
  Exchange(g, s);
  if (a.catch_left > 0)
  {
    // steps of the play position to run again were queued after the front's: none ran, and the memory is still
    // before step catch_at. That is the step the next frame starts from, and the move is asked for again there.
    a.wait_for = a.catch_at + a.catch_left;
    a.catch_left = 0;
    g.Write32(STEP, a.catch_at);
    Rows(g, s, a.catch_at);
    g.Write32(SOUNDS_FROM, SILENT);
    ++a.cut_catches;
    Notice("fzgx: scrubber: steps run again from step {} were queued at the race's end, and none ran: the move to step "
           "{} is taken again in the next frame", a.catch_at, a.wait_for);
  }
  else
    PlayStep(g, s);
  a.play_ran = false;
  a.own_open = false;  // (the frame shows no picture: no own work to measure)
  a.cut = true;
  ++a.cuts;
  Notice("fzgx: scrubber: the front edge stands at step {}: the race is over after step {}, which is the replay's end "
         "and is never run for a picture (the frame showed no new picture; the play position is at step {}); "
         "buffered in {:.1f} s, {} frames after the race's first", t, t - 1, a.play_step,
         Ms(Steady::now() - a.race_at) / 1000, a.frames);
}

void ScrubStop(Session& s)
{
  ScrubState& a = s.scrub;
  if (!a.forked)
    return;
  a.forked = false;
  a.over = true;
  a.in_front = false;
  s.front.Stop();
  a.other = Rollback::Moment{};
}
}  // namespace fzgx::net
