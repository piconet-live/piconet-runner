// A game mode's displays (mode_hud.h): the protocol's display vector (protocol/tests/vectors/modes/display.fzmode)
// run on a fake race, what the drawing reads from the block for a player and for a spectator, the HUD pass's writes,
// and the course map's dots.
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <bit>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>

#include "harness.h"
#include "mode.h"
#include "mode_hud.h"
#include "net_internal.h"
#include "parts.h"
#include "rollback.h"

namespace
{
namespace hud = mode::hud;

std::optional<mode::Package> DisplayVector()
{
  std::ifstream file(ProtocolFile("protocol/tests/vectors/modes") / "display.mode.json");
  std::stringstream text;
  text << file.rdbuf();
  picojson::value v;
  picojson::parse(v, text.str());
  std::string why;
  return mode::Read(v, &why);
}

constexpr u32 MACHINES = 0x80400000, PROGRESS = 0x80420000, BLOCK = 0x80500000;
constexpr u32 OUT_SEAT = 4;  // (4 % 3 == 1: the looks rule's too)

void FakeDisplayRace(Guest& g)
{
  LinkMainRel(g);
  g.Write32(MODULE + net::MACHINES, MACHINES);
  g.Write8(MODULE + net::MACHINE_COUNT, 30);
  g.Write16(MODULE + game::PHASE, game::PHASE_RACE);
  g.Write32(MODULE + game::RACE_CLOCK, 3000);
  for (u32 i = 0; i < 30; ++i)
  {
    const u32 m = MACHINES + i * game::MACHINE_SIZE;
    g.Write32(m + game::MACHINE_PROGRESS, PROGRESS + i * game::PROGRESS_SIZE);
    g.Write32(m + game::MACHINE_SPEED, std::bit_cast<u32>(400.0f));
  }
  g.Write32(MACHINES + OUT_SEAT * game::MACHINE_SIZE + game::MACHINE_FLAGS, game::MACHINE_FLAG_RETIRED);
}

u32 Record(u32 seat)
{
  return BLOCK + hud::SEATS_AT + seat * hud::SEAT_DISPLAY;
}
}  // namespace

