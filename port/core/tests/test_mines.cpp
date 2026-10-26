// Tests of the host's mine rule as the game reads it (files_match.cpp, files_replay.cpp, replay.cpp): "mineRegenMs" of
// a match start or a replay's header, the milliseconds after which a mine that went off comes back, into the race
// block's word in steps (the image's sites_mines.py counts it); and a recording of a race with it, version 5. What the
// image does with the word: port/image/tests/test_mines.py.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "stat_packs.h"

using namespace stat_packs;

namespace
{
// A replay of two seats whose set-up begins with `rules`, its version byte set to `version`.
void WriteAReplay(const std::string& path, const std::string& rules, char version)
{
  WriteReplay(path, {1, 0}, rules);
  std::string file;
  CHECK(ReadFileToString(path, file));
  file[5] = version;
  WriteText(path, file);
}

std::string MinesRule(const std::string& ms)
{
  return "\"rules\": {\"cpuLevel\": 3, \"mineRegenMs\": " + ms + "}, ";
}
}  // namespace

// The rule in steps, at 60 a second: 50 ms is 3. A set-up without it, as every recording made before it, gives 0, the
// game's own way (a mine stays gone). Kept in its range as the finish limit is: 5 seconds at most, none below zero, a
// number; a time shorter than a step is none.
void TestTheMineRuleInTheRaceBlock()
{
  BEGIN_TEST();
  const std::string dir = Temp();
  WriteABundle(dir);
  TestHost host;
  Options options;
  options.bundle_dir = dir;
  options.play_file = dir + "/mines.fzrp";
  for (const auto& [rules, steps] :
       {std::pair{std::string(), 0u}, std::pair{std::string("\"rules\": {\"cpuLevel\": 3}, "), 0u},
        std::pair{MinesRule("200"), 12u}, std::pair{MinesRule("50"), 3u}, std::pair{MinesRule("5000"), 300u},
        std::pair{MinesRule("100000"), 300u}, std::pair{MinesRule("-5"), 0u}, std::pair{MinesRule("16"), 0u},
        std::pair{MinesRule("17"), 1u}, std::pair{MinesRule("\"200\""), 0u}, std::pair{MinesRule("0"), 0u}})
  {
    WriteAReplay(options.play_file, rules, steps ? 5 : 2);
    host.log.clear();
    Init(host, options);
    CHECK(Active() && host.log.empty());
    FrameTick();
    CHECK(Data().Read(layout::RACE_MINE_REGEN_AT, 4) == steps);
    CHECK(Data().Read(layout::RACE_FINISH_LIMIT_AT, 4) == 0);
    Shutdown();
  }
  Init(host, Options{});
  std::filesystem::remove_all(dir);
}

// A recording of a race whose mines come back is version 5, the rule in its header's match as it came, with a stat pack
// or without; without the rule a race keeps its version. A file whose version and rule disagree is not played: a
// version 5 file without the rule, or another version's with it.
void TestTheMineRuleInARecording()
{
  BEGIN_TEST();
  TestHost host;
  const std::string dir = Temp();
  const auto recorded = [&](const std::string& setup) {
    PackOf(host, ARace(dir, setup));
    CHECK(boot::StartReplay(dir + "/rec.fzrp"));
    std::string file;
    CHECK(ReadFileToString(dir + "/rec.fzrp", file));
    Shutdown();
    return file;
  };
  const std::string with = recorded(MinesRule("200")), packed = recorded(MinesRule("200") + Stats(ThePack())),
                    without = recorded(MinesRule("0")), plain = recorded("");
  // (since 0.4.0 a race from a match has no rubber band: every recording of one is version 7, the rule or not)
  CHECK(with.compare(0, 6, std::string("FZRP\0\7", 6)) == 0 && packed.compare(0, 6, std::string("FZRP\0\7", 6)) == 0);
  CHECK(without.compare(0, 6, std::string("FZRP\0\7", 6)) == 0 && plain.compare(0, 6, std::string("FZRP\0\7", 6)) == 0);
  CHECK(Says(with, "\"mineRegenMs\":200") && Says(packed, "\"fingerprint\":\""));
  // read back
  WriteABundle(dir);
  Options options;
  options.bundle_dir = dir;
  options.play_file = dir + "/race.fzrp";
  const auto played = [&](const std::string& rules, char version) {
    WriteAReplay(options.play_file, rules, version);
    host.log.clear();
    Init(host, options);
    const std::string said = Active() ? "played" : host.log;
    net::Recording steps;
    CHECK(net::ReadRecording(options.play_file, &steps));
    Shutdown();
    return said;
  };
  CHECK(played(MinesRule("200"), 5) == "played" && played("", 2) == "played" && played(MinesRule("0"), 2) == "played");
  CHECK(Says(played("", 5), "a version 5 replay names no mine rule, and version 5 is a race with one: refused"));
  CHECK(Says(played(MinesRule("0"), 5), "a version 5 replay names no mine rule"));
  for (const char version : {2, 3, 4})
    CHECK(Says(played(MinesRule("250"), version), "holds the mine rule, which only version 5 does: refused"));
  Init(host, Options{});
  std::filesystem::remove_all(dir);
}
