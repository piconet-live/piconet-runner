// The game mode's reader and interpreter (mode.h): the protocol's vectors (protocol/tests/vectors/modes/, the
// compiler's own packages) read as the match start carries them, and Overdrive run on a fake race.
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <bit>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "files.h"
#include "harness.h"
#include "mode.h"
#include "parts.h"
#include "rollback.h"

namespace
{
picojson::value Vector(const char* name)
{
  std::ifstream file(ProtocolFile("protocol/tests/vectors/modes") / name);
  std::stringstream text;
  text << file.rdbuf();
  picojson::value v;
  picojson::parse(v, text.str());
  return v;
}

std::optional<mode::Package> ReadWith(const char* name, const char* key, const picojson::value& value)
{
  picojson::value v = Vector(name);
  v.get<picojson::object>()[key] = value;
  std::string why;
  return mode::Read(v, &why);
}

// A race of `count` machines in a game's memory: main.rel linked, the machines' blocks and progress records, the
// race phase, the clock at `steps`; the mode's block at BLOCK.
constexpr u32 MACHINES = 0x80400000, PROGRESS = 0x80420000, BLOCK = 0x80500000;
void FakeModeRace(Guest& g, u32 count, u32 steps)
{
  LinkMainRel(g);
  g.Write32(MODULE + net::MACHINES, MACHINES);
  g.Write8(MODULE + net::MACHINE_COUNT, static_cast<u8>(count));
  g.Write16(MODULE + game::PHASE, game::PHASE_RACE);
  g.Write32(MODULE + game::RACE_CLOCK, steps);
  for (u32 i = 0; i < count; ++i)
  {
    const u32 m = MACHINES + i * game::MACHINE_SIZE;
    g.Write32(m + game::MACHINE_PROGRESS, PROGRESS + i * game::PROGRESS_SIZE);
    g.Write32(m + game::MACHINE_SPEED, std::bit_cast<u32>(400.0f));
  }
}

std::vector<u8> BlockOf(Guest& g)
{
  std::vector<u8> bytes(mode::BLOCK);
  g.CopyFrom(bytes.data(), BLOCK, bytes.size());
  return bytes;
}
}  // namespace

// The vectors read; a package that is not the hash's, a parameter outside its range, or a newer api is refused.
void TestTheModeVectorsRead()
{
  BEGIN_TEST();
  std::string why;
  const std::optional<mode::Package> overdrive = mode::Read(Vector("overdrive.mode.json"), &why);
  CHECK(overdrive.has_value());
  if (!overdrive)
    return;
  CHECK(overdrive->name == "Overdrive" && overdrive->api == 1 && overdrive->params == std::vector<s32>{10});
  CHECK(overdrive->laps == 0u && overdrive->pedals && overdrive->accel == 0x3F800000 && overdrive->brake == 0);
  CHECK(overdrive->boost_off && overdrive->rules.size() == 2 && overdrive->seat_words.size() == 1);
  CHECK(mode::Read(Vector("words.mode.json"), &why).has_value());
  const std::optional<mode::Package> quick = mode::Read(Vector("competitive.mode.json"), &why);
  CHECK(quick && quick->LapsFor(36) == 8u && quick->LapsFor(1) == 6u && quick->LapsFor(2) == 3u);
  CHECK(!ReadWith("overdrive.mode.json", "hash", picojson::value(std::string(64, '0'))));
  CHECK(!ReadWith("overdrive.mode.json", "params", picojson::value(picojson::array{picojson::value(55.0)})));
  CHECK(!ReadWith("overdrive.mode.json", "params", picojson::value(picojson::array{})));
  CHECK(!ReadWith("overdrive.mode.json", "api", picojson::value(2.0)));
}