// Every display word into the seat's record, run again from the same memory to the same memory; what a player, a
// spectator and a course map read from it.
void TestTheDisplayWordsRun()
{
  BEGIN_TEST();
  const std::optional<mode::Package> p = DisplayVector();
  CHECK(p.has_value());
  if (!p)
    return;
  TestHost host;
  ModData data;
  Guest g(host, data);
  FakeDisplayRace(g);
  const auto before = host.ram;
  mode::Run(g, *p, BLOCK, MODULE);
  CHECK(g.Read32(BLOCK + mode::END) == 0 && g.Read32(BLOCK + mode::FAULT) == mode::FAULT_NONE);
  const auto after = host.ram;
  host.ram = before;
  mode::Run(g, *p, BLOCK, MODULE);
  CHECK(host.ram == after);
  // show TIMER and DISTANCE, hide DISTANCE: TIMER alone; every seat's number its 3 lives (+1), the out seat's -1
  // first, then the each-seat rule's 3 (the rules' order). 30 seats' displays, three times, in one step: displays
  // take no place in the queue of actions (64)
  for (u32 seat = 0; seat < 30; ++seat)
  {
    CHECK(g.Read32(Record(seat) + hud::PARTS) == 1u << modes::PART_TIMER);
    CHECK(g.Read32(Record(seat) + hud::NUMBER) == 4);
    const bool looked = seat % 3 == 1;
    CHECK(g.Read8(Record(seat) + hud::LOOK) == (looked ? modes::LOOK_GHOST : modes::LOOK_NORMAL));
    CHECK(g.Read8(Record(seat) + hud::OWN_LOOK) == (looked ? 1 + modes::LOOK_NORMAL : 0));
    CHECK(g.Read32(Record(seat) + hud::MAP_HIDE) == (looked ? ((1u << 30) - 1) & ~(1u << seat) : 0));
  }
  CHECK(g.Read32(Record(OUT_SEAT) + hud::COLOUR) == 0xFF2020FF);
  CHECK(g.Read32(Record(OUT_SEAT) + hud::MAP_MARK) == (hud::MARKED | 0xFF0000));
  CHECK(g.Read8(Record(OUT_SEAT) + hud::BANNER) == 1 + modes::BANNER_FINAL_LAP);
  CHECK(g.Read8(Record(0) + hud::BANNER) == 0);
  // the next run: no seat newly out, so no banner asked
  mode::Run(g, *p, BLOCK, MODULE);
  CHECK(g.Read8(Record(OUT_SEAT) + hud::BANNER) == 0);

  // a player in seat 1 (a ghost to everyone, solid to itself); a spectator following seat 1 (no own seat)
  const hud::Frame player = hud::Read(g, BLOCK, 1, 1), watcher = hud::Read(g, BLOCK, 1, 0xFFFFFFFF);
  CHECK(player.parts == hud::PART_BITS[modes::PART_TIMER] && player.digit == 0);
  CHECK(player.looks[1] == modes::LOOK_NORMAL && player.looks[4] == modes::LOOK_GHOST && player.looks[0] == modes::LOOK_NORMAL);
  CHECK(watcher.looks[1] == modes::LOOK_GHOST && watcher.looks[4] == modes::LOOK_GHOST);
  // the danger zone's count: the big digit and the warning, seconds rounded up (120 steps: 179, the digit 2)
  g.Write32(Record(1) + hud::DANGER, 120);
  const hud::Frame danger = hud::Read(g, BLOCK, 1, 1);
  CHECK(danger.parts == (hud::PART_BITS[modes::PART_TIMER] | hud::PART_BITS[modes::PART_DIGIT] |
                         hud::PART_BITS[modes::PART_WARNING]));
  CHECK(danger.digit == 179);
  g.Write32(Record(1) + hud::DANGER, 100000);
  CHECK(hud::Read(g, BLOCK, 1, 1).digit == 239);  // the sheet's last digit, 3
  g.Write32(Record(1) + hud::DANGER, 0);

  // the HUD pass for the player in seat 1: the mask's story bits off but TIMER, the source block, the looks
  g.Write32(layout::MODES + 4, BLOCK);
  g.Write32(net::SEAT_SLOT + 4, 1);
  g.Write32(MODULE + game::HUD_PARTS, 0xFF9FFFFF);
  hud::Hud(g);
  CHECK(g.Read32(MODULE + game::HUD_PARTS) == ((0xFF9FFFFF & ~hud::STORY_BITS) | 0x20000));
  CHECK(g.Read32(MODULE + net::BSS + 0x84) == 0);  // no DIGIT shown: no digit source
  CHECK(g.Read32(MODULE + net::BSS + 0x84 + 8) == PROGRESS + game::PROGRESS_SIZE);
  CHECK(g.Read32(MODULE + net::BSS + 0x84 + 4) == MACHINES + game::MACHINE_SIZE + game::MACHINE_SPEED);
  CHECK(g.Read8(layout::MODE_HUD + hud::WINDOW_LOOKS + 1) == modes::LOOK_NORMAL);
  CHECK(g.Read8(layout::MODE_HUD + hud::WINDOW_LOOKS + 4) == modes::LOOK_GHOST);
  g.Write32(MODULE + game::HUD_PARTS, 0);  // the HUD off (out, finished): it stays off
  hud::Hud(g);
  CHECK(g.Read32(MODULE + game::HUD_PARTS) == 0);

  // the course map: seat 1 and the out seat are hidden from every other racer's map, never from a spectator's
  std::array<bool, 30> present{};
  std::array<u32, 30> colour{};
  present.fill(true);
  hud::MapRacers(g, 0, true, present, colour);
  CHECK(!present[1] && !present[OUT_SEAT] && present[0] && present[2] && colour[OUT_SEAT] == 0xFF0000);
  present.fill(true);
  hud::MapRacers(g, 1, true, present, colour);
  CHECK(present[1] && !present[OUT_SEAT] && !present[7]);
  present.fill(true);
  hud::MapRacers(g, 1, false, present, colour);
  CHECK(present[1] && present[OUT_SEAT] && present[7]);

  // the story's pictures: the vector shows DISTANCE; Overdrive shows nothing
  CHECK(hud::NeedsStoryPictures(p->code) && hud::WindowFlags(*p) == hud::STORY_PICTURES);
}
