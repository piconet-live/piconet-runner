// Tests of what the core reads from files: a launch file's match, a replay's header and lineup, and the set-ups and files
// it refuses.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "harness.h"

namespace
{
// TestRaceFromAMatchFile's steps.

// A bundle and the launch file of a race in `dir`; gives the bundle's image.
std::string WriteARace(const std::string& dir)
{
  // a bundle: an image with its two magic words, a manifest, a code list with the two boot sections
  std::string image(0x100, '\0');
  image.replace(0, 4, "FZMD");
  image.replace(0x10, 4, "FZSI");
  WriteText(dir + "/mod.bin", image);
  WriteText(dir + "/manifest.json", "{\"commit\": \"abc1234\"}");
  WriteText(dir + "/codes.txt", "$Boot to race\n48000000 800030C8\n$Replay boot\n04000000 00000001\n$Race setup\n"
                                "C0000000 00000001\n4E800020 00000000\n");
  WriteText(dir + "/launch.json", R"({"about": "a test race", "widescreen": true, "bundle": ")" + dir + R"(", "match": {
    "match": {"matchId": "m7", "seat": 1, "token": "1f2e3d4c5b6a7988",
              "relay": {"host": "", "port": 47811},
              "setup": {"course": 8, "laps": 3, "shuffleSeed": 4660, "cpuSeed": 305419896,
                        "rules": {"cpuLevel": 2, "boostFromStart": true, "restore": false},
                        "seats": [
                          {"seat": 0, "machine": 5, "playerId": "p1", "name": "Tom & jerry!", "setting": 70,
                           "color": {"r": 255, "g": 102, "b": 0}},
                          {"seat": 1, "machine": 12, "playerId": "p2", "name": "abcdefghijklmnopqrstuvwxyz", "stockColor": 2},
                          {"seat": 2, "machine": 30, "cpu": true, "playerId": "bot", "name": "BOT"},
                          {"seat": 3, "machine": 7}]}},
    "relayHost": "127.0.0.1", "delay": 2, "overclock": 4.0, "otherSounds": "off"}})");
  return image;
}

