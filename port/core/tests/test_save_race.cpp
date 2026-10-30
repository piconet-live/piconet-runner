// Tests of the player's controller options in a race: the frame tick that writes them into the game (boot.cpp),
// from a save made here (save_files.h). The save file's reader: test_save.cpp.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "save_files.h"

using namespace save_files;

namespace
{
// The six blocks in the game's memory, as one set of options.
constexpr u32 AT_BINDINGS = MODULE + net::BSS + 0xAA0, AT_GATE = MODULE + net::DATA + 0x2AC0;
constexpr u32 AT_DEAD = MODULE + net::DATA + 0x2B40, AT_RANGE = MODULE + net::DATA + 0x2B50;
constexpr u32 AT_PLAY = MODULE + net::DATA + 0x2B58, AT_SCHEME = MODULE + net::DATA + 0x2B60;

void Hold(Guest& g, const ControllerOptions& o)
{
  g.CopyTo(AT_BINDINGS, o.bindings.data(), o.bindings.size());
  g.CopyTo(AT_GATE, o.stick_gate.data(), o.stick_gate.size());
  g.CopyTo(AT_DEAD, o.dead_zones.data(), o.dead_zones.size());
  g.CopyTo(AT_RANGE, o.wheel_range.data(), o.wheel_range.size());
  g.CopyTo(AT_PLAY, o.wheel_play.data(), o.wheel_play.size());
  g.CopyTo(AT_SCHEME, o.scheme.data(), o.scheme.size());
}

ControllerOptions Held(const Guest& g)
{
  ControllerOptions o;
  g.CopyFrom(o.bindings.data(), AT_BINDINGS, o.bindings.size());
  g.CopyFrom(o.stick_gate.data(), AT_GATE, o.stick_gate.size());
  g.CopyFrom(o.dead_zones.data(), AT_DEAD, o.dead_zones.size());
  g.CopyFrom(o.wheel_range.data(), AT_RANGE, o.wheel_range.size());
  g.CopyFrom(o.wheel_play.data(), AT_PLAY, o.wheel_play.size());
  g.CopyFrom(o.scheme.data(), AT_SCHEME, o.scheme.size());
  return o;
}

// What a race holds of a player's save over a game that held Options0: the standard controller's three blocks are
// the player's, and the steering wheel's three (its range, its play, the scheme) stay the game's.
ControllerOptions Raced(const ControllerOptions& players)
{
  ControllerOptions o = Options0();
  o.bindings = players.bindings;
  o.stick_gate = players.stick_gate;
  o.dead_zones = players.dead_zones;
  return o;
}

// A bundle and the launch file of a race in `dir`.
void WriteARace(const std::string& dir)
{
  std::string image(0x100, '\0');
  image.replace(0, 4, "FZMD");
  image.replace(0x10, 4, "FZSI");
  WriteText(dir + "/mod.bin", image);
  WriteText(dir + "/manifest.json", "{\"commit\": \"abc1234\"}");
  WriteText(dir + "/codes.txt", "$Boot to race\n48000000 800030C8\n$Replay boot\n04000000 00000001\n$Race setup\n"
                                "C0000000 00000001\n4E800020 00000000\n");
  WriteText(dir + "/launch.json", R"({"about": "a test race", "bundle": ")" + dir + R"(", "match": {
    "match": {"matchId": "m7", "seat": 0, "token": "1f2e3d4c5b6a7988", "relay": {"host": "", "port": 47811},
              "setup": {"course": 8, "laps": 3, "seats": [{"seat": 0, "machine": 5, "playerId": "p1", "name": "A"},
                                                          {"seat": 1, "machine": 12, "playerId": "p2", "name": "B"}]}},
    "relayHost": "127.0.0.1"}})");
}

// A start from `options` on a game that holds Options0: the frame tick `ticks` times. Gives the host's notices.
std::string Start(TestHost& host, const Options& options, int ticks)
{
  host.notices.clear();
  host.log.clear();
  Init(host, options);
  Guest g(host, Data());
  LinkMainRel(g);
  Hold(g, Options0());
  for (int i = 0; i < ticks; ++i)
    FrameTick();
  return host.notices;
}