// Overdrive on four seats: the pedals and boost words on every record, the speed held at 1000 + rise x steps / 600
// for the seats racing, a seat out booked in its word once; the same memory runs to the same block (a step run again).
void TestOverdriveRunsInTheBlock()
{
  BEGIN_TEST();
  std::string why;
  const std::optional<mode::Package> p = mode::Read(Vector("overdrive.mode.json"), &why);
  CHECK(p.has_value());
  if (!p)
    return;
  TestHost host;
  ModData data;
  Guest g(host, data);
  FakeModeRace(g, 4, 1200);
  const u32 broken = MACHINES + 2 * game::MACHINE_SIZE;
  g.Write32(broken, game::MACHINE_FLAG_BROKEN_DOWN);
  g.Write32(broken + game::MACHINE_SPEED, 0);  // at rest: out
  const std::vector<u8> before = BlockOf(g);
  mode::Run(g, *p, BLOCK, MODULE);
  for (u32 seat = 0; seat < 4; ++seat)
  {
    const u32 record = BLOCK + mode::RECORDS + seat * mode::RECORD;
    const bool held = seat != 2;
    CHECK(g.Read32(record) == (mode::PEDALS | mode::BOOST_OFF | (held ? mode::HELD : 0)));
    CHECK(g.Read32(record + 4) == 0x3F800000 && g.Read32(record + 8) == 0);
    CHECK(std::bit_cast<float>(g.Read32(record + 12)) == (held ? 1020.0f : 0.0f));  // 1000 + 10 x 1200 / 600
  }
  CHECK(g.Read32(BLOCK + mode::SeatWord(2, 0)) == 1200 && g.Read32(BLOCK + mode::SeatWord(1, 0)) == 0);
  CHECK(g.Read32(BLOCK + mode::OUT_MASK) == 4 && g.Read32(BLOCK + mode::END) == 0 && g.Read32(BLOCK + mode::RUNS) == 1);
  CHECK(g.Read32(BLOCK + mode::PAGE) == 1 && g.Read32(BLOCK + 2 * mode::PAGE) == 1);
  CHECK(g.Read32(BLOCK + mode::OPS) > 0 && g.Read32(BLOCK + mode::OPS) <= 720);
  const std::vector<u8> after = BlockOf(g);
  // a step run again from the same memory: the same block
  g.CopyTo(BLOCK, before.data(), before.size());
  mode::Run(g, *p, BLOCK, MODULE);
  CHECK(BlockOf(g) == after);
  // the next step: the out seat is not booked again
  g.Write32(MODULE + game::RACE_CLOCK, 1260);
  mode::Run(g, *p, BLOCK, MODULE);
  CHECK(g.Read32(BLOCK + mode::SeatWord(2, 0)) == 1200 && g.Read32(BLOCK + mode::RUNS) == 2);
  // the speed's cap: 9000 at most
  g.Write32(MODULE + game::RACE_CLOCK, 600 * 900);
  mode::Run(g, *p, BLOCK, MODULE);
  CHECK(std::bit_cast<float>(g.Read32(BLOCK + mode::RECORDS + 12)) == 9000.0f);
  // a mode's end word stops the runs
  g.Write32(BLOCK + mode::END, 1);
  mode::Run(g, *p, BLOCK, MODULE);
  CHECK(g.Read32(BLOCK + mode::RUNS) == 3);
}

// The words vector (every operation and word of the api) runs on thirty seats without a mode error, within the
// budget, and twice from the same memory to the same block.
void TestEveryWordRuns()
{
  BEGIN_TEST();
  std::string why;
  const std::optional<mode::Package> p = mode::Read(Vector("words.mode.json"), &why);
  CHECK(p.has_value());
  if (!p)
    return;
  TestHost host;
  ModData data;
  Guest g(host, data);
  FakeModeRace(g, 30, 3000);
  const auto before = host.ram;  // (its eliminate changes the machines too: the whole memory)
  mode::Run(g, *p, BLOCK, MODULE);
  CHECK(g.Read32(BLOCK + mode::END) == 0 && g.Read32(BLOCK + mode::FAULT) == mode::FAULT_NONE);
  CHECK(g.Read32(BLOCK + mode::OPS) > 0 && g.Read32(BLOCK + mode::OPS) <= static_cast<u32>(modes::BUDGET));
  const auto after = host.ram;
  host.ram = before;
  mode::Run(g, *p, BLOCK, MODULE);
  CHECK(host.ram == after);
}

// The game's square root: the protocol's sqrt.tsv, which the compiler's ModeMaths.GameSqrt wrote (its folds and these
// runs give the same bits).
void TestTheGamesSquareRootIsTheCompilers()
{
  BEGIN_TEST();
  std::ifstream file(ProtocolFile("protocol/tests/vectors/modes") / "sqrt.tsv");
  std::string line;
  int rows = 0;
  while (std::getline(file, line))
  {
    if (line.empty() || line[0] == '#')
      continue;
    const u32 in = static_cast<u32>(std::stoul(line.substr(0, 8), nullptr, 16));
    const u32 out = static_cast<u32>(std::stoul(line.substr(9, 8), nullptr, 16));
    const float root = mode::GameSqrt(std::bit_cast<float>(in));
    CHECK((std::isnan(root) ? 0x7FC00000u : std::bit_cast<u32>(root)) == out);
    ++rows;
  }
  CHECK(rows > 200);
}

