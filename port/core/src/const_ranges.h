// What a stat pack may hold beside the car data: the allowed words of the game's code constants (handbook/
// stat-packs.md, "Constants"). A pack names one by its slot in
// this list, in the constants' block (file::CONST_BLOCK): an entry (CONST_BLOCK, slot, the 32 bits). The list
// (CONSTANTS, Constant, Kind) is the protocol's generated/const_ranges.g.h, written from the one list in
// protocol/stats.toml, which also gives the launcher's and the server's (Online.Protocol's StatPackConstants.All) and
// the handbook's table, as the stat ranges are; below, its places are held to the facts table's.
//
// Every word is named here with every reader the game's code has (found in main.rel's code: the loads whose address
// is the word, through any pool register), so that a pack changes what it says and nothing else. A word another
// routine also reads is not on the list unless every reader is meant to change; the dash plate's 60.0 and 0.5 are
// read a dozen times in fn_1_26F34 alone, so its two numbers are words of their own, read by the one patched site
// (port/image/sites_dash.py).
//
// Where a value lies: in its seat's constants (the seats' block, table::SEAT_BLOCK: a record a seat), which the core
// writes for every race and replay (const_pack.cpp): the seat's pack's bits, or the game's own. Every reader is a
// patched site of the native module that reads the machine's seat's record (sites_seats.py, sites_dash.py): the
// game's own words in main.rel are never written, and a plain start reads them as ever. So each seat can race its own
// pack's constants; one pack for all is the same value in every record.
// Like the stat ranges, the list is frozen into a game build: a word not on it, or a value outside its range,
// needs the next build. Every value is the bits of 0 or a normal number (no denormal, -0, infinity or NaN).
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>

#include "const_ranges.g.h"  // CONSTANTS, Constant, Kind (protocol/stats.toml)
#include "game_facts.h"