// TestARaceTakesThePlayersControllerOptions' steps.

// The player's save in the host's folder: written where the game holds its own, again after the game's reset, and
// nothing beside the six blocks.
void APlayersSave(TestHost& host, const Options& race, const std::string& card)
{
  WriteBytes(card + "/" + boot::SaveFileName(), SaveOf(Options1()));
  Guest g(host, Data());
  // the bytes before and after the blocks, which the writes must leave: the button settings; the calibration (the
  // second stick's follows it, which no save holds); the dead zones (the second stick's follow); the wheel's range,
  // its play and the scheme, which stand in a row
  const std::array<u32, 8> beside{AT_BINDINGS - 1, AT_BINDINGS + 0x150, AT_GATE - 1, AT_GATE + 0x40,
                                  AT_DEAD - 1,     AT_DEAD + 8,         AT_RANGE - 1, AT_SCHEME + 4};
  Init(host, race);
  for (const u32 at : beside)
    g.Write8(at, 0xA5);
  std::string said = Start(host, race, 1);
  CHECK(Active() && host.log.empty());
  CHECK(Held(g) == Raced(Options1()));
  // (by the file's name alone: the folder's path holds the name of the player's account)
  CHECK(Says(said, "controller options: the player's own, from 8P-GFZE-f_zero.dat.gci"));
  CHECK(!Says(said, card));
  CHECK(Says(said, "controller options written where the game held others: button settings, stick calibration, dead zones\n"));
  // the steering wheel's blocks are the save's too, and are left as the game holds them: a wheel is not for a race
  CHECK(Options1().scheme != Options0().scheme && Options1().wheel_range != Options0().wheel_range &&
        Options1().wheel_play != Options0().wheel_play && Held(g).scheme == Options0().scheme);
  bool left = true;
  for (const u32 at : beside)
    left = left && g.Read8(at) == 0xA5;
  CHECK(left);
  // a frame with nothing to write says nothing
  FrameTick();
  CHECK(Count(host.notices, "controller options") == 2);
  // the title module's exit puts the stock button settings back: written again, and said
  const ControllerOptions stock = Options0();
  g.CopyTo(AT_BINDINGS, stock.bindings.data(), stock.bindings.size());
  FrameTick();
  CHECK(Held(g) == Raced(Options1()));
  CHECK(Says(host.notices, "controller options written where the game held others: button settings\n"));
  // a game that kept putting its own back gets four lines, not one a frame
  for (int i = 0; i < 10; ++i)
  {
    g.Write8(AT_DEAD, 8);
    FrameTick();
    CHECK(g.Read8(AT_DEAD) == 2);
  }
  CHECK(Count(host.notices, "controller options written") == 4 && Says(host.notices, "dead zones (no further lines)\n"));
  CHECK(Count(host.notices, "controller options: the player's own") == 1);   // the save is read once
  // main.rel not linked yet: nothing is written anywhere
  g.Write8(AT_DEAD, 8);
  g.Write32(net::MODULE_POINTER, 0);
  FrameTick();
  LinkMainRel(g);
  CHECK(g.Read8(AT_DEAD) == 8);
  // another module at the head of the list (the loader links fze.sample.rel, number 0x12, before main.rel): its
  // header is not taken for main.rel's, and neither the options nor the screen option are written after it
  g.Write32(MODULE, 0x12);
  g.Write32(MODULE + 0x229630, 0);
  FrameTick();
  CHECK(g.Read8(AT_DEAD) == 8 && g.Read32(MODULE + 0x229630) == 0 && !GameWidescreen());
  LinkMainRel(g);
  FrameTick();
  CHECK(g.Read8(AT_DEAD) == 2 && g.Read32(MODULE + 0x229630) == 1 && GameWidescreen() == std::optional<bool>(true));
  Shutdown();
}

