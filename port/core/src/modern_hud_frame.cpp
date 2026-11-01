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
#include "net_modes.h"
#include "mode.h"
#include "mode_hud.h"
#include "mode_gap.h"
#include "files.h"

#include "fzgx/hud_layout.h"
#include "fzgx/minimap.h"
#include "fzgx/observer.h"

#include <bit>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string_view>
#include <utility>

namespace fzgx::net
{
namespace
{
namespace hl = hud_layout;
namespace mh = mode::hud;

constexpr u32 COUNTDOWN = 0x28, RACE = 0x29, COURSE_INTRO = 0x27;
constexpr u32 RACE_LAPS = 16;                     // the race block's laps (observer_frame.cpp)
constexpr u32 MACHINE_MAX_ENERGY = 0x5DA;         // a machine's most energy, u16
constexpr u32 MACHINE_BOOST_LEFT = 0x18B;         // the countdown of an energy boost, plates not (machine-state.csv)
constexpr u32 RULE = BSS + 0x3C30, RULE_LAP_ONE_BOOST = 0x10;  // the rule word: boost on lap 1 (fn_1_331EC 0x33210)
constexpr u32 R_TIMER = 0x1BDC70 + 0xA4;          // the timed mode's clock, steps (mode.cpp R_BASE; the game's TIMER part fn_1_CF5B0 0xCF600)
constexpr u32 CLOCK_MOST = 999;                   // that part's most
constexpr u32 RACE_MACHINES = 32;                 // in the race block: the seats' machines (files_match.cpp)
constexpr u32 SURFACE = 0x218, SURFACE_PIT = 0x08000000;  // the refilling surface (fn_1_289B4 0x28C98)
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

// The names as the observer's tower has them: the race's set-up's, or a machine's name. Once: the set-up does not change.
struct Names
{
  bool read = false;
  std::array<std::array<char, 24>, 30> name{};
  std::array<u32, 30> colour{};
};
Names s_names;

void ReadNames(Guest& g)
{
  if (s_names.read)
    return;
  s_names.read = true;
  Timeline setup;
  SetupColours(setup);
  for (u32 i = 0; i < 30; ++i)
  {
    s_names.colour[i] = setup.colour[i] & 0xFFFFFF;
    // (the set-up names every computer racer just CPU: six of those would say nothing, so they go by their machines)
    if (setup.names[i][0] != 0 && std::string_view(setup.names[i]) != "CPU")
      std::snprintf(s_names.name[i].data(), s_names.name[i].size(), "%.*s", static_cast<int>(sizeof(setup.names[i])), setup.names[i]);
    else
      std::snprintf(s_names.name[i].data(), s_names.name[i].size(), "%s", observer::MachineName(g.Read8(boot::RACE + RACE_MACHINES + i)));
  }
}

// The modes' slots in hud_layout's order (the mode's `top6` apart).
static_assert(modes::SLOT_COUNT == 8 && modes::SLOT_UNDER_PLACE == 0 && modes::SLOT_UNDER_TOP6 == 6 && modes::SLOT_TOP6 == 7 &&
              static_cast<u32>(hl::SlotId::UnderTop6) - static_cast<u32>(hl::SlotId::UnderPlace) == 6 && hl::SLOTS == static_cast<u32>(hl::SlotId::UnderTop6) + 1);
static_assert(modes::COLOUR_COUNT == 5);
constexpr std::array<u32, modes::COLOUR_COUNT> PALETTE = {0xFFFFFF, 0x3DFF6A, 0xFF4D4D, 0xFFD500, 0xFF9F1A};  // the HUD's: overlay.h's TEXT, ACCENT, DANGER, GOLD, WARN

// A text of the package with its values written in; the characters 1 and 2 stand for them.
void WriteText(std::array<char, 80>& to, const std::string& text, s32 first, s32 second)
{
  std::size_t at = 0;
  for (const char c : text)
  {
    char number[16];
    const int wrote = c == 1 || c == 2 ? std::snprintf(number, sizeof(number), "%d", c == 1 ? first : second) : 0;
    for (int i = 0; i < wrote && at + 1 < to.size(); ++i)
      to[at++] = number[i];
    if (wrote == 0 && c != 1 && c != 2 && at + 1 < to.size())
      to[at++] = c;
  }
  to[at] = 0;
}

ModernHudShow::Gap ShowGap(const mh::TimeGap& gap)
{
  return {gap.known, gap.steps, gap.laps, gap.trend};
}

// What the mode's words have on the racer followed's slots, the gaps worked out from the block as it stands.
void ReadModeSlots(Guest& g, ModernHudShow& show, u32 viewed, u32 machines, u32 count)
{
  const u32 block = g.Read32(layout::MODES + 4);
  const boot::Match* match = boot::Get();
  if (block == 0 || !match || !match->game_mode)
    return;
  std::array<u32, 30> place_of{};
  place_of.fill(30);
  const auto ranks = [&] {
    for (u32 seat = 0; seat < count; ++seat)
      if (const u32 p = g.Read32(machines + seat * game::MACHINE_SIZE + game::MACHINE_PROGRESS); p != 0)
        place_of[g.Read8(p + game::PROGRESS_RANK) % 30] = seat;
  };
  bool ranked = false;
  for (u32 slot = 0; slot < modes::SLOT_COUNT; ++slot)
  {
    const u32 r = block + mh::SlotRecord(viewed, slot);
    const u8 kind = g.Read8(r + mh::SLOT_KIND);
    if (kind == mh::SLOT_NONE)
      continue;
    const u8 what = g.Read8(r + mh::SLOT_WHAT), colour = g.Read8(r + mh::SLOT_COLOUR);
    if (slot == modes::SLOT_TOP6)
    {
      show.row_gaps = kind == mh::SLOT_GAP;
      if (show.row_gaps)
        for (std::size_t i = 0; i < show.heads.size(); ++i)
          if (show.heads[i].address != 0 && show.heads[i].seat < count)
            show.row_gap[i] = ShowGap(mh::TimeGapOf(g, block, viewed, show.heads[i].seat));
      continue;
    }
    ModernHudShow::ModeSlot& to = show.mode_slot[slot];
    show.mode_slots = true;
    to.coloured = colour < PALETTE.size();
    to.rgb = to.coloured ? PALETTE[colour] : 0xFFFFFF;
    const u32 a = g.Read32(r + mh::SLOT_A), b = g.Read32(r + mh::SLOT_B);
    switch (kind)
    {
      case mh::SLOT_BAR:
        to.kind = ModernHudShow::ModeKind::Bar;
        to.bar = what;
        to.value = std::bit_cast<float>(a);
        to.most = std::bit_cast<float>(b);
        if (!std::isfinite(to.value))
          to.value = 0;
        if (!std::isfinite(to.most) || to.most <= 0)
          to.most = 1;
        break;
      case mh::SLOT_FIGURE:
        to.kind = ModernHudShow::ModeKind::Figure;
        to.figure = static_cast<s32>(a);
        break;
      case mh::SLOT_TEXT:
        if (what >= 1 && what <= match->game_mode->hud_texts.size())
        {
          to.kind = ModernHudShow::ModeKind::Text;
          WriteText(to.text, match->game_mode->hud_texts[what - 1], static_cast<s32>(a), static_cast<s32>(b));
        }
        break;
      case mh::SLOT_GAP:
      {
        to.kind = ModernHudShow::ModeKind::Gap;
        if (!std::exchange(ranked, true))
          ranks();
        const u32 place = g.Read8(g.Read32(machines + viewed * game::MACHINE_SIZE + game::MACHINE_PROGRESS) + game::PROGRESS_RANK);
        const u32 at = what == modes::TARGET_AHEAD ? (place == 0 ? 30 : place - 1) : what == modes::TARGET_BEHIND ? place + 1 : 0;
        if (at < 30 && place_of[at] < count && place_of[at] != viewed)
          to.gap = ShowGap(mh::TimeGapOf(g, block, viewed, place_of[at]));
        break;
      }
      default:
        break;
    }
  }
}

struct Stats
{
  u64 frames = 0, shown = 0, stripped = 0;
  std::chrono::nanoseconds total{}, most{};
};
Stats s_stats;
bool s_told = false;
}  // namespace

bool ModernHudOn(Guest& g)
{
  return TheOptions().modern_hud || (g.Read32(layout::MODE_HUD + mh::WINDOW_FLAGS) & mh::HUD_MODERN) != 0;
}

void ModernHudFrame(Guest& g)
{
  if (!g.Ram(MODULE_POINTER, 4) || g.Read32(MODULE_POINTER) != layout::MAIN_REL || !s_session)
    return;
  if (!ModernHudOn(g))
    return;
  const u32 module = layout::MAIN_REL, phase = g.Read16(module + game::PHASE);
  if (phase < COURSE_INTRO)
    return;
  const auto began = std::chrono::steady_clock::now();
  const u32 mask = g.Read32(module + game::HUD_PARTS);
  // A mode's clock is ours too: the mode's pass set its bit for this frame if the code shows it. (Without a mode that bit
  // is the default mask's and says nothing.)
  const u32 clock_bit = g.Read32(layout::MODES + 4) != 0 ? mh::PART_BITS[modes::PART_TIMER] : 0;
  // The game's parts off from the course's intro on, so no part is drawn even once. (A mask 0 stays 0.)
  if (mask & (mh::MODERN_PARTS | clock_bit))
  {
    g.Write32(module + game::HUD_PARTS, mask & ~(mh::MODERN_PARTS | clock_bit));
    ++s_stats.stripped;
  }
  ModernHudShow show;
  const u32 viewed = ViewedRacer(g), machines = g.Read32(module + MACHINES);
  const u32 count = std::min<u32>(g.Read8(module + MACHINE_COUNT), 30);
  // A watcher's Game view is this HUD for the racer followed, whatever camera the game's own HUD is off behind (the
  // image keeps its mask off behind the chase camera, `spechud`), while that racer races: the same racer's parts as the game's HUD.
  const auto& controls = s_session->spectator.controls;
  const bool game_view = s_session->spectator.shown && controls && controls->view() == spectator::View::Game && machines != 0 && viewed < count &&
                         (g.Read32(machines + viewed * game::MACHINE_SIZE) & (game::MACHINE_FLAG_OUT | game::MACHINE_FLAG_FINISHED)) == 0;
  show.on = (phase == COUNTDOWN || phase == RACE) && (minimap::HudOn(mask) || game_view) && machines != 0 && viewed < count;
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
      up(hl::SlotId::Speed, modes::PART_SPEED);
      up(hl::SlotId::Top6, modes::PART_FACES);
      show.meter = hl::MeterOf(TheOptions().hud_meter);
      show.clock = (mask & clock_bit) != 0 && show.layout[hl::SlotId::Clock].shown;
      show.shown[static_cast<std::size_t>(hl::SlotId::Clock)] = show.clock;
      show.clock_seconds = std::min<u32>((g.Read16(module + R_TIMER) + 59) / 60, CLOCK_MOST);
      const float energy = std::bit_cast<float>(g.Read32(m + game::MACHINE_ENERGY));
      const u32 most = g.Read16(m + MACHINE_MAX_ENERGY);
      show.energy = std::isfinite(energy) ? energy : 0;
      show.max_energy = most != 0 ? static_cast<float>(most) : 100.0f;
      show.pit = (g.Read32(m + SURFACE) & SURFACE_PIT) != 0;
      const float speed = std::bit_cast<float>(g.Read32(m + game::MACHINE_SPEED));
      show.speed = std::isfinite(speed) ? std::max(speed, 0.0f) : 0;
      // Red or rainbow: the game's own meter draws its rainbow on P+0x118 + 1 >= 2 or the rule's lap-1 boost (fn_1_C8DC0
      // 0xC90F4), the same test that lets a press boost (fn_1_331EC 0x33220); a mode's `boost off` takes the boost too.
      const bool lap_allows = (g.Read32(RULE) & RULE_LAP_ONE_BOOST) != 0 || static_cast<s32>(g.Read32(p + game::PROGRESS_LAPS)) >= 1;
      const bool mode_allows = g.Read32(layout::MODES + 4) == 0 || (g.Read32(mode::Record(g, viewed)) & mode::BOOST_OFF) == 0;
      show.boost_open = lap_allows && mode_allows;
      show.boost_left = g.Read8(m + MACHINE_BOOST_LEFT);
      show.place = g.Read8(p + game::PROGRESS_RANK) + 1u;
      // the face board's entries (.bss 0x7ACD8, 0x64 each, their number at +0xD), which FaceDisp draws into +0x60
      if (const u32 faces = g.Read32(module + BSS + 0x7ACD8); faces != 0)
        for (u32 i = 0; i < std::min<u32>(g.Read8(module + BSS + 0x7ACD8 + 0xD), 6); ++i)
        {
          const u32 entry = faces + i * 0x64;
          auto& head = show.heads[i];
          head.address = g.Read32(entry + 0x60), head.seat = g.Read32(entry + 4), head.row = g.Read32(entry + 0x10), head.rank = g.Read32(entry + 0x14);
          if (head.seat < 30)
          {
            ReadNames(g);
            head.name = s_names.name[head.seat];
            std::array<bool, 30> present{};
            std::array<u32, 30> colour = s_names.colour;
            mode::hud::MapRacers(g, viewed, false, present, colour);  // (seat.map_mark colours the dot)
            head.colour = colour[head.seat];
          }
        }
      ReadModeSlots(g, show, viewed, machines, count);
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
