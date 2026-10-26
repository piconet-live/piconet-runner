// What a stat pack may hold: the car data's blocks and, for every word of a block a pack can change, the range the
// game's physics is safe in. The table (RULES, Rule, Kind) is the protocol's generated/stat_ranges.g.h, written from
// the one list in protocol/stats.toml, which also gives the launcher's and the server's (Online.Protocol's
// StatPackRules.All) and the page all are compared with (handbook/stat-packs.md: the test here is
// TestTheStatRangesAreTheHandbooks). The ranges come from every division in the physics code (main.rel 0x21CFC to
// 0x2C97C): the handbook says which rule is read and which is caution.
// These are the limits a game build is made with: a pack outside them needs a new build, and nothing after this
// reader holds a value back. So every row is safe on its own, for a machine's block (a part's: below):
//   - a value is the bits of 0, or a normal number: no denormal (the race runs with denormals read as zero, and a
//     divisor that is one is a division by nothing), no -0, no infinity, no NaN;
//   - a divisor has a floor well above nothing (the weight: position += momentum / weight; the boost strength; a
//     hover point's Z), a hundredth of the smallest the disc holds or less;
//   - the max speed (in a divisor that stays positive from 0 up) and the boost duration (60 times it is kept in a
//     byte) are not negative; any other stat may be, to minus its most (a negative value is a pack's to choose
//     while the game runs it), and none is more than about
//     ten times the largest the disc holds, so that a product of every stat of a machine stays 20 orders of
//     magnitude inside what a 32-bit number holds.
// The narrower ranges of caution ("what has been tried") refuse nothing: they are a warning where a pack is made
// (the import, a server's start). A launcher holds a pack that arrives to these rows only, as this reader does.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <optional>
#include <string>

#include "game_facts.h"
#include "stat_ranges.g.h"  // RULES, Rule, Kind (protocol/stats.toml)

namespace fzgx::stats
{
// The car data's blocks, as a pack numbers them: the machines by id, then the custom parts.
constexpr u32 BLOCK_SIZE = file::CAR_BLOCK;
constexpr u32 MACHINES = file::CAR_MACHINES;
constexpr u32 BLOCKS = file::CAR_MACHINES + file::CAR_PARTS;
constexpr u32 WORDS = BLOCK_SIZE / 4 - 1;               // the words of a block a pack may change: all but word 0
constexpr u32 MOST_ENTRIES = BLOCKS * WORDS;
// Where a block begins in the car data, the file's and the loaded table's alike.
constexpr u32 BlockAt(u32 block)
{
  return block < MACHINES ? block * BLOCK_SIZE : file::CAR_PARTS_AT + (block - MACHINES) * BLOCK_SIZE;
}

// A custom part's block (41 to 115: bodies, cockpits, boosters) is not a machine's stats: its numbers are what the
// part adds to a custom machine, whose block is the sum of its three parts' (fn_1_14F6F8), and a part may take away
// (a negative number) what another gives. So a part's number is held by its size alone: the bits of 0 or a normal
// number, no farther from 0 than a machine's most. No floor, no sign and no Z rule of its own: no race of this
// build has a custom machine (a match's and a replay's machines are 0 to 40: files_match.cpp, files_replay.cpp),
// the code reads a part's block only for one (fn_1_82EDC and fn_1_83070 copy a machine's block when the id is
// below 41, and call the sum otherwise), and a pack is laid for a match's race and a playback only. What must be
// sound for a custom machine is the sum, which is the check of the build that lets one race.

// The table's one word of bytes is where the facts table has it.
static_assert([] {
  for (const Rule& rule : RULES)
    if (rule.kind == Kind::Bytes)
      return rule.at == file::CAR_BYTES_AT;
  return false;
}());

// The rule of a word of a block, by its offset; null: no word a pack may change is there (word 0, the name's
// place, among them).
const Rule* RuleAt(u32 offset);
// Why a block's word may not hold these 32 bits, as the end of a sentence ("is 0, and must be at least 1");
// nothing: it may. `block` and `offset` are a block and a word a pack may change. A machine's block is held to its
// row; a part's to the row's size alone (above).
std::optional<std::string> Refusal(u32 block, u32 offset, u32 bits);
// A word's name as a pack file writes it: "Weight", "Hover2Z".
std::string WordName(u32 offset);
// A number as a line of the log and the handbook's table write it: six decimals at most, no zeros at the end
// ("100000", "0.45", "-0").
std::string Text(float value);
}  // namespace fzgx::stats