// A save that is refused, or none: the race starts all the same, on what the game holds, with one line that says
// which file and why.
void ASaveThatIsNotTaken(TestHost& host, const Options& race, const std::string& card)
{
  Guest g(host, Data());
  ControllerOptions bad = Options1();
  bad.dead_zones[0] = 100;   // the input layer would divide by 100 - 100
  WriteBytes(card + "/" + boot::SaveFileName(), SaveOf(bad));
  std::string said = Start(host, race, 3);
  CHECK(Active() && host.log.empty());
  CHECK(Held(g) == Options0());
  CHECK(Count(said, "controller options") == 1);
  CHECK(Says(said, "controller options: the game's defaults, the player's are not loaded: "
                   "8P-GFZE-f_zero.dat.gci: port 1's dead zone is 100 (the game sets 0 to 50)\n") && !Says(said, card));
  Shutdown();

  std::filesystem::remove(card + "/" + boot::SaveFileName());
  said = Start(host, race, 3);
  CHECK(Active() && host.log.empty() && Held(g) == Options0());
  CHECK(Count(said, "controller options") == 1 && Says(said, "not loaded: no save file of F-Zero GX in the card's folder\n") && !Says(said, card));
  Shutdown();

  // a host with no folder of saves
  const std::string folder = host.card_folder;
  host.card_folder.clear();
  said = Start(host, race, 3);
  CHECK(Active() && host.log.empty() && Held(g) == Options0());
  CHECK(Count(said, "controller options") == 1 && Says(said, "not loaded: the memory card is not a folder of save files here\n"));
  Shutdown();
  host.card_folder = folder;
}

// A replay's playback and a start that is no race: the save is not even read.
void APlaybackTakesNone(TestHost& host, const std::string& dir, const std::string& card)
{
  WriteBytes(card + "/" + boot::SaveFileName(), SaveOf(Options1()));
  Guest g(host, Data());
  WriteReplay(dir + "/two.fzrp", {1, 0});
  Options playback;
  playback.bundle_dir = dir;
  playback.play_file = dir + "/two.fzrp";
  {
    const std::string said = Start(host, playback, 3);
    CHECK(Active() && host.log.empty());
    CHECK(Held(g) == Options0() && Count(said, "controller options") == 0);
    Shutdown();
  }
  const std::string said = Start(host, Options{}, 3);
  CHECK(!Active() && Held(g) == Options0() && Count(said, "controller options") == 0);
}
}  // namespace

void TestARaceTakesThePlayersControllerOptions()
{
  const std::string dir = Temp();
  const std::string card = dir + "/Card A";
  std::filesystem::create_directories(card);
  WriteARace(dir);
  TestHost host;
  host.card_folder = card;
  Options race;
  CHECK(!ReadLaunchFile(dir + "/launch.json", &race));
  APlayersSave(host, race, card);
  ASaveThatIsNotTaken(host, race, card);
  APlaybackTakesNone(host, dir, card);
  Init(host, Options{});
  std::filesystem::remove_all(dir);
}

