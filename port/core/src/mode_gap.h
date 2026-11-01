// A game mode's time gaps and the modern HUD's slots (protocol/modes.toml: seat.gap_ahead, gap_behind, gap_leader,
// show_gap, show_bar, show_figure, show_text), kept in two pages of the mode's block.
//
// The gaps are race state. At each run of a mode that uses them (the package's time_gaps) every seat's course distance
// (P+0xF8, which the game works out from its checkpoints: the race map's section 3) gives a mini-sector, 64 a lap, and the step
// a seat first reached each one goes in the page. It reads only fields the checksum covers and writes only the
// block, so every game holds the same page, the undo log puts it back with a rollback, and a rule may read a gap. A gap is
// worked out from two seats' rows and kept nowhere: the arrival of each at the last place both have passed,
// so it changes only as a seat reaches a new mini-sector. It needs no state on the drawing's side, and a rollback
// simply draws what the restored page says. A row is one lap long: a racer a lap or more away gives the laps and a
// time that is less than the whole gap.
//
// The slots are display words like the page in mode_hud.h: what each seat's player sees in each slot of the modern HUD, as the
// rules last said. Written at the mode point, read once a frame by modern_hud_frame.cpp. Nothing a step reads.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <algorithm>
#include <bit>

#include "fzgx/guest.h"
#include "mode.h"
#include "mode_words.g.h"

namespace fzgx::mode::hud
{
constexpr u32 SECTORS = 64;                     // a lap's mini-sectors
constexpr u32 GAPS_PAGE = 8 * PAGE;
constexpr u32 GAP_FURTHEST = GAPS_PAGE + 0x10;  // a word a seat: the furthest mini-sector over all laps, + 1 (0: none yet)
constexpr u32 GAP_ROWS = GAP_FURTHEST + 30 * 4;  // a seat's row: a u16 a mini-sector, the step it was reached (mod 65536)
constexpr u32 GapRow(u32 seat, u32 sector)
{
  return GAP_ROWS + 2 * (seat * SECTORS + sector % SECTORS);
}
constexpr u32 TREND_BACK = 8;   // the gap now against the gap this many mini-sectors ago says gaining or losing
constexpr s32 TREND_STEPS = 6;  // a change under this many steps (0.1 s) is holding

constexpr u32 SLOTS_PAGE = 7 * PAGE;
constexpr u32 SLOT_RECORD = 16;
// a record: +0 the kind, +1 the colour (a modes::COLOUR_*, or COLOUR_NONE), +2 the bar's kind, gap's target or text's number,
// +4 and +8 the two values (a bar's value and most, binary32; a figure's number; a text's values)
constexpr u32 SLOT_KIND = 0, SLOT_COLOUR = 1, SLOT_WHAT = 2, SLOT_A = 4, SLOT_B = 8;
constexpr u8 SLOT_NONE = 0, SLOT_BAR = 1, SLOT_FIGURE = 2, SLOT_TEXT = 3, SLOT_GAP = 4;
constexpr u8 COLOUR_NONE = 0xFF;
constexpr u32 SlotRecord(u32 seat, u32 slot)
{
  return SLOTS_PAGE + 0x10 + (seat * modes::SLOT_COUNT + slot) * SLOT_RECORD;
}
static_assert(BLOCK == 9 * PAGE && GapRow(29, SECTORS - 1) + 2 <= GAPS_PAGE + PAGE && SlotRecord(29, modes::SLOT_COUNT - 1) + SLOT_RECORD <= SLOTS_PAGE + PAGE);

bool IsSlotWord(u8 id);
// The word: `a`, the arguments after the seat, as the interpreter popped them (the choices' ranges checked here).
void ApplySlot(Guest& g, u32 block, u8 id, u32 seat, const std::array<u32, 5>& a);

// One run's arrivals, from the game's fields: every seat of `count` with a progress record.
void NoteGaps(Guest& g, u32 block, u32 module, u32 run);

struct TimeGap
{
  bool known = false;   // both have passed a place in common
  s32 steps = 0;        // above 0: `other` reached it first, by so many steps
  u32 laps = 0;         // a lap or more apart: how many; `steps` is then less than the whole gap
  s32 trend = 0;        // above 0 you are losing time on them, below 0 gaining, 0 holding or not known
};
// (A template for the interpreter's Mem and for the frame's Guest.)
template <class M>
TimeGap TimeGapOf(const M& g, u32 block, u32 you, u32 other)
{
  TimeGap gap;
  if (you >= 30 || other >= 30 || you == other)
    return gap;
  const u32 mine = g.Read32(block + GAP_FURTHEST + 4 * you), theirs = g.Read32(block + GAP_FURTHEST + 4 * other);
  if (mine == 0 || theirs == 0)
    return gap;
  const u32 at = std::min(mine, theirs) - 1;
  const auto between = [&](u32 sector) {
    const u16 a = g.Read16(block + GapRow(you, sector)), b = g.Read16(block + GapRow(other, sector));
    return mine <= theirs ? static_cast<s32>(static_cast<u16>(a - b)) : -static_cast<s32>(static_cast<u16>(b - a));
  };
  gap.known = true;
  gap.steps = between(at);
  gap.laps = (std::max(mine, theirs) - 1 - at) / SECTORS;
  // the place some way back, if both rows still hold it and both have been there (a stamp of 0: not reached)
  if (at >= TREND_BACK && std::max(mine, theirs) - 1 - (at - TREND_BACK) < SECTORS &&
      g.Read16(block + GapRow(you, at - TREND_BACK)) != 0 && g.Read16(block + GapRow(other, at - TREND_BACK)) != 0)
  {
    const s32 change = gap.steps - between(at - TREND_BACK);
    gap.trend = change >= TREND_STEPS ? 1 : change <= -TREND_STEPS ? -1 : 0;
  }
  return gap;
}
}  // namespace fzgx::mode::hud
