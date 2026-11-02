// The observer HUD over a race watched (fzgx/observer.h says what it shows and why): once a
// displayed frame, from spectator_frame.cpp, the race read for it and added to what the spectator overlay is told
// (Host::ShowSpectator), and the game's own HUD hidden behind it.
//   - The tower: every racer's colour and name from the race's set-up (timeline.cpp SetupColours: the colours the
//     course map's dots have too; a seat the set-up names none of goes by its machine's name), its gap to the leader
//     (fzgx/observer.h GapOf, from the race's front kept a step shown at a time), the leader's lap of the race's laps
//     (the race block's laps, memmap RACE_LAPS) and the racers still racing.
//   - The racer card, for the racer followed: its machine's name (by the race block's machine of its seat,
//     fzgx/observer.h MachineName), its speed (+0x17C, km/h, what the game's speedometer
//     draws), the lap it is on and that lap's time (its progress record's +0x160), or once it has
//     finished its race time (+0x1D8, the sum of its laps).
//   - The game's own HUD: hidden while the view is the observer HUD or the clean picture, by its part mask
//     (game::HUD_PARTS, viewport 0, which only drawing reads: the course map's note in minimap_frame.cpp) written 0
//     before the frame's steps, as the game itself writes it for a racer out or finished. Back to the game's HUD: the
//     mask the image keeps (control block +60: `spechud` keeps it, `hudstate` and `spechud` write the mask from it
//     after the steps, so it is held 0 too while the HUD is hidden, and kept here) is put back, and the mask itself
//     when the racer followed still races and the image's own spectating (+56) does not draw it (there `spechud`
//     writes the mask itself after the steps). A live racer whose knock-out a rollback took back races with it again;
//     at the race's end the kept word is put back for the results.
// Picture only: what is read changes nothing, and the one word written is drawing's.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "net_internal.h"
#include "files.h"

#include "fzgx/observer.h"

