// A game mode's time gaps (protocol/modes.toml: seat.gap_ahead, gap_behind, gap_leader), kept in a page of the mode's
// block, its last (page 8; page 7 before it held the modern HUD's slots until that HUD was retired, 2026-11-04: its
// head is unused, its tail holds the story values, mode_hud.h STORY, and the block keeps its size).
//
// The gaps are race state. At each run of a mode that uses them (the package's time_gaps) every seat's course distance
// (P+0xF8, which the game works out from its checkpoints: the race map's section 3) gives a mini-sector, 64 a lap, and the step
// a seat first reached each one goes in the page. It reads only fields the checksum covers and writes only the
// block, so every game holds the same page, the undo log puts it back with a rollback, and a rule may read a gap. A gap is
// worked out from two seats' rows and kept nowhere: the arrival of each at the last place both have passed,
// so it changes only as a seat reaches a new mini-sector. It needs no state on the drawing's side, and a rollback
// simply draws what the restored page says. A row is one lap long: a racer a lap or more away gives the laps and a
// time that is less than the whole gap.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <algorithm>
#include <bit>
#include <cstdint>

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

static_assert(BLOCK == 9 * PAGE && GapRow(29, SECTORS - 1) + 2 <= GAPS_PAGE + PAGE);

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
    return static_cast<s32>(static_cast<std::int16_t>(static_cast<u16>(a - b)));  // (signed: on one mini-sector either may have come first; 9 minutes either way)
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
