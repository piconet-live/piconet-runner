// A race's stat pack in the game: its values laid over the car data the game has loaded, and checked when the race
// is let go (handbook/stat-packs.md). The pack comes checked from the match's reader (files_stats.cpp).
//
// Where. The game's loader (main.rel fn_1_95C, once, before the first frame) reads game/cardata.lz into
// CAR_DATA_SIZE bytes below the top of the arena, turns every word but the name's and the four bytes at
// CAR_BYTES_AT high byte first, puts each block's name address into its word 0, and maps the table at
// CAR_DATA_MAPPED. Everything reads it there, and the simulation reads it in every step: a racer's block is filled
// from its machine's for every racer (fn_1_82EDC through the rule hook fn_1_331EC), so a changed word acts from the
// next step on. The table is laid over where it stands in main memory, CAR_DATA.
// When. Every frame from the boot on until the race runs, only the words that differ, never word 0. The table lies
// inside the undo log's range (below STATE_END), and the undo log starts at the race's first rollback frame, after
// the go: what is written before is the memory a rollback goes back to. So nothing is written once the race runs;
// from then on the table is only compared, and a difference only said.
// A game build is made with this file, and the servers that send packs come after it: nothing here stops a race
// for a reason that is not certain. The go is refused only on a table that does not hold the pack; what the racers
// hold then is a line of the log.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "files.h"

#include <algorithm>
#include <bit>
#include <format>

#include "stat_ranges.h"

