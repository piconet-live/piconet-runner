// The game mode's reader and interpreter (mode.h): the protocol's vectors (protocol/tests/vectors/modes/, the
// compiler's own packages) read as the match start carries them, and Overdrive run on a fake race.
// SPDX-License-Identifier: GPL-3.0-or-later

#include <bit>
#include <fstream>
#include <sstream>
#include <vector>

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
