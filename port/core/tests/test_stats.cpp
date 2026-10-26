// Tests of a race's stat pack as the game reads it (files_stats.cpp): the pack of a match start, every pack that is
// refused and the sentence that says why, the table of ranges against the page it is written on, every value of the
// list of cases both programs are held to, and the pack in a recording (version 3). The pack in the game's memory: test_stats_race.cpp.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "parts.h"
#include "stat_packs.h"

using namespace stat_packs;

namespace
{
// What the reader says of a race whose set-up holds `stats`: "" when it took the pack, else the log.
std::string Refusal(TestHost& host, const std::string& dir, const std::string& stats)
{
  const boot::StatPack* pack = PackOf(host, ARace(dir, stats));
  const std::string said = pack ? "" : host.log.empty() ? "no pack, and no word" : host.log;
  CHECK(pack != nullptr || !Active());   // a refused pack is a refused race
  Shutdown();
  return said;
}

std::string RefusalOf(TestHost& host, const std::string& dir, const Entries& entries)
{
  return Refusal(host, dir, Stats(entries));
}

// A member written by hand: other values, another fingerprint, another name than Stats() gives.
std::string Member(const std::string& name, const std::string& fingerprint, const std::string& values)
{
  return "\"stats\": {\"name\": \"" + name + "\", \"fingerprint\": \"" + fingerprint + "\", \"values\": \"" + values + "\"}, ";
}

void APackThatIsTaken(TestHost& host, const std::string& dir)
{
  // the made-up pack of both sides' tests: the bytes and the fingerprint the launcher's side writes for it
  CHECK(Base64(Bytes(ThePack())) == THE_PACKS_VALUES);
  CHECK(boot::StatFingerprint(Bytes(ThePack())) == THE_PACKS_FINGERPRINT);
  const boot::StatPack* pack = PackOf(host, ARace(dir, Member("GX Made Up 1.0", THE_PACKS_FINGERPRINT, THE_PACKS_VALUES)));
  CHECK(Active() && host.log.empty() && pack != nullptr);
  if (pack)
  {
    CHECK(pack->name == "GX Made Up 1.0" && pack->fingerprint == THE_PACKS_FINGERPRINT && pack->entries.size() == 5);
    CHECK(pack->entries[1].block == 6 && pack->entries[1].at == 0x48 && pack->entries[1].value == 0x03010000);
    CHECK(pack->entries[4].block == 115 && pack->entries[4].at == 0x34 && pack->entries[4].value == Bits(14.0f));
  }
  Shutdown();
  // a set-up without one, or with null where one would be: a race on the disc's own car data
  for (const char* none : {"", "\"stats\": null, "})
  {
    CHECK(PackOf(host, ARace(dir, none)) == nullptr && Active() && host.log.empty());
    Shutdown();
  }
  // every word of every block at once: the whole car data as a pack (a replacement file is such a pack)
  Entries whole;
  for (u32 block = 0; block < stats::BLOCKS; ++block)
  {
    for (u32 offset = 4; offset < stats::BLOCK_SIZE; offset += 4)
      whole.push_back({static_cast<u8>(block), static_cast<u8>(offset),
                       offset == file::CAR_BYTES_AT ? 0x07030000 : Bits(offset == 0x04 ? 1300.0f : 0.125f)});
  }
  CHECK(whole.size() == stats::MOST_ENTRIES && RefusalOf(host, dir, whole).empty());
}

void APackThatIsNoPack(TestHost& host, const std::string& dir)
{
  const std::string values = THE_PACKS_VALUES;
  // the fingerprint is the values' own
  std::string other = THE_PACKS_FINGERPRINT;
  other[7] = other[7] == '0' ? '1' : '0';
  CHECK(Says(Refusal(host, dir, Member("p", other, values)),
             std::string("its values' fingerprint is ") + THE_PACKS_FINGERPRINT + ", and it says another"));
  CHECK(Says(Refusal(host, dir, Member("p", "", values)), "and it says another"));
  // its three members, each a text
  CHECK(Says(Refusal(host, dir, "\"stats\": 7, "), "the race's stat pack is refused: it is not an object"));
  CHECK(Says(Refusal(host, dir, "\"stats\": {\"name\": \"p\", \"values\": \"" + values + "\"}, "), "no fingerprint or no values"));
  CHECK(Says(Refusal(host, dir, "\"stats\": {\"name\": \"p\", \"fingerprint\": 7, \"values\": \"" + values + "\"}, "),
             "no fingerprint or no values"));
  CHECK(Says(Refusal(host, dir, "\"stats\": {\"fingerprint\": \"x\", \"values\": \"" + values + "\"}, "), "its name is not"));
  for (const std::string& name : {std::string(), std::string(41, 'n'), std::string(" p"), std::string("p "),
                                  std::string("p\\n"), std::string("p\xC3\xA9")})
    CHECK(Says(Refusal(host, dir, Member(name, THE_PACKS_FINGERPRINT, values)), "its name is not 1 to 40 characters"));
  CHECK(Refusal(host, dir, Member(std::string(40, 'n'), THE_PACKS_FINGERPRINT, values)).empty());
  // the values: base64 as it is written one way, whole entries, at least one, no more than the car data has words
  for (const std::string& bad : {values.substr(1), "*" + values.substr(1), values.substr(0, 20) + " " + values.substr(20),
                                 values.substr(0, 20) + "=" + values.substr(21), values.substr(0, 38) + "B=",
                                 values.substr(0, 37) + "B==", std::string(42000, 'A')})
    CHECK(Says(Refusal(host, dir, Member("p", THE_PACKS_FINGERPRINT, bad)), "its values are not base64"));
  CHECK(Says(Refusal(host, dir, Member("p", THE_PACKS_FINGERPRINT, values.substr(0, 39) + "=")), "its values are 29 bytes"));
  CHECK(Says(Refusal(host, dir, Member("p", THE_PACKS_FINGERPRINT, "")), "its values are 0 bytes, which is not 1 to 5166 entries"));
  CHECK(Says(Refusal(host, dir, Member("p", THE_PACKS_FINGERPRINT, "BgRFHYAABkg=")), "its values are 8 bytes"));
}

void AnEntryThatIsRefused(TestHost& host, const std::string& dir)
{
  const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
  // which block, which word
  CHECK(Says(RefusalOf(host, dir, {{116, 4, Bits(1)}}), "entry 0 names block 116: the car data has 116"));
  CHECK(Says(RefusalOf(host, dir, {{6, 4, Bits(900)}, {6, 0, 0x23801CE0}}),
             "entry 1 would write block 6's word 0, the place of its name: a pack changes no name"));
  CHECK(Says(RefusalOf(host, dir, {{90, 0, 0}}), "would write block 90's word 0"));
  for (const u8 offset : {u8{0x06}, u8{0x49}, u8{0xB4}, u8{0xFC}})
    CHECK(Says(RefusalOf(host, dir, {{6, offset, Bits(1)}}), "no word a pack may change is there"));
  // each word once, in order
  CHECK(Says(RefusalOf(host, dir, {{6, 4, Bits(900)}, {6, 4, Bits(901)}}), "entry 1 (block 6, Weight) is there twice or out of order"));
  CHECK(Says(RefusalOf(host, dir, {{6, 8, Bits(1)}, {6, 4, Bits(900)}}), "entry 1 (block 6, Weight) is there twice"));
  CHECK(Says(RefusalOf(host, dir, {{7, 4, Bits(900)}, {6, 0x5C, Bits(1)}}), "entry 1 (block 6, Hover0Z) is there twice"));
  // a value in its range: the floors of the divisors, the signs, the caps, the byte
  CHECK(Says(RefusalOf(host, dir, {{6, 0x04, Bits(0)}}), "block 6's Weight is 0, and must be at least 1"));
  CHECK(Says(RefusalOf(host, dir, {{6, 0x04, Bits(0.5f)}}), "block 6's Weight is 0.5, and must be at least 1"));
  CHECK(Says(RefusalOf(host, dir, {{6, 0x04, Bits(-5)}}), "block 6's Weight is -5, and must be at least 1"));
  CHECK(Says(RefusalOf(host, dir, {{6, 0x04, Bits(25000.5f)}}), "block 6's Weight is 25000.5, and must be at most 25000"));
  CHECK(Says(RefusalOf(host, dir, {{6, 0x0C, Bits(-0.01f)}}), "block 6's MaxSpeed is -0.01, and must be at least 0"));
  CHECK(Says(RefusalOf(host, dir, {{6, 0x34, Bits(0.05f)}}), "block 6's BoostStrength is 0.05, and must be at least 0.1"));
  CHECK(Says(RefusalOf(host, dir, {{6, 0x38, Bits(4.26f)}}), "block 6's BoostDuration is 4.26, and must be below 4.26"));
  CHECK(Says(RefusalOf(host, dir, {{6, 0x10, Bits(-45.5f)}}), "block 6's Grip1 is -45.5, and must be at least -45"));
  CHECK(Says(RefusalOf(host, dir, {{6, 0x10, Bits(45.5f)}}), "block 6's Grip1 is 45.5, and must be at most 45"));
  CHECK(Says(RefusalOf(host, dir, {{6, 0x4C, Bits(-19.5f)}}), "block 6's CameraReorienting is -19.5, and must be at least -19"));
  CHECK(Says(RefusalOf(host, dir, {{6, 0x48, 0x03040000}}), "block 6's Bytes48 is 03040000: the second byte is at most 3"));
  CHECK(Says(RefusalOf(host, dir, {{6, 0x48, 0x03010001}}), "and the last two are 0"));
  CHECK(Says(RefusalOf(host, dir, {{6, 0xB0, Bits(-25.5f)}}), "block 6's Wall3Z is -25.5, and must be at least -25"));
  // on the bits: a denormal is zero where the race runs, so it is no divisor and no "not 0"; -0, infinity, no number
  for (const u32 bits : {0x00000001u, 0x007FFFFFu, 0x80000000u, 0x80000001u, Bits(inf), Bits(-inf), Bits(nan)})
  {
    const std::string said = std::format("is the bits {:08X}, which is no number a pack may hold", bits);
    CHECK(Says(RefusalOf(host, dir, {{6, 0x04, bits}}), "block 6's Weight " + said));
    CHECK(Says(RefusalOf(host, dir, {{6, 0x34, bits}}), "block 6's BoostStrength " + said));
    CHECK(Says(RefusalOf(host, dir, {{6, 0x5C, bits}}), "block 6's Hover0Z " + said));
    CHECK(Says(RefusalOf(host, dir, {{66, 0x10, bits}}), "block 66's Grip1 " + said));
  }
  // a machine's hover point's Z: a real distance from 0
  CHECK(Says(RefusalOf(host, dir, {{6, 0x5C, Bits(0)}}), "block 6's Hover0Z is 0, and a machine's hover point's Z is at least 0.01 from 0"));
  CHECK(Says(RefusalOf(host, dir, {{40, 0x80, Bits(-0.005f)}}), "block 40's Hover3Z is -0.005, and a machine's hover point's Z is at least 0.01"));
  // at a machine's edges
  const Entries fine = {{6, 0x04, Bits(1)}, {6, 0x08, Bits(0)}, {6, 0x10, Bits(45)}, {6, 0x14, Bits(3.5f)}, {6, 0x38, Bits(4.2599f)},
                        {6, 0x48, 0xFF030000}, {6, 0x4C, Bits(19)}, {6, 0x54, Bits(0)}, {6, 0x58, Bits(0)}, {6, 0x5C, Bits(-0.01f)},
                        {6, 0x8C, Bits(0)}};
  CHECK(RefusalOf(host, dir, fine).empty());
  // A custom part's block is what the part adds to a custom machine, of either sign: held by its size alone. What a
  // machine's block may not hold and a part's may: a negative number, 0 or less than a floor where a machine has
  // one (a weight, a booster's boost), a hover Z of 0; to a machine's most either side of 0, and not past it.
  const Entries parts = {{41, 0x04, Bits(0)}, {41, 0x08, Bits(-1)}, {41, 0x30, Bits(-0.05f)}, {41, 0x34, Bits(-220)}, {41, 0x38, Bits(-4.2599f)},
                         {41, 0x80, Bits(-0.005f)}, {66, 0x3C, Bits(-0.7f)}, {66, 0x5C, Bits(0)}, {91, 0x04, Bits(-25000)},
                         {91, 0x10, Bits(0)}, {91, 0x34, Bits(0)}, {115, 0x04, Bits(0.5f)}, {115, 0x48, 0xFF030000}};
  CHECK(RefusalOf(host, dir, parts).empty());
  CHECK(Says(RefusalOf(host, dir, {{41, 0x30, Bits(-10.5f)}}), "block 41's Grip2 is -10.5, and a part's value is at most 10 from 0"));
  CHECK(Says(RefusalOf(host, dir, {{115, 0x04, Bits(25000.5f)}}), "block 115's Weight is 25000.5, and a part's value is at most 25000 from 0"));
  CHECK(Says(RefusalOf(host, dir, {{94, 0x38, Bits(-4.26f)}}), "block 94's BoostDuration is -4.26, and a part's value is less than 4.26 from 0"));
  CHECK(Says(RefusalOf(host, dir, {{66, 0xB0, Bits(25.5f)}}), "block 66's Wall3Z is 25.5, and a part's value is at most 25 from 0"));
  CHECK(Says(RefusalOf(host, dir, {{66, 0x48, 0x03040000}}), "block 66's Bytes48 is 03040000: the second byte is at most 3"));
  // in a machine's block a negative grip is taken, to minus its most; the floors stay its own
  CHECK(RefusalOf(host, dir, {{40, 0x30, Bits(-0.05f)}}).empty());
  CHECK(RefusalOf(host, dir, {{11, 0x3C, Bits(-0.02f)}}).empty());   // F-Zero GX+'s Super Piranha
  CHECK(Says(RefusalOf(host, dir, {{40, 0x30, Bits(-10.5f)}}), "block 40's Grip2 is -10.5, and must be at least -10"));
  CHECK(Says(RefusalOf(host, dir, {{40, 0x04, Bits(0)}}), "block 40's Weight is 0, and must be at least 1"));
}

// The table's row on handbook/stat-packs.md.
std::string Row(const stats::Rule& r)
{
  // (a part's block: the row's size alone, either side of 0)
  const std::string low = stats::Text(r.low), high = stats::Text(r.high);
  switch (r.kind)
  {
  case stats::Kind::Bytes:
    return std::format("| 0x{:02X} | {} | two bytes, then two that are 0 | first: 0 to 255 | second: 0 to {} | as a machine's |", r.at,
                       r.name, high);
  case stats::Kind::Points:
    return std::format("| 0x{:02X} | {}0X to {}3Z | four points of X, Y, Z{} | at least {} | at most {} | as a machine's{} |", r.at,
                       r.name, r.name,
                       r.z_least != 0 ? std::format("; a machine's Z at least {} from 0", stats::Text(r.z_least)) : "", low, high,
                       r.z_least != 0 ? ", any Z" : "");
  default:
    return std::format("| 0x{:02X} | {} | a number | at least {} | {} {} | {} {} either side of 0 |", r.at, r.name, low,
                       r.below ? "below" : "at most", high, r.below ? "less than" : "to", high);
  }
}
}  // namespace