namespace fzgx::boot
{
namespace
{
constexpr u32 TABLE = game::CAR_DATA;

bool s_broken = false;  // the table lost a value under a running race: said once
u32 s_lines = 0;

u32 WordAt(const StatEntry& e)
{
  return TABLE + stats::BlockAt(e.block) + e.at;
}

u32 Differing(const Guest& g, const StatPack& pack)
{
  return static_cast<u32>(std::count_if(pack.entries.begin(), pack.entries.end(),
                                        [&](const StatEntry& e) { return g.Read32(WordAt(e)) != e.value; }));
}
}  // namespace

// The car data stands at TABLE as the loader leaves it: every block's word 0 is its name's address in the mapped
// table, the machines' in the names' part with the last machine's first, every part's the parts' names. Memory
// that holds anything else there (the game has not loaded the file yet; another build of the game) is not written.
bool TableStands(const Guest& g)
{
  if (!g.Ram(TABLE, game::CAR_DATA_SIZE))
    return false;
  u32 before = game::CAR_DATA_MAPPED + file::CAR_PARTS_AT;
  for (u32 machine = 0; machine < stats::MACHINES; ++machine)
  {
    const u32 name = g.Read32(TABLE + stats::BlockAt(machine));
    if (name >= before || name < game::CAR_DATA_MAPPED + file::CAR_NAMES_AT)
      return false;
    before = name;
  }
  if (before != game::CAR_DATA_MAPPED + file::CAR_NAMES_AT)
    return false;
  for (u32 block = stats::MACHINES; block < stats::BLOCKS; ++block)
  {
    if (g.Read32(TABLE + stats::BlockAt(block)) != game::CAR_DATA_MAPPED + file::CAR_PART_NAMES_AT)
      return false;
  }
  return true;
}

void ResetStats()
{
  s_broken = false;
  s_lines = 0;
  ResetSeats();
}

void LayStats(Guest& g, const StatPack& pack)
{
  if (pack.entries.empty() || !TableStands(g))
    return;
  const u32 differing = Differing(g, pack);
  if (g.Read32(table::RUNNING_AT) != 0)
  {
    // The race runs: a write now would be undone by the next rollback in one game and not in another. So it is
    // only said, once: the go's check has let the race start on the pack, the racers' blocks are in the state
    // checksum, and a game whose machines then differ from the others' is a desync, found as any other.
    if (differing != 0 && !s_broken)
    {
      s_broken = true;
      Error("fzgx: stat pack {}: {} of its values are not in the game's car data while the race runs (nothing is "
            "written now: a rollback would take it back)",
            pack.name, differing);
    }
    return;
  }
  if (differing == 0)
    return;
  for (const StatEntry& e : pack.entries)
  {
    if (g.Read32(WordAt(e)) != e.value)
      g.Write32(WordAt(e), e.value);
  }
  // a line for the first writes only (the game writes the table once, at its start)
  constexpr u32 LINES = 4;
  if (++s_lines <= LINES)
    Notice("fzgx: stat pack {} ({}): {} of its {} values written over the game's car data{}", pack.name,
           pack.fingerprint.substr(0, 8), differing, pack.entries.size(), s_lines == LINES ? " (no further lines)" : "");
}

// A custom seat races the sum of its three parts' blocks (fn_1_14F6F8), made from the table as the pack left it, in
// every game alike. A pack's part words are held to their size alone (stat_ranges.h), so the sum is held here to a
// machine's rows: the words the game adds (weight to drag but the grips and the boost's weight factor), the body
// value (body x cockpit), and the words it takes from one part as they are (the body's grips and corners, the
// cockpit's camera). The boost's factor of the summed weight (at most about 1.5) is left to the row's margin.
std::optional<std::string> CustomSumRefusal(const Guest& g, const Match& m)
{
  for (u32 seat = 0; seat < MAX_SEATS; ++seat)
  {
    if (!IsCustom(m.race, seat))
      continue;
    const u8* e = m.race.data() + RACE_CUSTOM + seat * CUSTOM_ENTRY;
    const auto word = [&](u32 part, u32 at) {
      return std::bit_cast<float>(g.Read32(TABLE + stats::BlockAt(stats::MACHINES + part) + at));
    };
    for (u32 at = 0x04; at <= 0x44; at += 4)
    {
      if (at == 0x10 || at == 0x14)
        continue;  // the grips are the body's alone: a part's own row
      const float v = at == 0x44 ? word(e[4], at) * word(e[12], at) : word(e[4], at) + word(e[12], at) + word(e[20], at);
      if (std::optional<std::string> why = stats::Refusal(0, at, std::bit_cast<u32>(v)))
        return std::format("seat {}'s custom machine ({}, {}, {}) sums its {} to a value the game is not safe with: "
                           "{}", seat, e[4], e[12], e[20], stats::WordName(at), *why);
    }
    // The words the machine takes from one part as they are (the
    // grips and the hover and wall corners the body's, the camera's two the cockpit's): a part's row holds them by
    // their size alone, so each is held here to a machine's row, a hover corner's Z off 0 among them (a divisor).
    for (u32 at = 0x10; at < stats::BLOCK_SIZE; at += 4)
    {
      const bool body = at == 0x10 || at == 0x14 || at >= 0x54, cockpit = at == 0x4C || at == 0x50;
      if (!body && !cockpit)
        continue;
      const u8 part = body ? e[4] : e[12];
      if (std::optional<std::string> why = stats::Refusal(0, at, g.Read32(TABLE + stats::BlockAt(stats::MACHINES + part) + at)))
        return std::format("seat {}'s custom machine ({}, {}, {}) takes its {} from its {} ({}), a value the game is "
                           "not safe with: {}", seat, e[4], e[12], e[20], stats::WordName(at), body ? "body" : "cockpit",
                           part, *why);
    }
  }
  return std::nullopt;
}

std::optional<std::string> StatsRefusal(const Guest& g)
{
  const Match* m = Get();
  if (!m)
    return std::nullopt;
  if (std::optional<std::string> seats = SeatsRefusal(g, *m))
    return seats;
  if (!m->stats)
    return std::nullopt;
  const StatPack& pack = *m->stats;
  if (pack.entries.empty())
    return std::nullopt;  // the car data is the disc's: nothing of it to hold
  if (!TableStands(g))
    return std::format("the game's car data is not at {:08X} as its loader leaves it, so the pack {} was not laid",
                       TABLE, pack.name);
  if (const u32 differing = Differing(g, pack); differing != 0)
    return std::format("{} of the pack {}'s {} values are not in the game's car data", differing, pack.name,
                       pack.entries.size());
  if (std::optional<std::string> sum = CustomSumRefusal(g, *m))
    return sum;
  // The table holds the pack: that is the whole rule, and every racer takes its values from the table again in
  // every step. What the racers hold at this moment is only said, for whoever reads a run's log: the weight is the
  // one value of a racer known to be the table's own bits (in races at CPU levels 3 and 1, setting 100; whether
  // the game alters it at another level or setting is not known, so nothing is refused on it). A weight of 0 is a
  // racer not filled yet.
  const u32 module = MainRel(g);
  const u32 machines = module != 0 ? g.Read32(module + game::MACHINES) : 0;
  const u32 racers = module != 0 ? std::min<u32>(g.Read8(module + game::MACHINE_COUNT), MAX_SEATS) : 0;
  u32 hold = 0, other = 0;
  std::string first;
  for (u32 racer = 0; racer < racers && g.Ram(machines, racers * game::MACHINE_SIZE); ++racer)
  {
    const u32 block = machines + racer * game::MACHINE_SIZE;
    const u32 machine = g.Read16(block + game::MACHINE_ID);
    const u32 held = g.Read32(block + game::MACHINE_WEIGHT);
    if (machine >= stats::MACHINES || held == 0)
      continue;  // a custom machine (its values are worked out of three parts'), or a racer not filled yet
    const u32 tables = g.Read32(TABLE + stats::BlockAt(machine) + 4);
    ++(held == tables ? hold : other);
    if (held != tables && first.empty())
      first = std::format(" (the first: racer {} on machine {} holds {:08X}, the table {:08X})", racer, machine, held, tables);
  }
  Notice("fzgx: stat pack {} ({}): its {} values are in the game's car data; of {} racers {} hold their machines' "
         "weights from it and {} another{}",
         pack.name, pack.fingerprint.substr(0, 8), pack.entries.size(), racers, hold, other, first);
  return std::nullopt;
}
}  // namespace fzgx::boot