// The generator's first state from the match id and the hash, as the protocol's random.json (an independent
// reference) has it; a mode's draw is its next().
void TestTheGeneratorIsTheReferences()
{
  BEGIN_TEST();
  const picojson::value v = Vector("random.json");
  for (const picojson::value& c : v.get("cases").get<picojson::array>())
  {
    std::array<u32, 4> state{};
    for (size_t k = 0; k < 4; ++k)
      state[k] = static_cast<u32>(c.get("state").get<picojson::array>()[k].get<double>());
    CHECK(mode::Seed(c.get("matchId").get<std::string>(), c.get("hash").get<std::string>()) == state);
  }
  // the waits vector's Restart rule draws once, random(1000) + 1, at step 4 (TestWaitsRunInTheBlock)
  std::string why;
  std::optional<mode::Package> p = mode::Read(Vector("waits.mode.json"), &why);
  CHECK(p.has_value());
  if (!p)
    return;
  const picojson::value& first = v.get("cases").get<picojson::array>()[0];
  p->seed = mode::Seed(first.get("matchId").get<std::string>(), first.get("hash").get<std::string>());
  TestHost host;
  ModData data;
  Guest g(host, data);
  FakeModeRace(g, 4, 100);
  for (int run = 0; run < 4; ++run)
    mode::Run(g, *p, BLOCK, MODULE);
  const u64 next = static_cast<u64>(first.get("next").get<picojson::array>()[0].get<double>());
  CHECK(g.Read32(BLOCK + mode::GLOBALS) == static_cast<u32>((next * 1000) >> 32) + 1);
  CHECK(g.Read32(BLOCK + mode::RANDOM) != 0);
}

