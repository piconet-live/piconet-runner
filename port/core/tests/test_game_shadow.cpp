// The game's drop shadow with rays on (game_shadow.h): bit 0x4 of each machine's CarShadow record is cleared, the rest
// of the record and the records past the race's machines are left alone; rays off, and before main.rel is linked,
// nothing is written.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "harness.h"
#include "game_shadow.h"

namespace
{
constexpr u32 RECORDS = 0x80900000;  // a heap place for the array, as fn_1_82724's allocation gives one

// three machines placed this frame (flags 0x4 and others), a fourth record past the race's machines
void Placed(Guest& g)
{
  LinkMainRel(g);
  g.Write32(game::CAR_SHADOWS_AT, RECORDS);
  g.Write8(game::MACHINE_COUNT_AT, 3);
  for (u32 r = 0; r < 4; ++r)
  {
    g.Write32(RECORDS + r * game::CAR_SHADOW_SIZE, 0x4 | 0x1 | (r << 8));
    g.Write32(RECORDS + r * game::CAR_SHADOW_SIZE + 8, 0x3F000000);  // its alpha
  }
}
}  // namespace

void TestTheGameShadowWithRays()
{
  BEGIN_TEST();
  TestHost host;
  ModData data;
  Guest g(host, data);
  // rays off: as the game left it
  Placed(g);
  game_shadow::Frame(g, false);
  CHECK(g.Read32(RECORDS) == 0x5 && g.Read32(RECORDS + 2 * game::CAR_SHADOW_SIZE) == 0x205);
  // rays on: the race's three lose bit 0x4 alone; the fourth and every other word stay
  game_shadow::Frame(g, true);
  bool cleared = true;
  for (u32 r = 0; r < 3; ++r)
    cleared = cleared && g.Read32(RECORDS + r * game::CAR_SHADOW_SIZE) == (0x1 | (r << 8)) &&
              g.Read32(RECORDS + r * game::CAR_SHADOW_SIZE + 8) == 0x3F000000;
  CHECK(cleared);
  CHECK(g.Read32(RECORDS + 3 * game::CAR_SHADOW_SIZE) == (0x5 | (3 << 8)));
  // no array yet, or main.rel not linked: nothing
  Placed(g);
  g.Write32(game::CAR_SHADOWS_AT, 0);
  game_shadow::Frame(g, true);
  CHECK(g.Read32(RECORDS) == 0x5);
  Placed(g);
  g.Write32(net::MODULE_POINTER, 0);
  game_shadow::Frame(g, true);
  CHECK(g.Read32(RECORDS) == 0x5);
}
