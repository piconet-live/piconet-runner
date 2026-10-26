// What the tests of a race's stat pack share: a pack made up here as the match start carries it, a bundle and a
// race's launch file with one, and car data made up here as the game's loader leaves it in memory. No value is the
// game's: a made-up table holds a number worked out of the block and the offset.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "harness.h"

#include "files.h"
#include "stat_ranges.h"

namespace stat_packs
{
using Entries = std::vector<boot::StatEntry>;

inline u32 Bits(float value)
{
  return std::bit_cast<u32>(value);
}

// The made-up pack of both sides' tests (net/tests/Online.Tests/StatPackTests.cs has the same five values, the same
// base64 and the same fingerprint, worked out by neither program): the Blue Falcon's weight doubled to 2,520 and
// its two bytes, the last machine's first hover point's Z, the first body's weight, the last booster's boost.
inline Entries ThePack()
{
  return {{6, 0x04, Bits(2520.0f)}, {6, 0x48, 0x03010000}, {40, 0x5C, Bits(-1.5f)}, {41, 0x04, Bits(500.0f)},
          {115, 0x34, Bits(14.0f)}};
}
constexpr const char* THE_PACKS_VALUES = "BgRFHYAABkgDAQAAKFy/wAAAKQRD+gAAczRBYAAA";
constexpr const char* THE_PACKS_FINGERPRINT = "8472faeb0e46573e5c279e752ecf65c50452d8ee";

// The entries as they travel: 6 bytes each.
inline std::vector<u8> Bytes(const Entries& entries)
{
  std::vector<u8> bytes;
  for (const boot::StatEntry& e : entries)
  {
    bytes.insert(bytes.end(), {e.block, e.at, static_cast<u8>(e.value >> 24), static_cast<u8>(e.value >> 16),
                               static_cast<u8>(e.value >> 8), static_cast<u8>(e.value)});
  }
  return bytes;
}

inline std::string Base64(const std::vector<u8>& bytes)
{
  static const char* const letters = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string text;
  for (size_t i = 0; i < bytes.size(); i += 3)
  {
    const size_t have = std::min<size_t>(3, bytes.size() - i);
    const u32 group = bytes[i] << 16 | (have > 1 ? bytes[i + 1] << 8 : 0) | (have > 2 ? bytes[i + 2] : 0);
    text += letters[group >> 18];
    text += letters[(group >> 12) & 63];
    text += have > 1 ? letters[(group >> 6) & 63] : '=';
    text += have > 2 ? letters[group & 63] : '=';
  }
  return text;
}

// A set-up's "stats" member, with a comma after it: the pack of `entries` under its own fingerprint.
inline std::string Stats(const Entries& entries, const std::string& name = "Made up")
{
  const std::vector<u8> bytes = Bytes(entries);
  return "\"stats\": {\"name\": \"" + name + "\", \"fingerprint\": \"" + boot::StatFingerprint(bytes) +
         "\", \"values\": \"" + Base64(bytes) + "\"}, ";
}

// A bundle in `dir`.
inline void WriteABundle(const std::string& dir)
{
  std::string image(0x100, '\0');
  image.replace(0, 4, "FZMD");
  image.replace(0x10, 4, "FZSI");
  WriteText(dir + "/mod.bin", image);
  WriteText(dir + "/manifest.json", "{\"commit\": \"abc1234\"}");
  WriteText(dir + "/codes.txt", "$Boot to race\n48000000 800030C8\n$Replay boot\n04000000 00000001\n$Race setup\n"
                                "C0000000 00000001\n4E800020 00000000\n");
}

// The options of a race from a launch file in `dir` whose set-up begins with `stats` (a "stats" member and its
// comma, or nothing): seat 0 on machine 6, seat 1 on machine 12.
inline Options ARace(const std::string& dir, const std::string& stats)
{
  WriteABundle(dir);
  WriteText(dir + "/launch.json", R"({"about": "a test race", "bundle": ")" + dir + R"(", "match": {
    "match": {"matchId": "m7", "seat": 0, "token": "1f2e3d4c5b6a7988", "relay": {"host": "", "port": 47811},
              "setup": {)" + stats + R"("course": 8, "laps": 3,
                        "seats": [{"seat": 0, "machine": 6, "playerId": "p1", "name": "A"},
                                  {"seat": 1, "machine": 12, "playerId": "p2", "name": "B"}]}},
    "relayHost": "127.0.0.1"}})");
  Options options;
  const std::optional<std::string> problem = ReadLaunchFile(dir + "/launch.json", &options);
  CHECK(!problem);
  return options;
}