// The waits vector on four seats in two teams, step by step: a rule's locals kept across its waits in the block, the
// waiting bits and the records freed as they end, abort and restart; and a step run again from the same memory gives
// the same memory (a rollback).
void TestWaitsRunInTheBlock()
{
  BEGIN_TEST();
  picojson::array teams{picojson::value(0.0), picojson::value(0.0), picojson::value(1.0), picojson::value(1.0)};
  const std::optional<mode::Package> p = ReadWith("waits.mode.json", "teams", picojson::value(teams));
  CHECK(p.has_value());
  if (!p)
    return;
  CHECK(!ReadWith("waits.mode.json", "teams", picojson::value(picojson::array{picojson::value(30.0)})));
  TestHost host;
  ModData data;
  Guest g(host, data);
  FakeModeRace(g, 4, 100);
  const auto kept = [&](u32 seat) { return g.Read32(BLOCK + mode::SeatWord(seat, 0)); };
  const auto done = [&](u32 seat) { return g.Read32(BLOCK + mode::SeatWord(seat, 1)); };
  const auto draw = [&] { return g.Read32(BLOCK + mode::GLOBALS); };
  mode::Run(g, *p, BLOCK, MODULE);  // run 1: Plain waits 2 on every seat, Restart waits 3
  CHECK(g.Read32(BLOCK + mode::WAITING) == 0xF && g.Read32(BLOCK + mode::WAITING + 8) == 1);
  CHECK(g.Read32(BLOCK + mode::TeamWord(0, 0)) == 2 && g.Read32(BLOCK + mode::TeamWord(1, 0)) == 2);
  mode::Run(g, *p, BLOCK, MODULE);  // run 2
  CHECK(kept(3) == 0 && done(3) == 0);
  const auto before = host.ram;
  mode::Run(g, *p, BLOCK, MODULE);  // run 3: Plain goes on, its local kept; waits 1 again
  for (u32 seat = 0; seat < 4; ++seat)
    CHECK(kept(seat) == seat + 10 && done(seat) == 0);
  CHECK(g.Read32(BLOCK + mode::WAITING) == 0xF && draw() == 0);
  mode::Run(g, *p, BLOCK, MODULE);  // run 4: Plain ends, Restart draws
  for (u32 seat = 0; seat < 4; ++seat)
    CHECK(done(seat) == 1);
  CHECK(g.Read32(BLOCK + mode::WAITING) == 0 && g.Read32(BLOCK + mode::WAITING + 8) == 0);
  CHECK(draw() >= 1 && draw() <= 1000 && g.Read32(BLOCK + mode::TeamWord(1, 0)) == 8);
  for (u32 i = 0; i < static_cast<u32>(modes::WAITS); ++i)
    CHECK(g.Read32(BLOCK + mode::WAIT_RECORDS + i * mode::WAIT) == 0);
  // runs 3 and 4 again from the memory before them: the same memory
  const auto after = host.ram;
  host.ram = before;
  mode::Run(g, *p, BLOCK, MODULE);
  mode::Run(g, *p, BLOCK, MODULE);
  CHECK(host.ram == after);
  // seat 3 out at run 5 (broken down, at rest): Abort waits 3; racing again at run 6 drops it
  const u32 m3 = MACHINES + 3 * game::MACHINE_SIZE;
  g.Write32(m3, game::MACHINE_FLAG_BROKEN_DOWN);
  g.Write32(m3 + game::MACHINE_SPEED, 0);
  mode::Run(g, *p, BLOCK, MODULE);
  CHECK(g.Read32(BLOCK + mode::WAITING + 4) == 8);
  g.Write32(m3, 0);
  g.Write32(m3 + game::MACHINE_SPEED, std::bit_cast<u32>(400.0f));
  mode::Run(g, *p, BLOCK, MODULE);  // run 6: aborted
  CHECK(g.Read32(BLOCK + mode::WAITING + 4) == 0 && done(3) == 1);
  // Plain again on seat 3 (racing anew at run 6: its edge), out again at run 7 for good: Abort ends at run 10
  g.Write32(m3, game::MACHINE_FLAG_BROKEN_DOWN);
  g.Write32(m3 + game::MACHINE_SPEED, 0);
  for (int run = 7; run <= 10; ++run)
    mode::Run(g, *p, BLOCK, MODULE);
  CHECK(done(3) == 100 && g.Read32(BLOCK + mode::WAITING + 4) == 0);
  CHECK(g.Read32(BLOCK + mode::END) == 0 && g.Read32(BLOCK + mode::FAULT) == mode::FAULT_NONE);

  // Restart: a seat broken down during the wait starts it again; once none is, 3 steps more
  TestHost host2;
  ModData data2;
  Guest g2(host2, data2);
  FakeModeRace(g2, 4, 100);
  mode::Run(g2, *p, BLOCK, MODULE);  // run 1: waits until run 4
  const u32 m0 = MACHINES;
  g2.Write32(m0, game::MACHINE_FLAG_BROKEN_DOWN);
  mode::Run(g2, *p, BLOCK, MODULE);  // run 2: broken: until run 5
  mode::Run(g2, *p, BLOCK, MODULE);  // run 3: still: until run 6
  g2.Write32(m0, 0);
  for (int run = 4; run <= 5; ++run)
  {
    mode::Run(g2, *p, BLOCK, MODULE);
    CHECK(g2.Read32(BLOCK + mode::GLOBALS) == 0);
  }
  mode::Run(g2, *p, BLOCK, MODULE);  // run 6
  CHECK(g2.Read32(BLOCK + mode::GLOBALS) != 0 && g2.Read32(BLOCK + mode::WAITING + 8) == 0);
}

// The language vector (subs, waits, teams, sets, vecs, random numbers, the library's every, at, gap_to and dist) on
// thirty seats past its at(10s), with no mode error, and a step run again from the same memory to the same memory.
void TestTheLanguageVectorRuns()
{
  BEGIN_TEST();
  std::string why;
  std::optional<mode::Package> p = mode::Read(Vector("language.mode.json"), &why);
  CHECK(p.has_value());
  if (!p)
    return;
  p->seed = mode::Seed("a match", p->hash);
  TestHost host;
  ModData data;
  Guest g(host, data);
  FakeModeRace(g, 30, 540);
  for (u32 i = 0; i < 30; ++i)
  {
    const u32 m = MACHINES + i * game::MACHINE_SIZE;
    g.Write32(m + game::MACHINE_POSITION_X, std::bit_cast<u32>(10.0f * static_cast<float>(i)));
    g.Write32(m + game::MACHINE_POSITION_Z, std::bit_cast<u32>(-3.0f));
    g.Write32(PROGRESS + i * game::PROGRESS_SIZE + game::PROGRESS_LAP_LENGTH, std::bit_cast<u32>(3000.0f));
    g.Write32(PROGRESS + i * game::PROGRESS_SIZE + game::PROGRESS_DISTANCE, std::bit_cast<u32>(100.0f * static_cast<float>(i)));
  }
  for (u32 steps = 540; steps <= 900; ++steps)
  {
    g.Write32(MODULE + game::RACE_CLOCK, steps);
    if (steps == 700)
    {
      const auto before = host.ram;
      mode::Run(g, *p, BLOCK, MODULE);
      const auto after = host.ram;
      host.ram = before;
      mode::Run(g, *p, BLOCK, MODULE);
      CHECK(host.ram == after);
      continue;
    }
    mode::Run(g, *p, BLOCK, MODULE);
  }
  CHECK(g.Read32(BLOCK + mode::END) == 0 && g.Read32(BLOCK + mode::FAULT) == mode::FAULT_NONE);
  CHECK(g.Read32(BLOCK + mode::OPS + 4) <= static_cast<u32>(modes::BUDGET));
  CHECK(g.Read32(BLOCK + mode::GLOBALS) > 0);                  // draws: every(1s) at 600, 660, ...
  CHECK(g.Read32(BLOCK + mode::GLOBALS + 8) == 1);             // marked: seat 0, by at(10s)'s mark
  CHECK(g.Read32(BLOCK + mode::SeatWord(0, 0)) == 3);          // waited: Restart's 2, then 1 more
  const float reach = std::bit_cast<float>(g.Read32(BLOCK + mode::GLOBALS + 20));
  CHECK(std::isfinite(reach));
}