namespace fzgx::consts
{
// the car data's blocks (stat_ranges.h): a constant's block comes after every one of them, so a pack's constants
// travel after its car data
constexpr u32 BLOCKS_BEFORE = file::CAR_MACHINES + file::CAR_PARTS;

// The slots' readers (main.rel .text offsets); every one runs in a machine's own step, the machine in r30, and reads
// its seat's record through a patched site, so each is per seat:
//   0 BoostEnergyUse: fn_1_26F34 (machine_boost_update) 0x27420 (the game's .rodata 0xAA0, its only reader there;
//     site seat_energy): while the boost's countdown (+0x18B) runs, energy (+0x184) -= this x the usage factor
//     (+0x4DC) every frame. The game's 1/6; GX Unleashed 0.15. 0 is a boost that costs nothing; the most is about ten
//     times the game's.
//   1, 2 SpeedCap: fn_1_294D0 0x29570 (the game's .rodata 0xB20, its only reader; site seat_speed_cap, lfd): above
//     this speed (216 x |momentum| / weight, km/h) the momentum is set to length 46.25 and the rest of the routine is
//     skipped (a near stop). The game's 9990; GX Unleashed the largest double, where the branch never runs: "no cap".
//   3 DashPlateBase, 4 DashPlateScale: a dash plate's boost lasts int(Base + Scale x the machine's boost duration)
//     frames (sites_dash.py: fn_1_26F34 0x271C4, its only reader). The game's 0 and 30; GX Unleashed 30 and 15. The
//     most of both keep it within the byte it is kept in (+0x18A): 60 + 45 x 4.26 (the boost duration's bound) <
//     256.
//   5 BoostLengthBase, 6 BoostLengthScale: an energy boost lasts int(Base + Scale x the machine's boost duration)
//     frames (sites_dash.py: fn_1_26F34 0x272D0, where the game multiplies 60.0, .rodata 0xA98, a word read eight
//     times; the site reads no word of the pool). The game's 0 and 60. The count is kept in two bytes (+0x18A,
//     +0x18B): a pack whose Base + Scale x its longest boost duration (a machine's, or a custom machine's sum)
//     passes 255 is refused (BoostLengthRefusal).
//   7 to 61: the machine setting's curve, fn_1_22B10 (the block a machine's step builds from the car data, and s =
//     the setting x 0.01; d = s - SettingMiddle): every number it reads but the six 0.0s it tests d against, one
//     slot a role (port/image/sites_curve.py: a site at each load or copy of a pool number of .rodata 0x840, read
//     from the seat's record while SEAT_SETTING names it, which the two calls in fn_1_331EC 0x336B8 and 0x336F4 do;
//     the pool's words are never written, so every other reader keeps them: 1.0 (.rodata 0x854) has 59 others, 0.5
//     30, 2.0 14, 0.1 16, 1.8 (0x8BC, DriftFastLoss) 2 in fn_1_290E8, and 1.4, 2.3, 3.2, -0.25, 0.16, 0.08, 0.45,
//     0.31, 0.03 and 700 none). Each slot's formula: handbook/stat-pack-constants.md. AccelGain is the pool's
//     double (lfd 0x22D14). F-Zero GX+ makes DriftFastLoss 1.0.
// No constant on the list is global: none is read outside a machine's step (the setting's curve runs in it, and
// with no seat named, in the menu's preview and the Grand Prix's check, it is the game's own).
// The generated list's places in a seat's record are the facts table's (game.tsv SEAT_ rows, which the image's
// sites read too: sites_seats.py, sites_dash.py, sites_curve.py).
constexpr const Constant& Named(std::string_view name, Kind kind)
{
  for (const Constant& c : CONSTANTS)
    if (name == c.name && c.kind == kind)
      return c;
  throw "no such constant";  // never a constant expression: the assertion that asks fails to compile
}
static_assert(Named("BoostEnergyUse", Kind::Number32).at == table::SEAT_ENERGY_USE);
static_assert(Named("SpeedCap", Kind::Number64High).at == table::SEAT_SPEED_CAP &&
              Named("SpeedCap", Kind::Number64Low).at == table::SEAT_SPEED_CAP + 4);
static_assert(Named("DashPlateBase", Kind::Number32).at == table::SEAT_DASH_BASE &&
              Named("DashPlateScale", Kind::Number32).at == table::SEAT_DASH_SCALE);
static_assert(Named("BoostLengthBase", Kind::Number32).at == table::SEAT_BOOST_BASE &&
              Named("BoostLengthScale", Kind::Number32).at == table::SEAT_BOOST_SCALE);
static_assert(Named("AccelGain", Kind::Number64High).at == table::SEAT_CURVE_DOUBLE &&
              Named("AccelGain", Kind::Number64Low).at == table::SEAT_CURVE_DOUBLE + 4);
// the setting's curve: from SettingMiddle on, every 32-bit slot is the next word from SEAT_CURVE, in slot order
static_assert([] {
  u32 at = table::SEAT_CURVE;
  for (const Constant* c = &Named("SettingMiddle", Kind::Number32); c != CONSTANTS.data() + CONSTANTS.size(); ++c)
  {
    if (c->kind != Kind::Number32)
      continue;
    if (c->at != at)
      return false;
    at += 4;
  }
  return true;
}());
constexpr u32 SLOTS = static_cast<u32>(CONSTANTS.size());
static_assert(file::CONST_BLOCK >= BLOCKS_BEFORE);
// The words' places as the facts table names them, held to main.rel's sections (a module's sections lie at their
// file offsets from its header): the import reads a modified module's file by the MODULE_ rows
// (net/src/Launcher.Core/Files/StatPackImport.Module.cs).
static_assert(game::BOOST_ENERGY_USE == game::REL_RODATA + 0xAA0 && game::SPEED_CAP == game::REL_RODATA + 0xB20);
static_assert(file::MODULE_BOOST_ENERGY_USE == game::BOOST_ENERGY_USE && file::MODULE_SPEED_CAP == game::SPEED_CAP);
static_assert(file::MODULE_DASH_PLATE == game::REL_TEXT + 0x271A8 && file::MODULE_DASH_POOL == game::REL_RODATA + 0x840);
static_assert(file::MODULE_SETTING_CURVE == game::REL_TEXT + 0x22B10 && file::MODULE_BOOST_LENGTH == game::REL_TEXT + 0x272C4);

// Why a slot may not hold these bits, as the end of a sentence ("is 2, and must be at most 1.7"); nothing: it may.
// A double's high word is judged with its low word (`low_bits`), and a low word alone is never judged.
std::optional<std::string> Refusal(u32 slot, u32 bits, u32 low_bits = 0);
// A value as a log line writes it: the shortest text that reads back to the same bits.
std::string ValueText(u32 slot, u32 bits, u32 low_bits = 0);
}  // namespace fzgx::consts
