// A game whose racer is out or has finished watches the race a little behind live, on official frames only: it
// never predicts and never rolls back, so every machine moves as in a replay (a watched machine predicted and rolled
// back is jumpy).
// SPDX-License-Identifier: GPL-3.0-or-later
//
// While its racer races a game runs ahead of the official frames and rolls back where a prediction was wrong. A
// watching game has no input of its own that matters, so it predicted every racer and rolled back in almost every
// frame (2,887 of 2,915 frames in run cam-r2c), and the cameras follow the machines' true places, which a rollback
// moves: the watched machine twitched against the camera. Here, in three states (Watch):
// - Racing: as before.
// - Drifting, from the frame the local seat is out (the image's count, OUT_FRAMES: out by the game's own verdict,
//   frame_spectate.py and outtest.py, so never for a wreck the Restore rule puts back, and a breakdown only at
//   rest) or has finished (its machine's flags, counted here the same way): the race's frames go on as before, one step a
//   frame, but the emulation runs at SPECTATE_SLOWEST (PaceTheWatching), so that the game falls behind the
//   official frames, gently, over some 15 s and more (chosen over a faster fall by the camera cut); no
//   catch-up step and no held frame for the schedule (schedule.cpp). The frames it ran ahead on are checked and
//   put right by the usual rollbacks as the official frames come.
//   The seat may be out only in a prediction: the frame that finds it back races again, at the host's speed.
// - Behind, once a drifting frame's steps all ran on official inputs with SPECTATE_INTO_LOCKSTEP more official
//   frames in hand (AfterADriftingFrame) and the next frame still finds the seat out, now on the official race
//   (FollowTheRacer): lockstep, the countdown's and the results' mode (frame.cpp LockstepFrame), with the
//   spectator's slack in place of LOCKSTEP_SLACK: the game stays about `slack` official frames behind the newest,
//   and gets there and keeps there only by playing a little slower or faster (SPECTATE_SLOWEST to
//   SPECTATE_CATCH_UP_MOST), never by lockstep's catch-up at three times the speed. A frame whose official frame
//   is not here yet waits for it, as lockstep does (a frozen picture; counted, and the slack is raised for the rest
//   of the race).
// A watcher's inputs reach the relay as a racer's do: one for every frame, sent by the step the time sync aims at
// (SendWatchersInput) and not by the step the game runs, which is behind, so the relay sees no input late; the
// pad while drifting, the neutral record once behind. Its checksums are sent as its steps become final, `slack`
// frames and the frames' way here later than a racer's: well inside the relay's vote window of two seconds
// (Online.Server ChecksumVote); a report that came after it would be ignored, never counted against anyone.
// The constants (net_internal.h). The slack is the official frames it keeps in
// hand past the step it runs (lockstep's own is LOCKSTEP_SLACK: a racer's countdown and results), so that the
// frames' arrival jitter never leaves it waiting; a wait raises it (SPECTATE_SLACK_MOST at most). It gets there
// by playing a little slower, SPECTATE_SLOWEST (a gentle switch, the slack filled over some 15 s and more), never
// by a frozen picture or a jump. Behind by more than the slack it plays faster, from
// SPECTATE_FASTEST near it up to SPECTATE_CATCH_UP_MOST from SPECTATE_FAR frames over it on (a stall's frames are
// made up in seconds, inside the relay's checksum vote, and a host slower than the relay still keeps up). A host
// whose clock runs slow against the relay's falls behind less at that speed (by 1.2% in the WSL test races, which
// is all of it): the mean of the frames in hand over each SPECTATE_DRIFT_CHECK frames is held against the window's
// before, and when two windows in a row gained less than half of what SPECTATE_SLOWEST gives on a host whose clock
// is the relay's, or one gained nothing, the speed goes SPECTATE_DRIFT_STEP lower, to SPECTATE_DRIFT_FLOOR at most. A
// window with half a second (SPECTATE_GAP frames) in which no official frame came is the network's, not the clock's:
// it starts the windows over, as a wait does.
// SPECTATE_CUT: the frames after which the image's spectator camera takes over (frame_spectate.py).
// Nothing of this changes what a step computes: every step runs with the official inputs, as in any game.

#include "net_internal.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

