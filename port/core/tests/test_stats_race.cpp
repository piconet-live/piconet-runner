// Tests of a race's stat pack in the game's memory (stat_pack.cpp): laid over the car data by the frame tick before
// the race runs, left alone by a rollback, and checked when the race is let go. The car data is made up here
// (stat_packs.h). The pack's reader, and the pack in a recording: test_stats.cpp.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "stat_packs.h"

using namespace stat_packs;

namespace
{
// A race started on `stats` over a made-up car data: the frame tick `ticks` times.
void Start(TestHost& host, const Options& options, int ticks)
{
  host.log.clear();
  host.notices.clear();
  Init(host, options);
  Guest g(host, Data());
  LinkMainRel(g);
  LoadCarData(g);
  for (int i = 0; i < ticks; ++i)
    FrameTick();
}

// TestAStatPackIsLaidOverTheCarData's steps.

void ThePackInTheTable(TestHost& host, const std::string& dir)
{
  Guest g(host, Data());
  Start(host, ARace(dir, Stats(ThePack(), "GX Made Up")), 1);
  // every value of the pack, and not one other word: no name's address, nothing between the blocks
  CHECK(Active() && host.log.empty() && HoldsOnly(g, ThePack()));
  CHECK(Word(g, 6, 4) == Bits(2520.0f) && Word(g, 6, 0x48) == 0x03010000 && Word(g, 115, 0x34) == Bits(14.0f));
  std::vector<u8> loaded(game::CAR_DATA_SIZE);
  {
    TestHost other;
    Guest o(other, Data());
    LoadCarData(o);
    loaded = CarData(o);
  }
  const std::vector<u8> laid = CarData(g);
  u32 changed = 0;
  for (u32 at = 0; at < laid.size(); ++at)
    changed += laid[at] != loaded[at];
  CHECK(changed > 0 && changed <= 5 * 4);
  for (u32 block = 0; block < stats::BLOCKS; ++block)
    CHECK(std::memcmp(laid.data() + stats::BlockAt(block), loaded.data() + stats::BlockAt(block), 4) == 0);
  CHECK(Says(host.notices, "fzgx: stat pack GX Made Up (8472faeb): 5 of its 5 values written over the game's car data\n"));
  // a frame with nothing to write says nothing and writes nothing
  FrameTick();
  CHECK(Count(host.notices, "stat pack") == 1 && CarData(g) == laid);
  // only the words that differ: one the game put back is written again, alone
  g.Write32(TABLE + stats::BlockAt(6) + 4, MadeUp(6, 4));
  FrameTick();
  CHECK(HoldsOnly(g, ThePack()) && Says(host.notices, "1 of its 5 values written"));
  Shutdown();
  // a pack with a value the table already holds writes the others
  Entries half = ThePack();
  half[0].value = MadeUp(6, 4);
  Start(host, ARace(dir, Stats(half)), 2);
  CHECK(HoldsOnly(g, half) && Says(host.notices, "4 of its 5 values written") && Count(host.notices, "stat pack") == 1);
  Shutdown();
}

void NoTableAndNoPack(TestHost& host, const std::string& dir)
{
  Guest g(host, Data());
  // no pack: not one byte of the car data is touched, by the frames or by the go's check
  Start(host, ARace(dir, ""), 3);
  CHECK(Active() && HoldsOnly(g, {}) && !boot::StatsRefusal(g));
  CHECK(!Says(host.notices, "stat pack") && host.log.empty());
  Shutdown();
  // the car data not loaded yet (the frame tick runs from the boot's first frame), or memory that is not the
  // loader's table: nothing is written there, whatever it holds
  const Options race = ARace(dir, Stats(ThePack()));
  for (int what = 0; what < 5; ++what)
  {
    Start(host, race, 0);
    if (what == 0)
      std::fill(host.ram.begin() + (TABLE - layout::RAM), host.ram.begin() + (TABLE - layout::RAM) + game::CAR_DATA_SIZE, 0);
    if (what == 1)
      g.Write32(TABLE + stats::BlockAt(40), game::CAR_DATA_MAPPED + file::CAR_NAMES_AT + 1);   // the last machine's name is the first
    if (what == 2)
      g.Write32(TABLE + stats::BlockAt(3), g.Read32(TABLE + stats::BlockAt(2)));               // two machines, one name
    if (what == 3)
      g.Write32(TABLE + stats::BlockAt(0), game::CAR_DATA_MAPPED + file::CAR_PARTS_AT);        // a name past the names
    if (what == 4)
      g.Write32(TABLE + stats::BlockAt(77), game::CAR_DATA_MAPPED + file::CAR_PART_NAMES_AT + 4);
    const std::vector<u8> before = CarData(g);
    FrameTick();
    FrameTick();
    CHECK(CarData(g) == before && !Says(host.notices, "stat pack") && host.log.empty());
    // and a race that reaches its go so is refused
    const std::optional<std::string> refused = boot::StatsRefusal(g);
    CHECK(refused && Says(*refused, "the game's car data is not at 81554000 as its loader leaves it"));
    // once the loader has run, the next frame lays the pack
    LoadCarData(g);
    FrameTick();
    CHECK(HoldsOnly(g, ThePack()));
    Shutdown();
  }
  // a playback lays its recording's pack the same way: TestAStatPackInARecording
}

void OnceTheRaceRuns(TestHost& host, const std::string& dir)
{
  Guest g(host, Data());
  Start(host, ARace(dir, Stats(ThePack())), 1);
  // the race runs: the table is compared and never written; a table that lost a value is said, once, and the game
  // goes on (its machines are in the checksum: if they differ from the other games', that is a desync)
  host.stops = 0;
  g.Write32(layout::RUNNING_AT, 1);
  FrameTick();
  CHECK(host.stops == 0 && host.log.empty());
  g.Write32(TABLE + stats::BlockAt(6) + 4, MadeUp(6, 4));
  FrameTick();
  FrameTick();
  CHECK(host.stops == 0 && Word(g, 6, 4) == MadeUp(6, 4));
  CHECK(Count(host.log, "stat pack Made up: 1 of its values are not in the game's car data while the race runs") == 1);
  Shutdown();
  // a pack that could not be laid before the race ran is not laid after
  Start(host, ARace(dir, Stats(ThePack())), 0);
  g.Write32(layout::RUNNING_AT, 1);
  FrameTick();
  CHECK(HoldsOnly(g, {}) && host.stops == 0 && Says(host.log, "5 of its values are not in the game's car data"));
  g.Write32(layout::RUNNING_AT, 0);
  Shutdown();
}

// TestAStatPackUnderRollback's game: every step stores each racer's weight from the car data into its block again,
// as the game's does (fn_1_82EDC), and moves a word of the racer.
void Step(Guest& g, u32 racers)
{
  for (u32 i = 0; i < racers; ++i)
  {
    const u32 block = RACERS + i * game::MACHINE_SIZE;
    g.Write32(block + game::MACHINE_WEIGHT, Word(g, g.Read16(block + game::MACHINE_ID), 4));
    g.Write32(block + game::MACHINE_POSITION_X, g.Read32(block + game::MACHINE_POSITION_X) * 1664525 + g.Read32(block + game::MACHINE_WEIGHT));
  }
}
}  // namespace