void TestAStatPackOfAMatch()
{
  BEGIN_TEST();
  TestHost host;
  const std::string dir = Temp();
  APackThatIsTaken(host, dir);
  APackThatIsNoPack(host, dir);
  AnEntryThatIsRefused(host, dir);
  std::filesystem::remove_all(dir);
}

// The ranges are described once (the protocol's protocol/stats.toml) and generated three times: here
// (stat_ranges.g.h, through stat_ranges.h), for the launcher and the server (Online.Protocol's StatPackRules.All),
// and on the handbook's page, which both programs' tests are held to. The rows under the page's heading of the table
// must be this table's, in its order, and no more.
void TestTheStatRangesAreTheHandbooks()
{
  BEGIN_TEST();
  const auto page = ProtocolFile("handbook/stat-packs.md");   // the protocol's page (its checkout: run.sh)
  std::ifstream file(page);
  CHECK(file.good());
  std::vector<std::string> rows;
  bool in_table = false;
  for (std::string line; std::getline(file, line);)
  {
    if (line.starts_with("| At | Name in a pack |"))
      in_table = true;
    else if (in_table && line.starts_with("| 0x"))
      rows.push_back(line);
    else if (in_table && !line.starts_with("|"))
      break;
  }
  CHECK(rows.size() == stats::RULES.size());
  for (size_t i = 0; i < stats::RULES.size(); ++i)
  {
    // (the page's row goes on with one more column, the launcher's and the server's caution, which is not the game's)
    const bool same = i < rows.size() && rows[i].starts_with(Row(stats::RULES[i]) + " ");
    CHECK(same);
    if (!same)
      std::printf("  the table's row: %s\n  the handbook's:  %s\n", Row(stats::RULES[i]).c_str(), i < rows.size() ? rows[i].c_str() : "");
  }
  // a points row is the same either side of 0
  for (const stats::Rule& rule : stats::RULES)
    CHECK(rule.kind != stats::Kind::Points || rule.low == -rule.high);
  // the rules cover a block's 44 words, each once, and no rule reaches word 0
  u32 words = 0;
  for (u32 offset = 0; offset < 0x100; ++offset)
    words += stats::RuleAt(offset) != nullptr;
  CHECK(words == stats::WORDS && !stats::RuleAt(0) && stats::RuleAt(4) && stats::RuleAt(0xB0) && !stats::RuleAt(0xB4));
  CHECK(stats::WordName(0x5C) == "Hover0Z" && stats::WordName(0xB0) == "Wall3Z" && stats::WordName(0x48) == "Bytes48");
  CHECK(stats::Text(100000.0f) == "100000" && stats::Text(0.45f) == "0.45" && stats::Text(-0.0f) == "-0" &&
        stats::Text(4.26f) == "4.26" && stats::Text(-100000.0f) == "-100000" && stats::Text(1330.0f) == "1330");
  CHECK(stats::BlockAt(40) == 40 * 0xB4 && stats::BlockAt(41) == 0x1F04 && stats::BlockAt(115) + 0xB4 == 0x53C0);
}

