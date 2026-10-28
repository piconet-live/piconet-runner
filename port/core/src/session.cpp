// The relay session from its start to its end: joining, the start gate, the hook before every step (RunSession),
// the wait for official frames, and what the rest of the core calls (Run, Shutdown).
// SPDX-License-Identifier: GPL-3.0-or-later

#include "net_internal.h"

#include "fzgx/panel.h"
#include "board_net.h"
#include "host_timers.h"

#include <chrono>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>

namespace fzgx::net
{
using namespace std::chrono_literals;

std::unique_ptr<Session> s_session;
bool s_config_failed = false;

bool WaitFor(Session& s, u32 count)
{
  // (a running race waits longer: the game asks for its seat again meanwhile)
  const auto deadline = Clock::now() + (s.phase == Phase::Running ? std::chrono::duration_cast<Clock::duration>(REJOIN_WAIT)
                                                                  : std::chrono::duration_cast<Clock::duration>(15s));
  // (for the freeze report: a frame held here is the core's own wait, said so; written only when it does wait)
  std::atomic<bool>& waits = freeze::ThePulse().waits_for_relay;
  while (s.official.size() < count)
  {
    if (!waits.exchange(true, std::memory_order_relaxed))
      board::Waits();  // (the launcher's status board says it is a wait: no game frame begins meanwhile)
    if (Clock::now() > deadline)
      return false;
    std::this_thread::sleep_for(500us);
    Drain(s);
    AskForTheSeatAgain(s);
    if (Clock::now() - s.last_input > 50ms)
      SendInputs(s);
  }
  if (waits.load(std::memory_order_relaxed))
    waits.store(false, std::memory_order_relaxed);
  return true;
}

void Fail(Guest& g, Session& s, u32 step)
{
  Error("fzgx: no official inputs for step {} in time; leaving the race", step);
  g.Write32(FAILED, 1);
  s.phase = Phase::Failed;
  freeze::ThePulse().racing = false;  // the game leaves the race: its frames are no longer watched
  // and no restore follows: the undo log ends here, so that nothing is told "a rollback keeps this memory"
  // (fzgx::UnderRollback) for the rest of a process that has left its race
  s.rollback.Stop();
}

namespace
{
bool Start(Guest& g)
{
  // The image's word that its race-over test said "over" is a race's before this one, if the process ran one and
  // the data window was not cleared since (Init clears it): left set, no step of this race would be predicted
  // (frame.cpp, KindOfFrame), and the whole race would run in lockstep.
  g.Write32(END_OVER, 0);
  if (const std::string play = TheOptions().play_file; !play.empty())
  {
    if (!StartPlayback(g, play))
    {
      s_config_failed = true;
      return false;
    }
    return true;
  }
  if (TheOptions().practice)
  {
    if (StartPractice(g))
      return true;
    s_config_failed = true;
    return false;
  }
  const Debug& debug = TheOptions().debug;
  std::string config = debug.relay;
  // a race from a launch file's match: the relay comes from it
  if (const auto relay = boot::GetRelay())
    config = std::format("{}:{}:{:x}", relay->host, relay->port, relay->token);
  const auto parts = SplitString(config, ':');
  u64 token = 0;
  unsigned short port = 0;
  const u32 lag = debug.lag_ms;
  std::optional<Endpoint> address;
  if (parts.size() == 3 && TryParse(parts[1], &port) && TryParse(parts[2], &token, 16))
    address = Udp::Resolve(parts[0], port);
  if (!address || token == 0)
  {
    Error("fzgx: the relay must be host:port:token(hex), got \"{}\"", config);
    s_config_failed = true;
    return false;
  }
  auto s = std::make_unique<Session>();
  s->relay = *address;
  s->token = token;
  s->key = RelayKey(token);
  s->lag_ms = lag;
  if (!s->socket.Open())
  {
    Error("fzgx: no UDP socket");
    s_config_failed = true;
    return false;
  }
  s->delay = g.Read32(SEAT_SLOT + 44);
  s->held = panel::TheBoard().until_live.load(std::memory_order_relaxed);
  g.Write32(SESSION, 1);
  g.Write32(GATE_ON, 1);
  s->autopilot = debug.autopilot;
  s->jitter_ms = debug.jitter_ms;
  s->jitter_state ^= static_cast<u32>(token);
  if (s->jitter_state == 0)
    s->jitter_state = 1;
  OpenWorkLog(*s);
  OpenHitLog(*s);
  OpenTrace(*s);
  s->normal_speed = TheHost().EmulationSpeed();
  s->normal_clock = TheHost().CpuClock();
  if (const std::string path = ReplayToWrite(*s, TheOptions().replay_file); !path.empty())
  {
    // recording: after the header and the frame preamble (written here for a race from a match, else by the
    // launcher); a game started again records beside the crashed game's replay until it is given the race
    if (boot::IsActive() && !boot::StartReplay(path))
      Error("fzgx: cannot start the replay {}", path);
    if (!s->recorder.Open(path))
      Error("fzgx: cannot record the replay to {}", path);
  }
  Notice("fzgx: network session with relay {}:{}, input delay {}, test lag {} ms{}",
                 parts[0], port, s->delay, lag,
                 s->jitter_ms ? std::format(" and up to {} ms more for each packet", s->jitter_ms) : std::string());
  s_session = std::move(s);
  return true;
}

// The game holds at the start gate, joined, and the relay has not said go: the panel says so once the wait is long
// enough to read (fzgx/panel.h). The game draws nothing here, so the host is told to present the panel itself: when
// it comes up, and twice a second after (a window that was resized meanwhile is drawn again). No count of ready
// players: the relay tells a game none.
void ShowTheWait(Session& s, Clock::time_point now)
{
  if (s.held)
    return;  // (a game started again: LOADING stays up, and the go comes at once)
  if (s.gate_since == Clock::time_point{})
    s.gate_since = now;
  const auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(now - s.gate_since).count();
  panel::Board& board = panel::TheBoard();
  if (panel::AtGate(board, static_cast<u32>(waited), 0, 0) ||
      (board.Now() == panel::Shows::Waiting && now - s.panel_told > 500ms))
  {
    s.panel_told = now;
    TheHost().PanelChanged();
  }
}

// The relay said go and the game holds at the start gate: the race runs by steps from here.
void TakeTheGo(Guest& g, Session& s)
{
  // the panel is down from here: the countdown's first picture is the next one the game draws. Not in a replay
  // held to the race (a playback with its scrubber): its countdown runs unseen, unpaced and silent, and the race's
  // first step takes the panel down (scrub.cpp, Fork). A live race is never held (it has no replay file).
  // A game started again into its race keeps LOADING up while it catches up (rejoin.cpp); when the relay says the
  // seat is not away (its race had not gone yet), the panel comes down here as in any race.
  panel::Board& board = panel::TheBoard();
  if (s.held && !s.returning)
  {
    s.held = false;
    panel::Live(board);
  }
  if (!board.until_race.load(std::memory_order_relaxed) && !board.until_live.load(std::memory_order_relaxed))
  {
    panel::Down(panel::TheBoard());
    TheHost().LoadPace(false);  // (long done, at the course's first frame: nothing is lifted past a go)
  }
  // A race with a stat pack starts only on that pack: the game's car data holds it (stat_pack.cpp). Otherwise this
  // game would race another race than the others: it stops here instead.
  if (const std::optional<std::string> refused = boot::StatsRefusal(g))
  {
    Error("fzgx: stat pack: {}: the race is refused; stopping", *refused);
    g.Write32(FAILED, 1);
    s.phase = Phase::Failed;
    TheHost().RequestStop();
    return;
  }
  s.recorder.WriteLineup(g);
  // a replay plays as the race: watched through the game's own camera, the recorder's at first; D-pad
  // right/left move the stand-in seat (standin.py, control block +0xA38), which the camera and the HUD follow
  if (s.playback)
  {
    s.as_race = true;
    g.Write32(CONTROL + 0xA38, 1);
  }
  s.last_official = Clock::now();
  g.Write32(STEP, 0);
  g.Write32(RUNNING, 1);
  g.Write32(GO, 1);
  s.phase = Phase::Running;
  freeze::ThePulse().countdown = true;
  freeze::ThePulse().racing = true;  // from here a game that stands still says so (fzgx/freeze.h)
  // the game's own clock, for the tests that give two games different ones: the system time the game's OS set
  // from the real-time clock when it booted (0x800030D8, 64 bits, in ticks of the time base)
  Notice("fzgx: go (this game's system time at boot: {:08x}{:08x})", g.Read32(game::BOOT_TIME),
         g.Read32(game::BOOT_TIME + 4));
}

// The results screens' state (u8, fn_1_14A028's block): 0 the race's own results page, 7 every racer's (A leaves it),
// 8 leaving (netframe.py +0xA30).
constexpr u32 RESULTS_STATE = BSS + 0x8E51D;

// The step from which the player's next A leaves the results: the first in the results phase (0x2A) whose results
// state is 7, every racer's page. Before it the game takes the leave press at no step (the race's own page goes to
// every racer's page on an A, once its countdown has run out), so one race's games leave at steps as far apart as
// their players' pages open, by their standings. The gates time each game's leave from this line (judge_race.py).
void SayResultsLeavable(Guest& g, Session& s)
{
  if (s.leavable_said || s.exit_sent)
    return;
  const u32 module = g.Read32(MODULE_POINTER);
  if (module == 0 || g.Read16(module + PHASE) != 0x2A || g.Read8(module + RESULTS_STATE) != 7)
    return;
  s.leavable_said = true;
  s.leavable_at = Clock::now();
  Notice("fzgx: the results can be left from step {} (every racer's page)", g.Read32(STEP));
}

// The player leaves the results: the image asked for it (+0xA30, the results state reached 8 after A on every
// racer's page). Stops the game once, as the window's stop button does.
void LeaveTheResults(Guest& g, Session& s, const char* why)
{
  s.exit_sent = true;
  Notice("fzgx: {}; stopping", why);
  ReportResultsPictures(g, "leaving the results");
  TheHost().RequestStop();
}

// A racer's game leaves the results by itself RESULTS_LEAVE after every racer's page opened (the owner,
// 28 October): as if the player pressed A there. The A is pressed through the image (+0xA34: A on port 0 for one
// frame, the press the HUD pass already gives a racer still racing when the results begin), not by a stop from
// here: the game's own results then take the step a player's A takes (state 8, where the image asks to leave), so
// the leave is the player's in everything, its log line, its results pictures and its exit code included. Should
// the game not take the press (it never did in a test), the stop follows RESULTS_LEAVE_GRACE later, by the same
// lines. The wall clock is enough: leaving is this game's own and changes nothing another game computes. Only a
// racer: a playback and a practice have no results to wait on, and a spectator stops when the relay drops the
// race (relay_wire.cpp TakeJoinedAgain).
void LeaveTheResultsInTime(Guest& g, Session& s)
{
  if (!s.leavable_said || s.exit_sent || s.watcher || s.playback || s.practice)
    return;
  const auto waited = Clock::now() - s.leavable_at;
  if (waited < RESULTS_LEAVE)
    return;
  if (!s.leave_pressed)
  {
    s.leave_pressed = true;
    Notice("fzgx: every racer's page stood {} s: A pressed for the player", RESULTS_LEAVE.count());
    g.Write32(PRESS_A, 1);
    return;
  }
  if (waited >= RESULTS_LEAVE + RESULTS_LEAVE_GRACE)
    LeaveTheResults(g, s, "the results were not left after the A pressed for the player");
}

void RunSession(Guest& g)
{
  if (!s_session && (s_config_failed || !Start(g)))
    return;
  Session& s = *s_session;
  if (s.phase == Phase::Failed)
    return;
  FZGX_TIME(Hook);
  Drain(s);
  if (s.phase == Phase::Failed)
    return;  // (the relay said the race this game was started again into is over: relay_wire.cpp)
  AskForTheSeatAgain(s);
  const auto now = Clock::now();
  if (s.phase == Phase::Joining)
  {
    if (now - s.last_join > 100ms)
    {
      Send(s, relay::Encode(relay::Join{.token = s.token}));
      s.last_join = now;
    }
    return;
  }
  if (s.phase == Phase::Joined)
  {
    if (g.Read32(READY) == 0)
      return;  // not at the race-start gate yet
    if (s.go)
      return TakeTheGo(g, s);
    if (now - s.last_ready > 100ms)
    {
      Send(s, relay::Encode(relay::Ready{.token = s.token, .cookie = s.cookie}));
      s.last_ready = now;
    }
    Ping(s);
    ShowTheWait(s, now);
    board::AtGate();
    std::this_thread::sleep_for(1ms);  // the gate's wait calls this in a loop
    return;
  }
  SayResultsLeavable(g, s);
  if (!s.exit_sent && g.Read32(EXIT_REQUEST) == 1)
    LeaveTheResults(g, s, "the player left the results");
  LeaveTheResultsInTime(g, s);
  // the machines' textures load in the step into the countdown (steps 2 to 4): then the racers' colours, once (a
  // race from a launch file's match)
  if (g.Read32(STEP) >= 6)
    boot::PaintMachines(g);
  const auto entered = Clock::now();
  const bool extra = Rollback::Resimulating(g);
  if (extra && s.work_open)
  {
    // the game's code since the hook last returned: the frame's first step, or a re-simulated one
    const double us = std::chrono::duration<double, std::micro>(entered - s.hook_left).count();
    if (s.hooks_in_frame == 1)
      s.work.first_us = us;
    else
    {
      s.work.resim_us += us;
      ++s.work.resim_n;
    }
  }
  KeepImageLogs(g);
  if (TheOptions().debug.dump_step != 0 && g.Read32(STEP) == TheOptions().debug.dump_step + 1 && s.dumps_after < 8)
    Dump(g, std::format("-after-{}{}", ++s.dumps_after, !extra ? "L" : s.hooks_in_frame == 1 ? "F" : "M"));
  ChecksumPrevious(g, s, extra);
  ShowHits(g, s, extra);
  Audit(g, s, extra);
  if (s.practice)
    PracticeHook(g, s, extra);
  else if (extra)
    ExtraStep(g, s);
  else
  {
    s.hooks_in_frame = 0;
    NormalStep(g, s);
  }
  AlterState(g, s);
  if (TheOptions().debug.dump_step != 0 && g.Read32(STEP) == TheOptions().debug.dump_step && s.dumps_before < 8)
  {
    // how the step is about to run: L a frame's last or only step, F its first with more to follow, M one between
    const u32 more = g.Read32(CONTROL + 8);
    Dump(g, std::format("-before-{}{}", ++s.dumps_before, extra ? (more == 1 ? "L" : "M") : more != 0 ? "F" : "L"));
  }
  ++s.hooks_in_frame;
  g.Write32(DESYNCS, s.desyncs);
  s.hook_left = Clock::now();
  s.work.hook_us += std::chrono::duration<double, std::micro>(s.hook_left - entered).count();
}
}  // namespace

void Run(Guest& g)
{
  if (TheOptions().debug.relay.empty() && TheOptions().play_file.empty() && !boot::IsActive())
    return;
  RunSession(g);
}

void Shutdown()
{
  MinimapSummary();
  PresentEnd();
  // (a stop the launcher asked for through its status board keeps no race for a rejoin: the launcher wanted it)
  if (s_session && ExitCode() == 0 && !board::LauncherStopped() && LeftWhileRacing(*s_session))
  {
    SetExitCode(EXIT_LEFT_RACING);
    Notice("fzgx: the game stopped while its racer raced: the race is kept for a rejoin (exit code {})",
           EXIT_LEFT_RACING);
  }
  if (s_session)
  {
    FZGX_TIME_REPORT(s_session->normal_steps, s_session->normal_steps + s_session->resim_steps);
    CloseFrameWork(*s_session, true);
    WatchSummary(*s_session);
    if (!s_session->playback)
      SpectatorSummary(*s_session);  // (a live watcher's controls; a playback says them with its own lines)
    ScrubSummary(*s_session);  // (a playback closed before its end says its scrubber's lines too)
  }
  if (s_session && s_session->trace && !s_session->trace_lines.empty())
    s_session->trace->WriteBytes(s_session->trace_lines.data(), s_session->trace_lines.size());
  if (s_session && s_session->practice)
    PracticeEnd(*s_session);
  if (s_session && !s_session->playback)
  {
    s_session->recorder.Close();
    LeaveTheCrashedReplay(*s_session);
  }
  // The race's line for the player's file: last, after the replay is closed, and nothing it throws leaves here (no
  // memory for the file's text, a path the system does not take). A report must never cost a replay.
  if (s_session)
    WriteSummaryWhateverHappens(*s_session);
  s_session.reset();
  s_config_failed = false;
  freeze::ThePulse().Reset();
  panel::TheBoard().Reset();
}
}  // namespace fzgx::net