namespace
{
// A machine's block and progress record in FakeModeRace's race.
u32 MachineAt(u32 seat)
{
  return MACHINES + seat * game::MACHINE_SIZE;
}
u32 ProgressAt(u32 seat)
{
  return PROGRESS + seat * game::PROGRESS_SIZE;
}
void SetFlags(Guest& g, u32 seat, u32 set, u32 clear = 0)
{
  g.Write32(MachineAt(seat) + game::MACHINE_FLAGS, (g.Read32(MachineAt(seat) + game::MACHINE_FLAGS) | set) & ~clear);
}
float RealAt(Guest& g, u32 at)
{
  return std::bit_cast<float>(g.Read32(at));
}
constexpr u32 R = MODULE + 0x1BDC70, CM = MODULE + 0x227860;
}  // namespace

// The race words vector on thirty seats through the countdown and past GO: its setup words act at the first run
// (every no-contact row on, every seat's Restore, the timed rule with its limit, the rolling start's writes); the
// countdown hands every seat to the CPU with the rolling hold; the events fire on the game's changes (a lap's award,
// a hit's push, a wall's freeze and danger zone, a KO's credit and retire); no mode error, within the budget, and a
// step run again from the same memory to the same memory.
void TestTheRaceWordsRun()
{
  BEGIN_TEST();
  std::string why;
  std::optional<mode::Package> p = mode::Read(Vector("race.mode.json"), &why);
  CHECK(p.has_value());
  if (!p)
    return;
  p->players = 0x3;  // seats 0 and 1 are players, the rest CPUs
  TestHost host;
  ModData data;
  Guest g(host, data);
  FakeModeRace(g, 30, 0);
  g.Write16(MODULE + game::PHASE, game::PHASE_RACE - 1);  // the countdown
  for (u32 i = 0; i < 30; ++i)
  {
    g.Write32(MachineAt(i) + 0x8, std::bit_cast<u32>(1080.0f));                // weight
    g.Write32(MachineAt(i) + 0x104, std::bit_cast<u32>(-1.0f));                // the nose along +y (row 1)
    g.Write32(MachineAt(i) + 0x100, std::bit_cast<u32>(1.0f));                 // up along y
    g.Write32(MachineAt(i) + game::MACHINE_ENERGY, std::bit_cast<u32>(100.0f));
    g.Write16(MachineAt(i) + 0x5DA, 100);
    g.Write32(MachineAt(i) + 0x484, 0x80700000 + 0x100 * i);                  // the CPU block
    g.Write8(R + 0xE + i, static_cast<u8>(i));
    SetFlags(g, i, 0x8000);                                                     // frozen through the countdown
  }
  mode::Run(g, *p, BLOCK, MODULE);
  CHECK(g.Read32(BLOCK + mode::FAULT) == mode::FAULT_NONE);
  CHECK(g.Read32(BLOCK + mode::NC) == 0xFFFFFFFF && g.Read32(BLOCK + mode::NC_ANY) == 0xFFFFFFFF);
  CHECK(g.Read32(BLOCK + mode::RESPAWN) == 0x3FFFFFFF);
  CHECK((g.Read32(R) & 0x40000) && g.Read16(R + 0x13F8) == 18000 && g.Read16(R + 0xA4) == 18000);
  CHECK(g.Read8(MODULE + 0x1BA040 + 0x9D) == 0);
  const u32 m0 = MachineAt(0);
  CHECK(RealAt(g, m0 + 0x224) == 5.0f && RealAt(g, m0 + 0x98) == 5000.0f);   // 1000 / 200; 1000 / 216 x 1080
  CHECK((g.Read32(m0 + game::MACHINE_FLAGS) & 0x8401) == 0x401 && g.Read8(m0 + 0x481) == 90);
  CHECK(g.Read32(BLOCK + mode::RECORDS) == (mode::HAND | mode::ROLL));
  // race_start: cpu_mode 23 and hand_to_cpu on the CPUs only
  CHECK(g.Read8(0x80700000 + 0x32) == 0 && g.Read8(0x80700000 + 0x100 * 2 + 0x32) == 23);
  CHECK(g.Read32(BLOCK + mode::HANDED) == 0x3FFFFFFC);
  // the countdown's next steps: the seats stay unfrozen, no start charge
  SetFlags(g, 4, 0x8000);
  g.Write32(MachineAt(4) + 0xDC, 7);
  mode::Run(g, *p, BLOCK, MODULE);
  CHECK(!(g.Read32(MachineAt(4) + game::MACHINE_FLAGS) & 0x8000) && g.Read32(MachineAt(4) + 0xDC) == 0);
  // GO: pads_back, the records without the hand
  g.Write16(MODULE + game::PHASE, game::PHASE_RACE);
  mode::Run(g, *p, BLOCK, MODULE);
  CHECK(g.Read32(BLOCK + mode::HANDED) == 0 && g.Read32(BLOCK + mode::NC) == 0);   // ghost off: no team rows
  for (u32 step = 1; step < 200; ++step)
  {
    g.Write32(MODULE + game::RACE_CLOCK, step);
    if (step == 20)
      g.Write32(ProgressAt(5) + game::PROGRESS_LAPS, 1);                  // a lap: award 10
    if (step == 30)
      g.Write32(BLOCK + mode::TOUCH + 4 * 6, 0x80000000u >> 7);           // the pair gate: 6 met 7
    if (step == 40)
    {
      SetFlags(g, 8, 0x00800000);                                         // a wall
      g.Write32(MachineAt(8) + 0x58C, 0x8);
    }
    if (step == 50)
    {
      g.Write8(MachineAt(9) + 0x482, 1);                                  // 9's skull, 10's KO
      SetFlags(g, 10, 0x80);
      g.Write8(MachineAt(10) + 0x593, 60);
    }
    if (step == 100)
    {
      const auto before = host.ram;
      mode::Run(g, *p, BLOCK, MODULE);
      const auto after = host.ram;
      host.ram = before;
      mode::Run(g, *p, BLOCK, MODULE);
      CHECK(host.ram == after);
      continue;
    }
    mode::Run(g, *p, BLOCK, MODULE);
    if (step == 30)
    {
      const u32 move = BLOCK + mode::MOVES + 16 * 6;
      CHECK(g.Read32(move) == 1);                                         // a push (both at rest: 0)
    }
  }
  CHECK(g.Read32(BLOCK + mode::END) == 0 && g.Read32(BLOCK + mode::FAULT) == mode::FAULT_NONE);
  CHECK(g.Read32(BLOCK + mode::OPS + 4) <= static_cast<u32>(modes::BUDGET));
  CHECK(g.Read32(BLOCK + mode::POINTS + 4 * 5) == 10 && g.Read32(BLOCK + mode::GLOBALS) == 1);   // laps_seen
  CHECK(g.Read32(BLOCK + mode::FROZEN) == 1u << 8);
  CHECK((g.Read32(BLOCK + mode::DANGER + 16 * 8 + 12) & mode::DANGER_ON) != 0);
  CHECK(g.Read32(BLOCK + mode::GLOBALS + 4) == 9 && g.Read32(BLOCK + mode::POINTS + 4 * 9) == 5);  // last_by = 9
  CHECK((g.Read32(MachineAt(10) + game::MACHINE_FLAGS) & 0x08000001) == 0x08000000);  // retired, Restore off
  CHECK(!(g.Read32(BLOCK + mode::RESPAWN) >> 10 & 1));
  CHECK(g.Read32(BLOCK + mode::RECORDS + 8 * mode::RECORD) & mode::HELD);           // frozen: held at 0
  // the danger zone on 8 (300 / 600 km/h, 3 s): armed at 700, under 300 the count from 240; the explode at 60 left
  u32 step = 200;
  const auto at = [&](float speed) {
    g.Write32(MachineAt(8) + game::MACHINE_SPEED, std::bit_cast<u32>(speed));
    g.Write32(MODULE + game::RACE_CLOCK, step++);
    mode::Run(g, *p, BLOCK, MODULE);
    return (g.Read32(BLOCK + mode::RECORDS + 8 * mode::RECORD) & mode::EXPLODE) != 0;
  };
  CHECK(!at(700.0f));
  u32 counted = 0;
  while (!at(200.0f) && counted < 400)
    ++counted;
  CHECK(counted == 179);                                                              // the 180th step under 300
  CHECK(!at(200.0f));                                                                 // once
  // the clock limit: the timer counts down from the race clock; at the limit the race ends in the mode's order
  CHECK(g.Read16(R + 0xA4) == 18000 - (step - 1));
  g.Write32(MODULE + game::RACE_CLOCK, 18000);
  mode::Run(g, *p, BLOCK, MODULE);
  CHECK(g.Read32(BLOCK + mode::END) == 2 && g.Read16(R + 0xA4) == 1);
}

