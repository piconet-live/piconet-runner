// Tests of a stat pack's code constants (const_ranges.h, const_pack.cpp, files_stats.cpp): the reader takes and
// refuses them, the race block's two words are the pack's or the game's own, the main.rel words are laid over the
// module before the race runs and only over the game's own bits, and the go refuses a race whose constants are not
// in the game. The car data's side: test_stats.cpp, test_stats_race.cpp.
// SPDX-License-Identifier: GPL-3.0-or-later

#include <bit>
#include <cstdlib>
#include <string_view>

#include "parts.h"
#include "const_ranges.h"
#include "stat_packs.h"

using namespace stat_packs;

namespace
{
constexpr u8 CONSTANT = file::CONST_BLOCK;
constexpr u8 ENERGY = 0, CAP_HIGH = 1, CAP_LOW = 2, BASE = 3, SCALE = 4, BOOST_BASE = 5, BOOST_SCALE = 6;
constexpr u8 DRIFT_FAST_LOSS = 11, ACCEL_GAIN_HIGH = 40, ACCEL_GAIN_LOW = 41;   // the curve's (const_ranges.h)
static_assert(std::string_view(consts::CONSTANTS[DRIFT_FAST_LOSS].name) == "DriftFastLoss" &&
              std::string_view(consts::CONSTANTS[ACCEL_GAIN_HIGH].name) == "AccelGain" &&
              consts::CONSTANTS[ACCEL_GAIN_LOW].kind == consts::Kind::Number64Low);

// GX Unleashed 5.0.4's three changes, behind the made-up pack's first value.
Entries Unleashed()
{
  return {{6, 0x04, Bits(2520.0f)},       {CONSTANT, ENERGY, 0x3E19999A}, {CONSTANT, CAP_HIGH, 0x7FEFFFFF},
          {CONSTANT, CAP_LOW, 0xFFFFFFFF}, {CONSTANT, BASE, Bits(30.0f)},  {CONSTANT, SCALE, Bits(15.0f)}};
}

// as they travel, and their fingerprint: net/tests/Protocol.Tests/StatPackConstantsTests.cs has the same two texts
constexpr const char* UNLEASHED_VALUES = "BgRFHYAA8AA+GZma8AF/7///8AL/////8ANB8AAA8ARBcAAA";
constexpr const char* UNLEASHED_FINGERPRINT = "f7929faed0937749e3cc81dcd35e2b500bb907a2";

// A seat's constants record as the match made it (Match::seat_block), and as the game's memory holds it.
u32 SeatWord(const boot::Match& m, u32 seat, u32 at)
{
  return paint::Be32(m.seat_block.data() + table::SEAT_CONSTANTS + seat * table::SEAT_CONSTANT + at);
}

u32 HeldSeatWord(const Guest& g, u32 seat, u32 at)
{
  return g.Read32(table::SEAT_CONSTANTS_AT + seat * table::SEAT_CONSTANT + at);
}

// A race of two seats (machine 6 and 12) whose set-up begins with `setup` and whose seats end with `seat0`, `seat1`
// (JSON members, each with a comma before it, or nothing).
Options ASeatsRace(const std::string& dir, const std::string& setup, const std::string& seat0, const std::string& seat1)
{
  WriteABundle(dir);
  WriteText(dir + "/launch.json", R"({"about": "a test race", "bundle": ")" + dir + R"(", "match": {
    "match": {"matchId": "m7", "seat": 0, "token": "1f2e3d4c5b6a7988", "relay": {"host": "", "port": 47811},
              "setup": {)" + setup + R"("course": 8, "laps": 3,
                        "seats": [{"seat": 0, "machine": 6, "playerId": "p1", "name": "A")" + seat0 + R"(},
                                  {"seat": 1, "machine": 12, "playerId": "p2", "name": "B")" + seat1 + R"(}]}},
    "relayHost": "127.0.0.1"}})");
  Options options;
  CHECK(!ReadLaunchFile(dir + "/launch.json", &options));
  return options;
}

// "packs": [the packs' objects], with a comma after it.
std::string Packs(const std::vector<std::string>& members)
{
  std::string text = "\"packs\": [";
  for (size_t i = 0; i < members.size(); ++i)
  {
    std::string member = members[i];
    member = member.substr(member.find('{'));
    member = member.substr(0, member.rfind('}') + 1);
    text += (i ? ", " : "") + member;
  }
  return text + "], ";
}

