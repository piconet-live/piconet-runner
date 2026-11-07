// What the status board shows of a race (board_net.h): the snapshot of every racer each displayed frame, and the
// phase as the session gives it. Reads the game and the session only; writes nothing of either.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "board_net.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>

#include "fzgx/freeze.h"
#include "net_internal.h"
#include "out_test.h"

namespace fzgx::board
{
namespace
{
// each racer's knockouts: the game counts them per attack (+0x212), and the increases are added up
std::array<u32, 30> s_kos{};
std::array<u8, 30> s_attack{};
Snapshot s_snapshot;
u32 s_view = 0;  // the view last said (CameraView, as the launcher keeps it: 1 to 3, 4 first person): 0, none yet in this race
}  // namespace

// The race for the launcher, every displayed race frame (60 a second), every racer, as the launcher's race page
// shows it (Online.Protocol RaceSnapshot). From the racer blocks (0x620 bytes): flags
// +0x0 (0x10000 finished; out by the game's own verdict, out_test.h, as the image's HUD shows it), position +0x7C,
// speed +0x17C, energy +0x184, maximum energy +0x5DA (u16), top speed +0xD8, knockouts in the current attack +0x212
// (u8); and the progress records (+0x49C): rank +0x115, distance covered over all laps +0xF8 (what orders the racers
// still racing), laps completed +0x118, current lap time +0x160, best lap +0x1CC, sum of the laps +0x1D8 (the finish
// time), race time +0x1E4. Picture only.
void Race(Guest& g, u32 step, int local_seat)
{
  if (!Taken())
    return;
  using namespace net;
  const u32 module = g.Read32(MODULE_POINTER);
  const u32 machines = g.Read32(module + MACHINES);
  const u32 count = std::min<u32>(g.Read8(module + MACHINE_COUNT), 30);
  if (machines == 0 || count == 0)
    return;
  const auto f = [&](u32 at) {
    const float v = std::bit_cast<float>(g.Read32(at));
    return std::isfinite(v) ? v : 0.0f;
  };
  Snapshot& s = s_snapshot;
  s.frame = step;
  s.local_seat = static_cast<u32>(std::max(local_seat, 0));
  s.count = 0;
  for (u32 i = 0; i < count; ++i)
  {
    // what a replay's results hold of the racer is read as they read it (net::ReadRacer)
    const std::optional<net::RacerRecord> racer = ReadRacer(g, machines, i);
    if (!racer)
      continue;
    const u32 base = racer->block;
    const u32 m = racer->progress;
    const u8 attack = g.Read8(base + game::MACHINE_ATTACK_KOS);
    if (attack > s_attack[i])
      s_kos[i] += attack - s_attack[i];
    s_attack[i] = attack;
    const bool finished = racer->finished;
    const s32 laps = racer->laps;
    Racer& r = s.racers[s.count++];
    r.seat = static_cast<u8>(i);
    r.state = finished ? RacerState::Finished : out_test::Out(g, module, base) ? RacerState::Out : RacerState::Racing;
    r.rank = racer->rank;
    r.flags = laps > 0 ? RACER_FLAGS_HAS_BEST_LAP : 0;
    r.lap = static_cast<u16>(std::clamp(laps, 0, 0xFFFF));
    r.kos = static_cast<u16>(std::min<u32>(s_kos[i], 0xFFFF));
    r.max_energy = g.Read16(base + game::MACHINE_MAX_ENERGY);
    r.lap_ms = RaceMs(g, m + game::PROGRESS_LAP_TIME);
    r.total_ms = finished ? racer->lap_sum_ms : RaceMs(g, m + game::PROGRESS_RACE_TIME);
    r.best_lap_ms = laps > 0 ? racer->best_lap_ms : 0;
    r.speed = f(base + game::MACHINE_SPEED);
    r.energy = f(base + game::MACHINE_ENERGY);
    r.top_speed = f(base + game::MACHINE_TOP_SPEED);
    r.x = f(base + game::MACHINE_POSITION_X);
    r.y = f(base + game::MACHINE_POSITION_Y);
    r.z = f(base + game::MACHINE_POSITION_Z);
    r.distance = f(m + game::PROGRESS_DISTANCE);
  }
  ++s.serial;
  PutSnapshot(s);
}

// The view of viewport 0's chase camera record ([.bss 0xF6C] +0xA4), which follows the local racer while it races
// (frame_tasks.py `racetasks`): the game's camera button changes it in the step (raceview.py), from the input rows,
// so a rollback makes the change again and the value read after a frame's steps is the player's. Read only while
// the racer races and this game does not spectate (control block +248 below 180: from there the spectator's
// controls write the record's view, frame_spectate.py), and only a view the launcher keeps (1 to 3, and the game's 0,
// first person, said as CAMERA_FIRST_PERSON; 5 is the retired view the game sets while the followed machine is out). The launcher keeps the last one said for the next
// race (protocol/formats.md section 2, "match.camera"). Picture only: reads the game, writes nothing of it.
constexpr u32 CAMERA_FIRST_PERSON = 4;  // match.camera's and the board's number for the game's view 0
void CameraView(Guest& g, bool racing)
{
  if (!Taken() || !racing || g.Read32(table::SPECTATE_FRAMES_AT) >= 180)
    return;
  const u32 module = g.Read32(net::MODULE_POINTER);
  if (module == 0)
    return;
  const u32 record = g.Read32(module + game::CHASE_RECORD);
  if (!g.Ram(record + game::CHASE_VIEW, 2))
    return;
  const u32 game_view = g.Read16(record + game::CHASE_VIEW);
  if (game_view > 3)
    return;
  const u32 view = game_view == 0 ? CAMERA_FIRST_PERSON : game_view;
  if (view == s_view)
    return;
  Camera(view, s_view);
  s_view = view;
}

void ResetRace()
{
  s_view = 0;
  s_kos = {};
  s_attack = {};
  s_snapshot = {};
}
}  // namespace fzgx::board

namespace fzgx::net
{
std::optional<board::Phase> SessionPhase(u32* step)
{
  using board::Phase;
  const Session* s = s_session.get();
  if (!s)
    return std::nullopt;
  *step = freeze::ThePulse().step.load(std::memory_order_relaxed);
  switch (s->phase)
  {
  case net::Phase::Joining: return Phase::Joining;
  case net::Phase::Joined: return s->gate_since != Clock::time_point{} ? Phase::Gate : Phase::Loading;
  case net::Phase::Failed: return Phase::Left;
  case net::Phase::Running: break;
  }
  if (s->playback)
    return Phase::Playback;
  if (freeze::ThePulse().countdown.load(std::memory_order_relaxed))
    return Phase::Countdown;
  if (s->replay_ended || (!s->practice && !s->watcher &&
                          (s->watch != Watch::Racing || s->settled || s->overs_sent != 0 || s->exit_sent)))
    return Phase::Finished;
  return Phase::Racing;
}
}  // namespace fzgx::net