// A recording of a race with a stat pack: version 3 before 0.4.0 (7 since: no rubber band), the pack in its header as it came, and a playback set up
// through the same reader. A file whose version and header disagree is not played.
void TestAStatPackInARecording()
{
  BEGIN_TEST();
  TestHost host;
  const std::string dir = Temp();
  const auto recorded = [&](const std::string& stats) {
    PackOf(host, ARace(dir, stats));
    CHECK(boot::StartReplay(dir + "/rec.fzrp"));
    std::string file;
    CHECK(ReadFileToString(dir + "/rec.fzrp", file));
    Shutdown();
    return file;
  };
  const std::string with = recorded(Stats(ThePack(), "Rec Pack")), without = recorded("");
  // (a race from a match has no rubber band since 0.4.0, so its recording is version 7 with a pack or without)
  CHECK(with.compare(0, 6, std::string("FZRP\0\7", 6)) == 0 && without.compare(0, 6, std::string("FZRP\0\7", 6)) == 0);
  CHECK(Says(with, std::string("\"fingerprint\":\"") + THE_PACKS_FINGERPRINT + "\"") && Says(with, "\"name\":\"Rec Pack\"") &&
        Says(with, "\"values\":\"BgRFHYAABkgDAQAAKFy") && !Says(without, "stats"));
  // read back: version 3 with the pack, version 2 without; the other two are refused, and so is a pack that is none
  const auto played = [&](int version, const std::string& stats) {
    WriteABundle(dir);
    WriteReplay(dir + "/race.fzrp", {0, 1}, stats);
    std::string file;
    CHECK(ReadFileToString(dir + "/race.fzrp", file));
    file[5] = static_cast<char>(version);
    WriteText(dir + "/race.fzrp", file);
    Options options;
    options.bundle_dir = dir;
    options.play_file = dir + "/race.fzrp";
    const boot::StatPack* pack = PackOf(host, options);
    const std::string said = Active() ? (pack ? "played with " + pack->name : "played") : host.log;
    net::Recording steps;
    CHECK(net::ReadRecording(dir + "/race.fzrp", &steps) == (version >= 2 && version <= 7));
    Shutdown();
    return said;
  };
  CHECK(played(3, Stats(ThePack(), "Rec Pack")) == "played with Rec Pack");
  CHECK(played(2, "") == "played");
  CHECK(Says(played(2, Stats(ThePack())), "a version 2 replay holds a stat pack, which only version 3 does: refused"));
  CHECK(Says(played(3, ""), "a version 3 replay names no stat pack, and version 3 is a race with one: refused"));
  CHECK(Says(played(3, Stats({{6, 4, Bits(0)}})), "the race's stat pack is refused: block 6's Weight is 0"));
  CHECK(Says(played(4, ""), "a version 4 replay names no custom machine, and version 4 is a race with one: refused"));
  CHECK(Says(played(5, ""), "a version 5 replay names no mine rule, and version 5 is a race with one: refused"));
  CHECK(Says(played(8, ""), "is not a version 2, 3, 4, 5, 6 or 7 replay file"));
  CHECK(Says(played(7, ""), "names no start grid and has the rubber band, and version 7 is a race of 0.4.0's set-up"));
  CHECK(Says(played(6, ""), "names no pack a seat, and version 6 is a race with one"));
  CHECK(Says(played(1, ""), "is not a version 2, 3, 4, 5, 6 or 7 replay file"));
  std::filesystem::remove_all(dir);
}

