// What the status board shows of a race (board_net.h): the snapshot of every racer each displayed frame, and the
// phase as the session gives it. Reads the game and the session only; writes nothing of either.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "board_net.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <string_view>

#include "files.h"
#include "fzgx/freeze.h"
#include "mode.h"
#include "mode_hud.h"
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
// time), race time +0x1E4; then the race's mode on it (ModeOnTheSnapshot). Picture only.
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
  const boot::Match* match = boot::Get();
  ModeOnTheSnapshot(g, match && match->game_mode ? &*match->game_mode : nullptr, g.Read32(layout::MODES + 4), s);
  ++s.serial;
  PutSnapshot(s);
}

// The race's mode on the snapshot: what the game's HUD shows of the mode's places and numbers, so that the launcher's
// standings (its own, the lobby's watchers', the coordinator's start grid) stand in the same order as the position
// on the screen and show the same score, whatever the mode scores by (the owner, 2026-11-12). No mode (`p` null) or
// no block: every place 0, no score.
// The places (Racer place, 1 first): the standing's rows (rank, P+0x115) once the mode orders the standing: with
// live_places and a seat placed (mode.cpp ShowPlaces writes the rows after the ranking pass), or once the mode ended
// the race (END 2: EndClause's order, held every run). Else 0: the launcher orders by finish times and distances, as
// the game's own ranking byte is not the race position.
// The score (Snapshot score_kind, Racer score with RACER_FLAGS_HAS_SCORE): each seat's show_score number when any seat
// shows one (Points, under the label the HUD draws: the tag text its word names, else POINTS), else each seat's
// show_distance metres when any seat shows them (Metres; Overdrive); a seat that shows neither has no score. Points
// before metres: Crown shows every seat's points and the holder's keep-moving metres. Reads the block only.
void ModeOnTheSnapshot(const Guest& g, const mode::Package* p, u32 block, Snapshot& s)
{
  using namespace mode::hud;
  s.score_kind = ScoreKind::None;
  s.score_label_length = 0;
  s.score_label.fill(0);
  const u32 count = std::min<u32>(s.count, 30);
  for (u32 i = 0; i < count; ++i)
  {
    s.racers[i].place = 0;
    s.racers[i].score = 0.0f;
    s.racers[i].flags &= static_cast<u8>(~RACER_FLAGS_HAS_SCORE);
  }
  if (!p || block == 0)
    return;
  bool placed = g.Read32(block + mode::END) == 2;
  for (u32 i = 0; i < count && !placed && p->live_places; ++i)
    placed = g.Read8(block + mode::PLACES + s.racers[i].seat) != 0;
  if (placed)
    for (u32 i = 0; i < count; ++i)
      s.racers[i].place = s.racers[i].rank < count ? static_cast<u8>(s.racers[i].rank + 1) : 0;
  bool points = false, metres = false;
  u32 label = 0;
  for (u32 i = 0; i < count; ++i)
  {
    const u32 seat = s.racers[i].seat;
    if (g.Read32(block + SCORES + seat * SCORE_RECORD + SCORE_VALUE) != 0)
    {
      // (the label: the first seat's that names one; a mode labels every seat alike)
      if (TextOf(label) == 0)
        label = g.Read32(block + SCORES + seat * SCORE_RECORD + SCORE_LABEL);
      points = true;
    }
    else if (g.Read8(block + STORY + seat * STORY_RECORD + STORY_FLAGS) & STORY_DISTANCE)
      metres = true;
  }
  if (!points && !metres)
    return;
  s.score_kind = points ? ScoreKind::Points : ScoreKind::Metres;
  for (u32 i = 0; i < count; ++i)
  {
    Racer& r = s.racers[i];
    if (points)
    {
      const u32 sc = g.Read32(block + SCORES + r.seat * SCORE_RECORD + SCORE_VALUE);
      if (sc == 0)
        continue;
      r.score = static_cast<float>(std::min(sc - 1, SCORE_MOST));
    }
    else
    {
      const u32 st = block + STORY + r.seat * STORY_RECORD;
      if (!(g.Read8(st + STORY_FLAGS) & STORY_DISTANCE))
        continue;
      const float metres_made = std::bit_cast<float>(g.Read32(st + STORY_METRES));
      r.score = std::isfinite(metres_made) ? metres_made : 0.0f;
    }
    r.flags |= RACER_FLAGS_HAS_SCORE;
  }
  if (!points)
    return;
  // the label as the HUD draws it (mode_hud.cpp: the tag text the word names from 1, else POINTS), cut at a character
  const u32 text = TextOf(label);
  const std::string_view shown =
      text >= 1 && text <= p->tag_texts.size() ? std::string_view(p->tag_texts[text - 1]) : std::string_view(DEFAULT_SCORE_LABEL);
  u32 n = static_cast<u32>(std::min<size_t>(shown.size(), s.score_label.size()));
  while (n > 0 && n < shown.size() && (static_cast<u8>(shown[n]) & 0xC0) == 0x80)
    --n;
  std::memcpy(s.score_label.data(), shown.data(), n);
  s.score_label_length = n;
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
