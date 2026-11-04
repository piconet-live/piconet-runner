// A practice's session (Options::practice; the design: fzgx/practice.h): one seat, no relay, the race run in
// the game's own Practice mode with a rewind of the last five minutes. Its game mode (protocol/quickplay/practice.fzmode, a
// `solo` mode) says the rewind's reach, the pause, the save and the countdown (WordsOfTheRace); the mode's block is
// game memory like the rest, so the one undo log below takes it back and a save, a mark on a step, holds it too. Each step's row is the local pad's record as the
// step comes to run, so the run needs nobody; Start pauses, the stick scrubs, Start resumes after a 1 s countdown,
// D-pad right saves and D-pad left loads. Rewind (practice.h) says what each displayed frame does; this file moves
// the game's memory by it.
//
// One memory and one undo log (Session::rollback), started at the race phase's first frame: every step of the
// reach keeps its entry (Rollback::Final drops what is older). The steps' rows and checksums are the kept
// run's (Line): a picture of an earlier moment is made as the replay scrubber makes a standing one, by putting the
// memory back to before the step before it (RestoreTo) and running that step again, silently (the mute word), with
// its row from the line; a later moment is reached by running the steps between again, as extra steps of the frame
// (whose presentation tasks the image leaves out), the last of them drawn. Every step run again is checked against
// the checksum it had when it first ran: the rewind's determinism, counted for the log. A resume from an earlier
// moment cuts the line there (a branch): the steps after it are no part of the run any more. Their rows and
// checksums are kept aside (the ghost) for as long as the new run's rows are the same: a step resumed with the same
// inputs must come to the same checksum (counted too).
//
// Outside the race phase (the countdown, the results) every step runs on the pad's row and is final at once: the
// phase changes there are not simulated again. The race phase over (the results), the practice ends: the rest of the
// line is final and the results are taken.
//
// What a step takes from outside the undo log is not put back: memory never rolled back (the sound system's), the
// data window. Of the data window the race's end words are cleared on every branch and while the picture stands
// (END_OFFICIAL 0: the image says no "over" on a picture of an earlier moment), as the race's end that came after
// an earlier moment is no part of the run that goes on from it.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "net_internal.h"