std::string Refused2(TestHost& host, const Options& options)
{
  host.log.clear();
  Init(host, options);
  const std::string said = boot::Get() ? "" : host.log;
  Shutdown();
  return said;
}

std::string Refused(TestHost& host, const std::string& dir, const Entries& entries)
{
  const boot::StatPack* pack = PackOf(host, ARace(dir, Stats(entries)));
  const std::string said = pack ? "" : host.log;
  Shutdown();
  return said;
}

void TheReader(TestHost& host, const std::string& dir)
{
  CHECK(Base64(Bytes(Unleashed())) == UNLEASHED_VALUES && boot::StatFingerprint(Bytes(Unleashed())) == UNLEASHED_FINGERPRINT);
  const boot::StatPack* pack = PackOf(host, ARace(dir, Stats(Unleashed(), "GX Unleashed")));
  CHECK(Active() && host.log.empty() && pack != nullptr);
  if (pack)
  {
    // apart, and the fingerprint is of both as they travel
    CHECK(pack->entries.size() == 1 && pack->constants.size() == 5 && pack->constants[4].value == Bits(15.0f));
    CHECK(pack->fingerprint == boot::StatFingerprint(Bytes(Unleashed())));
    const boot::Match& m = *boot::Get();
    for (const u32 seat : {0u, 1u, 29u})
    {
      CHECK(SeatWord(m, seat, table::SEAT_DASH_BASE) == Bits(30.0f) && SeatWord(m, seat, table::SEAT_DASH_SCALE) == Bits(15.0f));
      CHECK(SeatWord(m, seat, table::SEAT_ENERGY_USE) == 0x3E19999A && SeatWord(m, seat, table::SEAT_SPEED_CAP) == 0x7FEFFFFF &&
            SeatWord(m, seat, table::SEAT_SPEED_CAP + 4) == 0xFFFFFFFF);
    }
  }
  Shutdown();
  // the game's own A and B in a race without a pack, and in one whose pack names neither
  const std::string energy = Stats({{CONSTANT, ENERGY, Bits(0.15f)}});
  for (const std::string& stats : {std::string(), Stats(ThePack()), energy})
  {
    PackOf(host, ARace(dir, stats));
    CHECK(Active() && boot::Get() != nullptr);
    if (const boot::Match* m = boot::Get())
      CHECK(SeatWord(*m, 3, table::SEAT_DASH_BASE) == 0 && SeatWord(*m, 3, table::SEAT_DASH_SCALE) == Bits(30.0f) &&
            SeatWord(*m, 3, table::SEAT_ENERGY_USE) == (stats == energy ? Bits(0.15f) : 0x3E2AAAAB));
    Shutdown();
  }
  // a pack of constants alone, and the stock values themselves, are packs
  CHECK(Refused(host, dir, {{CONSTANT, BASE, 0}}).empty());
  CHECK(Refused(host, dir, {{CONSTANT, CAP_HIGH, 0x40C38300}, {CONSTANT, CAP_LOW, 0}}).empty());
  CHECK(Refused(host, dir, {{CONSTANT, ENERGY, 0}, {CONSTANT, BASE, Bits(60.0f)}, {CONSTANT, SCALE, Bits(45.0f)}}).empty());
}