// The player's camera view ("match.camera", boot.cpp WriteStartView): written into the game's options record, where
// the race start's camera set-up takes it, every frame until the relay's go and never after; not in a playback, and
// not without a view given.
void TestARaceStartsInThePlayersView()
{
  const std::string dir = Temp();
  WriteARace(dir);
  TestHost host;
  Options race;
  CHECK(!ReadLaunchFile(dir + "/launch.json", &race));
  race.camera = 3;
  Guest g(host, Data());
  constexpr u32 RECORD = 0x80700000, VIEW = RECORD + game::SAVED_VIEW;
  const auto start = [&](const Options& o) {
    Init(host, o);
    LinkMainRel(g);
    g.Write32(table::RUNNING_AT, 0);
    g.Write32(MODULE + game::GAME_OPTIONS, 0);
    FrameTick();   // no record yet: nothing written
    g.Write32(MODULE + game::GAME_OPTIONS, RECORD);
    g.Write8(VIEW, 1);
    g.Write8(VIEW + 1, 0xA5);
    FrameTick();
  };
  start(race);
  CHECK(g.Read8(VIEW) == 3 && g.Read8(VIEW - 1) == 0 && g.Read8(VIEW + 1) == 0xA5);
  g.Write8(VIEW, 1);   // the game's own put back (the title's defaults): written again
  FrameTick();
  CHECK(g.Read8(VIEW) == 3);
  g.Write32(table::RUNNING_AT, 1);   // the go: from here the game's own
  g.Write8(VIEW, 2);
  FrameTick();
  CHECK(g.Read8(VIEW) == 2);
  Shutdown();
  race.camera = 0;     // none given
  start(race);
  CHECK(g.Read8(VIEW) == 1);
  Shutdown();
  WriteReplay(dir + "/two.fzrp", {1, 0});
  Options playback;
  playback.bundle_dir = dir;
  playback.play_file = dir + "/two.fzrp";
  playback.camera = 3;  // (no launch file gives a playback one; the playback's view is the game's all the same)
  start(playback);
  CHECK(g.Read8(VIEW) == 1);
  Shutdown();
  Init(host, Options{});
  std::filesystem::remove_all(dir);
}

// The blocks a race writes, under rollback and the checksum: a rollback puts back what they held before the step it
// goes back to, whoever changed them since (the undo log covers main.rel's .data and .bss whole), and no byte of
// them is in the state checksum, so two games with other options keep equal checksums.
void TestThePlayersOptionsUnderRollback()
{
  TestHost host;
  Init(host, Options{});   // the core's log goes to this test's host
  ModData data;
  Guest g(host, data);
  std::mt19937 rng(1010);
  FakeGame(host, g, rng);
  const u32 machines = 0x80C00000, records = 0x80D00000;
  g.Write32(MODULE + net::MACHINES, machines);
  g.Write8(MODULE + net::MACHINE_COUNT, 30);
  for (u32 i = 0; i < 30; ++i)
    g.Write32(machines + i * 0x620 + 0x49C, records + i * 0x200);
  Hold(g, Options0());
  const u32 sum = net::StateChecksum(g, MODULE);
  net::Rollback rollback;
  rollback.Start(g);
  // 30 steps: the player's options come at step 10 (the race's first frame writes them), and at step 20 something
  // puts the game's own back (as the title module's exit does once)
  for (u32 step = 0; step < 30; ++step)
  {
    rollback.SaveStep(g, step, true);
    if (step == 10)
      Hold(g, Options1());
    if (step == 20)
      Hold(g, Options0());
    g.Write32(MODULE + net::BSS + 0x4000 + step * 4, step);   // a step's own work elsewhere
  }
  rollback.SaveStep(g, 30, true);
  CHECK(Held(g) == Options0());
  // outside the checksum: it is the same with either set of options, and with any byte of the blocks changed
  CHECK(net::StateChecksum(g, MODULE) == sum);
  Hold(g, Options1());
  CHECK(net::StateChecksum(g, MODULE) == sum);
  for (const u32 at : {AT_BINDINGS, AT_BINDINGS + 0x14F, AT_GATE, AT_GATE + 0x3F, AT_DEAD, AT_DEAD + 7, AT_RANGE, AT_PLAY, AT_SCHEME + 3})
  {
    g.Write8(at, static_cast<u8>(g.Read8(at) ^ 0x55));
    CHECK(net::StateChecksum(g, MODULE) == sum);
  }
  Hold(g, Options0());   // as step 30 found them
  // back to before step 20: the player's, as steps 10 to 19 held them; and to before step 10: the game's own
  rollback.RestoreTo(g, 30, 20);
  CHECK(Held(g) == Options1());
  rollback.RestoreTo(g, 20, 5);
  CHECK(Held(g) == Options0());
  CHECK(net::StateChecksum(g, MODULE) == sum);
  rollback.Stop();
}