// The knockout: every 15 s the last racing seat is placed by the count still racing, explodes and is out; at two the
// race ends with the mode's places, held in the standing and written in the results (no seat left RUNNING).
void TestTheKnockoutEndsWithItsPlaces()
{
  BEGIN_TEST();
  std::string why;
  const std::optional<mode::Package> p = mode::Read(Vector("knockout.mode.json"), &why);
  CHECK(p.has_value());
  if (!p)
    return;
  TestHost host;
  ModData data;
  Guest g(host, data);
  FakeModeRace(g, 4, 0);
  for (u32 i = 0; i < 4; ++i)
  {
    g.Write32(MachineAt(i) + 0x8, std::bit_cast<u32>(1080.0f));
    g.Write32(ProgressAt(i) + game::PROGRESS_LAP_LENGTH, std::bit_cast<u32>(3000.0f));
    g.Write32(ProgressAt(i) + game::PROGRESS_DISTANCE, std::bit_cast<u32>(100.0f * static_cast<float>(i)));
    g.Write8(R + 0xE + i, static_cast<u8>(3 - i));                       // the standing: 3, 2, 1, 0
    for (u32 k = 0; k < 4; ++k)
      g.Write8(R + 0xD4 + 0xA0 * i + 0xE + k, 0xFF);
  }
  for (u32 step = 0; step <= 2700; ++step)
  {
    g.Write32(MODULE + game::RACE_CLOCK, step);
    mode::Run(g, *p, BLOCK, MODULE);
    for (u32 seat = 0; seat < 4; ++seat)
    {
      const u32 record = BLOCK + mode::RECORDS + seat * mode::RECORD;
      if (g.Read32(record) & mode::EXPLODE)
      {
        // the image's explode: destroyed, out by the game's verdict; the game books it DISQUALIFIED
        CHECK(step == 900 * (seat + 1));
        SetFlags(g, seat, 0x40000000);
        g.Write8(R + 0xD4 + 0xA0 * seat + 0xE, 0xFE);
      }
    }
    if (g.Read32(BLOCK + mode::END) != 0)
    {
      CHECK(step == 2700);
      break;
    }
  }
  CHECK(g.Read32(BLOCK + mode::END) == 2 && g.Read32(BLOCK + mode::GLOBALS) == 3);
  // after the end: the order held against the ranking pass (and a place the game booked out after the end put
  // back), the rules run no more
  g.Write8(R + 0xE, 0);
  const u32 runs = g.Read32(BLOCK + mode::RUNS);
  mode::Run(g, *p, BLOCK, MODULE);
  CHECK(g.Read32(BLOCK + mode::RUNS) == runs);
  // places: seat 3 first, 2 second, 1 third, 0 fourth
  for (u32 seat = 0; seat < 4; ++seat)
  {
    CHECK(g.Read8(BLOCK + mode::PLACES + seat) == 4 - seat);
    CHECK(g.Read8(R + 0xE + (3 - seat)) == seat && g.Read8(CM + 0x21C + seat) == 3 - seat);
    CHECK(g.Read8(R + 0xD4 + 0xA0 * seat + 0xE) == 3 - seat);
  }
  CHECK(g.Read32(R + 0xD4 + 0x24) == (15u << 12 | 16));                    // seat 0 out at step 901: 15.016 s
  CHECK(g.Read32(R + 0xD4 + 0xA0 * 3 + 0x24) == (45u << 12));              // the winner: the race's 2700 steps
}

