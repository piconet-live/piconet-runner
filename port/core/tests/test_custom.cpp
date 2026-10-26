// Tests of a seat's custom machine (garage-built: three parts and a pilot): read from a match start into the race
// block (files_match.cpp CustomOfSeat), refused when a part is not of its kind, and carried by a replay (version 4,
// files_replay.cpp) whose lineup then names the custom machine's id.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "harness.h"
#include "stat_packs.h"

namespace
{
// A match start's set-up with these seats, read; nothing when it is refused.
std::optional<boot::Match> SetUpOf(const std::string& seats, const std::string& more = "")
{
  picojson::value root;
  const std::string text = R"({"setup": {"course": 8, "laps": 3, "seats": [)" + seats + "]" + more + "}}";
  if (!picojson::parse(root, text).empty())
    return std::nullopt;
  boot::Match m;
  if (!boot::FromMatch(m, root.get<picojson::object>(), "test"))
    return std::nullopt;
  return m;
}

u32 Word(const u8* p)
{
  return u32(p[0]) << 24 | u32(p[1]) << 16 | u32(p[2]) << 8 | p[3];
}

const char* PLAIN = R"({"seat": 0, "machine": 5})";
std::string Custom(const std::string& members)
{
  return R"({"seat": 1, "machine": 3, "playerId": "p1", "custom": {)" + members + "}}";
}

void TheEntryInTheRaceBlock()
{
  const auto m = SetUpOf(std::string(PLAIN) + ", " +
                         Custom(R"("body": 15, "cockpit": 43, "booster": 58, "pilot": 6,
                                   "colors": [4278190081, 16711681, 65281])"));
  CHECK(m.has_value());
  if (!m)
    return;
  const u8* r = m->race.data();
  CHECK(r[32] == 5 && r[33] == boot::CUSTOM_MACHINE);   // the seat races the custom machine's id
  CHECK(!boot::IsCustom(m->race, 0) && boot::IsCustom(m->race, 1) && !boot::IsCustom(m->race, 2));
  const u8* e = r + boot::RACE_CUSTOM + boot::CUSTOM_ENTRY;
  CHECK(Word(e) == 0xC0000000);
  CHECK(e[4] == 15 && e[12] == 43 && e[20] == 58 && e[28] == 6);
  CHECK(Word(e + 8) == 4278190081u && Word(e + 16) == 16711681u && Word(e + 24) == 65281u);
  CHECK(e[5] == 0 && e[29] == 0 && e[31] == 0);
  // no colours: 0, the game's own colour of each part
  const auto plain = SetUpOf(Custom(R"("body": 0, "cockpit": 25, "booster": 50, "pilot": 40)"));
  CHECK(plain.has_value() && Word(plain->race.data() + boot::RACE_CUSTOM + boot::CUSTOM_ENTRY + 8) == 0);
  // the whole block reaches the window: the entries are inside it
  CHECK(boot::RACE_CUSTOM + 30 * boot::CUSTOM_ENTRY == boot::RACE_SIZE);
}

void APartOfAnotherKindIsRefused()
{
  // every member checked by its kind before the game sees it (an id of the wrong kind is read past the game's tables)
  const char* refused[] = {
      R"("body": 25, "cockpit": 43, "booster": 58, "pilot": 6)",   // a cockpit as the body
      R"("body": 3, "cockpit": 24, "booster": 58, "pilot": 6)",    // a body as the cockpit
      R"("body": 3, "cockpit": 43, "booster": 75, "pilot": 6)",    // no such booster
      R"("body": 3, "cockpit": 43, "booster": 49, "pilot": 6)",
      R"("body": 3, "cockpit": 43, "booster": 58, "pilot": 41)",   // no such pilot
      R"("body": 3, "cockpit": 43, "booster": 58)",                // no pilot
      R"("cockpit": 43, "booster": 58, "pilot": 6)",               // no body
      R"("body": 3.5, "cockpit": 43, "booster": 58, "pilot": 6)",
      R"("body": "3", "cockpit": 43, "booster": 58, "pilot": 6)",
      R"("body": -1, "cockpit": 43, "booster": 58, "pilot": 6)",
      R"("body": 3, "cockpit": 43, "booster": 58, "pilot": 6, "colors": [1, 2])",
      R"("body": 3, "cockpit": 43, "booster": 58, "pilot": 6, "colors": [1, 2, 4294967296])",
      R"("body": 3, "cockpit": 43, "booster": 58, "pilot": 6, "colors": [1, 2, -3])",
      R"("body": 3, "cockpit": 43, "booster": 58, "pilot": 6, "colors": 7)",
      R"("body": 3, "cockpit": 43, "booster": 58, "pilot": 6, "colors": [1, 16711680, 1])",   // slot 0: paints nothing
      R"("body": 3, "cockpit": 43, "booster": 58, "pilot": 6, "colors": [1, 1, 16711682])",   // slot 2
  };
  for (const char* members : refused)
  {
    const bool refused_it = !SetUpOf(Custom(members)).has_value();
    CHECK(refused_it);
    if (!refused_it)
      std::printf("not refused: %s\n", members);
  }
  CHECK(!SetUpOf(R"({"seat": 1, "custom": 5})").has_value());
  CHECK(SetUpOf(Custom(R"("body": 24, "cockpit": 49, "booster": 74, "pilot": 0)")).has_value());
  // 0 (the part's own colour) and black of slot 1 are colours
  CHECK(SetUpOf(Custom(R"("body": 3, "cockpit": 43, "booster": 58, "pilot": 6, "colors": [0, 1, 4294967041])")).has_value());
}

void ARecordingOfARaceWithOne(const std::string& dir)
{
  std::string image(0x100, '\0');
  image.replace(0, 4, "FZMD");
  image.replace(0x10, 4, "FZSI");
  WriteText(dir + "/mod.bin", image);
  WriteText(dir + "/manifest.json", "{\"commit\": \"abc1234\"}");
  WriteText(dir + "/codes.txt", "$Boot to race\n48000000 800030C8\n$Replay boot\n04000000 00000001\n");
  Options options;
  options.bundle_dir = dir;
  options.play_file = dir + "/r.fzrp";
  // version, the seat's custom machine in the header, the lineup's machine of seat 1
  const auto played = [&](int version, bool custom, u8 lineup_machine) {
    const std::string seat1 = custom ? R"(, "custom": {"body": 3, "cockpit": 43, "booster": 58, "pilot": 6})" : "";
    const std::string header = R"({"match": {"matchId": "m9", "setup": {"course": 7, "laps": 3, "seats": [)"
                               R"({"seat": 0, "machine": 3, "playerId": "p0"}, {"seat": 1, "machine": 4, "playerId": "p1")" +
                               seat1 + R"(}]}}, "seat": 1, "mask": 3, "delay": 2})";
    std::string lineup(124, '\0');
    lineup[0] = 3;
    lineup[1] = static_cast<char>(lineup_machine);
    lineup[30] = 1;
    std::string file = "FZRP";
    const auto put32 = [&](u32 v) {
      for (int i = 3; i >= 0; --i)
        file += static_cast<char>(v >> (8 * i));
    };
    file += static_cast<char>(0);
    file += static_cast<char>(version);
    put32(static_cast<u32>(header.size()));
    file += header + "FZRF";
    put32(3);
    put32(1);
    file += "FZRL";
    put32(static_cast<u32>(lineup.size()));
    file += lineup + std::string(2 * (2 * 6 + 4), '\0');
    WriteText(options.play_file, file);
    TestHost host;
    Init(host, options);
    const bool active = Active();
    Shutdown();
    return active;
  };
  CHECK(played(4, true, boot::CUSTOM_MACHINE));    // a race with a custom machine: version 4, its id in the lineup
  CHECK(!played(2, true, boot::CUSTOM_MACHINE));   // a custom machine is version 4 and no other
  CHECK(!played(3, true, boot::CUSTOM_MACHINE));
  CHECK(!played(4, false, 4));                     // version 4 names one
  CHECK(!played(2, false, boot::CUSTOM_MACHINE));  // the custom id for a seat that has none
  CHECK(!played(4, true, 0x33));                   // another custom id
  CHECK(played(2, false, 4));                      // a plain race as before
}
}  // namespace

void TestASeatsCustomMachine()
{
  BEGIN_TEST();
  {
    TestHost host;   // (the refusals' log lines go to it: a game with no race)
    Init(host, Options{});
    TheEntryInTheRaceBlock();
    APartOfAnotherKindIsRefused();
    const std::string pack = std::string(R"(, "stats": {"name": "P", "fingerprint": ")") +
                             stat_packs::THE_PACKS_FINGERPRINT + R"(", "values": ")" + stat_packs::THE_PACKS_VALUES + R"("})";
    CHECK(SetUpOf(PLAIN, pack).has_value());   // the pack alone is one the game takes
    // with a stat pack: the race takes both (the sum is made from the pack's part blocks, and held to a machine's
    // rows at the go: stat_pack.cpp CustomSumRefusal)
    const auto both = SetUpOf(Custom(R"("body": 3, "cockpit": 43, "booster": 58, "pilot": 6)"), pack);
    CHECK(both.has_value() && both->stats.has_value() && boot::IsCustom(both->race, 1));
    Shutdown();
  }
  ARecordingOfARaceWithOne(Temp());

}
