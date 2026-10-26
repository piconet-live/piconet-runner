// A game that comes back into its running race: the
// seat's records while the AI drives it, asking for the seat again after a network drop, the catch-up under LOADING,
// the hand-back's counter, and the race's end told to the relay (after it no seat comes back).
// SPDX-License-Identifier: GPL-3.0-or-later

#include "net_internal.h"

#include "fzgx/panel.h"

#include <algorithm>
#include <chrono>
#include <format>
#include <string>

namespace fzgx::net
{
using namespace std::chrono_literals;

namespace
{
constexpr std::array<u8, RECORD> AI_RECORD = {0, 0x80, 0, 0, 0, 0};  // the relay's "AI drives" (RelayProtocol.AiDrives)

// What the image's hand-back monitors (monitors.py) say of themselves, for the log: the pictures read from the disc
// (2: read), the picture last decoded, the step control comes back at (table::MONITORS).
std::string MonitorsSaid(Guest& g)
{
  return std::format("the monitors: pictures {} (2: read), picture {}, control back at step {}",
                     g.Read32(table::MONITORS + 4), g.Read32(table::MONITORS + 36), g.Read32(table::MONITORS));
}
}  // namespace

std::array<u8, RECORD> PadFor(const Session& s, u32 t)
{
  // the AI drives the seat below the step it is given back at: what this game predicts for it is what every game
  // will be sent, and nothing of the pad is sent for those steps (SendInputs)
  return s.own_from == NEVER || t < s.own_from ? AI_RECORD : s.pad;
}

void AskForTheSeatAgain(Session& s)
{
  // (a practice has no relay and no official frames from one: its socket is never opened, so a Join went nowhere,
  // but the log said "no official frame from the relay" at its start)
  if (s.phase != Phase::Running || s.playback || s.practice || s.exit_sent)
    return;
  const auto now = Clock::now();
  if (now - s.last_official < REJOIN_AFTER || now - s.last_rejoin < 200ms)
    return;
  if (s.last_rejoin < s.last_official)
    Notice("fzgx: no official frame from the relay for {} ms; asking for this seat again",
           std::chrono::duration_cast<std::chrono::milliseconds>(now - s.last_official).count());
  s.last_rejoin = now;
  Send(s, relay::Encode(relay::Join{.token = s.token}));
}

void SayTheRaceIsOver(Guest& g, Session& s, u32 step)
{
  if (s.playback || s.overs_sent >= 5 || g.Read32(END_OVER) == 0)
    return;
  const auto now = Clock::now();
  if (now - s.last_over < 200ms)
    return;
  s.last_over = now;
  ++s.overs_sent;
  Send(s, relay::Encode(relay::Over{.token = s.token, .cookie = s.cookie, .frame = step}));
}

void CatchingUpOrLive(Guest& g, Session& s, u32 step)
{
  panel::Board& board = panel::TheBoard();
  if (s.watcher && s.returning && !s.held)
  {
    // a spectator has no seat to be given back: its catch-up ends with LOADING (or at once, a race not yet running)
    s.returning = false;
    Notice("fzgx: step {}: a spectator caught up with the race (the relay's frame {})", step, s.target);
  }
  if (s.held)
  {
    const bool over = g.Read32(END_OVER) != 0 || s.settled;
    const bool caught_up = s.target > 0 && step + CAUGHT_UP >= s.target && Have(s) + CAUGHT_UP >= s.target;
    if (over || caught_up)
    {
      s.held = false;
      if (panel::Live(board))
      {
        TheHost().LoadPace(false);
        TheHost().PanelChanged();
      }
      Notice("fzgx: {} at step {} (the relay's frame {}): LOADING is down", over ? "the race is over" : "caught up",
             step, s.target);
    }
    else if (panel::CatchingUp(board, step, s.target))
      TheHost().PanelChanged();
  }
  // the hand-back's counter for the picture (the countdown monitors draw it): steps left until the player drives.
  // A test's debug.handbackAt writes it instead (KeepImageLogs, earlier in the hook): left alone then, or this
  // line put it back to 0 every step and the monitors never showed in a playback.
  if (TheOptions().debug.handback_at != 0)
    return;
  const u32 left = s.hand_back > step ? s.hand_back - step : 0;
  if (g.Read32(HANDBACK) != left)
  {
    if (left == 0 && s.hand_back != 0)
      Notice("fzgx: step {}: the player drives this seat again; {}", step, MonitorsSaid(g));
    else if (g.Read32(HANDBACK) == 0)
      Notice("fzgx: step {}: the hand-back counts {} steps down; {}", step, left, MonitorsSaid(g));
    g.Write32(HANDBACK, left);
  }
}

bool CatchingUp(const Session& s, u32 step)
{
  return s.returning || (s.held && step + CAUGHT_UP < s.target);
}
// The replay of a game started again. The crashed game's replay (the launcher names the same file) holds the race up
// to the crash; the new game records the whole race from step 0 again, but only once the relay has given it the race
// may it take that file's place. Until then it records beside it (`.rejoin`): a relay that says the race is over, or
// a game that never joins, leaves the crashed game's replay as it was (rj-p3c: it was replaced, then removed as
// empty).
std::string ReplayToWrite(Session& s, const std::string& path)
{
  if (path.empty() || !TheOptions().rejoin)
    return path;
  s.replay_aside = path + ".rejoin";
  RemoveQuietly(s.replay_aside);  // (a leftover of an earlier start: the header is appended to)
  return s.replay_aside;
}

// The relay gave this game the race (its first Joined, not "over"): its replay becomes the race's replay, and the
// recording goes on in it.
void GiveTheReplayItsPlace(Session& s)
{
  if (s.replay_aside.empty())
    return;
  const std::string& path = TheOptions().replay_file;
  const bool open = s.recorder.IsOpen();
  s.recorder.Close();  // (no results yet: nothing is written)
  if (!MoveOver(s.replay_aside, path))
  {
    Error("fzgx: cannot move the replay {} over {}; it stays where it is", s.replay_aside, path);
    if (open && !s.recorder.Open(s.replay_aside))
      Error("fzgx: cannot record the replay to {}", s.replay_aside);
    s.replay_aside.clear();
    return;
  }
  Notice("fzgx: given the race: this game's replay replaces the crashed game's ({})", path);
  s.replay_aside.clear();
  if (open && !s.recorder.Open(path))
    Error("fzgx: cannot record the replay to {}", path);
}

// The session's end with the race never given (the relay said it is over, or never answered): the crashed game's
// replay stays as it was, and this game's is removed.
void LeaveTheCrashedReplay(Session& s)
{
  if (s.replay_aside.empty())
    return;
  RemoveQuietly(s.replay_aside);
  Notice("fzgx: not given the race: the crashed game's replay is kept as it was");
  s.replay_aside.clear();
}

// The game stops, and no stop of its own asked for it (the player closed its window or pressed Escape, or the
// emulator stopped), while this game's seat still races: it had the relay's go; it is a racer's (no spectator, no
// playback, no practice); the game asked for no stop (the results left, a desync, the race over: exit_sent); its
// racer was not out or over the line by the game's own test (watch: Racing); and the race was not over (the race's
// end checked and the results begun: settled; the image's race-over test said "over": an Over sent). Then the
// process ends with EXIT_LEFT_RACING and the launcher keeps the race for Rejoin, as after a crash.
bool LeftWhileRacing(const Session& s)
{
  return s.phase == Phase::Running && !s.watcher && !s.playback && !s.practice && s.my_index >= 0 && !s.exit_sent &&
         s.desyncs == 0 && s.watch == Watch::Racing && !s.settled && s.overs_sent == 0;
}
}  // namespace fzgx::net