// The lobby's restore rule never reaches a mode's race: rule 0x8000 is never asked for, and a mode's restore is its own
// words alone.
void TestTheLobbysRestoreStaysOut()
{
  BEGIN_TEST();
  TestHost host;
  const std::string dir = (std::filesystem::temp_directory_path() / "fzgx-test-restore").string();
  std::filesystem::create_directories(dir);
  std::string image(0x100, '\0');
  image.replace(0, 4, "FZMD");
  image.replace(0x10, 4, "FZSI");
  WriteText(dir + "/mod.bin", image);
  WriteText(dir + "/manifest.json", "{\"commit\": \"abc1234\"}");
  WriteText(dir + "/codes.txt", "$Boot to race\n48000000 800030C8\n$Race setup\nC0000000 00000001\n4E800020 00000000\n");
  Options options;
  options.bundle_dir = dir;
  Init(host, options);
  const auto match = [](const char* restore, const char* vector) {
    picojson::value mode = Vector(vector);
    const std::string json = R"({"matchId": "m7", "seat": 0, "token": "1f2e3d4c5b6a7988", "relay": {"host": "127.0.0.1", "port": 47811},
      "setup": {"course": 8, "laps": 3, "shuffleSeed": 1, "cpuSeed": 2, "rules": {"restore": )" + std::string(restore) +
                             R"(}, "seats": [{"seat": 0, "machine": 5, "playerId": "p1", "name": "A"},
      {"seat": 1, "machine": 7}], "mode": )" + mode.serialize() + "}}";
    return boot::ReadMatch(json, "test");
  };
  const auto word = [](const boot::Match& m) {
    const u8* r = m.race.data() + boot::RACE_RESTORE;
    return u32(r[0]) << 24 | u32(r[1]) << 16 | u32(r[2]) << 8 | u32(r[3]);
  };
  for (const char* restore : {"true", "false"})
  {
    // a mode that says nothing: no restore, whatever the lobby's rule says
    const auto plain = match(restore, "overdrive.mode.json");
    CHECK(plain && plain->game_mode && !plain->game_mode->restore && word(*plain) == 0);
    // a mode that says `restore on`: the seats' own Restore (Begin), never the rule
    const auto own = match(restore, "race.mode.json");
    CHECK(own && own->game_mode && own->game_mode->restore == true && word(*own) == 0);
  }
  Shutdown();
  std::filesystem::remove_all(dir);
}