#include "files.h"
#include "fzgx/panel.h"
#include "fzgx/practice.h"
#include "mode.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace fzgx::net
{
namespace
{
constexpr u32 SOUNDS_FROM = CONTROL + 492;  // the image's mute word (mute.py): a step below it is silent
constexpr u32 SILENT = 0xFFFFFFFF;
constexpr u32 PAD_STATUS = BSS + 0x998;     // PADStatus of port 0: the buttons (u16), then the stick's x (s8)
constexpr u32 LOG_EVERY = 3600;             // the undo log's size into the log once a minute of the race
constexpr double SLOW_FRAME_MS = 50;        // a displayed frame longer than this is named in the log
constexpr u32 BIG_MOVE = 1000;              // ... and one that put back or ran again this many steps
using Steady = std::chrono::steady_clock;

// A displayed frame's own moves of the memory, for the log: the undo entries put back and the steps run again.
struct Moves
{
  u32 restored = 0, reran = 0, keys = 0;
  double restore_ms = 0;
};

struct Practice
{
  std::unique_ptr<practice::Rewind> rewind;  // from the race phase's first frame
  practice::Line line{0};
  u32 first = 0;
  bool begun = false, over = false;
  bool last_live = false;      // the frame before ran its step on the pad's row
  bool reruns = false;         // the steps since the last hook were run again (a picture of an earlier moment)
  Row live_row;                // the row the live step ran with
  std::vector<practice::Line::Step> ghost;  // a branch's dropped steps, from ghost_at, while the rows are the same
  u32 ghost_at = 0;
  bool ghost_on = false;
  // for the log
  u32 rerun_checked = 0, rerun_differed = 0, ghost_checked = 0, ghost_differed = 0, live_steps = 0;
  u32 held_frames = 0, restores = 0;
  std::size_t log_most = 0, keys_most = 0;
  // a displayed frame's time, hook to hook (with the steps it ran again), and the longest of a frame that moved
  Moves moves, longest_moves;
  Steady::time_point hooked{};
  double longest_ms = 0;
  u32 longest_at = 0, slow_frames = 0, told_moves = 0;
};

std::unique_ptr<Practice> s_practice;

// The memory back to before step `to` from before `from`, timed for the log.
void PutBack(Guest& g, Session& s, Practice& p, u32 from, u32 to)
{
  const Steady::time_point began = Steady::now();
  s.rollback.RestoreTo(g, from, to);
  p.moves.restored += from - to;
  p.moves.restore_ms += std::chrono::duration<double, std::milli>(Steady::now() - began).count();
  ++p.restores;
}

// The frame before's time (from its hook to this one), named when it moved the memory and was slow.
void TimeTheFrame(Practice& p, u32 t)
{
  const Steady::time_point now = Steady::now();
  if (p.hooked != Steady::time_point{} && (p.moves.restored > 0 || p.moves.reran > 0 || p.moves.keys > 0))
  {
    const double ms = std::chrono::duration<double, std::milli>(now - p.hooked).count();
    if (ms > p.longest_ms)
    {
      p.longest_ms = ms;
      p.longest_moves = p.moves;
      p.longest_at = t;
    }
    const bool slow = ms > SLOW_FRAME_MS;
    p.slow_frames += slow ? 1 : 0;
    if ((slow || p.moves.keys > 0 || p.moves.restored >= BIG_MOVE || p.moves.reran >= BIG_MOVE) && ++p.told_moves <= 40)
      Notice("fzgx: practice: a frame took {:.1f} ms before step {}: {} keys taken, {} undo entries put back ({:.1f} ms), "
             "{} steps run again", ms, t, p.moves.keys, p.moves.restored, p.moves.restore_ms, p.moves.reran);
  }
  p.moves = {};
  p.hooked = now;
}

// The local seat's row: the pad's record, or (tests, Debug::autopilot) "the AI drives" (frame.cpp's AI_DRIVES).
Row PadRow(Guest& g, u32 module)
{
  Row row(RECORD);
  if (TheOptions().debug.autopilot)
    row[1] = 0x80;
  else
    g.CopyFrom(row.data(), module + PAD_RECORD, RECORD);
  return row;
}

// The steps of the line below `below` are final: to the replay.
void WriteFinal(Session& s, Practice& p, u32 below)
{
  const auto steps = p.line.TakeFinal(below);
  for (const auto& [step, kept] : steps)
    s.recorder.WriteStep(kept.row, kept.checksum);
  if (!steps.empty())
    s.recorder.Flush();
}

// The step before the hook ran again: its checksum against the one it had.
void CheckRerun(Guest& g, Practice& p, u32 t)
{
  if (t == 0)
    return;
  const practice::Line::Step* kept = p.line.At(t - 1);
  if (!kept)
    return;
  ++p.rerun_checked;
  const u32 value = Rollback::KeptChecksum(g, t - 1);
  if (value == kept->checksum)
    return;
  if (++p.rerun_differed <= 20)
    Warn("fzgx: practice: step {} run again differs from its first run ({:08x}, first {:08x})", t - 1, value,
         kept->checksum);
}

// The frame before ran step t - 1 on the pad's row: the line takes it, and the rewind its front.
void TookLiveStep(Guest& g, Practice& p, u32 t)
{
  const u32 ran = t - 1;
  const u32 value = Rollback::KeptChecksum(g, ran);
  if (p.ghost_on && ran >= p.ghost_at && ran - p.ghost_at < p.ghost.size())
  {
    const practice::Line::Step& old = p.ghost[ran - p.ghost_at];
    if (old.row == p.live_row)
    {
      ++p.ghost_checked;
      if (old.checksum != value && ++p.ghost_differed <= 20)
        Warn("fzgx: practice: step {} resumed on the same inputs differs from its first run ({:08x}, first {:08x})",
             ran, value, old.checksum);
    }
    else
      p.ghost_on = false;
  }
  if (!p.line.Ran(ran, p.live_row, value))
    Warn("fzgx: practice: step {} could not be kept (the line ends at {})", ran, p.line.End());
  if (p.begun && !p.over)
    p.rewind->Ran(ran);
}

// A step on the pad's row, with its sounds.
void RunLive(Guest& g, Session& s, Practice& p, u32 t, u32 module)
{
  p.live_row = PadRow(g, module);
  WriteRow(g, s, t, p.live_row);
  g.Write32(SOUNDS_FROM, t);
  g.Write32(END_OFFICIAL, t + 1);
  g.Write32(FINAL_STEPS, t);
  Rollback::Resimulate(g, 0);
  p.last_live = true;
  ++p.live_steps;
}

// A picture of the moment before step `to`: the memory brought to before step to - 1 (from before `at`), and the
// steps from there to it run again, silently, the last drawn.
void ShowMoment(Guest& g, Session& s, Practice& p, u32 at, u32 to)
{
  const u32 run_from = std::max(to, p.first + 1) - 1;
  u32 from = at;
  if (run_from < at)
  {
    PutBack(g, s, p, at, run_from);
    from = run_from;
  }
  const u32 last = std::max(run_from, from);  // (a moment past the memory's: the steps between run first)
  p.moves.reran += last - from + 1;
  g.Write32(STEP, from);
  WriteRow(g, s, from, p.line.At(from) ? p.line.At(from)->row : Row(RECORD, 0));
  g.Write32(SOUNDS_FROM, SILENT);
  g.Write32(END_OFFICIAL, 0);
  Rollback::Resimulate(g, last - from);
  p.reruns = true;
  p.last_live = false;
  ++p.held_frames;
}

// The race's end that came after an earlier moment is not this run's.
void ForgetTheEnd(Guest& g)
{
  g.Write32(END_OVER, 0);
  g.Write32(END_ASKED, 0);
}

// The replay scrubber's bar (fzgx/host.h, ScrubBar), told every frame: the reach, the moment shown, the
// front. The host shows it while the picture stands (`paused`) and lets the sound through while the race runs
// (`heard`). It stands only while paused, never in the countdown: the host lays the game's window again when the
// bar comes or goes, and the picture's window resized holds the game's thread up for 50 to 200 ms (pacing logs of
// a field of 53 to 106 ms 4 to 14 fields after every bar change). After a pause that falls in the
// countdown's standing picture, where nothing moves, and not in the race's first frames; a load's countdown shows
// no bar at all.
void TellTheBar(const Practice& p, u32 shown, bool runs, bool paused)
{
  ScrubBar bar;
  if (p.rewind && !p.over)
  {
    bar.first = p.rewind->Oldest();
    bar.last = std::max(p.rewind->Front(), bar.first + 1);
    bar.played = shown;
    bar.buffered = p.rewind->Front();
  }
  bar.standing = paused;
  bar.heard = runs;
  TheHost().ReplayBar(bar);
}

// The countdown's figure over the standing picture (fzgx/panel.h): 3, 2, 1 over its second; down otherwise.
void ShowTheCountdown(const practice::Rewind& r)
{
  panel::Board& board = panel::TheBoard();
  if (r.Now() == practice::Mode::Countdown && r.CountdownLeft() > 0)
    panel::Counting(board, (r.CountdownLeft() * 3 + r.Said().countdown - 1) / r.Said().countdown);
  else if (board.Now() == panel::Shows::Countdown)
    panel::Down(board);
}

// The practice's words: its game mode's; a start with no mode (a launcher's before Practice was a mode), today's.
practice::Words WordsOfTheRace()
{
  const boot::Match* m = boot::Get();
  if (!m || !m->game_mode)
    return {};
  const mode::Package& mode = *m->game_mode;
  return {mode.rewind, mode.pause, mode.save_state, mode.resume_countdown};
}

void Begin(Guest& g, Session& s, Practice& p, u32 t)
{
  p.begun = true;
  p.first = t;
  p.rewind = std::make_unique<practice::Rewind>(t, WordsOfTheRace());
  if (!s.rollback.Active())
    s.rollback.Start(g);
  Notice("fzgx: practice: the race begins at step {}: Start pauses, the stick scrubs back {} steps at most, "
         "D-pad right saves and left loads; the undo log's copy of the memory takes {:.1f} MiB", t, p.rewind->Said().reach,
         static_cast<double>(s.rollback.TrackedBytes()) / (1 << 20));
}

void End(Guest& g, Session& s, Practice& p, u32 t)
{
  p.over = true;
  s.rollback.Stop();
  WriteFinal(s, p, p.line.End());
  if (s.recorder.IsOpen() && !s.recorder.HasResults())
    s.recorder.TakeResults(g);
  Notice("fzgx: practice: the race phase is over at step {}: every step of the run is final", t);
}

void Frame(Guest& g, Session& s, Practice& p, u32 t)
{
  const u32 module = g.Read32(MODULE_POINTER);
  if (p.reruns)
    CheckRerun(g, p, t);
  else if (p.last_live && t > 0)
    TookLiveStep(g, p, t);
  p.reruns = false;
  const bool race_phase = g.Read16(module + PHASE) == 0x29;
  if (!p.begun && race_phase)
    Begin(g, s, p, t);
  else if (p.begun && !p.over && !race_phase)
    End(g, s, p, t);
  if (!p.begun || p.over)
  {
    WriteFinal(s, p, t);
    TellTheBar(p, t, true, false);
    return RunLive(g, s, p, t, module);
  }
  s.rollback.SaveStep(g, t, true);
  p.log_most = std::max(p.log_most, s.rollback.LogBytes());
  p.keys_most = std::max(p.keys_most, s.rollback.KeyBytes());
  if (t > p.first && (t - p.first) % LOG_EVERY == 0 && p.last_live)
  {
    const u32 machines = g.Read32(module + MACHINES);
    const u32 progress = machines != 0 ? g.Read32(machines + game::MACHINE_PROGRESS) : 0;
    Notice("fzgx: practice: at step {} ({} laps done) the buffer reaches back {} steps; the undo log holds {:.1f} MiB", t,
           progress != 0 ? static_cast<s32>(g.Read32(progress + game::PROGRESS_LAPS)) : -1, t - p.rewind->Oldest(),
           static_cast<double>(s.rollback.LogBytes()) / (1 << 20));
  }
  practice::Rewind& r = *p.rewind;
  practice::Pad pad;
  pad.buttons = g.Read16(module + PAD_STATUS);
  pad.stick_x = static_cast<std::int8_t>(g.Read8(module + PAD_STATUS + 2));
  // the bar, pressed or held while the picture stands: the moment under the pointer
  if (const u32 pointer = TheHost().ReplayScrub(); pointer != NONE_STEP && r.Now() == practice::Mode::Paused)
    r.Seek(pointer);
  const practice::Frame frame = r.Next(pad);
  TellTheBar(p, frame.step, frame.run, r.Now() == practice::Mode::Paused);
  ShowTheCountdown(r);
  r.TakeDropped();  // (no keys are kept: the undo log reaches the whole buffer)
  if (frame.saved)
  {
    // the host marks it briefly, where the bar would show it (TellTheBar's span)
    const u32 first = r.Oldest(), last = std::max(r.Front(), first + 1);
    panel::Saved(panel::TheBoard(), static_cast<u32>(u64{std::clamp(frame.step, first, last) - first} * panel::FULL /
                                                     (last - first)));
    Notice("fzgx: practice: saved at step {}", frame.step);
  }
  if (frame.loaded)
    Notice("fzgx: practice: loaded the save at step {} (the race was at step {})", frame.step, t);
  if (frame.run)
  {
    if (frame.branch || frame.step != t)
    {
      if (frame.step < t)
        PutBack(g, s, p, t, frame.step);
      const u32 dropped = p.line.End() > frame.step ? p.line.End() - frame.step : 0;
      p.ghost.clear();
      for (u32 i = frame.step; i < p.line.End(); ++i)
        p.ghost.push_back(*p.line.At(i));
      p.ghost_at = frame.step;
      p.ghost_on = !p.ghost.empty();
      p.line.Cut(frame.step);
      ForgetTheEnd(g);
      Notice("fzgx: practice: the race goes on from step {}; the {} steps after it are dropped", frame.step, dropped);
      t = frame.step;
      g.Write32(STEP, t);
    }
    s.rollback.Final(std::max(r.Oldest(), p.first + 1) - 1);
    WriteFinal(s, p, r.FinalBelow());
    return RunLive(g, s, p, t, module);
  }
  ForgetTheEnd(g);
  ShowMoment(g, s, p, t, frame.step);
}

// A step run again between a frame's hooks (a later moment reached): its row from the line, silently.
void Extra(Guest& g, Session& s, Practice& p)
{
  const u32 t = g.Read32(STEP);
  CheckRerun(g, p, t);
  s.rollback.SaveStep(g, t, false);
  WriteRow(g, s, t, p.line.At(t) ? p.line.At(t)->row : Row(RECORD, 0));
  g.Write32(SOUNDS_FROM, SILENT);
}
}  // namespace

bool PracticePaused()
{
  return s_practice && s_practice->rewind && s_practice->rewind->Now() == practice::Mode::Paused;
}

bool StartPractice(Guest& g)
{
  const std::string path = TheOptions().replay_file;
  auto s = std::make_unique<Session>();
  s->practice = true;
  s->seats = {0};
  s->my_seat = 0;
  s->my_index = 0;
  s->phase = Phase::Joined;
  s->go = true;
  s->delay = 0;
  g.Write32(SESSION, 1);
  g.Write32(GATE_ON, 1);
  OpenWorkLog(*s);
  OpenTrace(*s);
  if (!path.empty())
  {
    if (!boot::StartReplay(path))
      Error("fzgx: cannot start the replay {}", path);
    if (!s->recorder.Open(path))
      Error("fzgx: cannot record the replay to {}", path);
  }
  s_practice = std::make_unique<Practice>();
  Notice("fzgx: practice session: one seat, no relay; each step's row is the pad's");
  s_session = std::move(s);
  return true;
}

void PracticeHook(Guest& g, Session& s, bool extra)
{
  if (!s_practice)
    return;
  if (extra)
    return Extra(g, s, *s_practice);
  TimeTheFrame(*s_practice, g.Read32(STEP));
  Frame(g, s, *s_practice, g.Read32(STEP));
}

void PracticeEnd(Session& s)
{
  if (!s_practice)
    return;
  Practice& p = *s_practice;
  WriteFinal(s, p, p.line.End());
  const practice::Counts c = p.rewind ? p.rewind->Count() : practice::Counts{};
  Notice("fzgx: practice: {} steps kept in the run, {} live steps; pauses {}, branches {}, saves {}, loads {}, saves "
         "expired {}, dropped {}; {} frames showed an earlier moment, {} restores",
         p.line.End(), p.live_steps, c.pauses, c.branches, c.saves, c.loads, c.saves_expired, c.saves_dropped,
         p.held_frames, p.restores);
  Notice("fzgx: practice: steps run again: {} checked against their first run, {} differed; steps resumed on the same "
         "inputs: {} checked, {} differed; the undo log held {:.1f} MiB at most",
         p.rerun_checked, p.rerun_differed, p.ghost_checked, p.ghost_differed,
         static_cast<double>(p.log_most) / (1 << 20));
  Notice("fzgx: practice: the longest frame that moved the memory took {:.1f} ms (before step {}): {} keys taken, {} "
         "undo entries put back ({:.1f} ms), {} steps run again; {} frames over {:.0f} ms; the keys held {:.1f} MiB at most",
         p.longest_ms, p.longest_at, p.longest_moves.keys, p.longest_moves.restored, p.longest_moves.restore_ms,
         p.longest_moves.reran, p.slow_frames, SLOW_FRAME_MS, static_cast<double>(p.keys_most) / (1 << 20));
  s_practice.reset();
}
}  // namespace fzgx::net
