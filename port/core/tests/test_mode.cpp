// The game mode's reader and interpreter (mode.h): the protocol's vectors (protocol/tests/vectors/modes/, the
// compiler's own packages) read as the match start carries them, and Overdrive run on a fake race.
// SPDX-License-Identifier: GPL-3.0-or-later

#include <memory>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "files.h"
#include "harness.h"
#include "stat_packs.h"
#include "mode.h"
#include "mode_hud.h"
#include "parts.h"
#include "fzgx/practice.h"
#include "mode_board.h"
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
  const auto before = host.ram;  // (its retire changes the machines too: the whole memory)
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
// What fn_1_25004 leaves once the machine's next step has begun: flag 0x40000000 is gone by then (fn_1_289B4), the
// record +0x58C 0x10 stays, and the flags 0x800, 0x80, 0x00800000 with it.
void Explode(Guest& g, u32 seat)
{
  SetFlags(g, seat, 0x800 | 0x80 | 0x00800000);
  g.Write32(MachineAt(seat) + 0x58C, g.Read32(MachineAt(seat) + 0x58C) | 0x10);
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
  CHECK(g.Read32(BLOCK + mode::RECORDS) == (mode::HELD | mode::HAND | mode::ROLL));  // and the speed held, to GO
  CHECK(g.Read32(BLOCK + mode::RECORDS + 12) == 0x447A0000);  // 1000 km/h
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
  g.Write16(R + 0xD4 + 0xA0 * 1 + 0x4, 1234);
  g.Write8(R + 0xD4 + 0xA0 * 1 + 0x9, 123);
  for (u32 step = 0; step <= 2700; ++step)
  {
    g.Write32(MODULE + game::RACE_CLOCK, step);
    mode::Run(g, *p, BLOCK, MODULE);
    if (step == 0)
    {
      // totals as award leaves them: a negative one and one past the field
      g.Write32(BLOCK + mode::POINTS + 4 * 1, static_cast<u32>(-5));
      g.Write32(BLOCK + mode::POINTS + 4 * 2, 70000);
    }
    for (u32 seat = 0; seat < 4; ++seat)
    {
      const u32 record = BLOCK + mode::RECORDS + seat * mode::RECORD;
      if (g.Read32(record) & mode::EXPLODE)
      {
        // the image's explode: destroyed, out by the game's verdict; the game books it DISQUALIFIED
        CHECK(step == 900 * (seat + 1));
        Explode(g, seat);
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
  // a negative total shows 0 and stays as it is in the block; a total past the field shows its end; no total: untouched
  CHECK(g.Read16(R + 0xD4 + 0xA0 * 1 + 0x4) == 0 && g.Read8(R + 0xD4 + 0xA0 * 1 + 0x9) == 0);
  CHECK(g.Read32(BLOCK + mode::POINTS + 4 * 1) == static_cast<u32>(-5));
  CHECK(g.Read16(R + 0xD4 + 0xA0 * 2 + 0x4) == 0xFFFF && g.Read8(R + 0xD4 + 0xA0 * 2 + 0x9) == 0xFF);
  CHECK(g.Read16(R + 0xD4 + 0xA0 * 3 + 0x4) == 0);
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

// `race_waits everyone`: a seat that went out on its own, with no place from the mode, is ranked by when it went out and
// below the racers still in, with that time, not DISQUALIFIED; and a place the mode gave is that row, whatever the rest.
void TestARaceThatWaitsRanksTheOut()
{
  BEGIN_TEST();
  std::string why;
  std::optional<mode::Package> p = mode::Read(Vector("knockout.mode.json"), &why);
  CHECK(p.has_value());
  if (!p)
    return;
  p->race_waits = true;
  TestHost host;
  ModData data;
  Guest g(host, data);
  FakeModeRace(g, 4, 0);
  for (u32 i = 0; i < 4; ++i)
  {
    g.Write32(MachineAt(i) + 0x8, std::bit_cast<u32>(1080.0f));
    g.Write32(ProgressAt(i) + game::PROGRESS_LAP_LENGTH, std::bit_cast<u32>(3000.0f));
    g.Write32(ProgressAt(i) + game::PROGRESS_DISTANCE, std::bit_cast<u32>(100.0f * static_cast<float>(i)));
    g.Write8(R + 0xE + i, static_cast<u8>(3 - i));
    for (u32 k = 0; k < 4; ++k)
      g.Write8(R + 0xD4 + 0xA0 * i + 0xE + k, 0xFF);
  }
  for (u32 step = 0; step <= 2700 && g.Read32(BLOCK + mode::END) == 0; ++step)
  {
    g.Write32(MODULE + game::RACE_CLOCK, step);
    if (step == 100)
    {
      Explode(g, 3);  // the leader dies on its own, long before any cut
      g.Write8(R + 0xD4 + 0xA0 * 3 + 0xE, 0xFE);
    }
    mode::Run(g, *p, BLOCK, MODULE);
    for (u32 seat = 0; seat < 4; ++seat)
      if (g.Read32(BLOCK + mode::RECORDS + seat * mode::RECORD) & mode::EXPLODE)
      {
        Explode(g, seat);
        g.Write8(R + 0xD4 + 0xA0 * seat + 0xE, 0xFE);
      }
  }
  CHECK(g.Read32(BLOCK + mode::END) == 2);
  mode::Run(g, *p, BLOCK, MODULE);  // the hold: a place the game booked out after the end put back
  CHECK(g.Read8(R + 0xD4 + 0xA0 * 3 + 0xE) != 0xFE);  // ranked, not disqualified
  // no seat is left without a row, and no two share one
  u32 rows = 0;
  for (u32 seat = 0; seat < 4; ++seat)
    rows |= 1u << g.Read8(R + 0xD4 + 0xA0 * seat + 0xE);
  CHECK(rows == 0xF);
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
// How a seat's end is told: the explosion by the game's record +0x58C 0x10, whoever ran the routine (the pulse
// 0x40000000 alone is nothing, and a mode's explode never shows it); a fall is the flag 0x800 alone, and the routine
// under the fall planes only ends it; a wreck touched later still explodes; a restore's exit opens a new life.
void TestDestroyedAndOutTellAnEndApart()
{
  std::string why;
  const std::optional<mode::Package> p = mode::Read(Vector("wrecks.mode.json"), &why);
  CHECK(p.has_value());
  if (!p)
    return;
  TestHost host;
  ModData data;
  Guest g(host, data);
  FakeModeRace(g, 8, 3000);
  g.Write16(MODULE + game::PHASE, game::PHASE_RACE);
  constexpr u32 COURSE = 0x80440000, HEIGHTS = 0x80440100;
  g.Write32(MODULE + 0x1BDC20, COURSE);
  g.Write32(COURSE + 0x24, HEIGHTS);
  g.Write32(HEIGHTS, std::bit_cast<u32>(-100.0f));                         // the course's floor: its plane at -1000
  const auto run = [&] { mode::Run(g, *p, BLOCK, MODULE); };
  const auto blown = [&](u32 seat) { return g.Read32(BLOCK + mode::SeatWord(seat, 0)); };
  const auto fell = [&](u32 seat) { return g.Read32(BLOCK + mode::SeatWord(seat, 1)); };
  const auto cause = [&](u32 seat) { return g.Read32(BLOCK + mode::SeatWord(seat, 2)); };
  const auto height = [&](u32 seat, float y) { g.Write32(MachineAt(seat) + game::MACHINE_POSITION_X + 4, std::bit_cast<u32>(y)); };
  for (u32 seat = 0; seat < 8; ++seat)
    height(seat, 10.0f);
  run();
  // the pulse alone: nothing
  SetFlags(g, 0, 0x40000000);
  run();
  CHECK(blown(0) == 0 && fell(0) == 0);
  SetFlags(g, 0, 0, 0x40000000);
  // a mode's explode: the record and 0x800 with no pulse, the next run: once, the read true in its rule, out by 2
  Explode(g, 1);
  run();
  run();
  CHECK(blown(1) == 1 && fell(1) == 0 && cause(1) == 2);
  CHECK(g.Read32(BLOCK + mode::SeatWord(1, 3)) == 1);
  // a fall: 0x800 alone (lost course), out by 3; the routine under the plane much later is no explosion
  SetFlags(g, 2, 0x800);
  run();
  CHECK(blown(2) == 0 && fell(2) == 1 && cause(2) == 3);
  height(2, -1000.5f);
  Explode(g, 2);
  run();
  CHECK(blown(2) == 0 && fell(2) == 1 && cause(2) == 3);
  // the machine that reaches the plane with no flag before it: a fall, once, out by 3; at -5000 under any floor too
  height(3, -1000.5f);
  Explode(g, 3);
  run();
  CHECK(blown(3) == 0 && fell(3) == 1 && cause(3) == 3);
  g.Write32(HEIGHTS, std::bit_cast<u32>(-4900.0f));
  height(4, -5000.5f);
  Explode(g, 4);
  run();
  g.Write32(HEIGHTS, std::bit_cast<u32>(-100.0f));
  CHECK(blown(4) == 0 && fell(4) == 1 && cause(4) == 3);
  // energy: at rest, out by 1; a wreck touched later explodes, its cause stays
  SetFlags(g, 5, 0x80 | 0x800000);
  g.Write32(MachineAt(5) + game::MACHINE_SPEED, 0);
  run();
  CHECK(cause(5) == 1 && blown(5) == 0);
  Explode(g, 5);
  run();
  CHECK(cause(5) == 1 && blown(5) == 1 && fell(5) == 0);
  // retired, and time over: their own
  SetFlags(g, 6, 0x08000000);
  SetFlags(g, 7, 0x10000000);
  run();
  CHECK(cause(6) == 4 && cause(7) == 5);
  // a restore's exit clears the record: the next explosion counts again, a second run of the same one does not
  run();
  CHECK(blown(1) == 1);
  g.Write32(MachineAt(1) + 0x58C, 0);
  SetFlags(g, 1, 0, 0x800 | 0x80 | 0x00800000);
  run();
  CHECK(g.Read32(BLOCK + mode::SeatWord(1, 3)) == 1);
  Explode(g, 1);
  run();
  CHECK(blown(1) == 2);
}

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
  const auto number = [&](u32 seat) { return g.Read32(BLOCK + mode::hud::SEATS_AT + seat * mode::hud::SEAT_DISPLAY + mode::hud::NUMBER); };
  const auto left = [&](u32 seat) { return g.Read32(BLOCK + mode::SeatWord(seat, 0)); };
  const auto restore = [&](u32 seat) {
    g.Write16(MachineAt(seat) + 0x214, 120);
    run();
    g.Write16(MachineAt(seat) + 0x214, 60);
    run();
    g.Write16(MachineAt(seat) + 0x214, 0);
    SetFlags(g, seat, 0, 0x800 | 0x80 | 0x00800000);
    g.Write32(MachineAt(seat) + 0x58C, g.Read32(MachineAt(seat) + 0x58C) & ~0x10u);
    run();
  };
  CHECK(left(0) == 3 && number(0) == 3 + 1);
  // a fall, restored: costs nothing
  SetFlags(g, 0, 0x800);
  run();
  restore(0);
  CHECK(left(0) == 3 && number(0) == 3 + 1 && (g.Read32(BLOCK + mode::RESPAWN) & 1));
  // a breakdown, restored: costs nothing
  SetFlags(g, 0, 0x80);
  run();
  restore(0);
  CHECK(left(0) == 3 && (g.Read32(BLOCK + mode::RESPAWN) & 1));
  // an explosion costs one, once
  Explode(g, 0);
  run();
  CHECK(left(0) == 2 && number(0) == 2 + 1 && (g.Read32(MachineAt(0) + game::MACHINE_FLAGS) & 1));
  restore(0);
  CHECK(left(0) == 2 && number(0) == 2 + 1 && (g.Read32(BLOCK + mode::RESPAWN) & 1));
  // the one that leaves one life: still restored (the flag stays until the restore has begun and reaches its exit)
  Explode(g, 0);
  run();
  CHECK(left(0) == 1 && (g.Read32(BLOCK + mode::RESPAWN) & 1) && (g.Read32(MachineAt(0) + game::MACHINE_FLAGS) & 1));
  g.Write16(MachineAt(0) + 0x214, 120);
  run();
  CHECK(!(g.Read32(BLOCK + mode::RESPAWN) & 1));
  CHECK(g.Read32(MachineAt(0) + game::MACHINE_FLAGS) & 1);                 // kept through the wreck
  g.Write16(MachineAt(0) + 0x214, 20);
  run();
  CHECK(!(g.Read32(MachineAt(0) + game::MACHINE_FLAGS) & 1));              // off at its exit
  g.Write16(MachineAt(0) + 0x214, 0);
  SetFlags(g, 0, 0, 0x800 | 0x80 | 0x00800000);
  g.Write32(MachineAt(0) + 0x58C, g.Read32(MachineAt(0) + 0x58C) & ~0x10u);
  run();
  CHECK(left(0) == 1 && number(0) == 1 + 1);
  // the last: out, nothing shown, the count at 0; an explosion after the out takes it no lower
  Explode(g, 0);
  run();
  CHECK(left(0) == 0 && number(0) == 0);
  run();
  CHECK(left(0) == 0 && number(0) == 0);
  // a finished seat keeps its flag whatever the words say
  SetFlags(g, 1, 0x10000);
  g.Write32(BLOCK + mode::RESPAWN, 0x4);
  run();
  CHECK(g.Read32(MachineAt(1) + game::MACHINE_FLAGS) & 1);
}

// cpu_mode 18 takes: the sim generator draws for it. The player's seat stays out of it.
void TestCpuModeEighteen()
{
  BEGIN_TEST();
  std::string why;
  std::optional<mode::Package> p = mode::Read(Vector("edges.mode.json"), &why);
  CHECK(p.has_value());
  if (!p)
    return;
  p->players = 0x1;
  TestHost host;
  ModData data;
  Guest g(host, data);
  FakeModeRace(g, 3, 0);
  g.Write16(MODULE + game::PHASE, game::PHASE_RACE - 1);
  for (u32 i = 0; i < 3; ++i)
    g.Write32(MachineAt(i) + 0x484, 0x80700000 + 0x100 * i);
  mode::Run(g, *p, BLOCK, MODULE);
  CHECK(g.Read8(0x80700000 + 0x100 * 2 + 0x32) == 18);
  CHECK(g.Read8(0x80700000 + 0x32) == 0);
}

// The prepared interpreter is the plain one (mode.h, Prepared): every vector on a moving race gives the same block and
// machines at every step, and the same again with its code changed at random, where every fault has to fall on the
// same operation with the same memory. The plain loop reads the code's bytes, as the first interpreter did.
void TestThePreparedInterpreterIsThePlainOne()
{
  BEGIN_TEST();
  const auto race = [](const mode::Package& p, std::vector<u8>* out, u32* last_ops, u32* last_fault) {
    TestHost host;
    ModData data;
    Guest g(host, data);
    FakeModeRace(g, 30, 0);
    g.Write16(MODULE + game::PHASE, game::PHASE_RACE - 1);
    u32 x = 0x2F6E2B1;
    const auto next = [&] { x ^= x << 13; x ^= x >> 17; x ^= x << 5; return x; };
    for (u32 step = 0; step < 90; ++step)
    {
      g.Write32(MODULE + game::RACE_CLOCK, step);
      if (step == 20)
        g.Write16(MODULE + game::PHASE, game::PHASE_RACE);
      for (u32 s = 0; s < 30; ++s)
      {
        const u32 r = next();
        g.Write32(MachineAt(s) + game::MACHINE_SPEED, std::bit_cast<u32>(static_cast<float>(r % 1200)));
        if ((r & 0x1F) == 1)
          SetFlags(g, s, 0x20);
        if ((r & 0x3F) == 2)
          SetFlags(g, s, 0, 0x20);
        if ((r & 0xFF) == 3)
          SetFlags(g, s, 0x80);
        if ((r & 0x3F) == 4)
          g.Write32(ProgressAt(s) + 0x140, g.Read32(ProgressAt(s) + 0x140) + 1);
        if ((r & 0xFF) == 5)
          g.Write32(ProgressAt(s) + game::PROGRESS_LAPS, g.Read32(ProgressAt(s) + game::PROGRESS_LAPS) + 1);
      }
      mode::Run(g, p, BLOCK, MODULE);
      const std::vector<u8> block = BlockOf(g);
      out->insert(out->end(), block.begin(), block.end());
      for (u32 s = 0; s < 30; ++s)
      {
        const u32 m = MachineAt(s);
        for (u32 at = 0; at < game::MACHINE_SIZE; at += 4)
          for (const u8 b : {g.Read8(m + at), g.Read8(m + at + 1), g.Read8(m + at + 2), g.Read8(m + at + 3)})
            out->push_back(b);
      }
    }
    *last_ops = g.Read32(BLOCK + mode::OPS + 4);
    *last_fault = g.Read32(BLOCK + mode::FAULT);
  };
  u32 x = 0x51ED27;
  const auto next = [&] { x ^= x << 13; x ^= x >> 17; x ^= x << 5; return x; };
  u32 faults = 0;
  for (const char* name : {"overdrive", "words", "waits", "language", "race", "knockout", "lives", "display", "wrecks"})
  {
    std::string why;
    std::optional<mode::Package> read = mode::Read(Vector((std::string(name) + ".mode.json").c_str()), &why);
    CHECK(read.has_value());
    if (!read)
      continue;
    for (u32 round = 0; round < 20; ++round)
    {
      mode::Package prepared = *read;
      if (round != 0)
      {
        for (u32 c = 1 + next() % 3; c > 0 && !prepared.code.empty(); --c)
          prepared.code[next() % prepared.code.size()] = static_cast<u8>(next());
        mode::Prepare(prepared);
      }
      CHECK(prepared.prepared.ops.size() == prepared.code.size() + 1);
      mode::Package plain = prepared;
      plain.prepared = {};
      std::vector<u8> a, b;
      u32 ops_a = 0, ops_b = 0, fault_a = 0, fault_b = 0;
      race(prepared, &a, &ops_a, &fault_a);
      race(plain, &b, &ops_b, &fault_b);
      CHECK(a == b && ops_a == ops_b && fault_a == fault_b);
      faults += fault_a != mode::FAULT_NONE ? 1 : 0;
    }
  }
  CHECK(faults != 0);   // the changed code does reach the faults
}

// A mode's `board` lines read from the package, and their values from the block: the columns in the file's order within
// a scope, the name cut at a character, the same values from the same block whoever reads (a watcher, a replay); and a
// reading writes nothing of the game.
void TestTheBoardsLinesAndValues()
{
  BEGIN_TEST();
  std::string why;
  const std::optional<mode::Package> p = mode::Read(Vector("board.mode.json"), &why);
  CHECK(p.has_value());
  if (!p)
    return;
  CHECK(p->board.size() == 9);
  const board::ModeHead head = mode::hud::BoardHead(*p);
  CHECK(head.serial == 1 && head.count == 9 && head.name_length == 5 && std::memcmp(head.name.data(), "Board", 5) == 0);
  CHECK(head.hash[0] == 0x0C && head.hash[1] == 0x45 && p->hash.starts_with("0c45"));
  CHECK(head.lines[1].scope == board::ModeBoardScope::Global && head.lines[1].type == board::ModeBoardType::Real && head.lines[1].column == 1);
  CHECK(head.lines[3].type == board::ModeBoardType::Seat && head.lines[4].type == board::ModeBoardType::Seats);
  CHECK(head.lines[5].scope == board::ModeBoardScope::Seat && head.lines[5].column == 0 && head.lines[6].column == 1);
  CHECK(head.lines[7].scope == board::ModeBoardScope::Team && head.lines[7].label_length == 4 && std::memcmp(head.lines[7].label.data(), "Tank", 4) == 0);
  // the line's words, in the head, the empty one kept; a line without has none and no one is simulation state
  CHECK(head.lines[8].word_count == 3 && head.lines[0].word_count == 0);
  CHECK(head.words[8 * 16].length == 0 && head.words[8 * 16 + 1].length == 4 && std::memcmp(head.words[8 * 16 + 1].text.data(), "S\xC3\xBBr", 4) == 0);
  CHECK(head.words[8 * 16 + 2].length == 3 && std::memcmp(head.words[8 * 16 + 2].text.data(), "Out", 3) == 0 && head.words[8 * 16 + 3].length == 0);
  // and on a status board: a launcher's read of the head has them, at the layout's place
  alignas(64) static u8 file[board::SIZE];
  board::View view(file);
  view.WriteModeHead(head);
  auto read = std::make_unique<board::ModeHead>();
  CHECK(view.ReadModeHead(read.get()));
  CHECK(read->lines[8].word_count == 3 && read->words[8 * 16 + 2].length == 3);
  CHECK(std::memcmp(file + board::at::MODE_HEAD + 1144 + (8 * 16 + 2) * 20 + 4, "Out", 3) == 0);
  mode::Package long_name = *p;
  long_name.name = std::string(63, 'x') + "\xC3\xA9";
  CHECK(mode::hud::BoardHead(long_name).name_length == 63);

  TestHost host;
  ModData data;
  Guest g(host, data);
  FakeModeRace(g, 4, 100);
  for (int run = 0; run < 3; ++run)
    mode::Run(g, *p, BLOCK, MODULE);
  const auto before = host.ram;
  board::ModeValues a, b;
  mode::hud::BoardValues(g, *p, BLOCK, &a);
  mode::hud::BoardValues(g, *p, BLOCK, &b);
  CHECK(host.ram == before);
  CHECK(a.globals[0].value == 10);                                       // next_cut: 7, one more a run
  CHECK(std::bit_cast<float>(a.globals[1].value) == 1.5f);               // pace
  CHECK(a.globals[3].value == -1);                                       // leader: none
  CHECK(a.seats[2 * 8].value == static_cast<s32>(g.Read32(BLOCK + mode::SeatWord(2, 0))));
  CHECK(a.seats[29 * 8].value == static_cast<s32>(g.Read32(BLOCK + mode::SeatWord(29, 0))));
  CHECK(a.teams[3 * 8].value == 6);                                      // the team word: seat 3's, its own team
  CHECK(std::memcmp(&a, &b, sizeof a) == 0);
}

// Practice's file: its words as the session reads them, its restore a parameter on the seat's own flag; a package with
// no board reads as before; a practice's undo: the mode's block, run again from the moment before, is the same block
// (the rewind, a load and a resume on the same inputs all are that).
void TestPracticeIsAMode()
{
  BEGIN_TEST();
  std::string why;
  const std::optional<mode::Package> p = mode::Read(Vector("practice.mode.json"), &why);
  CHECK(p.has_value());
  if (!p)
    return;
  CHECK(p->solo && p->pause && p->save_state && p->rewind == practice::WINDOW && p->resume_countdown == practice::COUNTDOWN);
  CHECK(p->laps == 0u && p->board.empty() && !p->restore && p->params.size() == 2 && p->params[0] == 0 && p->params[1] == 0);
  // the page's Laps row: the second parameter, resolved into the mode's laps before the race
  picojson::value counted = Vector("practice.mode.json");
  counted.get<picojson::object>()["params"].get<picojson::array>()[1] = picojson::value(7.0);
  const std::optional<mode::Package> seven = mode::Read(counted, &why);
  CHECK(seven && seven->laps == 7u);
  counted.get<picojson::object>()["params"].get<picojson::array>()[1] = picojson::value(100.0);
  CHECK(!mode::Read(counted, &why));
  const std::optional<mode::Package> over = mode::Read(Vector("overdrive.mode.json"), &why);
  CHECK(over && !over->solo && !over->pause && over->rewind == 0 && over->board.empty());

  // a mode rule running with the practice's words: Overdrive's own package, run, kept, run again
  TestHost host;
  ModData data;
  Guest g(host, data);
  FakeModeRace(g, 1, 100);
  for (int run = 0; run < 40; ++run)
    mode::Run(g, *over, BLOCK, MODULE);
  const auto kept = host.ram;
  for (int run = 0; run < 40; ++run)
    mode::Run(g, *over, BLOCK, MODULE);
  const auto straight = host.ram;
  host.ram = kept;
  for (int run = 0; run < 40; ++run)
    mode::Run(g, *over, BLOCK, MODULE);
  CHECK(host.ram == straight);
}

// A solo mode's words in a match: a practice takes it and refuses a mode without; a race with a relay refuses it.
void TestSoloModesAndTheRelay()
{
  BEGIN_TEST();
  TestHost host;
  const std::string dir = (std::filesystem::temp_directory_path() / "fzgx-test-solo").string();
  std::filesystem::create_directories(dir);
  std::string image(0x100, '\0');
  image.replace(0, 4, "FZMD");
  image.replace(0x10, 4, "FZSI");
  WriteText(dir + "/mod.bin", image);
  WriteText(dir + "/manifest.json", "{\"commit\": \"abc1234\"}");
  WriteText(dir + "/codes.txt", "$Boot to race\n48000000 800030C8\n$Race setup\nC0000000 00000001\n4E800020 00000000\n");
  const auto match = [](const char* vector, const std::string& relay) {
    return R"({"matchId": "m7", "seat": 0, )" + relay + R"("setup": {"course": 8, "laps": 3, "seats": [{"seat": 0, "machine": 5, "playerId": "p1", "name": "A"}], "rules": {"restore": true}, "mode": )" +
           Vector(vector).serialize() + "}}";
  };
  const std::string relayed = R"("token": "1f2e3d4c5b6a7988", "relay": {"host": "127.0.0.1", "port": 47811}, )";
  Options options;
  options.bundle_dir = dir;
  Init(host, options);
  CHECK(!boot::ReadMatch(match("practice.mode.json", relayed), "test"));
  CHECK(boot::ReadMatch(match("overdrive.mode.json", relayed), "test").has_value());
  Shutdown();
  options.practice = true;
  Init(host, options);
  const auto practice = boot::ReadMatch(match("practice.mode.json", ""), "test");
  CHECK(practice && practice->game_mode && practice->game_mode->solo);
  if (practice)  // laps unlimited over the match's 3: the game's free run; the rule's restore off the seat's own
  {
    const u8* r = practice->race.data();
    CHECK(r[19] == 0 && r[23] == 3 && r[boot::RACE_RESTORE + 3] == 0);
  }
  CHECK(!boot::ReadMatch(match("overdrive.mode.json", ""), "test"));
  Shutdown();
  std::filesystem::remove_all(dir);
}

// `sqrt` counts modes::FSQRT_COST, in both loops, and the budget's fault lands on the same operation with the same
// memory in both, whether it is a square root that passes the budget or the operation after one.
// A setup.mode field this build does not know (a later version's) is no refusal: a match reads it, a recording keeps it
// in its header's match as it came, and a replay that holds it plays.
void TestAnUnknownModeFieldPassesThrough()
{
  BEGIN_TEST();
  TestHost host;
  const std::string dir = Temp();
  stat_packs::WriteABundle(dir);
  picojson::value mode = Vector("overdrive.mode.json");
  mode.get<picojson::object>()["fromLater"] = picojson::value(7.0);
  const std::string member = "\"mode\": " + mode.serialize() + ", ";
  Options options;
  options.bundle_dir = dir;
  Init(host, options);
  const auto race = boot::ReadMatch(R"({"matchId": "m7", "seat": 0, "token": "1f2e3d4c5b6a7988", "relay": {"host": "127.0.0.1", "port": 47811},
    "setup": {"course": 8, "laps": 3, "shuffleSeed": 1, "cpuSeed": 2, "seats": [{"seat": 0, "machine": 5, "playerId": "p1", "name": "A"},
    {"seat": 1, "machine": 7}], )" + member.substr(0, member.size() - 2) + "}}", "test");
  CHECK(race && race->game_mode);
  Shutdown();
  options.play_file = dir + "/later.fzrp";
  WriteReplay(options.play_file, {1, 0}, member, ", \"simGenerator\": true");
  std::string file;
  CHECK(ReadFileToString(options.play_file, file) && file.find("\"fromLater\":7") != std::string::npos);
  file[5] = 9;
  WriteText(options.play_file, file);
  Init(host, options);
  CHECK(Active() && boot::Get() && boot::Get()->game_mode);
  Shutdown();
  Init(host, Options{});
  std::filesystem::remove_all(dir);
}

void TestASquareRootsCostInBothLoops()
{
  BEGIN_TEST();
  std::string why;
  std::optional<mode::Package> base = mode::Read(Vector("overdrive.mode.json"), &why);
  CHECK(base.has_value());
  if (!base)
    return;
  CHECK(modes::FSQRT_COST == 8);
  const auto with_code = [&](const std::vector<u8>& code) {
    mode::Package p = *base;
    p.globals.clear();
    p.seat_words.clear();
    p.team_words.clear();
    mode::Rule rule;
    rule.label = "R";
    rule.event = modes::EVENT_EVERYSTEP;
    rule.body = 0;
    p.rules = {rule};
    p.code = code;
    mode::Prepare(p);
    return p;
  };
  const auto run = [&](const mode::Package& p, bool prepared, u32* ops, u32* fault) {
    mode::Package q = p;
    if (!prepared)
      q.prepared = {};
    TestHost host;
    ModData data;
    Guest g(host, data);
    FakeModeRace(g, 1, 100);
    mode::Run(g, q, BLOCK, MODULE);
    *ops = g.Read32(BLOCK + mode::OPS);
    *fault = g.Read32(BLOCK + mode::FAULT);
    return BlockOf(g);
  };
  const std::vector<u8> push = {modes::OP_PUSH, 0x40, 0x80, 0, 0};  // 4.0f
  const auto then = [&](std::initializer_list<u8> rest, u32 pad) {
    std::vector<u8> code;
    for (u32 i = 0; i < pad; ++i)
    {
      code.insert(code.end(), push.begin(), push.end());
      code.push_back(modes::OP_POP);
    }
    code.insert(code.end(), rest);
    return code;
  };
  // one square root against one operation that costs one: seven more, in both
  std::vector<u8> root = push, neg = push;
  root.insert(root.end(), {modes::OP_FSQRT, modes::OP_POP, modes::OP_END});
  neg.insert(neg.end(), {modes::OP_FNEG, modes::OP_POP, modes::OP_END});
  for (const bool prepared : {false, true})
  {
    u32 a = 0, b = 0, fault = 0;
    run(with_code(root), prepared, &a, &fault);
    CHECK(fault == mode::FAULT_NONE);
    run(with_code(neg), prepared, &b, &fault);
    CHECK(a == b + modes::FSQRT_COST - 1);
  }
  // a loop of them to the budget, its start moved on by a few operations so that the budget falls on each of the
  // loop's operations: the square root's seven and the others'
  std::set<u32> ends;
  for (u32 pad = 0; pad < 12; ++pad)
  {
    std::vector<u8> code = then({}, pad);
    const u32 loop = static_cast<u32>(code.size());
    code.insert(code.end(), push.begin(), push.end());
    code.insert(code.end(), {modes::OP_FSQRT, modes::OP_POP, modes::OP_JUMP, 0, 0, static_cast<u8>(loop >> 8), static_cast<u8>(loop)});
    const mode::Package p = with_code(code);
    u32 ops_plain = 0, ops_prepared = 0, fault_plain = 0, fault_prepared = 0;
    const std::vector<u8> plain = run(p, false, &ops_plain, &fault_plain);
    const std::vector<u8> prepared = run(p, true, &ops_prepared, &fault_prepared);
    CHECK(fault_plain == mode::FAULT_BUDGET && fault_prepared == mode::FAULT_BUDGET);
    CHECK(ops_plain == ops_prepared && plain == prepared);
    ends.insert(ops_plain);
  }
  CHECK(ends.size() > 1);  // (a sqrt's fault overshoots the budget by up to seven; another's by one)
}

// The cut race's words as a package gives them: race_waits, results points and the title, and the clock's use.
void TestTheCutRaceWords()
{
  BEGIN_TEST();
  std::string why;
  std::optional<mode::Package> p = mode::Read(Vector("cuts.mode.json"), &why);
  CHECK(p.has_value());
  if (!p)
    return;
  CHECK(p->race_waits && p->results_points);
  CHECK(p->results_title == "KNOCKOUT");
  CHECK(mode::hud::UsesClock(*p));
  CHECK((mode::hud::WindowFlags(*p) & mode::hud::POINTS_ONLY) != 0);
  std::optional<mode::Package> old = mode::Read(Vector("overdrive.mode.json"), &why);
  CHECK(old && !old->race_waits && !old->results_points && old->results_title.empty() && !mode::hud::UsesClock(*old));
  CHECK(old && !(mode::hud::WindowFlags(*old) & mode::hud::POINTS_PAGE));
}

// The on/off setup words as a switch parameter gives them, and the title with its space.
void TestTheOnOffWordsByParameter()
{
  BEGIN_TEST();
  std::string why;
  std::optional<mode::Package> p = mode::Read(Vector("switches.mode.json"), &why);
  CHECK(p.has_value());
  if (!p)
    return;
  CHECK(p->boost_off && p->contact_off && p->restore == true && p->cpus_allowed);
  CHECK(p->mine_regen == static_cast<u32>(modes::SETUP_MINEREGEN_ON));
  CHECK(p->results_title == "A B");
  // a title with no award still takes the results through the points page, the title's only place
  CHECK(mode::hud::WindowFlags(*p) & mode::hud::POINTS_PAGE);
  picojson::array flipped = {picojson::value(1.0), picojson::value(0.0)};
  std::optional<mode::Package> q = ReadWith("switches.mode.json", "params", picojson::value(flipped));
  CHECK(q.has_value());
  if (!q)
    return;
  CHECK(!q->boost_off && !q->contact_off && q->restore == false && !q->cpus_allowed);
  CHECK(q->mine_regen == 0u);
}

// A mode without `cpus on`: a player seat with the "AI drives" record retires at that step (flag set, its Restore
// cleared), once, a seat that has driven or one never ready from the start; a finished seat keeps its finish; an autopilot's
// marked record and all zeros and a seat that is no player's stay as they are; `cpus on` and `solo` never retire anyone;
// the same memory from a run again.
void TestAGonePlayerRetires()
{
  BEGIN_TEST();
  std::string why;
  std::optional<mode::Package> p = mode::Read(Vector("board.mode.json"), &why);
  std::optional<mode::Package> with_cpus = mode::Read(Vector("overdrive.mode.json"), &why);
  std::optional<mode::Package> solo = mode::Read(Vector("practice.mode.json"), &why);
  CHECK(p && with_cpus && solo);
  if (!p || !with_cpus || !solo)
    return;
  CHECK(!p->cpus_allowed && with_cpus->cpus_allowed && !solo->cpus_allowed);
  const auto row = [](Guest& g, u32 step, u32 seat, bool ai) {
    const u32 at = layout::INPUTS + ((step & 1023) * 30 + seat) * 8;
    const std::array<u8, 6> real = {0, 0, 0x80, 0x80, 0, 0}, gone = {0, 0x80, 0, 0, 0, 0};
    for (u32 k = 0; k < 6; ++k)
      g.Write8(at + k, ai ? gone[k] : real[k]);
  };
  const auto race = [&](mode::Package q, TestHost& host, ModData& data, std::function<void(Guest&)> check) {
    q.players = 0xF;  // seats 0 to 3 are players', 4 is no player's
    Guest g(host, data);
    FakeModeRace(g, 5, 100);
    for (u32 seat = 0; seat < 5; ++seat)
      g.Write32(MachineAt(seat), 0);
    g.Write32(MachineAt(1), game::MACHINE_FLAG_FINISHED);
    for (u32 step = 0; step < 6; ++step)
    {
      g.Write32(net::STEP, step);
      // seat 0 and 1 drive to step 2 and are gone from 3; seat 2 is the AI's from the start; seat 3 drives on; seat 4 no player's
      row(g, step, 0, step >= 3);
      row(g, step, 1, step >= 3);
      row(g, step, 2, true);
      row(g, step, 3, false);
      row(g, step, 4, true);
      mode::Run(g, q, BLOCK, MODULE);
      if (step == 0 && !q.cpus_allowed && !q.solo)
        CHECK(g.Read32(MachineAt(2)) & game::MACHINE_FLAG_RETIRED);   // never ready: out from the start
      if (step == 2)
        CHECK(!(g.Read32(MachineAt(0)) & game::MACHINE_FLAG_RETIRED));
      if (step == 3)
        check(g);
    }
    check(g);
  };
  {
    TestHost host;
    ModData data;
    race(*p, host, data, [](Guest& g) {
      CHECK(g.Read32(MachineAt(0)) & game::MACHINE_FLAG_RETIRED);
      CHECK(!(g.Read32(MachineAt(0)) & game::MACHINE_FLAG_RESTORE));
      CHECK(!(g.Read32(MachineAt(1)) & game::MACHINE_FLAG_RETIRED));
      CHECK(g.Read32(MachineAt(2)) & game::MACHINE_FLAG_RETIRED);
      CHECK(!(g.Read32(MachineAt(3)) & game::MACHINE_FLAG_RETIRED));
      CHECK(!(g.Read32(MachineAt(4)) & game::MACHINE_FLAG_RETIRED));
    });
  }
  for (const mode::Package* q : {&*with_cpus, &*solo})
  {
    TestHost host;
    ModData data;
    race(*q, host, data, [](Guest& g) {
      CHECK(!(g.Read32(MachineAt(0)) & game::MACHINE_FLAG_RETIRED));
      CHECK(!(g.Read32(MachineAt(2)) & game::MACHINE_FLAG_RETIRED));
    });
  }
  // a test game's autopilot: the record with its last byte marked drives, and retires only when the relay's comes
  {
    TestHost host;
    ModData data;
    Guest g(host, data);
    mode::Package q = *p;
    q.players = 1;
    FakeModeRace(g, 1, 100);
    g.Write32(MachineAt(0), 0);
    for (u32 step = 0; step < 4; ++step)
    {
      g.Write32(net::STEP, step);
      row(g, step, 0, true);
      if (step < 3)
        g.Write8(layout::INPUTS + (step & 1023) * 30 * 8 + 5, 1);
      mode::Run(g, q, BLOCK, MODULE);
      CHECK(((g.Read32(MachineAt(0)) & game::MACHINE_FLAG_RETIRED) != 0) == (step == 3));
    }
  }
}

// seat.stat.* and damage on a made-up race (the wrapper that takes the values into the machine, and the game's own damage,
// are the image's: test_mode_sites.py): the record, what a stat read and the machine's own give, the pack's ranges, a real
// that is not finite ending the race, a weight that keeps the speed, and a step run again giving the same block.
void TestTheStatWords()
{
  BEGIN_TEST();
  std::string why;
  const std::optional<mode::Package> p = mode::Read(Vector("stats.mode.json"), &why);
  CHECK(p.has_value());
  if (!p)
    return;
  TestHost host;
  ModData data;
  Guest g(host, data);
  FakeModeRace(g, 4, 600);
  const auto real = [](float f) { return std::bit_cast<u32>(f); };
  const auto stat = [](u32 seat) { return BLOCK + mode::STATS + seat * mode::STAT; };
  const auto machine = [](u32 seat) { return MACHINES + seat * game::MACHINE_SIZE; };
  for (u32 seat = 0; seat < 4; ++seat)
  {
    for (const u32 field : mode::STAT_FIELDS)
      g.Write32(machine(seat) + field, real(0.5f));
    g.Write32(machine(seat) + 0x8, real(1000.0f));              // the weight
    g.Write32(machine(seat) + 0x94, real(200.0f));              // the momentum
    g.Write32(machine(seat) + 0x98, real(-100.0f));
    g.Write32(machine(seat) + 0x9C, real(50.0f));
  }
  mode::Run(g, *p, BLOCK, MODULE);                              // (not yet GO's: nothing is written)
  CHECK(g.Read32(BLOCK + mode::END) == 0);
  CHECK(g.Read32(stat(0)) == 1023 && g.Read32(stat(3)) == 1023);
  const auto value = [&](u32 seat, u32 i) { return std::bit_cast<float>(g.Read32(stat(seat) + mode::STAT_VALUES + 4 * i)); };
  const auto base = [&](u32 seat, u32 i) { return std::bit_cast<float>(g.Read32(stat(seat) + mode::STAT_BASE + 4 * i)); };
  // (body: base + 1, then += 1 reads the step as it stood, language 6: 1.5, not 2.5)
  CHECK(value(0, 0) == 2000.0f && value(0, 1) == 0.75f && value(0, 2) == 0.25f && value(0, 9) == 1.5f);
  for (u32 i = 0; i < mode::STAT_FIELDS.size(); ++i)
    CHECK(base(0, i) == (i == 0 ? 1000.0f : 0.5f));
  // the machine's field is the wrapper's to write; the weight's change has scaled the momentum, so the speed stays
  CHECK(std::bit_cast<float>(g.Read32(machine(0) + 0x8)) == 1000.0f);
  CHECK(std::bit_cast<float>(g.Read32(machine(0) + 0x94)) == 400.0f && std::bit_cast<float>(g.Read32(machine(0) + 0x98)) == -200.0f);
  // the pack's ranges (protocol/stats.toml): drag 0.15, boost length below 4.26, boost strength 220 x 0.57, weight at least 1
  CHECK(value(1, 6) == 0.15f && value(1, 8) < 4.26f && value(1, 8) > 4.25f);
  CHECK(value(1, 7) == static_cast<float>(static_cast<double>(220.0f) * 0.57));
  CHECK(value(1, 0) == 1.0f);
  // damage: asked for in the record, the run's amounts together
  for (u32 seat = 0; seat < 4; ++seat)
    CHECK(g.Read32(BLOCK + mode::RECORDS + seat * mode::RECORD) & mode::DAMAGE);
  CHECK(std::bit_cast<float>(g.Read32(stat(2) + mode::STAT_DAMAGE)) == 6.0f);
  // the next run: the wrapper has written the values; the machine's own stays what it was, and the record's bits stay
  for (u32 i = 0; i < mode::STAT_FIELDS.size(); ++i)
    g.Write32(machine(0) + mode::STAT_FIELDS[i], g.Read32(stat(0) + mode::STAT_VALUES + 4 * i));
  const std::vector<u8> before = BlockOf(g);
  g.Write32(MODULE + game::RACE_CLOCK, 601);
  mode::Run(g, *p, BLOCK, MODULE);
  CHECK(base(0, 0) == 1000.0f && value(0, 0) == 2000.0f && value(0, 2) == 0.25f);
  CHECK(!(g.Read32(BLOCK + mode::RECORDS) & mode::DAMAGE));     // (`go`'s one run)
  CHECK(std::bit_cast<float>(g.Read32(machine(0) + 0x94)) == 400.0f);  // no change of weight, no change of momentum
  // a step run again from the same memory: the same block
  const std::vector<u8> after = BlockOf(g);
  g.CopyTo(BLOCK, before.data(), before.size());
  mode::Run(g, *p, BLOCK, MODULE);
  CHECK(BlockOf(g) == after);
  // a real that is not finite is a mode error, for every game alike
  picojson::array bad = {picojson::value(1.0), picojson::value(5.0)};
  const std::optional<mode::Package> q = ReadWith("stats.mode.json", "params", picojson::value(bad));
  CHECK(q.has_value());
  if (!q)
    return;
  TestHost host2;
  ModData data2;
  Guest g2(host2, data2);
  FakeModeRace(g2, 4, 600);
  mode::Run(g2, *q, BLOCK, MODULE);
  CHECK(g2.Read32(BLOCK + mode::END) == 1 && g2.Read32(BLOCK + mode::FAULT) == mode::FAULT_REAL);
}

// The combat values' live words (the sites that read them: test_mode_sites.py), the time bonus and the points table.
void TestTheCombatWords()
{
  BEGIN_TEST();
  std::string why;
  const std::optional<mode::Package> p = mode::Read(Vector("combat.mode.json"), &why);
  CHECK(p.has_value());
  if (!p)
    return;
  TestHost host;
  ModData data;
  Guest g(host, data);
  FakeModeRace(g, 4, 0);
  const auto word = [&](u32 at) { return g.Read32(BLOCK + mode::COMBAT + at); };
  const auto real = [&](u32 at) { return std::bit_cast<float>(word(at)); };
  mode::Run(g, *p, BLOCK, MODULE);
  CHECK(real(mode::C_DAMAGE) == 2.0f && real(mode::C_FACTOR) == 0.002f * 2.0f && real(mode::C_CAP) == 40.0f);
  CHECK(real(mode::C_KNOCK) == 3.0f && real(mode::C_WALL) == 0.5f && real(mode::C_BOUNCE) == 10.0f && real(mode::C_ONE) == 1.0f);
  CHECK(word(mode::C_SPIN_OFF) == 1 && word(mode::C_SIDE_OFF) == 1 && word(mode::C_IMMUNE) == 1);
  // the game's four bonuses in place; a bonus the game adds to the timer is earned, and moves the end
  constexpr u32 R = 0x1BDC70;
  CHECK(g.Read16(MODULE + R + 0x13FA) == 600 && g.Read16(MODULE + R + 0x1400) == 600);
  g.Write32(MODULE + game::RACE_CLOCK, 30);
  mode::Run(g, *p, BLOCK, MODULE);
  CHECK(g.Read16(MODULE + R + 0xA4) == 7200 - 30);
  g.Write16(MODULE + R + 0xA4, 7200 - 30 + 600);
  g.Write32(MODULE + game::RACE_CLOCK, 60);
  mode::Run(g, *p, BLOCK, MODULE);
  CHECK(g.Read32(BLOCK + mode::CLOCK_EARNED) == 600 && g.Read16(MODULE + R + 0xA4) == 7800 - 60);
  // the live words at the `when`'s run: back to the game's is 0, the ranges hold, a switch flips
  CHECK(word(mode::C_DAMAGE) == 0 && word(mode::C_FACTOR) == 0 && word(mode::C_CAP) == 0);
  CHECK(real(mode::C_KNOCK) == 6.0f && real(mode::C_WALL) == 10.0f && real(mode::C_BOUNCE) == 0.1f);
  CHECK(word(mode::C_SPIN_OFF) == 1 && word(mode::C_SIDE_OFF) == 0 && word(mode::C_IMMUNE) == 0);
  // a step run again gives the same block
  const std::vector<u8> before = BlockOf(g);
  g.Write32(MODULE + game::RACE_CLOCK, 61);
  mode::Run(g, *p, BLOCK, MODULE);
  const std::vector<u8> after = BlockOf(g);
  g.CopyTo(BLOCK, before.data(), before.size());
  mode::Run(g, *p, BLOCK, MODULE);
  CHECK(BlockOf(g) == after);
  // the end at limit + earned, not before; the table pays by the order
  g.Write32(MODULE + game::RACE_CLOCK, 7300);
  mode::Run(g, *p, BLOCK, MODULE);
  CHECK(g.Read32(BLOCK + mode::END) == 0);
  g.Write32(MODULE + game::RACE_CLOCK, 7800);
  mode::Run(g, *p, BLOCK, MODULE);
  CHECK(g.Read32(BLOCK + mode::END) == 2);
  std::vector<u32> paid;
  for (u32 seat = 0; seat < 4; ++seat)
    paid.push_back(g.Read32(BLOCK + mode::POINTS + 4 * seat));
  std::sort(paid.begin(), paid.end());
  CHECK((paid == std::vector<u32>{0, 25, 50, 100}));
  CHECK(mode::hud::WindowFlags(*p) & mode::hud::POINTS_PAGE);
}

// vec and cross: a push sideways, in momentum (km/h x W / 216).