void WhatTheReaderRefuses(TestHost& host, const std::string& dir)
{
  CHECK(Says(Refused(host, dir, {{CONSTANT, 62, 0}}), "names the constant slot 62: the game has 62"));
  // the curve's and the boost length's ranges
  CHECK(Says(Refused(host, dir, {{CONSTANT, BOOST_SCALE, Bits(61.0f)}}), "BoostLengthScale is 61, and must be at most 60"));
  CHECK(Says(Refused(host, dir, {{CONSTANT, DRIFT_FAST_LOSS, Bits(-10.5f)}}), "DriftFastLoss is -10.5, and must be at least -10"));
  CHECK(Says(Refused(host, dir, {{CONSTANT, ACCEL_GAIN_HIGH, 0x3FE33333}}), "the constant AccelGain is a 64-bit number"));
  CHECK(Says(Refused(host, dir, {{CONSTANT, SCALE, 0}, {CONSTANT, BASE, 0}}), "is there twice or out of order"));
  CHECK(Says(Refused(host, dir, {{CONSTANT, BASE, 0}, {6, 4, Bits(2520.0f)}}), "is there twice or out of order"));
  // a double's two words, both or neither
  CHECK(Says(Refused(host, dir, {{CONSTANT, CAP_HIGH, 0x7FEFFFFF}}), "the constant SpeedCap is a 64-bit number"));
  CHECK(Says(Refused(host, dir, {{CONSTANT, CAP_LOW, 0}}), "the constant SpeedCap is a 64-bit number"));
  CHECK(Says(Refused(host, dir, {{CONSTANT, CAP_HIGH, 0x7FEFFFFF}, {CONSTANT, BASE, 0}}), "is a 64-bit number"));
  // the ranges, and the bits before them
  CHECK(Says(Refused(host, dir, {{CONSTANT, ENERGY, Bits(1.75f)}}), "the constant BoostEnergyUse is 1.75, and must be at most 1.7"));
  CHECK(Says(Refused(host, dir, {{CONSTANT, BASE, Bits(-1.0f)}}), "DashPlateBase is -1, and must be at least 0"));
  CHECK(Says(Refused(host, dir, {{CONSTANT, SCALE, Bits(45.5f)}}), "DashPlateScale is 45.5, and must be at most 45"));
  for (const u32 bits : {0x80000000u, 0x00000001u, 0x7F800000u, 0x7FC00000u})
    CHECK(Says(Refused(host, dir, {{CONSTANT, BASE, bits}}), "which is no number a pack may hold"));
  CHECK(Says(Refused(host, dir, {{CONSTANT, CAP_HIGH, 0x408F3FFF}, {CONSTANT, CAP_LOW, 0xFFFFFFFF}}),
             "SpeedCap is 999.9999999999999, and must be at least 1000"));
  for (const auto& [high, low] : {std::pair<u32, u32>{0x7FF00000, 0}, {0x7FF80000, 0}, {0x80000000, 0}, {0, 1}})
    CHECK(Says(Refused(host, dir, {{CONSTANT, CAP_HIGH, high}, {CONSTANT, CAP_LOW, low}}), "which is no number a pack may hold"));
}

void Start(TestHost& host, const Options& options, Guest& g, int ticks)
{
  host.log.clear();
  host.notices.clear();
  Init(host, options);
  LinkMainRel(g);
  LoadCarData(g);
  for (int i = 0; i < ticks; ++i)
    FrameTick();
}