void ARecordingsHeader(TestHost& host, const std::string& dir, const std::string& image)
{
  // a recording's header: the match without the relay's token, the mod image, the game, and what recorded it
  // (the host's engine and module; a host that says nothing leaves both out)
  const auto header_of = [&] {
    CHECK(boot::StartReplay(dir + "/rec.fzrp"));
    std::ifstream file(dir + "/rec.fzrp", std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  };
  std::string recorded = header_of();
  CHECK(recorded.compare(0, 4, "FZRP") == 0 && recorded.find("\"game\":\"GFZE01\"") != std::string::npos);
  CHECK(recorded.find("\"mod_sha1\":\"" + Sha1(reinterpret_cast<const u8*>(image.data()), image.size()) + "\"") != std::string::npos);
  CHECK(recorded.find("engine") == std::string::npos && recorded.find("module_sha1") == std::string::npos &&
        recorded.find("\"flow\":1") != std::string::npos);   // (a race of the game's flow: its playback keeps it)
  CHECK(recorded.find("1f2e3d4c5b6a7988") == std::string::npos);
  host.engine = "native";
  host.module = std::string(40, 'a');
  recorded = header_of();
  CHECK(recorded.find("\"engine\":\"native\"") != std::string::npos &&
        recorded.find("\"module_sha1\":\"" + std::string(40, 'a') + "\"") != std::string::npos);
  CHECK(recorded.compare(recorded.size() - 12, 4, "FZRF") == 0);
}

// The race as the frame tick writes it into the data window.
void TheRaceInTheWindow(TestHost& host, Guest& g)
{
  ModData& d = Data();
  using namespace layout;
  CHECK(d.Read(IMAGE, 4) == 0x465A4D44 && d.Read(SLOT, 4) == 0x465A5349);
  CHECK(d.Read(SLOT + 4, 4) == 1 && d.Read(SLOT + 36, 4) == 1 && d.Read(SLOT + 40, 4) == 0b111 && d.Read(SLOT + 44, 4) == 2);
  CHECK(d.Read(RACE, 4) == 0x465A5243 && d.Read(RACE + RACE_COURSE, 4) == 8 && d.Read(RACE + RACE_MACHINE0, 4) == 5);
  CHECK(d.Read(RACE + RACE_OPPONENTS, 4) == 3 && d.Read(RACE + RACE_LAPS, 4) == 3 && d.Read(RACE + RACE_TYPE, 4) == 6);
  CHECK(d.Read(RACE + RACE_SHUFFLE, 4) == 4660 && d.Read(RACE + RACE_CPUSEED, 4) == 305419896);
  CHECK(d.Read(RACE + RACE_MACHINES, 4) == 0x050C1E07);
  CHECK(d.Read(RACE + RACE_CPULEVEL, 4) == 2 && d.Read(RACE + RACE_BOOST, 4) == 1 && d.Read(RACE + RACE_RESTORE, 4) == 0);
  // (no end after the first finish: 0, as every race before the rule; a race from a match is the game's flow, with
  // no rubber band)
  CHECK(d.Read(RACE_FINISH_LIMIT_AT, 4) == 0 && d.Read(RACE + RACE_FLOW, 4) == 1 && d.Read(RACE + RACE_RUBBER_BAND_ON, 4) == 0);
  CHECK(d.Read(RACE + RACE_SETTINGS, 1) == 70 && d.Read(RACE + RACE_SETTINGS + 1, 1) == 50);
  // names as the game's font draws them: upper case, & stored as a comma, other marks dropped, 16 at most; none
  // for a CPU seat or a seat without a player
  char name[NAME_SIZE] = {};
  d.CopyFrom(name, NAMES, NAME_SIZE);
  CHECK(std::string(name) == "TOM , JERRY");
  d.CopyFrom(name, NAMES + NAME_SIZE, NAME_SIZE);
  CHECK(std::string(name) == "ABCDEFGHIJKLMNOP");
  CHECK(d.Read(NAMES + 2 * NAME_SIZE, 1) == 0 && d.Read(NAMES + 3 * NAME_SIZE, 1) == 0);
  // other racers' sounds off, boost from the start, and the game's 16:9 option
  CHECK(d.Read(CONTROL + 548, 4) == 1 && d.Read(CONTROL + 520, 4) == 1);
  CHECK(g.Read32(MODULE + 0x229630) == 1);
  CHECK(GameWidescreen() == std::optional<bool>(true));
  // nothing of it went into main memory
  CHECK(std::memcmp(host.ram.data() + 0x3100, std::vector<u8>(0x1000, 0).data(), 0x1000) == 0);
}

void TheGamesScreenOption(Guest& g)
{
  // the game's screen option is read from the game, race or no race: the player's own (0 Normal, 1 or 2 Wide
  // Screen); unknown before main.rel is loaded, when the module would not fit in memory, and for any other value
  g.Write32(MODULE + 0x229630, 0);
  CHECK(GameWidescreen() == std::optional<bool>(false));
  g.Write32(MODULE + 0x229630, 2);
  CHECK(GameWidescreen() == std::optional<bool>(true));
  FrameTick();
  CHECK(g.Read32(MODULE + 0x229630) == 2);
  g.Write32(MODULE + 0x229630, 0x3F800000);
  CHECK(!GameWidescreen());
  g.Write32(net::MODULE_POINTER, 0);
  CHECK(!GameWidescreen());
  g.Write32(net::MODULE_POINTER, 0x817FFFF0);
  CHECK(!GameWidescreen());
  // and while another module is at the head of the list (fze.sample.rel, number 0x12, is linked before main.rel)
  LinkMainRel(g);
  g.Write32(MODULE + 0x229630, 1);
  g.Write32(MODULE, 0x12);
  CHECK(!GameWidescreen());
  LinkMainRel(g);
  CHECK(GameWidescreen() == std::optional<bool>(true));
}
}  // namespace

void TestRaceFromAMatchFile()
{
  const std::string dir = Temp();
  const std::string image = WriteARace(dir);
  TestHost host;
  Options options;
  CHECK(!ReadLaunchFile(dir + "/launch.json", &options));
  Init(host, options);
  CHECK(Active());
  CHECK(host.log.empty());
  ARecordingsHeader(host, dir, image);
  const BootSettings boot = Boot();
  CHECK(boot.dual_core && boot.fixed_clock && boot.clock == 0x386D4380 && boot.cpu_clock == 4.0f && boot.widescreen);
  const std::vector<Code> codes = Codes();
  CHECK(codes.size() == 2 && codes[0].name == "Boot to race" && codes[1].name == "Race setup" &&
        codes[1].lines.size() == 2 && codes[1].lines[1].first == 0x4E800020);

  Guest g(host, Data());
  LinkMainRel(g);
  FrameTick();
  TheRaceInTheWindow(host, g);
  Shutdown();

  // no match file, no race
  Init(host, Options{});
  CHECK(!Active());
  TheGamesScreenOption(g);
  std::filesystem::remove_all(dir);
}

