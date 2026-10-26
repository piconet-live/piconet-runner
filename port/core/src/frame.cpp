// A displayed frame of the race: what it does before its own step (NormalStep): lockstep, or prediction and
// rollback. NormalStep is the list of its steps, which are here in its order; the two of the time sync (how far
// ahead of its schedule the game is, a step more or a frame held) are schedule.cpp's.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "net_internal.h"

#include "board_net.h"
#include "host_timers.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <optional>
#include <utility>

// Rollback: during the race (MD_GAME phase 0x29) the game runs
// ahead of the official frames, predicting each remote seat's input as its last official one. What every step
// changes in the simulation's memory (main.rel's .data and .bss and the heap up to 0x8155B000, and the DOL's
// random generator) goes to an undo log (PreStep); when an official frame differs from the
// inputs a step used, the memory goes back to before that step and the mod's frame routine re-simulates up to the
// present inside the displayed frame (its extra steps, which call the hook before each). Outside the race phase (the
// countdown, the results) it waits for each official frame (lockstep): the phase changes there happen outside the
// simulation steps and cannot be re-simulated.

namespace fzgx::net
{
namespace
{
void SendChecksums(Guest& g, Session& s)
{
  // steps below final_steps are final; report every 60th
  while (s.checksums_sent + 60 <= s.final_steps)
  {
    s.checksums_sent += 60;
    const u32 step = s.checksums_sent - 1;
    const u32 value = Rollback::KeptChecksum(g, step);
    Send(s, relay::Encode(relay::Checksum{.token = s.token, .cookie = s.cookie, .frame = step, .value = value}));
  }
}

// Tests: the "AI drives" record (steering 0x80, outside a stick's range): the mod's seat routine keeps the AI's own
// controls for a seat whose record this is, so an autopilot game's own machine races like a CPU, the same everywhere.
constexpr std::array<u8, RECORD> AI_DRIVES = {0, 0x80, 0, 0, 0, 0};

// Lockstep catching up (NormalStep): the emulation speed and CPU clock while it does, and back.
void SetCatchUp(Session& s, bool on)
{
  s.speeding = on;
  const float clock = on ? std::min(s.normal_clock, CATCH_UP_CLOCK) : s.normal_clock;
  TheHost().SetPace(on ? CATCH_UP_SPEED : s.normal_speed, clock);
}

// The steps of NormalStep, in its order.

// The local seat's input, to the relay; and the race as it stands, to the launcher (the status board).
void SendLocalInput(Guest& g, Session& s, u32 step, u32 module)
{
  // the local pad now is the local seat's input for step + delay
  if (s.autopilot)
    s.pad = AI_DRIVES;
  else
    g.CopyFrom(s.pad.data(), (module + PAD_RECORD), RECORD);
  // Kept, when the step already has one: a frame that ran no new step (held, below) is followed by a frame that
  // begins at the same step, and the input sent in the first of the two is the one the relay keeps for it (the
  // first it receives for a frame: Online.Server RaceTimeline). Taking the second, this game ran its own machine
  // on an input the relay never used and was put right a round trip later.
  if (!SendWatchersInput(s, step))
    NoteOwnInput(s, step + s.delay, s.my_inputs.try_emplace(step + s.delay, PadFor(s, step + s.delay)).second);
  s.frontier = std::max(s.frontier, step + 1);
  SendInputs(s);
  Ping(s);
  if (!s.playback)
    board::Race(g, step, s.my_seat);
}

// The steps that are final by now: their count to the mod, their checksums to the relay, their rows to the replay.
void CloseFinalSteps(Guest& g, Session& s)
{
  s.final_steps = s.checked;
  g.Write32(FINAL_STEPS, s.final_steps);
  SendChecksums(g, s);
  if (!s.playback)
    s.recorder.Record(g, s.final_steps, s.seats);
}

// A playback's frame: the check against the recording, the cameras. False: the recording has ended, and the frame
// runs no step of it (with the scrubber the replay pauses at its end instead: scrub_move.cpp).
bool PlaybackFrame(Guest& g, Session& s, u32 step, u32 module)
{
  VerifyReplay(g, s);
  SpectateReplay(g, s, step);
  SpectatorFrame(g, s, step, module);
  if (step >= s.official.size())
  {
    // the recording ends here (the player left the results): stop, as the player did
    if (!s.replay_ended)
    {
      s.replay_ended = true;
      freeze::ThePulse().racing = false;
      // every step of a playback ran with the recording's inputs: the last ones (held back while rollbacks are
      // forced) are final too
      s.final_steps = step;
      VerifyReplay(g, s);
      Notice("fzgx: the replay ended at step {}; {} steps differed", step, s.desyncs);
      PlaybackSummary(g, s);
      TheHost().RequestStop();
    }
    return false;
  }
  return true;
}

// What a frame is: in the game's race phase or not; a race's frame, which runs ahead on predicted inputs and rolls
// back; the frame that settles the race, the last with a rollback. Neither of the two: lockstep.
struct FrameKind
{
  bool race_phase = false;
  bool race = false;
  bool settle = false;
  bool check = false;  // a race's frame that checks the steps the race's end rests on, and runs no new step
};

// The race's end, held until it is official. The image's race-over test runs once a
// displayed frame, before this hook, on what the frame before left; with every machine finished or out it says
// "over" only when no step below the one about to run is a prediction (END_OFFICIAL: OfficialSteps), and asks
// otherwise (END_ASKED). Gives whether it asked, and clears the word.
// The image also runs no further step in a frame once a step has made the race look over (frame_entry.py,
// `extra_cut`): the frame before may have ended below the step it was given, and the steps it left out are not
// final, so `checked` is cut to the step the game stands at (before CloseFinalSteps reads it).
bool TakeTheEndsQuestion(Guest& g, Session& s, u32 step)
{
  s.checked = std::min(s.checked, step);
  const bool asked = g.Read32(END_ASKED) != 0;
  if (asked)
    g.Write32(END_ASKED, 0);
  return asked;
}

// Says to the image how far the official steps go: every step below `steps` ran with the official inputs, or runs
// with them in this frame. It stays true: a step below it that is run again takes its official row.
void OfficialSteps(Guest& g, u32 steps)
{
  g.Write32(END_OFFICIAL, steps);
}

// The frame's kind; with late rows (tests), the rows known by now. Says once what it finds: the presentation as the
// race settles, the race phase back after it.
FrameKind KindOfFrame(Guest& g, Session& s, u32 step, u32 module, const Forcing& forcing, bool end_asked)
{
  // A playback runs in lockstep throughout: every official frame is here. With rollbacks forced (tests) its race
  // phase goes through the rollback path instead, every step still run with the recording's inputs: the check that
  // a restore and a re-simulation give the recorded race.
  // The same with the recording's rows known late (tests): the race then runs on predictions and rolls back where
  // one was wrong. The lateness ends 230 steps before the recording's end, so that every prediction is checked
  // before the replay ends, and when the race settles.
  const u32 late = s.playback ? TheOptions().debug.late_steps : 0;
  if (late != 0)
  {
    if (step + 230 >= s.official.size())
      s.all_known = true;
    // (what lockstep ran before the race is known)
    s.known_rows = std::max({s.known_rows, s.checked, step > late ? step - late : 0});
  }
  // Not once the race has settled (below): no step is predicted after that check, whatever the phase word says.
  // Nor once the image's race-over test has said "over" (END_OVER): the game asks for the results phase when its
  // finish sequence is done, seconds later, and no step up to there may be one a rollback could go back before (it
  // would take the request and the results phase's first frame with it: the race ended a second time).
  const bool race_phase = g.Read16(module + PHASE) == 0x29;
  const bool end_over = g.Read32(END_OVER) != 0;
  const bool behind = s.watch == Watch::Behind;  // watching behind live, in lockstep (spectate.cpp)
  const bool race = race_phase && !s.settled && !end_over && !behind &&
                    (!s.playback || forcing.every != 0 || late != 0 || ScrubOn(s));
  // Leaving the race (the results, phase 0x2A), the steps simulated with predicted inputs are checked first: one last
  // rollback frame waits for the official frames up to its own step, puts back the first step whose prediction was
  // wrong and re-simulates from it; lockstep takes over from the next frame. (When lockstep took over at once, the
  // race's last steps, up to a round trip of them, stayed as predicted: a game whose predictions were wrong ended the
  // race differently from the official one, and its replay differed from that step on.)
  // That last rollback takes the phase word back with the rest of the memory when it goes back before the race's
  // end, and the game then ends the race a second time, a frame or more later.
  // Those frames are in the race phase again and run in lockstep all the same: run as a race's, their steps were
  // predicted (a catch-up step among them) and no frame checked them any more, so a seat whose input changed in
  // them was simulated with one input here and another in the other games, and each game recorded its own as final.
  const bool settle = !race && !behind && s.rollback.Active() && !s.settled;
  const bool settles = settle || SettleTheWatching(s, race_phase, end_over);
  if (settles)
    ReportPresentation(g, s);
  if (settles && end_over)
  {
    Notice("fzgx: the race is over before step {}: every step before it ran with the official inputs", step);
    // With the host's rule that ends a race so long after the first finish (the image's raceend.py), machines may
    // still be racing here, and they race on behind the results: the recording's results are the standing at this
    // step, where the race ended for everyone, and not what those machines have come to some frames later, when
    // the game is out of the race phase (LockstepFrame). The memory is the official race's: the image said "over"
    // only with every step below this one run on the official inputs. So too with the race's flow (the image's
    // raceend.py): the race then waits for no computer racer. Without either nothing is taken here.
    const bool may_race_on = g.Read32(table::RACE_FINISH_LIMIT_AT) != 0 || g.Read32(table::RACE_FLOW_AT) != 0;
    if (may_race_on && !s.playback && s.recorder.IsOpen() && !s.recorder.HasResults())
      s.recorder.TakeResults(g);
  }
  // (Not after the race-over test's "over": the game then stays in the race phase by itself, for the seconds of its
  // finish sequence, and those frames are lockstep's by design.)
  if (race_phase && s.settled && !end_over && !s.playback && !s.race_back)
  {
    s.race_back = true;
    Notice("fzgx: the race phase is back at step {} (the check of the race's last steps went back before its end); "
           "in lockstep from here",
           step);
  }
  // The frame the image asked for (TakeTheEndsQuestion): a race's frame with a step before its own not yet final.
  return {race_phase, race, settle, end_asked && race && step > s.checked};
}

// A frame in lockstep: it waits for its step's official frame and runs that step alone.
void LockstepFrame(Guest& g, Session& s, u32 step, bool race_phase)
{
  // lockstep
  // The results a recording carries are taken here: in the first frame after the race settled that finds the game
  // out of the race phase. Every step before this one has then run on official inputs, and the race is over in
  // that state. (They were taken in the settling frame itself, before its rollback: from the state the predicted
  // steps had left. A racer whose finish lay in those steps got the predicted finish's time and place in this
  // game's recording, and when the rollback went back before the race's end, results of a race not yet over.)
  if (s.settled && !race_phase && !s.playback && s.recorder.IsOpen() && !s.recorder.HasResults())
    s.recorder.TakeResults(g);
  const bool watching = s.watch == Watch::Behind;  // its own slack, never CATCH_UP_SPEED (spectate.cpp)
  BeforeTheWatchersWait(s, step);
  if (!WaitFor(s, step + 1))
    return Fail(g, s, step);
  if (s.rollback.Active())
  {
    s.rollback.Stop();
    s.used.clear();
    ScrubStop(s);
  }
  WriteRow(g, s, step, s.official[step]);
  s.checked = step + 1;
  OfficialSteps(g, step + 1);
  // Official frames beyond the next ones are here when the game fell behind the relay: the step into the countdown
  // (MD_GAME phase 0x27 to 0x28) loads for about 0.9 s while the relay's clock runs on, and lockstep alone never made
  // that up, so the countdown ran about 55 frames behind (the local input that late) and the race began with
  // seconds of catching up. The emulation then runs at CATCH_UP_SPEED until the game is back, still one step per
  // displayed frame: a phase's own update runs once per displayed frame, not per step (the countdown's, fn_1_39C60,
  // starts the race and changes the race state on its way), so extra steps in a frame, tried first, put the race's
  // start at another step than in the other games and they desynced.
  const bool behind = !watching && static_cast<u32>(s.official.size()) - (step + 1) > LOCKSTEP_SLACK && !s.playback &&
                      !s.settled && step >= s.start_barrier;
  // Not after the race (settled): everything is decided and leaving the results is each player's own choice, so
  // the game plays the results at its own pace however far behind the relay it falls (switching into the results
  // stalls it as the countdown's load does; caught up at CATCH_UP_SPEED, their sound played fast).
  if (behind != s.speeding && !s.held)  // (held under LOADING the pace is lifted already: rejoin.cpp)
    SetCatchUp(s, behind);
  AfterTheWatchersStep(s, step);
  s.pace_last = Clock::time_point{};
  s.pace_owed_ms = 0;
  s.pace_long = s.pace_calm = 0;
  s.pace_burst_ms = s.pace_worst_ms = 0;
  Rollback::Resimulate(g, 0);
}

// A race's frame begins: the undo log on, the step's entry saved, and the waits for official frames (all of them
// in the frame that settles the race; those of every step below its own in the frame that checks the race's end;
// else those beyond the run-ahead limit). False: they did not come.
bool BeginRollbackFrame(Guest& g, Session& s, u32 step, bool settle, bool check)
{
  if (s.speeding)
    SetCatchUp(s, false);
  if (!s.rollback.Active())
    s.rollback.Start(g);
  s.rollback.SaveStep(g, step, true);
  s.work_open = true;
  if (settle)
  {
    if (!WaitFor(s, step + 1))
      return false;
    s.settled = true;
    s.all_known = true;
  }
  // The frame that checks the race's end: CheckPredictions then goes back to the first step predicted wrong, and
  // CatchUpOrHold (schedule.cpp) runs no new step, so that the game stands where it stood, before the same step,
  // when the image's test runs again in the next frame. If the race is over on the official inputs too, the test
  // says so there; if not, the race goes on predicted as before, one wait later.
  if (check)
  {
    if (!WaitFor(s, step))
      return false;
    s.known_rows = std::max(s.known_rows, step);
  }

  if (step >= s.official.size() + MAX_AHEAD && !WaitFor(s, step + 1 - MAX_AHEAD))
    return false;
  return true;
}

// The predictions checked against the official frames that are here; the memory back to before the first wrong step
// (or, tests, a forced rollback's).
FrameSteps CheckPredictions(Guest& g, Session& s, u32 step, bool settle, const Forcing& forcing)
{
  // the first step whose inputs turned out wrong, among the steps whose official frames are here
  const u32 arrived = std::min<u32>(Have(s), step);
  // (the first wrong one of the steps below is found and run again here; a step from `arrived` on takes its
  // official row when it runs)
  OfficialSteps(g, Have(s));
  // Steps below `known` become final in this frame. With rollbacks forced, the deepest of them stay open (their undo
  // entries kept) although their official frames are here, as if those frames came that much later: so a forced
  // rollback of any asked depth has steps to go back over, in a race with no lag and in a playback. Not in the
  // frame that settles the race: every step is checked there.
  const u32 hold = settle ? 0 : forcing.deepest;
  const u32 known = hold == 0 ? arrived : std::max(s.checked, std::min(arrived, step > hold ? step - hold : 0));
  std::optional<u32> wrong;
  for (u32 t = s.checked; t < arrived; ++t)
  {
    const auto it = s.used.find(t);
    if (it == s.used.end() || it->second != s.official[t])
    {
      wrong = t;
      break;
    }
  }
  // the test switch
  if (forcing.every != 0 && !wrong && !settle && s.normal_steps % forcing.every == 0)
  {
    const u32 depth = forcing.depths[s.force_next % forcing.depths.size()];
    if (step >= s.checked + depth)
    {
      wrong = step - depth;
      ++s.force_next;
      ++s.forced;
      s.work.forced = true;
    }
  }
  u32 run_from = wrong.value_or(step);
  if (wrong)
  {
    s.rollback.RestoreTo(g, step, run_from);
    ++s.rollbacks;
    s.max_depth = std::max(s.max_depth, step - run_from);
    s.summary.Rollback(step - run_from);
  }
  return {known, run_from};
}

// The frame's steps asked of the mod's frame routine, the steps checked made final, and the first step's inputs.
void RunSteps(Guest& g, Session& s, u32 step, const FrameSteps& steps)
{
  const auto [known, run_from, catch_up, held] = steps;
  // the steps the frame runs: from run_from to `step`, the catch-up steps after it; held, to the step before `step`
  const u32 extra = step - run_from + catch_up - (held ? 1 : 0);
  Rollback::Resimulate(g, extra);
  s.last_extra = extra;
  s.work.depth = step - run_from;
  s.work.catch_up = catch_up;
  s.work.held = held;
  s.work.extra = s.last_extra;
  s.checked = known;
  for (auto it = s.used.begin(); it != s.used.end() && it->first < s.checked;)
    it = s.used.erase(it);
  s.rollback.Final(s.checked);
  s.audit_first.erase(s.audit_first.begin(), s.audit_first.lower_bound(s.checked));

  Row row = RowFor(s, run_from);
  WriteRow(g, s, run_from, row);
  s.used[run_from] = std::move(row);
}
}  // namespace

// Before a displayed frame's own step s.
void NormalStep(Guest& g, Session& s)
{
  const u32 step = g.Read32(STEP);
  ++s.normal_steps;
  freeze::ThePulse().step.store(step, std::memory_order_relaxed);  // for the freeze report: one store, nothing read
  if (!TheOptions().debug.stand_steps.empty()) [[unlikely]]
    StandStill(s, step);
  const auto frame_now = Clock::now();
  {
    FZGX_TIME(FrameBegin);
    if (!s.playback)
      TakePictureTimes(s);
    ScrubVerify(g, s);
    TakeLastFramesWork(g, s, step, frame_now);
    ReportFrameTimes(g, s);
  }
  const u32 module = g.Read32(MODULE_POINTER);
  StartBlackout(s, step);
  CatchingUpOrLive(g, s, step);
  SayTheRaceIsOver(g, s, step);
  const bool end_asked = TakeTheEndsQuestion(g, s, step);
  FollowTheLocalRacer(g, s, module);
  ResultsCamera(g, s, module);
  if (!s.playback)
    SpectatorFrame(g, s, step, module);  // (a live watcher's controls; a playback's come with PlaybackFrame)
  {
    FZGX_TIME(SendInput);
    SendLocalInput(g, s, step, module);
  }
  {
    FZGX_TIME(CloseFinal);
    CloseFinalSteps(g, s);
  }
  if (s.playback && !PlaybackFrame(g, s, step, module))
    return;

  const Forcing forcing = ForcedRollbacks(g);
  const FrameKind kind = KindOfFrame(g, s, step, module, forcing, end_asked);
  // (for the freeze report: the countdown is over at the first frame that runs ahead; written when it changes)
  if (std::atomic<bool>& countdown = freeze::ThePulse().countdown; countdown.load(std::memory_order_relaxed) && (kind.race || kind.settle))
    countdown.store(false, std::memory_order_relaxed);
  if (!kind.race && !kind.settle)
  {
    // (the summary's tally of a frame in lockstep: one read of the clock, where the schedule reads none)
    if (!s.playback && !TallyTheWatchersFrame(s, kind.race_phase, frame_now, step))
      s.summary.Frame(s.summary.phase == RaceSummary::Countdown ? RaceSummary::Countdown : RaceSummary::After,
                      frame_now, -StepsAhead(s, step), step, step + s.delay);
    ScrubBarOutside(s, step);
    return LockstepFrame(g, s, step, kind.race_phase);
  }
  if (kind.race && ScrubOn(s))  // (the replay scrubber: the frame is its own)
  {
    FZGX_TIME(ScrubFrame);
    return ScrubFrame(g, s, step);
  }

  bool begun;
  {
    FZGX_TIME(RollbackBegin);
    begun = BeginRollbackFrame(g, s, step, kind.settle, kind.check);
  }
  if (!begun)
    return Fail(g, s, step);
  if (kind.race && !kind.settle && !kind.check)  // (a frame that may skip its drawing when late: present.cpp)
    PresentRaceFrame();
  FrameSteps steps;
  {
    FZGX_TIME(Predictions);
    steps = CheckPredictions(g, s, step, kind.settle, forcing);
  }
  const double ahead = StepsAhead(s, step);
  if (!s.playback && !TallyTheWatchersFrame(s, true, frame_now, step))  // (the schedule's own figure, no clock read)
    s.summary.Frame(RaceSummary::Race, frame_now, -ahead, step, step + s.delay);
  steps = CatchUpOrHold(g, s, step, kind.settle, kind.check, frame_now, ahead, steps);
  {
    FZGX_TIME(RunSteps);
    RunSteps(g, s, step, steps);
  }
  FZGX_TIME(FrameEnd);
  AfterADriftingFrame(s, step, steps);
  ReportCounters(g, s, step);
}
}  // namespace fzgx::net
