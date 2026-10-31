// A game mode's displays (mode_hud.h): the display words into the block's display page, and the drawing's passes
// that read it once a frame.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mode_hud.h"

#include <algorithm>
#include <bit>
#include <string_view>

#include "internal.h"
#include "mode.h"
#include "out_test.h"
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
    case ACTION_SHOWNUMBEROFF:
    case ACTION_SEATGHOSTTINT:
    case ACTION_SEATGHOSTSOUND:
    case ACTION_STOCKBANNER:
    case ACTION_SEATLOOK:
    case ACTION_SEATOWNLOOK:
    case ACTION_SEATCOLOUR:
    case ACTION_MAPHIDE:
    case ACTION_MAPMARK:
    case ACTION_TAG:
    case ACTION_TAGOFF:
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
        g.Write32(r + PARTS, (id == ACTION_SHOW) == (value < PART_RANK) ? g.Read32(r + PARTS) | 1u << value
                                                                        : g.Read32(r + PARTS) & ~(1u << value));
      return true;
    case ACTION_SHOWNUMBER:
      // a negative number hides; above 99 shows 99 (the icon draws two digits: 100 would read 00)
      g.Write32(r + NUMBER, static_cast<s32>(value) < 0 ? 0 : std::min<u32>(value, 99) + 1);
      return true;
    case ACTION_SHOWNUMBEROFF:
      g.Write32(r + NUMBER, 0);
      return true;
    case ACTION_SEATGHOSTTINT:
      g.Write32(block + STATS + seat * STAT + STAT_GHOST_TINT, value);
      return true;
    case ACTION_SEATGHOSTSOUND:
      g.Write32(block + GHOST_SOUNDS, value != 0 ? g.Read32(block + GHOST_SOUNDS) | 1u << seat : g.Read32(block + GHOST_SOUNDS) & ~(1u << seat));
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
    case ACTION_TAG:
      // the compiler's number of a text the package holds; anything else: no tag
      g.Write8(block + TAGS_AT + seat, value >= 1 && value <= TAG_TEXTS ? static_cast<u8>(value) : 0);
      return true;
    case ACTION_TAGOFF:
      g.Write8(block + TAGS_AT + seat, 0);
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
      if (value != PART_TIMER && value != PART_LEADER_GAP && value != PART_GHOST_GAP && value < PART_RANK)
        return true;
    }
    i += static_cast<size_t>(size);
  }
  return false;
}

// Whether the code pushes `value` right before the action `id` (a choice's name, ModeEmitter). Code the compiler did not
// write: yes, rather than miss it.
bool Pushes(const std::vector<u8>& code, u8 id, u32 value)
{
  for (size_t i = 0; i < code.size();)
  {
    const u8 op = code[i];
    const int size = OpSize(op);
    if (size <= 0)
      return true;
    if (op == OP_PUSH && i + 7 <= code.size() && code[i + 5] == OP_ACTION && code[i + 6] == id &&
        (static_cast<u32>(code[i + 1]) << 24 | code[i + 2] << 16 | code[i + 3] << 8 | code[i + 4]) == value)
      return true;
    i += static_cast<size_t>(size);
  }
  return false;
}

bool NeedsGhostTask(const std::vector<u8>& code)
{
  return Pushes(code, ACTION_SEATLOOK, LOOK_GHOST) || Pushes(code, ACTION_SEATOWNLOOK, LOOK_GHOST) || Pushes(code, ACTION_SHOW, PART_GHOST_GAP);
}
}  // namespace

bool NeedsStoryPictures(const std::vector<u8>& code)
{
  return Uses(code, ACTION_SHOW, true) || Uses(code, ACTION_DANGERZONE, false);
}

bool UsesClock(const Package& p)
{
  return Uses(p.code, ACTION_SHOWCLOCK, false);
}