void TestAReplaysLineup()
{
  const std::string dir = Temp();
  std::string image(0x100, '\0');
  image.replace(0, 4, "FZMD");
  image.replace(0x10, 4, "FZSI");
  WriteText(dir + "/mod.bin", image);
  WriteText(dir + "/manifest.json", "{\"commit\": \"abc1234\"}");
  WriteText(dir + "/codes.txt", "$Boot to race\n48000000 800030C8\n$Replay boot\n04000000 00000001\n");
  using namespace layout;
  TestHost host;
  Options options;
  options.bundle_dir = dir;

  // a race of 30 as the fork recorded them: racer 0 started last, racer 1 first. It plays as the race: the race's
  // own boot (a fork bundle from before 0.3.0 still holds the game's replay mode's boot, which never runs), with the
  // lineup's machines in the race block and nothing of the replay mode's lineup (+64 to +256 stay 0)
  std::vector<u8> ranks(30);
  for (u8 i = 0; i < 30; ++i)
    ranks[i] = i == 0 ? 29 : i - 1;
  WriteReplay(dir + "/thirty.fzrp", ranks);
  options.play_file = dir + "/thirty.fzrp";
  Init(host, options);
  CHECK(Active() && host.log.empty());
  CHECK(Codes().size() == 1 && Codes()[0].name == "Boot to race");
  Guest g(host, Data());
  LinkMainRel(g);
  FrameTick();
  CHECK(Data().Read(RACE + RACE_OPPONENTS, 4) == 29 && Data().Read(SLOT + 4, 4) == 1 && Data().Read(SLOT + 40, 4) == 0x3FFFFFFF);
  CHECK(Data().Read(RACE + RACE_MACHINE0, 4) == 3 && Data().Read(RACE + RACE_MACHINES, 1) == 3 &&
        Data().Read(RACE + RACE_MACHINES + 29, 1) == 32);
  bool unused_zero = true;
  for (u32 at = 64; at < RACE_CPULEVEL; ++at)
    unused_zero = unused_zero && (Data().Read(RACE + at, 1) == 0 || (at >= RACE_RUBBER_BAND_ON && at < RACE_RUBBER_BAND_ON + 4));
  CHECK(unused_zero && Data().Read(RACE + RACE_RUBBER_BAND_ON, 4) == 1);   // a recording from before 0.4.0: the band
  // a replay from before the host's rules names no machine settings: its race ran with what 100 gives (a match
  // start that names none gets 50: TestRaceFromAMatchFile)
  CHECK(Data().Read(RACE + RACE_SETTINGS, 1) == 100 && Data().Read(RACE + RACE_SETTINGS + 29, 1) == 100);
  // nor an end after the first finish: the race plays as it was recorded, to its last machine
  CHECK(Data().Read(RACE_FINISH_LIMIT_AT, 4) == 0 && Data().Read(RACE + RACE_FLOW, 4) == 0);   // nor the flow
  Shutdown();

  // a race of two, recorded with the rules
  WriteReplay(dir + "/two.fzrp", {1, 0}, "\"rules\": {\"cpuLevel\": 3, \"finishLimitSeconds\": 90}, ");
  options.play_file = dir + "/two.fzrp";
  Init(host, options);
  CHECK(Active() && host.log.empty());
  FrameTick();
  CHECK(Data().Read(RACE + RACE_OPPONENTS, 4) == 1 && Data().Read(SLOT + 40, 4) == 3);
  CHECK(Data().Read(RACE + RACE_SETTINGS, 1) == 50);   // recorded with the rules: a seat naming no setting had 50
  CHECK(Data().Read(RACE_FINISH_LIMIT_AT, 4) == 5400);   // the rule's seconds, as the steps the image counts
  Shutdown();
  Init(host, Options{});
  std::filesystem::remove_all(dir);
}