#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace fzgx::net
{
namespace
{
namespace ob = observer;
namespace sp = spectator;
constexpr u32 SPECTATING = CONTROL + 56, HUD_KEPT = CONTROL + 60;  // (spectator_frame.cpp, minimap_frame.cpp)
constexpr u32 BLOCK = game::MACHINE_SIZE, PROGRESS = game::MACHINE_PROGRESS, FINISHED = game::MACHINE_FLAG_FINISHED;
constexpr u32 SPEED = game::MACHINE_SPEED;  // in a racer's block
constexpr u32 LAP_LENGTH = game::PROGRESS_LAP_LENGTH, LAPS_DONE = game::PROGRESS_LAPS, LAP_TIME = game::PROGRESS_LAP_TIME,
              LAPS_SUM = game::PROGRESS_LAP_SUM;  // in a progress record
constexpr u32 RACE_LAPS = 16, RACE_MACHINES = 32;  // in the race block: the laps; the seats' machines (files_match.cpp)

// A time record (frames, fraction, minutes, seconds, milliseconds: port/facts/machine-state.csv) in milliseconds.
u32 Ms(Guest& g, u32 at)
{
  return g.Read8(at + 8) * 60000u + g.Read8(at + 9) * 1000u + g.Read16(at + 10);
}

float Float(Guest& g, u32 at)
{
  const float v = std::bit_cast<float>(g.Read32(at));
  return std::isfinite(v) ? v : 0.0f;
}

void Name(ObserverState& o, Guest& g, u32 count)
{
  Timeline setup;
  SetupColours(setup);
  for (u32 i = 0; i < 30; ++i)
  {
    o.colour[i] = setup.colour[i] & 0xFFFFFF;
    std::memcpy(o.name[i].data(), setup.names[i], sizeof(setup.names[i]));
    o.name[i].back() = 0;
    if (i < count)
      std::snprintf(o.machine[i].data(), o.machine[i].size(), "%s",
                    ob::MachineName(g.Read8(boot::RACE + RACE_MACHINES + i)));
  }
  o.named = true;
}

template <std::size_t N>
void Put(char (&to)[N], const char* from)
{
  std::strncpy(to, from, N - 1);
  to[N - 1] = 0;
}
}  // namespace

void ObserverFill(Guest& g, Session& s, SpectatorShow& show, const sp::Racers& racers, u32 machines, u32 count,
                  u32 step)
{
  ObserverState& o = s.spectator.observer;
  const sp::Controls& c = *s.spectator.controls;
  if (!o.named)
    Name(o, g, count);
  std::array<ob::Standing, 30> stand{};
  float front = 0;
  for (u32 i = 0; i < count; ++i)
  {
    const sp::Racer& r = racers[i];
    if (!r.present)
      continue;
    const u32 base = machines + i * BLOCK, progress = g.Read32(base + PROGRESS);
    ob::Standing& st = stand[i];
    st.present = true;
    st.out = r.out;
    st.finished = r.finished;
    st.rank = r.rank;
    st.distance = r.distance;
    st.lap_length = Float(g, progress + LAP_LENGTH);
    st.speed = Float(g, base + SPEED);
    st.finish_ms = r.finished ? Ms(g, progress + LAPS_SUM) : 0;
    if (!r.out)
      front = std::max(front, r.distance);
  }
  o.front.Record(step, front);
  const u32 winner = ob::WinnerTime(stand.data(), count);
  for (u32 k = 0; k < show.count; ++k)
  {
    SpectatorRow& row = show.rows[k];
    const ob::Gap gap = ob::GapOf(stand[row.seat], o.front, step, winner);
    row.gap_kind = static_cast<u8>(gap.kind);
    row.gap_ms = gap.ms;
    row.gap_laps = gap.laps;
    row.colour = o.colour[row.seat];
    Put(row.name, o.name[row.seat][0] != 0 ? o.name[row.seat].data() : o.machine[row.seat].data());
  }
  show.laps = g.Read32(boot::RACE + RACE_LAPS);
  show.lap = ob::LapOf(show.count != 0 ? show.rows[0].laps : 0, show.laps);
  show.racing = ob::Racing(stand.data(), count);
  show.view = static_cast<u32>(c.view());
  show.role = static_cast<u32>(c.role());
  show.own = c.role() == sp::Role::KnockedOut && s.my_seat >= 0 ? static_cast<u32>(s.my_seat) : 0xFFFFFFFF;
  if (show.follow < count && racers[show.follow].present)
  {
    const u32 base = machines + show.follow * BLOCK, progress = g.Read32(base + PROGRESS);
    const bool finished = (g.Read32(base) & FINISHED) != 0;
    Put(show.card.machine, o.machine[show.follow].data());
    show.card.speed = Float(g, base + SPEED);
    show.card.lap = ob::LapOf(std::min<u32>(g.Read32(progress + LAPS_DONE), 255), show.laps);
    show.card.lap_ms = Ms(g, progress + (finished ? LAPS_SUM : LAP_TIME));
  }
  ++o.frames[std::min<u32>(show.view, sp::VIEWS - 1)];
  if (!std::exchange(o.said, true))
    Notice("fzgx: observer: {} racers, lap {} of {}; racer {} followed: '{}', machine '{}', colour {:06x}, {:.0f} km/h",
           show.count, show.lap, show.laps, show.follow,
           show.follow < 30 ? (o.name[show.follow][0] != 0 ? o.name[show.follow].data() : "(none)") : "",
           show.card.machine, show.follow < 30 ? o.colour[show.follow] : 0, show.card.speed);
}

void ObserverGameHud(Guest& g, Session& s, u32 module, sp::View view, bool followed_races)
{
  ObserverState& o = s.spectator.observer;
  const u32 parts = module + game::HUD_PARTS;
  if (view != sp::View::Game)
  {
    // the mask, and the word the image puts it back from after the steps (frame_hud.py `hudstate`, `spechud`)
    if (const u32 kept = g.Read32(HUD_KEPT); kept != 0)
    {
      o.kept = kept;
      g.Write32(HUD_KEPT, 0);
    }
    if (g.Read32(parts) != 0)
      g.Write32(parts, 0);
    o.hud_hidden = true;
    return;
  }
  // (what the view found last frame, for the log: 0 is the game's HUD not drawn)
  ++o.game_mask[g.Read32(parts) != 0 ? 1 : 0];
  if (!std::exchange(o.hud_hidden, false) || o.kept == 0)
    return;
  g.Write32(HUD_KEPT, o.kept);
  if (followed_races && g.Read32(SPECTATING) == 0)
  {
    g.Write32(parts, o.kept);
    ++o.hud_back;
  }
}

void ObserverOff(Guest& g, Session& s, u32 module, bool racing)
{
  TheHost().ShowSpectator({});
  ObserverGameHud(g, s, module, sp::View::Game, racing);
}

void ObserverSummary(const Session& s)
{
  const ObserverState& o = s.spectator.observer;
  Notice("fzgx: observer: frames shown in the observer HUD {}, the game's HUD {}, clean {}; the game's HUD put back "
         "{} times; in that view its mask was off {} frames, on {}", o.frames[0], o.frames[1], o.frames[2], o.hud_back,
         o.game_mask[0], o.game_mask[1]);
}
}  // namespace fzgx::net