namespace fzgx::net
{
namespace
{
constexpr u32 FINISHED = 0x10000;  // a machine's flags: it has crossed the line (frame_spectate.py)
constexpr u32 MACHINE_SIZE = 0x620;
constexpr double BUFFER_SMOOTHING = 16;
constexpr std::array<u8, RECORD> NEUTRAL{};  // frames: the official frames in hand are taken as an average this long

// A word of the game's main memory, or 0 where there is none.
u32 MainWord(Guest& g, u32 address)
{
  return g.Ram(address, 4) != nullptr ? g.Read32(address) : 0;
}

u16 MainHalf(Guest& g, u32 address)
{
  return g.Ram(address, 2) != nullptr ? g.Read16(address) : 0;
}

float AsFloat(u32 word)
{
  float value;
  std::memcpy(&value, &word, 4);
  return value;
}

// The local machine: its flags word's place, or 0.
u32 LocalMachine(Guest& g, u32 module)
{
  const u32 seat = g.Read32(table::SLOT + 4);
  const u32 machines = MainWord(g, module + MACHINES);
  return seat < 30 && machines != 0 ? machines + seat * MACHINE_SIZE : 0;
}

// (the host's speed is asked too: lockstep's catch-up may have set its own since)
void SetWatchSpeed(Session& s, float speed)
{
  if (speed == s.watch_speed && TheHost().EmulationSpeed() == s.normal_speed * speed)
    return;
  s.watch_speed = speed;
  s.watch_slowest = std::min(s.watch_slowest, speed);
  s.watch_fastest = std::max(s.watch_fastest, speed);
  TheHost().SetPace(s.normal_speed * speed, s.normal_clock);
}

void FollowTheRacer(Guest& g, Session& s, u32 module);
void PaceTheWatching(Session& s, u32 step);
void WatchedPlaces(Guest& g, Session& s, u32 module);
}  // namespace

// Before a race's displayed frame: whether the local racer is out or has finished, and so whether the game watches.
void FollowTheLocalRacer(Guest& g, Session& s, u32 module)
{
  FollowTheRacer(g, s, module);
  s.work.watch = s.watch;
  s.work.speed = s.watch_speed;
  WatchedPlaces(g, s, module);
}

namespace
{
void FollowTheRacer(Guest& g, Session& s, u32 module)
{
  if (s.watcher && !s.playback && s.watch != Watch::Behind && !s.held && !s.returning &&
      g.Read16(module + PHASE) == 0x29)
  {
    // a spectator (relay_wire.cpp), from the race phase on (the countdown is lockstep's as for any game, and
    // watching behind before the race would settle it: SettleTheWatching): every step it runs from here is on
    // official frames, behind live as a racer who is out watches once it has fallen behind, with nothing to drift
    // from (its catch-up, if any, is over)
    s.watch = Watch::Behind;
    s.buffer_seen = false;
    s.buffer_full = false;
    s.drift_frames = 0;
    s.drift_sum = 0;
    s.drift_before = std::numeric_limits<double>::quiet_NaN();
    s.drift_short = 0;
    s.drift_gap = false;
    Notice("fzgx: step {}: a spectator: watching in lockstep on official frames, {} in hand", g.Read32(STEP),
           Have(s) - std::min<u32>(Have(s), g.Read32(STEP)));
    return;
  }
  if (s.playback || s.watcher || s.watch == Watch::Behind || s.my_index < 0)
    return;
  // Read on what the frame before left: on a frame after one that ran only official steps (behind_next), the
  // official race's; else maybe a prediction's, which a rollback can take back (the image's count then goes back
  // to 0, and the flags with the memory).
  const bool race_on = g.Read16(module + PHASE) == 0x29 && !s.settled && g.Read32(END_OVER) == 0;
  u32 out = 0;
  if (race_on)
  {
    const u32 machine = LocalMachine(g, module);
    const bool finished = machine != 0 && (MainWord(g, machine) & FINISHED) != 0;
    s.finished_frames = finished ? std::min(s.finished_frames + 1, SPECTATE_CUT) : 0;
    out = std::max(std::min(g.Read32(OUT_FRAMES), SPECTATE_CUT), s.finished_frames);
  }
  else
    s.finished_frames = 0;
  const bool confirm = s.behind_next;
  s.behind_next = false;
  if (out != 0 && s.watch == Watch::Racing)
  {
    s.watch = Watch::Drifting;
    s.buffer_seen = false;
    s.drift_frames = 0;
    s.drift_sum = 0;
    s.drift_before = std::numeric_limits<double>::quiet_NaN();
    s.drift_short = 0;
    s.drift_gap = false;
    Notice("fzgx: step {}: the local racer is {}; watching from here, falling behind live to official frames only",
           g.Read32(STEP), s.finished_frames != 0 ? "over the line" : "out");
  }
  else if (out == 0 && s.watch == Watch::Drifting)
  {
    // (a knock-out a rollback took back, or the race ended first; its inputs go on by the schedule until the
    // game's own steps overtake it: SendWatchersInput)
    s.watch = Watch::Racing;
    SetWatchSpeed(s, 1.0f);
    Notice("fzgx: step {}: no longer watching: {}", g.Read32(STEP), race_on ? "the local racer races again" : "the race is over");
  }
  else if (out != 0 && confirm && s.watch == Watch::Drifting)
  {
    // every step so far ran on official inputs: the seat is out (or over the line) in the official race
    s.watch = Watch::Behind;
    Notice("fzgx: step {}: watching in lockstep on official frames, {} in hand; rollbacks so far {}", g.Read32(STEP),
           Have(s) - g.Read32(STEP), s.rollbacks);
  }
}
}  // namespace

// A watching game's input, for every frame up to the one a racer's game would send it for now (the schedule's step
// and the input delay), each frame once, so the relay sees no input late while the game falls behind. While it
// drifts, the pad: the seat may be out only in a prediction, and its inputs then are the player's. Once behind, the
// neutral record (no stick, no button: the relay's own for a seat before its first input): from there the race and
// its recordings hold no spectator's press on a machine that is out (the image reads the spectator's buttons from
// the pad itself); the presses made while it fell behind are in them as the seat's inputs. Never no record: the relay would hold the seat's last input. False: the game races, and sends as a
// racer.
// A game racing again after it drifted is behind its schedule: it sends the pad by the schedule, as while it drifted,
// until it is within a frame of the schedule (the time sync's own band), the frames between filled.
bool SendWatchersInput(Session& s, u32 step)
{
  if (s.watch == Watch::Racing && s.next_input == 0)
    return false;
  const double scheduled = std::max(0.0, std::floor(ScheduledStep(s) + 0.5));
  const u32 to = static_cast<u32>(std::min(scheduled, 4.0e9)) + s.delay;
  if (s.watch == Watch::Racing && step + s.delay + 1 >= to)
  {
    for (u32 f = s.next_input; f < step + s.delay; ++f)
      NoteOwnInput(s, f, s.my_inputs.try_emplace(f, s.pad).second);
    s.next_input = 0;
    return false;
  }
  if (s.next_input == 0)
    s.next_input = s.my_inputs.empty() ? std::min(to, step + s.delay) : s.my_inputs.rbegin()->first + 1;
  const std::array<u8, RECORD>& record = s.watch == Watch::Behind ? NEUTRAL : s.pad;
  for (u32 f = std::max(s.next_input, to + 1 > MAX_FRAMES ? to + 1 - MAX_FRAMES : 0); f <= to; ++f)
    NoteOwnInput(s, f, s.my_inputs.try_emplace(f, record).second);
  s.next_input = std::max(s.next_input, to + 1);
  return true;
}

namespace
{
// A watching frame's speed: a little slower until the slack is in hand, then 1, a little slower or a little faster
// to keep it there.
void PaceTheWatching(Session& s, u32 step)
{
  const double buffer = static_cast<double>(Have(s)) - static_cast<double>(step + 1);
  s.buffer_avg = s.buffer_seen ? s.buffer_avg + (buffer - s.buffer_avg) / BUFFER_SMOOTHING : buffer;
  s.buffer_seen = true;
  const double slack = s.slack;
  if (!s.buffer_full && s.buffer_avg >= slack - 0.5)
  {
    s.buffer_full = true;
    Notice("fzgx: step {}: watching {:.1f} official frames behind the newest", step, s.buffer_avg);
  }
  float speed;
  if (!s.buffer_full)
  {
    // the window's mean against the one before: a host whose clock is the relay's gains 1 - SPECTATE_SLOWEST a frame
    s.drift_sum += buffer;
    s.drift_still = Have(s) == s.drift_have ? s.drift_still + 1 : 0;
    s.drift_have = Have(s);
    s.drift_gap = s.drift_gap || s.drift_still >= SPECTATE_GAP;
    if (++s.drift_frames >= SPECTATE_DRIFT_CHECK && s.drift_gap)
    {
      // (a gap in the official frames coming, not the clocks: no window of this is held against another)
      s.drift_gap = false;
      s.drift_short = 0;
      s.drift_before = std::numeric_limits<double>::quiet_NaN();
      s.drift_frames = 0;
      s.drift_sum = 0;
    }
    else if (s.drift_frames >= SPECTATE_DRIFT_CHECK)
    {
      const double mean = s.drift_sum / SPECTATE_DRIFT_CHECK;
      const double gained = mean - s.drift_before;
      const double should = SPECTATE_DRIFT_CHECK * (1.0 - SPECTATE_SLOWEST);
      // (two windows in a row, one can hold a race's own jumps of the frames in hand; one that gained nothing at all
      // is enough; false while the first is NaN)
      s.drift_short = gained < should / 2 ? s.drift_short + 1 : 0;
      if ((s.drift_short >= 2 || gained <= 0) && s.drift_speed > SPECTATE_DRIFT_FLOOR + 1e-6)
      {
        s.drift_short = 0;
        s.drift_speed = std::max(s.drift_speed - SPECTATE_DRIFT_STEP, SPECTATE_DRIFT_FLOOR);
        Notice("fzgx: step {}: watching falls behind more slowly than its speed says ({:.1f} frames in {}); speed "
               "{:.3f} from here", step, gained, SPECTATE_DRIFT_CHECK, s.drift_speed);
      }
      s.drift_before = mean;
      s.drift_frames = 0;
      s.drift_sum = 0;
    }
    speed = s.drift_speed;
  }
  else if (s.buffer_avg < slack - 3)
    speed = s.drift_speed;
  else if (s.buffer_avg > slack + 4)
  {
    // further behind, faster: SPECTATE_FASTEST near the slack, SPECTATE_CATCH_UP_MOST from SPECTATE_FAR over it
    const double over = std::min(1.0, (s.buffer_avg - slack - 4) / (SPECTATE_FAR - 4));
    speed = static_cast<float>(SPECTATE_FASTEST + over * (SPECTATE_CATCH_UP_MOST - SPECTATE_FASTEST));
  }
  else if (std::abs(s.buffer_avg - slack) < 1)
    speed = 1.0f;
  else
    speed = std::clamp(s.watch_speed, s.drift_speed, SPECTATE_FASTEST);
  SetWatchSpeed(s, std::round(speed * 200) / 200);
  s.work.speed = s.watch_speed;
  s.work.buffer = buffer;
}
}  // namespace

// A race's frame of a drifting game, before its steps run: its speed; and when every step it runs has its official
// frame here, and more are in hand, the next frame is lockstep's if the seat is still out then (FollowTheRacer).
void AfterADriftingFrame(Session& s, u32 step, const FrameSteps& steps)
{
  if (s.watch != Watch::Drifting)
    return;
  PaceTheWatching(s, step);
  // (the steps run after this hook: whether the seat is out on them is read when the next frame begins)
  s.behind_next = steps.catch_up == 0 && !steps.held && Have(s) >= step + 1 + SPECTATE_INTO_LOCKSTEP;
}

// The race's end for a game watching behind (frame.cpp KindOfFrame): it has nothing to check, every step it ran
// had its official frame; its race settles in the first frame out of the race phase, or once the image's race-over
// test said "over". True in that frame.
bool SettleTheWatching(Session& s, bool race_phase, bool end_over)
{
  if (s.watch != Watch::Behind || s.settled || (race_phase && !end_over))
    return false;
  s.settled = true;
  s.all_known = true;
  return true;
}

// A lockstep frame of a game watching behind, before its wait: when its official frame is not here, the slack was
// not enough. The picture stands until the frame comes (LockstepFrame's wait), and the slack is raised for the rest
// of the race.
void BeforeTheWatchersWait(Session& s, u32 step)
{
  if (s.watch != Watch::Behind || s.official.size() >= step + 1)
    return;
  ++s.watch_waits;
  if (s.slack < SPECTATE_SLACK_MOST)
  {
    s.slack = std::min(s.slack + 3, SPECTATE_SLACK_MOST);
    s.buffer_full = false;
    s.drift_frames = 0;
    s.drift_sum = 0;
    s.drift_before = std::numeric_limits<double>::quiet_NaN();
    s.drift_short = 0;
    s.drift_gap = false;
    Notice("fzgx: step {}: watching waited for an official frame; slack {} from here", step, s.slack);
  }
}

// ... and after: its speed (after the race, the host's own), and a line of the rollback log (depth 0).
void AfterTheWatchersStep(Session& s, u32 step)
{
  if (s.watch != Watch::Behind)
    return;
  ++s.watch_frames;
  s.work_open = true;
  s.work.sched = StepsAhead(s, step);
  if (!s.settled)
    PaceTheWatching(s, step);
  else
  {
    SetWatchSpeed(s, 1.0f);
    s.work.speed = s.watch_speed;
  }
}

// The race summary's tally of a watching game's race frame (frame.cpp NormalStep): how far its inputs are behind
// the schedule (as a racer's: below 0, ahead) and the frame of its latest. False: not a watching game's race frame.
bool TallyTheWatchersFrame(Session& s, bool race_phase, Clock::time_point now, u32 step)
{
  if (s.watch == Watch::Racing || !race_phase || s.settled || s.next_input == 0)
    return false;
  const double behind = ScheduledStep(s) - (static_cast<double>(s.next_input) - 1 - s.delay);
  s.summary.Frame(RaceSummary::Race, now, behind, step, s.next_input - 1);
  return true;
}

namespace
{
// Tests (with the rollback log): as a race's frame begins, who the cameras follow, the camera's place and that
// racer's drawn and true places, as the frame before showed them. The camera: frame_specwatch.py and main.rel's
// fn_1_681C (the TV cameras, mode 9 or 10: [.bss 0xF70] +0x1C; else the record of screen 0, [.bss 0xF6C] +0x10).
// The drawn place: smooth.py's per seat +24 (a rival's; the local racer is drawn where it is).
void WatchedPlaces(Guest& g, Session& s, u32 module)
{
  if (!s.work_log || g.Read16(module + PHASE) != 0x29)
    return;
  const u32 bss = module + BSS;
  const u32 state = MainWord(g, bss + 0xF68);
  if (state == 0)
    return;
  const u8 mode = g.Ram(state + 0x48, 1) != nullptr ? g.Read8(state + 0x48) : 0;
  const bool tv = mode == 9 || mode == 10;
  const u32 live = MainWord(g, bss + 0xF70), record = MainWord(g, bss + 0xF6C);
  const u32 holder = tv ? live : record;
  if (holder == 0)
    return;
  const u32 seat = MainHalf(g, holder + (tv ? 8 : 2));
  const u32 machines = MainWord(g, module + MACHINES);
  if (seat >= 30 || machines == 0)
    return;
  s.work.watched = static_cast<int>(seat);
  const u32 camera = tv ? live + 0x1C : record + 0x10;
  const u32 machine = machines + seat * MACHINE_SIZE + 0x7C;
  const bool local = seat == g.Read32(table::SLOT + 4);
  for (u32 k = 0; k < 3; ++k)
  {
    s.work.places[k] = AsFloat(MainWord(g, camera + 4 * k));
    s.work.places[6 + k] = AsFloat(MainWord(g, machine + 4 * k));
    s.work.places[3 + k] = local ? s.work.places[6 + k] : AsFloat(g.Read32(layout::SMOOTH + seat * 48 + 24 + 4 * k));
  }
}
}  // namespace

// When the session closes: what watching was, in one line.
void WatchSummary(const Session& s)
{
  if (s.watch_frames == 0 && s.watch == Watch::Racing)
    return;
  Notice("fzgx: watched behind live: {} lockstep frames, {} of them waited for an official frame; slack {}, last "
         "{:.1f} in hand; speeds {:.3f} to {:.3f}",
         s.watch_frames, s.watch_waits, s.slack, s.buffer_avg, s.watch_slowest, s.watch_fastest);
}
}  // namespace fzgx::net