// A replay this game does not play is refused when the game is set up, with the reason in the log: another game's,
// and a file that ends early (half a step, or no step). A race cut short ends on a whole step and plays.
void TestAReplayThatIsNotPlayed()
{
  const std::string dir = Temp();
  std::string image(0x100, '\0');
  image.replace(0, 4, "FZMD");
  image.replace(0x10, 4, "FZSI");
  WriteText(dir + "/mod.bin", image);
  WriteText(dir + "/manifest.json", "{\"commit\": \"abc1234\"}");
  WriteText(dir + "/codes.txt", "$Boot to race\n48000000 800030C8\n$Replay boot\n04000000 00000001\n");
  Options options;
  options.bundle_dir = dir;
  options.play_file = dir + "/r.fzrp";
  const auto played = [&](const std::string& more, size_t cut, const char* reason) {
    TestHost host;
    WriteReplay(options.play_file, {1, 0}, "", more, cut);
    Init(host, options);
    const bool active = Active();
    const bool said = reason ? host.log.find(reason) != std::string::npos : host.log.empty();
    Shutdown();
    if (!said)
      std::printf("log: %s\n", host.log.c_str());
    return active && said ? 1 : !active && said ? 0 : -1;
  };
  CHECK(played("", 0, nullptr) == 1);
  CHECK(played(", \"game\": \"GFZE01\"", 0, nullptr) == 1);
  CHECK(played(", \"game\": \"GFZJ01\"", 0, "another version of the game (GFZJ01)") == 0);
  CHECK(played("", 16, nullptr) == 1);                              // one whole step of two seats gone: cut short
  CHECK(played("", 5, "is incomplete") == 0);                       // half a step
  CHECK(played("", 32, "is incomplete") == 0);                      // no step at all
  CHECK(played("", 40, "no header or lineup") == 0);                // the lineup cut off
  TestHost host;
  Init(host, Options{});
  std::filesystem::remove_all(dir);
}

namespace
{
// A replay file from parts a test can make wrong one at a time: the header, the frame preamble's player seats and
// recorder's seat, the lineup, and the steps.
void WriteReplayOf(const std::string& path, const std::string& header, u32 mask, u32 seat, const std::string& lineup,
                   const std::string& steps, const std::string& after = "")
{
  std::string file = "FZRP";
  const auto put32 = [&](u32 v) {
    for (int i = 3; i >= 0; --i)
      file += static_cast<char>(v >> (8 * i));
  };
  file += std::string("\0\2", 2);
  put32(static_cast<u32>(header.size()));
  file += header + "FZRF";
  put32(mask);
  put32(seat);
  file += "FZRL";
  put32(static_cast<u32>(lineup.size()));
  file += lineup + steps + after;
  WriteText(path, file);
}
}  // namespace