void InTheGame(TestHost& host, const std::string& dir)
{
  Guest g(host, Data());
  // every seat's record holds the race's pack's constants; main.rel's own words are never written
  const u32 energy = g.Read32(game::BOOST_ENERGY_USE_AT), cap = g.Read32(game::SPEED_CAP_AT);
  Start(host, ARace(dir, Stats(Unleashed(), "GX Unleashed")), g, 1);
  CHECK(host.log.empty() && g.Read32(game::BOOST_ENERGY_USE_AT) == energy && g.Read32(game::SPEED_CAP_AT) == cap);
  for (const u32 seat : {0u, 1u, 29u})
  {
    CHECK(HeldSeatWord(g, seat, table::SEAT_ENERGY_USE) == 0x3E19999A && HeldSeatWord(g, seat, table::SEAT_SPEED_CAP) == 0x7FEFFFFF);
    CHECK(HeldSeatWord(g, seat, table::SEAT_DASH_BASE) == Bits(30.0f) && HeldSeatWord(g, seat, table::SEAT_DASH_SCALE) == Bits(15.0f));
    CHECK(g.Read8(table::SEAT_BLOCK + seat) == 0);   // one pack for all: no row of a seat's own
  }
  CHECK(!boot::StatsRefusal(g) && !Says(host.notices, "stat packs a seat"));
  // the go refuses a race whose seats' constants are not the match's
  g.Write32(table::SEAT_CONSTANTS_AT + 5 * table::SEAT_CONSTANT + table::SEAT_ENERGY_USE, Bits(0.5f));
  CHECK(Says(boot::StatsRefusal(g).value_or(""), "the seats' constants are not the ones the race's packs give"));
  Shutdown();
  // no pack: the game's own numbers in every record
  Start(host, ARace(dir, ""), g, 1);
  CHECK(HeldSeatWord(g, 7, table::SEAT_ENERGY_USE) == 0x3E2AAAAB && HeldSeatWord(g, 7, table::SEAT_SPEED_CAP) == 0x40C38300 &&
        HeldSeatWord(g, 7, table::SEAT_DASH_BASE) == 0 && HeldSeatWord(g, 7, table::SEAT_DASH_SCALE) == Bits(30.0f));
  // and the boost length's and the curve's: every slot the game's bits (the curve's double too)
  for (u32 slot = 0; slot < consts::SLOTS; ++slot)
    CHECK(HeldSeatWord(g, 7, consts::CONSTANTS[slot].at) == consts::CONSTANTS[slot].stock);
  CHECK(HeldSeatWord(g, 7, table::SEAT_CURVE_DOUBLE) == 0x3FE33333 && HeldSeatWord(g, 7, table::SEAT_CURVE_DOUBLE + 4) == 0x40000000 &&
        HeldSeatWord(g, 29, table::SEAT_CURVE + 16) == Bits(1.8f) && HeldSeatWord(g, 7, table::SEAT_BOOST_SCALE) == Bits(60.0f));
  CHECK(g.Read32(table::SEAT_BLOCK + table::SEAT_SETTING) == 0);
  CHECK(!boot::StatsRefusal(g));
  Shutdown();
  // a pack of constants alone: the car data untouched
  Start(host, ARace(dir, Stats({{CONSTANT, ENERGY, Bits(0.15f)}})), g, 1);
  CHECK(HoldsOnly(g, {}) && HeldSeatWord(g, 0, table::SEAT_ENERGY_USE) == Bits(0.15f) && !boot::StatsRefusal(g));
  Shutdown();
  // F-Zero GX+'s drift loss: its slot in every record, the rest the game's
  Start(host, ARace(dir, Stats({{CONSTANT, DRIFT_FAST_LOSS, Bits(1.0f)}})), g, 1);
  CHECK(HeldSeatWord(g, 0, table::SEAT_CURVE + 16) == Bits(1.0f) && HeldSeatWord(g, 29, table::SEAT_CURVE + 16) == Bits(1.0f) &&
        !boot::StatsRefusal(g));
  // every slot the pack leaves out reads the game's own value, in every seat
  bool others = true;
  for (const u32 seat : {0u, 13u, 29u})
  {
    for (u32 slot = 0; slot < consts::SLOTS; ++slot)
    {
      if (slot != DRIFT_FAST_LOSS)
        others = others && HeldSeatWord(g, seat, consts::CONSTANTS[slot].at) == consts::CONSTANTS[slot].stock;
    }
  }
  CHECK(others);
  Shutdown();
  // a boost length within the byte for the test car data's longest boost (8.91 s, a custom machine's), and one past it
  Start(host, ARace(dir, Stats({{CONSTANT, BOOST_SCALE, Bits(25.0f)}})), g, 1);
  CHECK(HeldSeatWord(g, 3, table::SEAT_BOOST_BASE) == 0 && HeldSeatWord(g, 3, table::SEAT_BOOST_SCALE) == Bits(25.0f));
  CHECK(!boot::StatsRefusal(g));
  Shutdown();
  Start(host, ARace(dir, Stats({{CONSTANT, BOOST_BASE, Bits(10.0f)}, {CONSTANT, BOOST_SCALE, Bits(50.0f)}})), g, 1);
  CHECK(Says(boot::StatsRefusal(g).value_or(""), "lasts 10 + 50 x boost duration, 455 frames for the longest boost duration"));
  Shutdown();
}

