// A practice's session (Options::practice; the design: fzgx/practice.h): one seat, no relay, the race run in
// the game's own Practice mode with a rewind of the last minute. Each step's row is the local pad's record as the
// step comes to run, so the run needs nobody; Start pauses, the stick scrubs, Start resumes after a 1 s countdown,
// D-pad right saves and D-pad left loads. Rewind (practice.h) says what each displayed frame does; this file moves
// the game's memory by it.
//
// One memory and one undo log (Session::rollback), started at the race phase's first frame: every step of the
// minute's reach keeps its entry (Rollback::Final drops what is older). The steps' rows and checksums are the kept
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

#include "fzgx/panel.h"
#include "fzgx/practice.h"

#include <algorithm>
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
  std::size_t log_most = 0;
};

std::unique_ptr<Practice> s_practice;

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
    s.rollback.RestoreTo(g, at, run_from);
    ++p.restores;
    from = run_from;
  }
  const u32 last = std::max(run_from, from);  // (a moment past the memory's: the steps between run first)
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

// The replay scrubber's bar (fzgx/host.h, ScrubBar), told every frame: the minute's reach, the moment shown, the
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
    panel::Counting(board, (r.CountdownLeft() * 3 + practice::COUNTDOWN - 1) / practice::COUNTDOWN);
  else if (board.Now() == panel::Shows::Countdown)
    panel::Down(board);
}

void Begin(Guest& g, Session& s, Practice& p, u32 t)
{
  p.begun = true;
  p.first = t;
  p.rewind = std::make_unique<practice::Rewind>(t);
  if (!s.rollback.Active())
    s.rollback.Start(g);
  Notice("fzgx: practice: the race begins at step {}: Start pauses, the stick scrubs back a minute at most, "
         "D-pad right saves and left loads", t);
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
  r.TakeDropped();  // (no keys are kept: the undo log reaches the whole minute)
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
      {
        s.rollback.RestoreTo(g, t, frame.step);
        ++p.restores;
      }
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
  s_practice.reset();
}
}  // namespace fzgx::net