// What this reader says of a value, and what the launcher's and the server's says (Online.Protocol/StatPackRules.cs),
// are held to one list: the protocol's protocol/tests/vectors/stat-cases.tsv, which stat-cases.py beside it writes
// from the handbook's table. Its test there
// (StatPackRulesTests) reads the same file, so the two programs cannot differ on a denormal, a -0, a floor or a bound.
void TestTheStatCasesOfBothPrograms()
{
  BEGIN_TEST();
  std::ifstream file(ProtocolFile("protocol/tests/vectors/stat-cases.tsv"));
  CHECK(file.good());
  u32 cases = 0, refused = 0, wrong = 0;
  std::string line;
  std::getline(file, line);   // the columns' names
  while (std::getline(file, line))
  {
    const std::vector<std::string> cells = SplitString(line, '\t');
    u32 block = 0, offset = 0, bits = 0;
    if (cells.size() != 5 || !TryParse(cells[0], &block) || !TryParse(cells[1], &offset, 16) || !TryParse(cells[2], &bits, 16) ||
        (cells[3] != "take" && cells[3] != "refuse") || block >= stats::BLOCKS || !stats::RuleAt(offset))
    {
      ++wrong;
      std::printf("  not a case: %s\n", line.c_str());
      continue;
    }
    const std::optional<std::string> said = stats::Refusal(block, offset, bits);
    ++cases;
    refused += said.has_value();
    if (said.has_value() != (cells[3] == "refuse"))
    {
      ++wrong;
      std::printf("  %s: the list says %s, the reader %s\n", cells[4].c_str(), cells[3].c_str(), said ? said->c_str() : "takes it");
    }
  }
  CHECK(wrong == 0);
  CHECK(cases > 1500 && refused > 900 && refused < cases);
}