// The boost length's bound (const_pack.cpp BoostLengthRefusal): the longest of the machines and of a custom machine's
// three parts summed, counted as the game counts.
void TheBoostLengthsBound()
{
  std::array<u32, stats::BLOCKS> disc{};
  disc.fill(Bits(1.0f));
  disc[40] = Bits(2.0f);                                                    // the longest machine
  boot::StatPack none, base, scale;
  none.name = base.name = scale.name = "p";
  CHECK(!boot::BoostLengthRefusal(none, disc));                             // no boost length: never judged
  disc[3] = Bits(4.25f);
  CHECK(!boot::BoostLengthRefusal(none, disc));
  disc[3] = Bits(1.0f);
  scale.constants = {{CONSTANT, BOOST_SCALE, Bits(60.0f)}};
  CHECK(!boot::BoostLengthRefusal(scale, disc));                            // 60 x 3 (the parts' sum) = 180
  base.constants = {{CONSTANT, BOOST_BASE, Bits(60.0f)}, {CONSTANT, BOOST_SCALE, Bits(60.0f)}};
  CHECK(!boot::BoostLengthRefusal(base, disc));                             // 60 + 180 = 240
  base.entries = {{static_cast<u8>(stats::MACHINES + 60), 0x38, Bits(1.25f)}};  // a booster: 3.25 summed, 60 + 195
  CHECK(!boot::BoostLengthRefusal(base, disc));
  base.entries = {{static_cast<u8>(stats::MACHINES + 60), 0x38, Bits(1.3f)}};   // 3.3: 60 + 198 = 258
  CHECK(Says(boot::BoostLengthRefusal(base, disc).value_or(""), "the game counts at most 255"));
  base.entries = {{7, 0x38, Bits(4.0f)}};                                   // a machine of 4 s: 60 + 240 = 300
  CHECK(Says(boot::BoostLengthRefusal(base, disc).value_or(""), "300 frames for the longest boost duration its car data gives (4)"));
  scale.constants = {{CONSTANT, BOOST_SCALE, Bits(59.0f)}};
  base.entries.clear();
  disc[40] = Bits(4.32f);                                                   // 59 x 4.32 = 254.88
  CHECK(!boot::BoostLengthRefusal(scale, disc));
}

