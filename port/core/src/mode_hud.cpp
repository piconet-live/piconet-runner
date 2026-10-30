// A game mode's displays (mode_hud.h): the display words into the block's display page, and the drawing's passes
// that read it once a frame.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mode_hud.h"

#include <algorithm>
#include <bit>

#include "internal.h"
#include "mode.h"
#include "rollback.h"
#include "util.h"

namespace fzgx::mode::hud
{
namespace
{
using namespace modes;

constexpr u32 NONE = 0xFFFFFFFF;
constexpr u32 SRC = net::BSS + 0x84;  // the story parts' source block: +0 s32*, +4 f32*, +8 P*, +0xC R*, +0x10 u32
constexpr u32 R = net::BSS + 0x3C30;
constexpr u32 PHASE_INTRO = 0x27;
constexpr s32 DIGIT_MOST = 4 * 60 - 1;  // the digit sheet 0xBA0A holds 0 to 3: value / 60 must stay below 4

u32 Seat(u32 block, u32 seat)
{
  return block + SEATS_AT + seat * SEAT_DISPLAY;
}
}  // namespace

bool IsDisplay(u8 id)
{
  switch (id)
  {
    case ACTION_SHOW:
    case ACTION_HIDE:
    case ACTION_SHOWNUMBER:
    case ACTION_STOCKBANNER:
    case ACTION_SEATLOOK:
    case ACTION_SEATOWNLOOK:
    case ACTION_SEATCOLOUR:
    case ACTION_MAPHIDE:
    case ACTION_MAPMARK:
      return true;
    default:
      return false;
  }
}

bool Apply(Guest& g, u32 block, u8 id, u32 seat, u32 value)
{
  const u32 r = Seat(block, seat);
  switch (id)
  {
    case ACTION_SHOW:
    case ACTION_HIDE:
      if (value < PART_COUNT)
        g.Write32(r + PARTS, id == ACTION_SHOW ? g.Read32(r + PARTS) | 1u << value : g.Read32(r + PARTS) & ~(1u << value));
      return true;
    case ACTION_SHOWNUMBER:
      // 0 to 99 shows; anything else hides (the icon draws two digits: 100 would read 00)
      g.Write32(r + NUMBER, value <= 99 ? value + 1 : 0);
      return true;
    case ACTION_STOCKBANNER:
      if (value < BANNER_COUNT)
        g.Write8(r + BANNER, static_cast<u8>(value + 1));
      if (value == BANNER_GAP || value == BANNER_WRONG_WAY)
      {
        const u32 module = layout::MAIN_REL, machines = g.Read32(module + net::MACHINES);
        g.Write8(r + ASKED, static_cast<u8>(value));
        g.Write32(r + ASKED_AT, g.Read32(module + game::RACE_CLOCK));
        if (value == BANNER_GAP && machines != 0)
        {
          // the time fn_1_327F8 gives the popup at a lap line, which the popup draws against the leader's
          const u32 p = g.Read32(machines + seat * game::MACHINE_SIZE + game::MACHINE_PROGRESS);
          if (p != 0)
            g.Write32(r + GAP_TIME, static_cast<u32>(g.Read8(p + 0x1E0)) << 20 | static_cast<u32>(g.Read8(p + 0x1E1)) << 12 |
                                        g.Read16(p + 0x1E2));
        }
      }
      return true;
    case ACTION_SEATLOOK:
      if (value < LOOK_COUNT)
        g.Write8(r + LOOK, static_cast<u8>(value));
      return true;
    case ACTION_SEATOWNLOOK:
      if (value < LOOK_COUNT)
        g.Write8(r + OWN_LOOK, static_cast<u8>(value + 1));
      return true;
    case ACTION_SEATCOLOUR:
      g.Write32(r + COLOUR, value);
      return true;
    case ACTION_MAPHIDE:
      g.Write32(r + MAP_HIDE, value & ((1u << SEATS) - 1));
      return true;
    case ACTION_MAPMARK:
      g.Write32(r + MAP_MARK, static_cast<s32>(value) < 0 ? 0 : MARKED | (value & 0xFFFFFF));
      return true;
    default:
      return false;
  }
}

void BeginRun(Guest& g, u32 block)
{
  for (u32 seat = 0; seat < SEATS; ++seat)
    g.Write8(Seat(block, seat) + BANNER, 0);
}

namespace
{
// Whether the code holds the action `id` (with `part`: a show of a part other than TIMER, the compiler writing a
// choice as a Push right before the action: ModeEmitter). Code the compiler did not write: yes, rather than miss it.
bool Uses(const std::vector<u8>& code, u8 id, bool part)
{
  for (size_t i = 0; i < code.size();)
  {
    const u8 op = code[i];
    const int size = OpSize(op);
    if (size <= 0)
      return true;
    if (!part && op == OP_ACTION && i + 1 < code.size() && code[i + 1] == id)
      return true;
    if (part && op == OP_PUSH && i + 7 <= code.size() && code[i + 5] == OP_ACTION && code[i + 6] == id)
    {
      const u32 value = static_cast<u32>(code[i + 1]) << 24 | code[i + 2] << 16 | code[i + 3] << 8 | code[i + 4];
      if (value != PART_TIMER)
        return true;
    }
    i += static_cast<size_t>(size);
  }
  return false;
}
}  // namespace

bool NeedsStoryPictures(const std::vector<u8>& code)
{
  return Uses(code, ACTION_SHOW, true) || Uses(code, ACTION_DANGERZONE, false);
}

u32 WindowFlags(const Package& p)
{
  return (NeedsStoryPictures(p.code) ? STORY_PICTURES : 0) | (Uses(p.code, ACTION_AWARD, false) ? POINTS_PAGE : 0);
}

Frame Read(const Guest& g, u32 block, u32 viewed, u32 own)
{
  Frame f;
  for (u32 seat = 0; seat < SEATS; ++seat)
  {
    const u32 r = Seat(block, seat);
    const u8 own_look = g.Read8(r + OWN_LOOK);
    const u8 look = seat == own && own_look != 0 ? own_look - 1 : g.Read8(r + LOOK);
    f.looks[seat] = look < LOOK_COUNT ? look : static_cast<u8>(LOOK_NORMAL);
    f.colours[seat] = g.Read32(r + COLOUR);
  }
  if (viewed >= SEATS)
    return f;
  const u32 r = Seat(block, viewed);
  const u32 parts = g.Read32(r + PARTS);
  for (u32 p = 0; p < PART_COUNT; ++p)
    if (parts >> p & 1)
      f.parts |= PART_BITS[p];
  // the danger zone's display (vocabulary 4.4): while its count runs (the race side's, mode.h DANGER: from 60 x
  // seconds + 60 down, the machine exploding at 60) the big digit and the warning, as story chapter 6's, whose digit
  // is the count / 60; the sheet's digits go to 3
  const u32 d = block + mode::DANGER + 16 * viewed, state = g.Read32(d + 12);
  if (const s32 left = static_cast<s32>(g.Read32(d + 8)); (state & DANGER_ON) && !(state & DANGER_DONE) && left > 60)
  {
    f.parts |= PART_BITS[PART_DIGIT] | PART_BITS[PART_WARNING];
    f.digit = std::min(left, DIGIT_MOST);
  }
  return f;
}

void Hud(Guest& g)
{
  const u32 block = g.Read32(layout::MODES + 4);
  if (block == 0 || !g.Ram(net::MODULE_POINTER, 4) || g.Read32(net::MODULE_POINTER) != layout::MAIN_REL)
    return;
  const u32 module = layout::MAIN_REL;
  if (g.Read16(module + game::PHASE) < PHASE_INTRO)
    return;
  static bool said = false;  // (where the block is, once: for a test's dump or poke of the display page)
  if (!said)
  {
    said = true;
    Notice("fzgx: game mode: block at {:#010x}, display page at {:#010x}, window flags {:#x}", block, block + DISPLAY,
           g.Read32(layout::MODE_HUD + WINDOW_FLAGS));
  }
  const u32 viewed = net::ViewedRacer(g);
  const Frame f = Read(g, block, viewed, net::OwnSeat(g));
  // the part mask: the mode's parts over what the HUD's owners wrote this frame (hudstate, spechud, the game's own
  // when the machine is out or finished: 0 stays 0, the HUD is off). Every story part the mode does not show is off:
  // the default mask has them on.
  u32 gap_part = 0;
  if (const u32 own = net::OwnSeat(g); own < SEATS)
  {
    // GAP's popup and WRONG_WAY's sign, for the local seat: from the page, so a rollback changes nothing; the sign
    // starts once for each ask (the window is not rolled back, a step run again asks at the same clock)
    static u32 s_wrong_at = 0;
    const u32 r = Seat(block, own), at = g.Read32(r + ASKED_AT), since = g.Read32(module + game::RACE_CLOCK) - at;
    if (at != 0 && since <= GAP_STEPS)
    {
      if (g.Read8(r + ASKED) == BANNER_GAP)
        gap_part = GAP_PART;
      else if (at != s_wrong_at)
      {
        s_wrong_at = at;
        g.Write32(layout::MODE_HUD + WINDOW_WRONG, 1);
      }
    }
  }
  const u32 mask = g.Read32(module + game::HUD_PARTS);
  if (mask != 0)
    g.Write32(module + game::HUD_PARTS, (mask & ~STORY_BITS) | f.parts | gap_part);
  // the source block, whole: the digit's value in the window; the viewed racer's speed and progress record; R
  const u32 window = layout::MODE_HUD;
  g.Write32(window + WINDOW_DIGIT, static_cast<u32>(f.digit));
  u32 speed = 0, progress = 0;
  const u32 machines = g.Read32(module + net::MACHINES);
  if (machines != 0 && viewed < std::min<u32>(g.Read8(module + net::MACHINE_COUNT), SEATS))
  {
    const u32 m = machines + viewed * game::MACHINE_SIZE;
    speed = m + game::MACHINE_SPEED;
    progress = g.Read32(m + game::MACHINE_PROGRESS);
  }
  g.Write32(module + SRC, f.parts & PART_BITS[PART_DIGIT] ? window + WINDOW_DIGIT : 0);
  g.Write32(module + SRC + 4, speed);
  g.Write32(module + SRC + 8, progress);
  g.Write32(module + SRC + 0xC, progress ? module + R : 0);
  g.Write32(module + SRC + 0x10, 0);
  // a seat's colour tints its machine drawn normally (a ghost keeps the ghost's tint)
  for (u32 seat = 0; seat < SEATS; ++seat)
  {
    const bool coloured = f.looks[seat] == LOOK_NORMAL && f.colours[seat] != 0;
    g.Write8(window + WINDOW_LOOKS + seat, coloured ? COLOURED : f.looks[seat]);
    g.Write32(window + WINDOW_TINTS + 4 * seat, coloured ? f.colours[seat] : 0);
  }
}

void MapRacers(const Guest& g, u32 you, bool racing, std::array<bool, 30>& present, std::array<u32, 30>& colour)
{
  const u32 block = g.Read32(layout::MODES + 4);
  if (block == 0)
    return;
  for (u32 seat = 0; seat < SEATS; ++seat)
  {
    const u32 r = Seat(block, seat);
    if (racing && you < SEATS && seat != you && (g.Read32(r + MAP_HIDE) >> you & 1))
      present[seat] = false;
    if (const u32 mark = g.Read32(r + MAP_MARK); mark & MARKED)
      colour[seat] = mark & 0xFFFFFF;
  }
}
}  // namespace fzgx::mode::hud