// What the game was started with: the race's pack, or why the race was refused (the host's log).
inline const boot::StatPack* PackOf(TestHost& host, const Options& options)
{
  host.log.clear();
  host.notices.clear();
  Init(host, options);
  const boot::Match* match = boot::Get();
  return match && match->stats ? &*match->stats : nullptr;
}

constexpr u32 TABLE = game::CAR_DATA;

// A made-up word of the car data: a number of its own for every block and offset (2 to 3.3).
inline u32 MadeUp(u32 block, u32 offset)
{
  return offset == file::CAR_BYTES_AT ? (block % 200) << 24 | (block % 4) << 16
                                      : Bits(2.0f + static_cast<float>(block * 44 + offset / 4) / 4096.0f);
}

// The car data in memory as the loader leaves it: every block's word 0 its name's address in the mapped table (the
// machines' names a byte apart, last machine first), every other word made up, and the names' part filled.
inline void LoadCarData(Guest& g)
{
  for (u32 at = 0; at < game::CAR_DATA_SIZE; ++at)
    g.Write8(TABLE + at, 0xEE);
  for (u32 block = 0; block < stats::BLOCKS; ++block)
  {
    const u32 at = TABLE + stats::BlockAt(block);
    g.Write32(at, block < stats::MACHINES ? game::CAR_DATA_MAPPED + file::CAR_NAMES_AT + 13 * (stats::MACHINES - 1 - block)
                                           : game::CAR_DATA_MAPPED + file::CAR_PART_NAMES_AT);
    for (u32 offset = 4; offset < stats::BLOCK_SIZE; offset += 4)
      g.Write32(at + offset, MadeUp(block, offset));
  }
}

inline std::vector<u8> CarData(const Guest& g)
{
  std::vector<u8> bytes(game::CAR_DATA_SIZE);
  g.CopyFrom(bytes.data(), TABLE, bytes.size());
  return bytes;
}

inline u32 Word(const Guest& g, u32 block, u32 offset)
{
  return g.Read32(TABLE + stats::BlockAt(block) + offset);
}

// Whether the table holds every value of `entries`, and everywhere else what LoadCarData put there.
inline bool HoldsOnly(const Guest& g, const Entries& entries)
{
  for (u32 block = 0; block < stats::BLOCKS; ++block)
  {
    for (u32 offset = 4; offset < stats::BLOCK_SIZE; offset += 4)
    {
      u32 expected = MadeUp(block, offset);
      for (const boot::StatEntry& e : entries)
      {
        if (e.block == block && e.at == offset)
          expected = e.value;
      }
      if (Word(g, block, offset) != expected)
        return false;
    }
  }
  return true;
}

// Racers for the go's check: `machines` racers' blocks at an address of the simulation's memory, each with its
// machine's id and the weight the car data holds for it now.
constexpr u32 RACERS = 0x80800000;
inline void MakeRacers(Guest& g, const std::vector<u16>& machines)
{
  LinkMainRel(g);
  g.Write32(MODULE + game::MACHINES, RACERS);
  g.Write8(MODULE + game::MACHINE_COUNT, static_cast<u8>(machines.size()));
  for (u32 i = 0; i < machines.size(); ++i)
  {
    g.Write16(RACERS + i * game::MACHINE_SIZE + game::MACHINE_ID, machines[i]);
    g.Write32(RACERS + i * game::MACHINE_SIZE + game::MACHINE_WEIGHT,
              machines[i] < stats::MACHINES ? Word(g, machines[i], 4) : 0x12345678);
  }
}

inline bool Says(const std::string& text, const std::string& part)
{
  return text.find(part) != std::string::npos;
}

inline int Count(const std::string& text, const std::string& part)
{
  int n = 0;
  for (std::size_t at = text.find(part); at != std::string::npos; at = text.find(part, at + 1))
    ++n;
  return n;
}
}  // namespace stat_packs