namespace
{
// TestASetUpTheGameDoesNotHave's steps.

// A replay's numbers, of which a test makes one wrong.
struct SetUp
{
  std::string course = "7", laps = "3", machine1 = "4", seats_more = "", header_seat = "1", header_mask = "3", delay = "2";
  u32 mask = 3, seat = 1;
  u8 lineup_machine1 = 4;
  u8 lineup_slot7 = 0, lineup_slot29 = 0;   // slots after the race's two racers: 0 in every recording that was written
  bool no_seats = false;
};

// 1: set up; 0: refused with this reason; -1: neither
int Played(const Options& options, const SetUp& f, const char* reason)
{
  const std::string seats = f.no_seats ? "" :
      "{\"seat\": 0, \"machine\": 3, \"playerId\": \"p0\", \"name\": \"P0\"}, {\"seat\": 1, \"machine\": " + f.machine1 +
      ", \"playerId\": \"p1\", \"name\": \"P1\"" + f.seats_more + "}";
  const std::string header = "{\"match\": {\"matchId\": \"m9\", \"setup\": {\"course\": " + f.course + ", \"laps\": " + f.laps +
                             ", \"seats\": [" + seats + "]}}, \"seat\": " + f.header_seat + ", \"mask\": " + f.header_mask +
                             ", \"delay\": " + f.delay + "}";
  std::string lineup(124, '\0');
  lineup[0] = 3;
  lineup[1] = static_cast<char>(f.lineup_machine1);
  lineup[7] = static_cast<char>(f.lineup_slot7);
  lineup[29] = static_cast<char>(f.lineup_slot29);
  lineup[30] = 1;
  WriteReplayOf(options.play_file, header, f.mask, f.seat, lineup, std::string(2 * (std::popcount(f.mask) * 6 + 4), '\0'));
  TestHost host;
  Init(host, options);
  const bool active = Active();
  const bool said = reason ? host.log.find(reason) != std::string::npos : host.log.empty();
  Shutdown();
  if (!said)
    std::printf("log: %s\n", host.log.c_str());
  return active && said ? 1 : !active && said ? 0 : -1;
}

void AReplaysNumbers(const Options& options)
{
  const auto played = [&](const SetUp& f, const char* reason) { return Played(options, f, reason); };
  CHECK(played({}, nullptr) == 1);
  // machines: the game has 41 (0 to 40)
  CHECK(played({.machine1 = "40", .lineup_machine1 = 40}, nullptr) == 1);
  CHECK(played({.machine1 = "41"}, "a machine the game does not have") == 0);
  CHECK(played({.machine1 = "255"}, "a machine the game does not have") == 0);
  CHECK(played({.machine1 = "-1"}, "a machine the game does not have") == 0);
  CHECK(played({.machine1 = "4.5"}, "a machine the game does not have") == 0);
  CHECK(played({.machine1 = "\"4\""}, "a machine the game does not have") == 0);
  CHECK(played({.lineup_machine1 = 41}, "has a machine the game does not have (41)") == 0);
  CHECK(played({.lineup_machine1 = 255}, "has a machine the game does not have (255)") == 0);
  // and in the 28 slots after this race's two racers, which go into the race block all the same: a recording holds
  // 0 there (the race set up above), a machine's id is still one, and a custom machine's id is refused with the slot
  CHECK(played({.lineup_slot7 = 40, .lineup_slot29 = 40}, nullptr) == 1);
  CHECK(played({.lineup_slot7 = 41}, "the lineup's slot without a racer, 7 has a machine the game does not have (41)") == 0);
  CHECK(played({.lineup_slot29 = 255}, "the lineup's slot without a racer, 29 has a machine the game does not have (255)") == 0);
  CHECK(played({.lineup_machine1 = 41, .lineup_slot7 = 41}, "the lineup's racer 1 has a machine the game does not have (41)") == 0);
  // courses: the stage numbers the disc holds
  CHECK(played({.course = "50"}, nullptr) == 1);
  CHECK(played({.course = "2"}, "a course the disc does not hold") == 0);
  CHECK(played({.course = "0"}, "a course the disc does not hold") == 0);
  CHECK(played({.course = "4294967297"}, "a course the disc does not hold") == 0);
  CHECK(played({.course = "1e300"}, "a course the disc does not hold") == 0);
  // a number too large to be one, anywhere in the header: refused before the parser, which ends the program on it
  CHECK(played({.course = "1e999"}, "no header or lineup") == 0);
  CHECK(played({.seats_more = ", \"setting\": -1e999"}, "no header or lineup") == 0);
  // laps
  CHECK(played({.laps = "0"}, nullptr) == 1);
  CHECK(played({.laps = "99"}, nullptr) == 1);
  CHECK(played({.laps = "100"}, "laps above 99") == 0);
  CHECK(played({.laps = "-3"}, "laps above 99") == 0);
  CHECK(played({.no_seats = true}, "no seats") == 0);
  // the header's own numbers: the seat the game's routines take as the local one, the player seats, the delay
  CHECK(played({.header_seat = "29"}, nullptr) == 1);
  CHECK(played({.header_seat = "30"}, "not a number in its range") == 0);
  CHECK(played({.header_seat = "-1"}, "not a number in its range") == 0);
  CHECK(played({.header_seat = "1e30"}, "not a number in its range") == 0);
  CHECK(played({.header_mask = "1073741824"}, "not a number in its range") == 0);   // seat 30
  CHECK(played({.header_mask = "-1"}, "not a number in its range") == 0);
  CHECK(played({.header_mask = "1073741823"}, nullptr) == 1);   // seats 0 to 29, every one
  CHECK(played({.header_mask = "3.5"}, "not a number in its range") == 0);
  CHECK(played({.delay = "60"}, nullptr) == 1);
  CHECK(played({.delay = "61"}, "not a number in its range") == 0);
  CHECK(played({.delay = "-1"}, "not a number in its range") == 0);
  CHECK(played({.delay = "2.5"}, "not a number in its range") == 0);
  CHECK(played({.delay = "4000000000"}, "not a number in its range") == 0);
  // the frame preamble: the session's player seats and the recorder's seat
  CHECK(played({.mask = 0x40000003}, "names a seat the game does not have") == 0);
  CHECK(played({.mask = 0x80000003}, "names a seat the game does not have") == 0);
  CHECK(played({.seat = 30}, "names a seat the game does not have") == 0);
  CHECK(played({.seat = 200}, "names a seat the game does not have") == 0);
  CHECK(played({.seat = 0xFFFFFFFF}, "names a seat the game does not have") == 0);
  CHECK(played({.seat = 29}, nullptr) == 1);   // a seat of the game that did not play: the file's own business
  // a colour is a look, not a place in memory: any number is a shade
  CHECK(played({.seats_more = ", \"color\": {\"r\": 1e30, \"g\": -5, \"b\": 128.7}"}, nullptr) == 1);
}

void AHeaderThatOpensBracketsWithoutEnd(const Options& options)
{
  // a header that opens brackets without end (finding L4): the parser took a call for each, to the end of the stack.
  // Refused before it is parsed; brackets inside text are text
  for (const bool deep : {true, false})
  {
    const std::string nest = deep ? std::string(200000, '[') + std::string(200000, ']') : "\"" + std::string(200000, '[') + "\"";
    const std::string header = "{\"match\": {\"matchId\": \"m9\", \"setup\": {\"course\": 7, \"laps\": 3, \"seats\": [{\"seat\": 0, "
                               "\"machine\": 3, \"playerId\": \"p0\", \"name\": \"P0\"}]}}, \"seat\": 0, \"mask\": 1, \"x\": " + nest + "}";
    std::string lineup(124, '\0');
    lineup[0] = 3;
    WriteReplayOf(options.play_file, header, 1, 0, lineup, std::string(2 * 10, '\0'));
    TestHost host;
    Init(host, options);
    CHECK(Active() == !deep);
    CHECK(deep ? host.log.find("no header or lineup") != std::string::npos : host.log.empty());
    Shutdown();
  }
}

void TheSessionsReaderOfTheSameFile(const std::string& dir)
{
  // the session's reader of the same file refuses the same seats, and a results block whose length is no length
  // does not move the steps
  TestHost host;
  Init(host, Options{});
  net::Recording back;
  const std::string header = "{\"match\": {}}";
  const std::string steps(3 * 16, '\x11');
  WriteReplayOf(dir + "/s.fzrp", header, 3, 1, std::string(124, '\0'), steps);
  CHECK(net::ReadRecording(dir + "/s.fzrp", &back) && back.rows.size() == 3 && back.seat == 1 && back.index == 1);
  for (const auto& [mask, seat] : {std::pair<u32, u32>{0x40000001, 0}, {0xC0000000, 0}, {0, 0}, {3, 30}, {3, 0x80000000}, {3, 0xFFFFFFFF}})
  {
    WriteReplayOf(dir + "/s.fzrp", header, mask, seat, std::string(124, '\0'), steps);
    CHECK(!net::ReadRecording(dir + "/s.fzrp", &back));
  }
  CHECK(host.log.find("names a seat the game does not have") != std::string::npos);
  for (const u32 length : {0xFFFFFFFCu, 0xFFFFFFF8u, 0x7FFFFFFFu})
  {
    std::string after;
    for (int i = 3; i >= 0; --i)
      after += static_cast<char>(length >> (8 * i));
    WriteReplayOf(dir + "/s.fzrp", header, 3, 1, std::string(124, '\0'), steps + std::string(8, '\x22'), after + "FZRS");
    CHECK(net::ReadRecording(dir + "/s.fzrp", &back) && back.rows.size() == 4);   // 48 + 8 + 8 bytes: four steps
  }
  Shutdown();
}

void ALaunchFilesMatch(const std::string& dir)
{
  // a launch file's match: the same set-up rules, and this player's seat and the relay's port. What the launch
  // file's reader refuses comes back as its sentence; what the match's reader refuses is in the log
  std::string token = "1f2e3d4c5b6a7988", relay_host = "127.0.0.1", overclock = "4.0";
  float clock = 0;
  const auto raced = [&](const std::string& seat, const std::string& machine, const std::string& course, const std::string& port,
                         const char* reason) {
    WriteText(dir + "/launch.json",
              "{\"about\": \"a test race\", \"bundle\": \"" + dir + "\", \"match\": {\"match\": {\"matchId\": \"m7\", \"seat\": " + seat + ", \"token\": \"" + token + "\", \"relay\": {\"host\": \"\", "
              "\"port\": " + port + "}, \"setup\": {\"course\": " + course + ", \"laps\": 3, \"seats\": [{\"seat\": 0, \"machine\": " +
              machine + ", \"playerId\": \"p1\", \"name\": \"A\"}]}}, \"relayHost\": \"" + relay_host + "\", \"delay\": 2, "
              "\"overclock\": " + overclock + "}}");
    Options o;
    TestHost host;
    const std::optional<std::string> refused = ReadLaunchFile(dir + "/launch.json", &o);
    if (refused)
      host.log = *refused;
    else
      Init(host, o);
    const bool active = !refused && Active();
    clock = refused ? 0 : Boot().cpu_clock;
    const bool said = reason ? host.log.find(reason) != std::string::npos : host.log.empty();
    Shutdown();
    if (!said)
      std::printf("log: %s\n", host.log.c_str());
    return active && said ? 1 : !active && said ? 0 : -1;
  };
  CHECK(raced("0", "5", "8", "47811", nullptr) == 1 && clock == 4.0f);
  // a relay to join: a host, a port and a token (finding L7: without one the game raced alone)
  CHECK(raced("0", "5", "8", "0", "names no relay to join") == 0);
  for (const char* none : {"", "0", "xyz", "1f2e3d4c5b6a79881"})
  {
    token = none;
    CHECK(raced("0", "5", "8", "47811", "names no relay to join") == 0);
  }
  token = "1f2e3d4c5b6a7988";
  relay_host = "";
  CHECK(raced("0", "5", "8", "47811", "names no relay to join") == 0);
  relay_host = "127.0.0.1";
  // the emulated CPU's clock factor: one that is none (not from 1 to 16, or no number) is refused by its name
  for (const char* none : {"0", "-4", "1e30", "0.001", "17", "\"fast\""})
  {
    overclock = none;
    CHECK(raced("0", "5", "8", "47811", "\"match.overclock\" is not a number from 1 to 16") == 0);
  }
  overclock = "4.0";
  CHECK(raced("30", "5", "8", "47811", "not a number in its range") == 0);
  CHECK(raced("-1", "5", "8", "47811", "not a number in its range") == 0);
  CHECK(raced("0", "5", "8", "70000", "not a number in its range") == 0);
  CHECK(raced("0", "99", "8", "47811", "a machine the game does not have") == 0);
  CHECK(raced("0", "5", "6", "47811", "a course the disc does not hold") == 0);
}
}  // namespace


// The security audit's finding H5: a match is the coordinator's word and a
// replay is a file somebody sent, and their numbers went to the game as they came: a machine or a course the disc
// does not hold, a seat outside the race, a recorder's seat the game's cameras were then pointed at. A set-up the
// game does not have is refused when the game is set up, with the reason in the log; the same file with the one
// number put right plays.
void TestASetUpTheGameDoesNotHave()
{
  BEGIN_TEST();
  const std::string dir = Temp();
  std::string image(0x100, '\0');
  image.replace(0, 4, "FZMD");
  image.replace(0x10, 4, "FZSI");
  WriteText(dir + "/mod.bin", image);
  WriteText(dir + "/manifest.json", "{\"commit\": \"abc1234\"}");
  WriteText(dir + "/codes.txt", "$Boot to race\n48000000 800030C8\n$Replay boot\n04000000 00000001\n");
  Options options;
  options.bundle_dir = dir;
  options.play_file = dir + "/r.fzrp";
  AReplaysNumbers(options);
  AHeaderThatOpensBracketsWithoutEnd(options);
  TheSessionsReaderOfTheSameFile(dir);
  ALaunchFilesMatch(dir);
  TestHost host;
  Init(host, Options{});
  std::filesystem::remove_all(dir);
}