// Lives 3: every seat respawns; each restore costs a life; the wreck that leaves one life turns the seat's Restore off
// once its restore is done (never during the wreck), and a finished seat's Restore is never touched.
void TestTheLivesLibraryRuns()
{
  BEGIN_TEST();
  std::string why;
  const std::optional<mode::Package> p = mode::Read(Vector("lives.mode.json"), &why);
  CHECK(p.has_value());
  if (!p)
    return;
  TestHost host;
  ModData data;
  Guest g(host, data);
  FakeModeRace(g, 3, 0);
  u32 step = 0;
  const auto run = [&] { g.Write32(MODULE + game::RACE_CLOCK, step++); mode::Run(g, *p, BLOCK, MODULE); };
  run();
  CHECK(g.Read32(BLOCK + mode::RESPAWN) == 0x7 && (g.Read32(MachineAt(0) + game::MACHINE_FLAGS) & 1));
  const auto wreck = [&](u32 seat) {
    g.Write16(MachineAt(seat) + 0x214, 120);
    run();
    g.Write16(MachineAt(seat) + 0x214, 60);
    run();
    g.Write16(MachineAt(seat) + 0x214, 0);
    run();
  };
  wreck(0);
  CHECK(g.Read32(BLOCK + mode::SeatWord(0, 0)) == 2 && (g.Read32(BLOCK + mode::RESPAWN) & 1));
  g.Write16(MachineAt(0) + 0x214, 120);
  run();
  CHECK(g.Read32(BLOCK + mode::SeatWord(0, 0)) == 1 && !(g.Read32(BLOCK + mode::RESPAWN) & 1));
  CHECK(g.Read32(MachineAt(0) + game::MACHINE_FLAGS) & 1);                 // kept through the wreck
  g.Write16(MachineAt(0) + 0x214, 20);
  run();
  CHECK(!(g.Read32(MachineAt(0) + game::MACHINE_FLAGS) & 1));              // off at its exit
  // a finished seat keeps its flag whatever the words say
  SetFlags(g, 1, 0x10000);
  g.Write32(BLOCK + mode::RESPAWN, 0x4);
  run();
  CHECK(g.Read32(MachineAt(1) + game::MACHINE_FLAGS) & 1);
}