// A pack a seat: seat 0 on GX Unleashed's (pack 1), seat 1 on the disc's own (0), the race on the made-up pack.
void APackASeat(TestHost& host, const std::string& dir)
{
  Guest g(host, Data());
  const std::string setup = Stats(ThePack(), "Made") + Packs({Stats(Unleashed(), "GX Unleashed")});
  Start(host, ASeatsRace(dir, setup, R"(, "pack": 1)", R"(, "pack": 0)"), g, 1);
  CHECK(Active() && host.log.empty());
  // the shared table holds the race's pack; each seat's row its own: the disc's block with its pack's values over it
  CHECK(HoldsOnly(g, ThePack()) && g.Read8(table::SEAT_BLOCK) == 1 && g.Read8(table::SEAT_BLOCK + 1) == 1 &&
        g.Read8(table::SEAT_BLOCK + 2) == 0);
  bool row0 = true, row1 = true;
  for (u32 at = 4; at < stats::BLOCK_SIZE; at += 4)
  {
    row0 = row0 && g.Read32(table::SEAT_ROWS_AT + at) == (at == 4 ? Bits(2520.0f) : MadeUp(6, at));
    row1 = row1 && g.Read32(table::SEAT_ROWS_AT + stats::BLOCK_SIZE + at) == MadeUp(12, at);
  }
  CHECK(row0 && row1 && g.Read32(table::SEAT_ROWS_AT) == g.Read32(TABLE + stats::BlockAt(6)));
  // constants: seat 0 GX Unleashed's, seat 1 the game's own, every other seat the race's (the made-up pack has none)
  CHECK(HeldSeatWord(g, 0, table::SEAT_DASH_BASE) == Bits(30.0f) && HeldSeatWord(g, 1, table::SEAT_DASH_BASE) == 0 &&
        HeldSeatWord(g, 2, table::SEAT_DASH_SCALE) == Bits(30.0f) && HeldSeatWord(g, 0, table::SEAT_ENERGY_USE) == 0x3E19999A &&
        HeldSeatWord(g, 1, table::SEAT_ENERGY_USE) == 0x3E2AAAAB);
  CHECK(Says(host.notices, "stat packs a seat: 2 seats race a row of their own"));
  CHECK(!boot::StatsRefusal(g) && Says(host.notices, "2 rows on; seats of their own: 0:GX Unleashed 1:disc"));
  // a row turned off is refused at the go
  g.Write8(table::SEAT_BLOCK + 1, 0);
  CHECK(Says(boot::StatsRefusal(g).value_or(""), "seat 1's row is off, and its pack says one"));
  Shutdown();
  // without a pack of the race: the rows all the same
  Start(host, ASeatsRace(dir, Packs({Stats(Unleashed())}), R"(, "pack": 1)", ""), g, 1);
  CHECK(HoldsOnly(g, {}) && g.Read8(table::SEAT_BLOCK) == 1 && g.Read8(table::SEAT_BLOCK + 1) == 0 &&
        g.Read32(table::SEAT_ROWS_AT + 4) == Bits(2520.0f) && !boot::StatsRefusal(g));
  Shutdown();
  // what is refused: a pack the race does not have, more than four packs, a pack that is no pack
  CHECK(Says(Refused2(host, ASeatsRace(dir, Packs({Stats(Unleashed())}), R"(, "pack": 2)", "")), "seat 0 names a pack the race does not have"));
  const std::string five = Packs({Stats(ThePack()), Stats(ThePack()), Stats(ThePack()), Stats(ThePack()), Stats(ThePack())});
  CHECK(Says(Refused2(host, ASeatsRace(dir, five, "", "")), "not a list of at most 4"));
  CHECK(Says(Refused2(host, ASeatsRace(dir, Packs({Stats({{CONSTANT, BASE, Bits(-1.0f)}})}), "", "")),
             "the race's pack a seat 1 is refused: the constant DashPlateBase is -1"));
}
// The constants are described once (the protocol's protocol/stats.toml) and generated three times: here
// (const_ranges.g.h, through const_ranges.h), for the launcher and the server (Online.Protocol's
// StatPackConstants.All), and in the table of the protocol's handbook/stat-pack-constants.md, which both programs'
// tests are held to (StatPackConstantsTests), so neither reads the other's source. Row for row, in
// order and no more: the slot (a double's two), the name, the size, the game's value (its bits; a double's two
// words), the lowest and the highest. A double's low word has no row: it follows its high word, of the same name,
// and bounds nothing.
void TheListIsTheHandbooks()
{
  std::ifstream file(ProtocolFile("handbook/stat-pack-constants.md"));
  CHECK(file.good());
  std::vector<std::vector<std::string>> rows;   // a row's cells, trimmed: "", the slot, the name, the size, ...
  bool in_table = false;
  for (std::string line; std::getline(file, line);)
  {
    if (line.starts_with("| Slot | Name in a pack |"))
      in_table = std::getline(file, line).good();   // and the line under the heads
    else if (in_table && line.starts_with("|"))
    {
      std::vector<std::string> cells = SplitString(line, '|');
      for (std::string& cell : cells)
      {
        const size_t first = cell.find_first_not_of(' ');
        cell = first == std::string::npos ? std::string() : cell.substr(first, cell.find_last_not_of(' ') + 1 - first);
      }
      rows.push_back(std::move(cells));
    }
    else if (in_table)
      break;
  }
  CHECK(!rows.empty());
  size_t row = 0;
  for (u32 slot = 0; slot < consts::SLOTS; ++slot)
  {
    const consts::Constant& c = consts::CONSTANTS[slot];
    if (c.kind == consts::Kind::Number64Low)
    {
      CHECK(slot > 0 && consts::CONSTANTS[slot - 1].kind == consts::Kind::Number64High &&
            std::string_view(consts::CONSTANTS[slot - 1].name) == c.name && c.low == 0 && c.high == 0);
      continue;
    }
    CHECK(row < rows.size());
    if (row >= rows.size())
      break;
    const std::vector<std::string>& r = rows[row++];
    CHECK(r.size() >= 7);
    if (r.size() < 7)
      continue;
    const bool wide = c.kind == consts::Kind::Number64High;
    CHECK(!wide || (slot + 1 < consts::SLOTS && consts::CONSTANTS[slot + 1].kind == consts::Kind::Number64Low));
    CHECK(r[1] == (wide ? std::format("{}, {}", slot, slot + 1) : std::to_string(slot)));
    CHECK(r[2] == c.name);
    CHECK(r[3] == (wide ? "64 bits" : "32 bits"));
    if (wide)
    {
      const u64 stock = std::bit_cast<u64>(std::strtod(r[4].c_str(), nullptr));
      CHECK(static_cast<u32>(stock >> 32) == c.stock && static_cast<u32>(stock) == consts::CONSTANTS[slot + 1].stock);
    }
    else
      CHECK(std::bit_cast<u32>(std::strtof(r[4].c_str(), nullptr)) == c.stock);
    CHECK(std::strtod(r[5].c_str(), nullptr) == c.low);
    CHECK(std::strtod(r[6].c_str(), nullptr) == c.high);
  }
  CHECK(row == rows.size());
}

}  // namespace

void TestAStatPacksConstants()
{
  BEGIN_TEST();
  TestHost host;
  const std::string dir = Temp();
  TheReader(host, dir);
  WhatTheReaderRefuses(host, dir);
  InTheGame(host, dir);
  TheBoostLengthsBound();
  APackASeat(host, dir);
  TheListIsTheHandbooks();
  std::filesystem::remove_all(dir);
}