u32 WindowFlags(const Package& p)
{
  // the points page holds the only place the game draws the title, so a title takes the results through it, award or none
  const bool points_page = Uses(p.code, ACTION_AWARD, false) || !p.results_title.empty() || !p.points_table.empty();
  return (NeedsStoryPictures(p.code) ? STORY_PICTURES : 0) | (points_page ? POINTS_PAGE : 0) |
         (p.results_points ? POINTS_ONLY : 0) | (p.rolling ? ROLLING : 0) | (p.start_together ? TOGETHER : 0) |
         (NeedsGhostTask(p.code) ? GHOST_TASK : 0);
}

void ShowFault(Guest& g, const Package& p, bool broken)
{
  // (the flag first: this runs every step, and WindowFlags walks the code)
  if (((g.Read32(layout::MODE_HUD + WINDOW_FLAGS) & FAULT_SHOWN) != 0) == broken)
    return;
  g.Write32(layout::MODE_HUD + WINDOW_FLAGS, WindowFlags(p) | (broken ? POINTS_PAGE | FAULT_SHOWN : 0));
  const std::string_view title = broken ? std::string_view("MODE ERROR") : std::string_view(p.results_title);
  for (u32 k = 0; k < WINDOW_TITLE_SIZE; ++k)
    g.Write8(layout::MODE_HUD + WINDOW_TITLE + k, k < title.size() ? static_cast<u8>(title[k]) : 0);
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
    f.tags[seat] = g.Read8(block + TAGS_AT + seat);
    f.ghost_tints[seat] = g.Read32(block + STATS + seat * STAT + STAT_GHOST_TINT);
    if (f.looks[seat] == LOOK_GHOST && seat != viewed && !(g.Read32(block + GHOST_SOUNDS) >> seat & 1))
      f.silent |= 1u << seat;
  }
  if (viewed >= SEATS)
    return f;
  const u32 r = Seat(block, viewed);
  const u32 parts = g.Read32(r + PARTS);
  for (u32 p = 0; p < PART_COUNT; ++p)
    if (parts >> p & 1)
      (p < PART_RANK ? f.parts : f.hidden) |= PART_BITS[p];
  f.faces_hidden = (parts >> PART_FACES & 1) != 0;
  f.ghost_gap = (parts >> PART_GHOST_GAP & 1) != 0;
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

void GapTable::Clear()
{
  *this = GapTable{};
}

void GapTable::Note(u32 seat, u32 bucket, u32 step)
{
  std::vector<u32>& at = m_at[seat];
  if (step < m_step[seat])  // gone back (a rollback past this frame, a rewind, the scrubber): what came later is gone
    for (u32& entry : at)
      if (entry > step + 1)
        entry = 0;
  m_step[seat] = step;
  if (bucket >= at.size())
    at.resize(bucket + BUCKETS_PER_LAP, 0);
  for (u32 b = std::min(m_last[seat] + 1, bucket); b <= bucket; ++b)
    if (at[b] == 0)
      at[b] = step + 1;
  m_last[seat] = bucket;
}

s32 GapTable::Frames(u32 you, u32 other, u32 now) const
{
  const u32 mine = m_last[you], theirs = m_last[other];
  const auto since = [now](const std::vector<u32>& at, u32 bucket) -> s32 {
    return bucket < at.size() && at[bucket] != 0 && at[bucket] <= now + 1 ? static_cast<s32>(now + 1 - at[bucket]) : 0;
  };
  return theirs > mine ? since(m_at[other], mine) : theirs < mine ? -since(m_at[you], theirs) : 0;
}

GapPicture GapOf(s32 frames, bool other_finished)
{
  const u32 ms = (frames < 0 ? 0u - static_cast<u32>(frames) : static_cast<u32>(frames)) * 1000 / 60;
  const u32 ahead = other_finished ? 0xBF404000 : 0xFF808000, level = other_finished ? 0xBFBFBF00 : 0xFFFFFF00,
            behind = other_finished ? 0x4040BF00 : 0x8080FF00;  // .rodata 0x6CC8 to 0x6CDC
  return {frames > 0 ? ahead : frames < 0 ? behind : level, frames > 0 ? 0x3Au : frames < 0 ? 0x3Bu : 0x20u, ms / 60000, ms % 60000 / 1000, ms % 1000};
}

