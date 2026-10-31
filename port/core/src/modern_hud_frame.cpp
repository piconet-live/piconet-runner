// The modern HUD's reading (ModernHudShow, fzgx/host.h), at the drawing's ask, after the frame routine's HUD passes and the
// mode's: the game's own parts for that element off in the part mask, and what they would have drawn told to the host.
//   - The mask is the one word written. Only drawing reads it (fzgx/minimap.h HUD_PARTS note; every reader in main.rel)
//     and no checksum covers it, and the drawers skipped write nothing a step reads (notes modern-hud/plan.md a).
//   - The values are the game's own for the racer the picture follows, read after every step of this frame, a rollback's
//     included, so they are the step shown, as the game's parts would have them; nothing is kept from step to step.
//   - Up when the game's HUD would be: the countdown and the race, with the mask's race parts up (mm::HudOn: hudstate,
//     spechud, the observer's views and the banner pass take them off; the mask's other high bits stay to tell).
// With neither the player's setting nor the mode's word, nothing here touches anything: the game's HUD, bit for bit.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "net_internal.h"
#include "mode.h"
#include "mode_hud.h"
#include "files.h"

#include "fzgx/hud_layout.h"
#include "fzgx/minimap.h"
#include "fzgx/observer.h"

#include <bit>
#include <chrono>
#include <cmath>

namespace fzgx::net
{
namespace
{
namespace hl = hud_layout;
namespace mh = mode::hud;

constexpr u32 COUNTDOWN = 0x28, RACE = 0x29, COURSE_INTRO = 0x27;
constexpr u32 RACE_LAPS = 16;                     // the race block's laps (observer_frame.cpp)
constexpr u32 MACHINE_MAX_ENERGY = 0x5DA;         // a machine's most energy, u16
constexpr u32 BOOST_ASK = 0x40, DEAD = 0x800;     // the lightning's test on the flags (fn_1_C8DC0 0xC9580 on)
constexpr u32 TOTAL_TIME = 0x1E4, LAP_TIME = game::PROGRESS_LAP_TIME, LAP_RECORD = 12, BEST = game::PROGRESS_BEST_LAP;

u32 Ms(const Guest& g, u32 record)
{
  return g.Read8(record + 8) * 60000u + g.Read8(record + 9) * 1000u + g.Read16(record + 10);
}

const hl::Layout& TheLayout()
{
  static const hl::Layout layout = [] {
    const std::string& path = TheOptions().hud_layout;
    std::string text, why;
    if (path.empty() || !ReadFileToString(path, text))
      return hl::Default();
    hl::Layout l = hl::FromText(text, &why);
    if (!why.empty())
      Warn("fzgx: modern HUD: {}: {}", path, why);
    return l;
  }();
  return layout;
}

struct Stats
{
  u64 frames = 0, shown = 0, stripped = 0;
  std::chrono::nanoseconds total{}, most{};
};
Stats s_stats;
bool s_told = false;
}  // namespace

void ModernHudFrame(Guest& g)
{
  if (!g.Ram(MODULE_POINTER, 4) || g.Read32(MODULE_POINTER) != layout::MAIN_REL || !s_session)
    return;
  if (!TheOptions().modern_hud && (g.Read32(layout::MODE_HUD + mh::WINDOW_FLAGS) & mh::HUD_MODERN) == 0)
    return;
  const u32 module = layout::MAIN_REL, phase = g.Read16(module + game::PHASE);
  if (phase < COURSE_INTRO)
    return;
  const auto began = std::chrono::steady_clock::now();
  const u32 mask = g.Read32(module + game::HUD_PARTS);
  // The game's parts off from the course's intro on, so no part is drawn even once. (A mask 0 stays 0.)
  if (mask & mh::MODERN_PARTS)
  {
    g.Write32(module + game::HUD_PARTS, mask & ~mh::MODERN_PARTS);
    ++s_stats.stripped;
  }
  ModernHudShow show;
  const u32 viewed = ViewedRacer(g), machines = g.Read32(module + MACHINES);
  const u32 count = std::min<u32>(g.Read8(module + MACHINE_COUNT), 30);
  show.on = (phase == COUNTDOWN || phase == RACE) && minimap::HudOn(mask) && machines != 0 && viewed < count;
  if (show.on)
  {
    const u32 m = machines + viewed * game::MACHINE_SIZE, p = g.Read32(m + game::MACHINE_PROGRESS);
    show.on = p != 0;
    if (show.on)
    {
      show.layout = TheLayout();
      const u32 parts = mh::SeatParts(g, viewed);
      const auto up = [&](hl::SlotId id, u32 part) {
        show.shown[static_cast<std::size_t>(id)] = show.layout[id].shown && !(parts >> part & 1);
      };
      up(hl::SlotId::Energy, modes::PART_ENERGY);
      up(hl::SlotId::Position, modes::PART_RANK);
      up(hl::SlotId::Lap, modes::PART_LAPS);
      up(hl::SlotId::RaceTime, modes::PART_RACE_TIME);
      up(hl::SlotId::LapTimes, modes::PART_LAP_TIMES);
      const float energy = std::bit_cast<float>(g.Read32(m + game::MACHINE_ENERGY));
      const u32 most = g.Read16(m + MACHINE_MAX_ENERGY);
      show.energy = std::isfinite(energy) ? energy : 0;
      show.max_energy = most != 0 ? static_cast<float>(most) : 100.0f;
      const u32 flags = g.Read32(m);
      show.boost = (flags & BOOST_ASK) != 0 && (flags & DEAD) == 0;
      show.place = g.Read8(p + game::PROGRESS_RANK) + 1u;
      for (u32 seat = 0; seat < count; ++seat)
        if ((g.Read32(machines + seat * game::MACHINE_SIZE) & game::MACHINE_FLAG_OUT) == 0)
          ++show.racers;
      const u32 done = std::min<u32>(g.Read32(p + game::PROGRESS_LAPS), 255);
      show.laps = g.Read32(boot::RACE + RACE_LAPS);
      show.lap = observer::LapOf(done, show.laps);
      show.race_ms = Ms(g, p + TOTAL_TIME);
      show.lap_ms = Ms(g, p + LAP_TIME);
      show.best_ms = Ms(g, p + BEST);
      show.lap_count = std::min<u32>(done, static_cast<u32>(show.lap_ms_ago.size()));
      for (u32 k = 0; k < show.lap_count; ++k)
        show.lap_ms_ago[k] = Ms(g, p + LAP_TIME + LAP_RECORD * (k + 1));
    }
  }
  if (show.on || s_told)
    TheHost().ShowModernHud(show);
  if (show.on && !s_told)
    Notice("fzgx: modern HUD: up at phase {:#x}, the mask {:#010x}, racer {} followed", phase, mask, viewed);
  s_told = show.on;
  const auto took = std::chrono::steady_clock::now() - began;
  ++s_stats.frames;
  s_stats.shown += show.on ? 1 : 0;
  s_stats.total += took;
  s_stats.most = std::max<std::chrono::nanoseconds>(s_stats.most, took);
}

void ModernHudSummary()
{
  if (s_stats.frames == 0)
    return;
  Notice("fzgx: modern HUD: reading {} frames, shown in {}, the mask written in {}; mean {} ns, most {} ns", s_stats.frames,
         s_stats.shown, s_stats.stripped, s_stats.total.count() / s_stats.frames, s_stats.most.count());
}
}  // namespace fzgx::net