void TestAStatPackIsLaidOverTheCarData()
{
  BEGIN_TEST();
  TestHost host;
  const std::string dir = Temp();
  ThePackInTheTable(host, dir);
  NoTableAndNoPack(host, dir);
  OnceTheRaceRuns(host, dir);
  std::filesystem::remove_all(dir);
}

// The car data is simulation memory the undo log covers (it lies below STATE_END). Laid before the undo log starts,
// the pack is what a rollback goes back to; a word written after it started is taken back, which is why nothing is
// written once the race runs. And two games on different tables differ in a word the checksum covers at once.
void TestAStatPackUnderRollback()
{
  BEGIN_TEST();
  CHECK(TABLE >= ((MODULE + net::DATA) & ~(net::PAGE - 1)) && TABLE + game::CAR_DATA_SIZE <= net::STATE_END);
  CHECK(net::Rollback::InChecksum(false, game::MACHINE_WEIGHT) && net::Rollback::InChecksum(false, game::MACHINE_ID));
  TestHost host;
  const std::string dir = Temp();
  Guest g(host, Data());
  Start(host, ARace(dir, Stats(ThePack())), 1);
  MakeRacers(g, {6, 12});
  for (u32 i = 0; i < 2; ++i)
    g.Write32(RACERS + i * game::MACHINE_SIZE + game::MACHINE_PROGRESS, RACERS + 0x4000 + i * 0x400);
  const std::vector<u8> laid = CarData(g);
  net::Rollback rollback;
  rollback.Start(g);
  std::vector<u32> sums;
  for (u32 t = 0; t < 12; ++t)
  {
    rollback.SaveStep(g, t, true);
    Step(g, 2);
    sums.push_back(net::Rollback::Checksum(g));
    FrameTick();   // the race does not run by the control block's word here: the tick would write, and has nothing to
  }
  rollback.SaveStep(g, 12, true);
  // a rollback over every step, and the steps again: the table as it was laid, the same race
  rollback.RestoreTo(g, 12, 2);
  CHECK(CarData(g) == laid && HoldsOnly(g, ThePack()));
  for (u32 t = 2; t < 12; ++t)
  {
    rollback.SaveStep(g, t, false);
    Step(g, 2);
    CHECK(net::Rollback::Checksum(g) == sums[t]);
  }
  // a word of the table written after the undo log started is taken back by a rollback before it
  rollback.SaveStep(g, 12, true);
  g.Write32(TABLE + stats::BlockAt(12) + 4, Bits(3000.0f));
  Step(g, 2);
  rollback.SaveStep(g, 13, true);
  CHECK(Word(g, 12, 4) == Bits(3000.0f));
  rollback.RestoreTo(g, 13, 12);
  CHECK(Word(g, 12, 4) == MadeUp(12, 4) && CarData(g) == laid);
  rollback.Stop();
  // the same steps on a table without the pack: another checksum from the first step on
  Shutdown();
  TestHost plain_host;
  Guest plain(plain_host, Data());
  Start(plain_host, ARace(dir, ""), 1);
  MakeRacers(plain, {6, 12});
  for (u32 i = 0; i < 2; ++i)
    plain.Write32(RACERS + i * game::MACHINE_SIZE + game::MACHINE_PROGRESS, RACERS + 0x4000 + i * 0x400);
  Step(plain, 2);
  CHECK(net::Rollback::Checksum(plain) != sums[0]);
  Shutdown();
  std::filesystem::remove_all(dir);
}