namespace
{
// The gap, once a frame: every seat's bucket into the table, then the picture for the viewed racer against the seat
// furthest ahead, as Time Attack's largest ghost (0xED654 to 0xED66C). Seats that are out are no one to chase.
void DrawGap(Guest& g, u32 module, const Frame& f, u32 viewed)
{
  static GapTable table;
  static u32 s_block = 0;
  if (const u32 block = g.Read32(layout::MODES + 4); block != s_block)
  {
    s_block = block;
    table.Clear();
  }
  const u32 window = layout::MODE_HUD + WINDOW_GAP, machines = g.Read32(module + net::MACHINES);
  const u32 count = std::min<u32>(g.Read8(module + net::MACHINE_COUNT), SEATS);
  const u32 now = g.Read32(module + game::RACE_CLOCK);
  if (machines == 0 || now == 0)
  {
    g.Write32(window, 0);
    return;
  }
  for (u32 seat = 0; seat < count; ++seat)
  {
    const u32 p = g.Read32(machines + seat * game::MACHINE_SIZE + game::MACHINE_PROGRESS);
    if (p == 0)
      continue;
    const float distance = std::bit_cast<float>(g.Read32(p + game::PROGRESS_DISTANCE));
    const float lap = std::bit_cast<float>(g.Read32(p + game::PROGRESS_LAP_LENGTH));
    if (distance > 0 && lap > 0)
      table.Note(seat, static_cast<u32>(distance / lap * GapTable::BUCKETS_PER_LAP), now);
  }
  u32 best = SEATS;
  if (f.ghost_gap && viewed < count)
    for (u32 seat = 0; seat < count; ++seat)
    {
      const u32 m = machines + seat * game::MACHINE_SIZE;
      if (seat == viewed || (net::out_test::Out(g, module, m) && (g.Read32(m) & game::MACHINE_FLAG_FINISHED) == 0))
        continue;
      if (best == SEATS || table.Bucket(seat) > table.Bucket(best))
        best = seat;
    }
  const u32 you = viewed < count ? machines + viewed * game::MACHINE_SIZE : 0;
  if (best == SEATS || (g.Read32(you) & game::MACHINE_FLAG_FINISHED) != 0 || net::out_test::Out(g, module, you))
  {
    g.Write32(window, 0);
    return;
  }
  const GapPicture picture = GapOf(table.Frames(viewed, best, now), (g.Read32(machines + best * game::MACHINE_SIZE) & game::MACHINE_FLAG_FINISHED) != 0);
  g.Write32(window, picture.colour);
  g.Write32(window + 4, picture.sign);
  g.Write32(window + 8, picture.minutes);
  g.Write32(window + 12, picture.seconds);
  g.Write32(window + 16, picture.thousandths);
}
}  // namespace

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
    g.Write32(module + game::HUD_PARTS, (mask & ~STORY_BITS & ~f.hidden) | f.parts | gap_part);
  g.Write32(layout::MODE_HUD + WINDOW_FACES, f.faces_hidden ? 1 : 0);
  g.Write32(layout::MODE_HUD + WINDOW_RANK, f.hidden & PART_BITS[PART_RANK] ? 1 : 0);
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
  // a seat's colour tints its machine drawn normally; a ghost has seat.ghost_tint (0: Time Attack's yellow)
  for (u32 seat = 0; seat < SEATS; ++seat)
  {
    const bool coloured = f.looks[seat] == LOOK_NORMAL && f.colours[seat] != 0;
    g.Write8(window + WINDOW_LOOKS + seat, coloured ? COLOURED : f.looks[seat]);
    g.Write32(window + WINDOW_TINTS + 4 * seat, coloured ? f.colours[seat] : f.looks[seat] == LOOK_GHOST ? f.ghost_tints[seat] : 0);
    g.Write8(window + WINDOW_TAGS + seat, f.tags[seat]);
  }
  g.Write32(window + WINDOW_SILENT, f.silent);
  if (g.Read32(window + WINDOW_FLAGS) & GHOST_TASK)
    DrawGap(g, module, f, viewed);
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