// When the race is let go: the table holds the pack, or the game stops before its first step. What the racers hold
// of it then is a line of the log and never a refusal: whether the game alters a racer's weight at some CPU level or
// machine setting is not known, and a refusal built into a game build could not be taken back. Through a
// playback's session, which takes its go as a race's does.
void TestAStatPackAtTheGo()
{
  BEGIN_TEST();
  TestHost host;
  const std::string dir = Temp();
  Guest g(host, Data());
  // 0: all is well; 1: a racer that holds another weight than its machine's (said, not refused); 2: a value gone
  // from the table (refused); 3: no racers made yet; 4: a custom machine among the racers (41 or more: not held
  // against the table); 5: a racer not filled yet (a weight of 0)
  for (int what = 0; what < 6; ++what)
  {
    WriteABundle(dir);
    WriteReplay(dir + "/race.fzrp", {0, 1}, Stats(ThePack(), "Go Pack"));
    std::string file;
    CHECK(ReadFileToString(dir + "/race.fzrp", file));
    file[5] = 3;
    WriteText(dir + "/race.fzrp", file);
    Options options;
    options.bundle_dir = dir;
    options.play_file = dir + "/race.fzrp";
    Start(host, options, 1);
    CHECK(Active() && host.log.empty() && HoldsOnly(g, ThePack()));
    if (what != 3)
      MakeRacers(g, what == 4 ? std::vector<u16>{6, 41, 12} : std::vector<u16>{6, 12, 40});
    if (what == 1)
      g.Write32(RACERS + game::MACHINE_WEIGHT, MadeUp(6, 4));
    if (what == 2)
      g.Write32(TABLE + stats::BlockAt(115) + 0x34, MadeUp(115, 0x34));
    if (what == 5)
      g.Write32(RACERS + 2 * game::MACHINE_SIZE + game::MACHINE_WEIGHT, 0);
    host.stops = 0;
    g.Write32(layout::READY_AT, 1);
    NetSync();
    const bool runs = what != 2;
    CHECK((g.Read32(layout::RUNNING_AT) == 1) == runs && (g.Read32(layout::GO_AT) == 1) == runs);
    CHECK(host.stops == (runs ? 0 : 1) && Count(host.log, "the race is refused; stopping") == (runs ? 0 : 1));
    if (runs)
      CHECK(Says(host.notices, std::format("stat pack Go Pack (8472faeb): its 5 values are in the game's car data; of {} racers {} "
                                           "hold their machines' weights from it and {} another",
                                           what == 3 ? 0 : 3, what == 0 ? 3 : what == 3 ? 0 : 2, what == 1 ? 1 : 0)));
    if (what == 1)
      CHECK(Says(host.notices, std::format("and 1 another (the first: racer 0 on machine 6 holds {:08X}, the table 451D8000)",
                                           MadeUp(6, 4))) && host.log.empty());
    if (what == 2)
      CHECK(Says(host.log, "fzgx: stat pack: 1 of the pack Go Pack's 5 values are not in the game's car data: the race is refused"));
    // a refused race stays refused: no later pass of the gate's wait lets it go
    if (!runs)
    {
      NetSync();
      CHECK(g.Read32(layout::RUNNING_AT) == 0 && g.Read32(layout::GO_AT) == 0 && host.stops == 1);
    }
    Shutdown();
    g.Write32(layout::READY_AT, 0);
    g.Write32(layout::RUNNING_AT, 0);
    g.Write32(layout::GO_AT, 0);
    g.Write32(MODULE + game::MACHINE_COUNT, 0);
  }
  std::filesystem::remove_all(dir);
}
