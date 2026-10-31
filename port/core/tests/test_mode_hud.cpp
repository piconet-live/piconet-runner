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

// `tag` and `tag off` on the page: a number the package cannot hold is no tag, and a word on one seat touches no other.
void TestTheModeTagWords()
{
  BEGIN_TEST();
  TestHost host;
  ModData data;
  Guest g(host, data);
  CHECK(hud::IsDisplay(modes::ACTION_TAG) && hud::IsDisplay(modes::ACTION_TAGOFF));
  CHECK(hud::Apply(g, BLOCK, modes::ACTION_TAG, 3, 2));
  CHECK(g.Read8(BLOCK + hud::TAGS_AT + 3) == 2);
  CHECK(hud::Read(g, BLOCK, 0, 0).tags[3] == 2);
  CHECK(hud::Read(g, BLOCK, 0, 0).tags[2] == 0 && hud::Read(g, BLOCK, 0, 0).tags[4] == 0);
  CHECK(hud::Apply(g, BLOCK, modes::ACTION_TAG, 4, modes::TAG_TEXTS));
  CHECK(g.Read8(BLOCK + hud::TAGS_AT + 4) == modes::TAG_TEXTS);
  CHECK(hud::Apply(g, BLOCK, modes::ACTION_TAG, 4, modes::TAG_TEXTS + 1));
  CHECK(g.Read8(BLOCK + hud::TAGS_AT + 4) == 0);
  CHECK(hud::Apply(g, BLOCK, modes::ACTION_TAG, 4, 0xFFFFFFFF));
  CHECK(g.Read8(BLOCK + hud::TAGS_AT + 4) == 0);
  CHECK(hud::Apply(g, BLOCK, modes::ACTION_TAGOFF, 3, 0));
  CHECK(g.Read8(BLOCK + hud::TAGS_AT + 3) == 0);
}

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
  // `tag`: the text's number on the out seat's byte (after `tag s off`), none on the others', and no other word moved
  CHECK(g.Read8(BLOCK + hud::TAGS_AT + OUT_SEAT) == 1);
  CHECK(p->tag_texts == std::vector<std::string>{"OUT"});
  for (u32 seat = 0; seat < 30; ++seat)
    if (seat != OUT_SEAT)
      CHECK(g.Read8(BLOCK + hud::TAGS_AT + seat) == 0);
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
  // the danger zone's count (the race side's: mode.h DANGER, 60 x seconds + 60 down to 60): the big digit and the
  // warning, the count as the story's digit value (179: the digit 2); the sheet's last digit, 3, at most; none once
  // done or at 60
  const u32 d = BLOCK + mode::DANGER + 16 * 1;
  g.Write32(d + 12, mode::DANGER_ON | mode::DANGER_ARMED);
  g.Write32(d + 8, 179);
  const hud::Frame danger = hud::Read(g, BLOCK, 1, 1);
  CHECK(danger.parts == (hud::PART_BITS[modes::PART_TIMER] | hud::PART_BITS[modes::PART_DIGIT] |
                         hud::PART_BITS[modes::PART_WARNING]));
  CHECK(danger.digit == 179);
  g.Write32(d + 8, 600);
  CHECK(hud::Read(g, BLOCK, 1, 1).digit == 239);
  g.Write32(d + 8, 60);
  CHECK(hud::Read(g, BLOCK, 1, 1).parts == hud::PART_BITS[modes::PART_TIMER]);
  g.Write32(d + 8, 179);
  g.Write32(d + 12, mode::DANGER_ON | mode::DANGER_DONE);
  CHECK(hud::Read(g, BLOCK, 1, 1).parts == hud::PART_BITS[modes::PART_TIMER]);
  g.Write32(d + 12, 0);
  g.Write32(d + 8, 0);

  // (the pass logs once: to this test's host, not a host of an earlier test that has gone)
  Init(host, Options{});
  // the HUD pass for the player in seat 1: the mask's story bits off but TIMER, the source block, the looks
  g.Write32(layout::MODES + 4, BLOCK);
  g.Write32(net::SEAT_SLOT + 4, 1);
  g.Write32(MODULE + game::HUD_PARTS, 0xFF9FFFFF);
  hud::Hud(g);
  CHECK(g.Read32(MODULE + game::HUD_PARTS) == ((0xFF9FFFFF & ~hud::STORY_BITS) | 0x20000));
  // the standard parts: hide clears the part's bit for the viewed seat only, show puts it back, the stock mask stays,
  // FACES has no bit but the window's word; a seat not viewed holds nothing
  CHECK(g.Read32(layout::MODE_HUD + hud::WINDOW_FACES) == 0);
  for (const u32 part : {modes::PART_RANK, modes::PART_LAPS, modes::PART_SPEED, modes::PART_ENERGY,
                         modes::PART_LAP_TIMES, modes::PART_RACE_TIME})
  {
    CHECK(hud::Apply(g, BLOCK, modes::ACTION_HIDE, 1, part));
    g.Write32(MODULE + game::HUD_PARTS, 0xFF9FFFFF);
    hud::Hud(g);
    CHECK(g.Read32(MODULE + game::HUD_PARTS) == ((0xFF9FFFFF & ~hud::STORY_BITS & ~hud::PART_BITS[part]) | 0x20000));
    CHECK(hud::Apply(g, BLOCK, modes::ACTION_SHOW, 1, part));
    g.Write32(MODULE + game::HUD_PARTS, 0xFF9FFFFF);
    hud::Hud(g);
    CHECK(g.Read32(MODULE + game::HUD_PARTS) == ((0xFF9FFFFF & ~hud::STORY_BITS) | 0x20000));
  }
  CHECK(hud::Apply(g, BLOCK, modes::ACTION_HIDE, 2, modes::PART_RANK));
  CHECK(hud::Apply(g, BLOCK, modes::ACTION_HIDE, 1, modes::PART_FACES));
  g.Write32(MODULE + game::HUD_PARTS, 0xFF9FFFFF);
  hud::Hud(g);
  CHECK(g.Read32(MODULE + game::HUD_PARTS) == ((0xFF9FFFFF & ~hud::STORY_BITS) | 0x20000));
  CHECK(g.Read32(layout::MODE_HUD + hud::WINDOW_FACES) == 1);
  CHECK(hud::Apply(g, BLOCK, modes::ACTION_SHOW, 1, modes::PART_FACES));
  CHECK(hud::Apply(g, BLOCK, modes::ACTION_SHOW, 2, modes::PART_RANK));
  g.Write32(MODULE + game::HUD_PARTS, 0xFF9FFFFF);
  hud::Hud(g);
  CHECK(g.Read32(layout::MODE_HUD + hud::WINDOW_FACES) == 0);
  CHECK(g.Read32(MODULE + net::BSS + 0x84) == 0);  // no DIGIT shown: no digit source
  CHECK(g.Read32(MODULE + net::BSS + 0x84 + 8) == PROGRESS + game::PROGRESS_SIZE);
  CHECK(g.Read32(MODULE + net::BSS + 0x84 + 4) == MACHINES + game::MACHINE_SIZE + game::MACHINE_SPEED);
  CHECK(g.Read8(layout::MODE_HUD + hud::WINDOW_LOOKS + 1) == modes::LOOK_NORMAL);
  CHECK(g.Read8(layout::MODE_HUD + hud::WINDOW_LOOKS + 4) == modes::LOOK_GHOST);
  // the out seat's colour (0xFF2020FF) only tints a machine drawn normally: as a ghost it is not drawn so
  CHECK(g.Read32(layout::MODE_HUD + hud::WINDOW_TINTS + 4 * OUT_SEAT) == 0);
  g.Write8(Record(OUT_SEAT) + hud::LOOK, modes::LOOK_NORMAL);
  hud::Hud(g);
  CHECK(g.Read8(layout::MODE_HUD + hud::WINDOW_LOOKS + OUT_SEAT) == hud::COLOURED);
  CHECK(g.Read32(layout::MODE_HUD + hud::WINDOW_TINTS + 4 * OUT_SEAT) == 0xFF2020FF);
  // the page's tags reach the window for the name tags, whoever is viewed
  CHECK(g.Read8(layout::MODE_HUD + hud::WINDOW_TAGS + OUT_SEAT) == 1);
  CHECK(g.Read8(layout::MODE_HUD + hud::WINDOW_TAGS + 1) == 0);
  CHECK(g.Read32(layout::MODE_HUD + hud::WINDOW_TINTS) == 0);
  g.Write8(Record(OUT_SEAT) + hud::LOOK, modes::LOOK_GHOST);
  g.Write32(MODULE + game::HUD_PARTS, 0);  // the HUD off (out, finished): it stays off
  hud::Hud(g);
  CHECK(g.Read32(MODULE + game::HUD_PARTS) == 0);

  // GAP (seat 1, this game's): the seat's time at its last lap line for the popup, its part for 60 steps; WRONG_WAY:
  // the sign starts once for an ask, however often the frame is drawn
  const u32 p1 = g.Read32(MACHINES + game::MACHINE_SIZE + game::MACHINE_PROGRESS);
  g.Write8(p1 + 0x1E0, 1);
  g.Write8(p1 + 0x1E1, 2);
  g.Write16(p1 + 0x1E2, 3);
  g.Write32(MODULE + game::RACE_CLOCK, 1000);
  g.Write32(MODULE + game::HUD_PARTS, 0xFF9FFFFF);
  CHECK(hud::Apply(g, BLOCK, modes::ACTION_STOCKBANNER, 1, modes::BANNER_GAP));
  CHECK(g.Read32(Record(1) + hud::GAP_TIME) == (1u << 20 | 2u << 12 | 3));
  CHECK(g.Read8(Record(1) + hud::BANNER) == 1 + modes::BANNER_GAP);
  hud::Hud(g);
  CHECK(g.Read32(MODULE + game::HUD_PARTS) & hud::GAP_PART);
  g.Write32(MODULE + game::RACE_CLOCK, 1000 + hud::GAP_STEPS + 1);
  g.Write32(MODULE + game::HUD_PARTS, 0xFF9FFFFF);
  hud::Hud(g);
  CHECK(!(g.Read32(MODULE + game::HUD_PARTS) & hud::GAP_PART));
  CHECK(g.Read32(layout::MODE_HUD + hud::WINDOW_WRONG) == 0);
  CHECK(hud::Apply(g, BLOCK, modes::ACTION_STOCKBANNER, 1, modes::BANNER_WRONG_WAY));
  hud::Hud(g);
  CHECK(g.Read32(layout::MODE_HUD + hud::WINDOW_WRONG) == 1);
  g.Write32(layout::MODE_HUD + hud::WINDOW_WRONG, 0);
  hud::Hud(g);
  CHECK(g.Read32(layout::MODE_HUD + hud::WINDOW_WRONG) == 0);
  g.Write32(MODULE + game::HUD_PARTS, 0);

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

  // show_number's range: 0 to 99 as it is, above 99 clamped to 99, a negative hidden (the record holds number + 1)
  for (const auto& [value, record] : std::array<std::pair<s32, u32>, 6>{{{0, 1}, {99, 100}, {100, 100}, {12345, 100}, {-1, 0}, {-50, 0}}})
  {
    CHECK(hud::Apply(g, BLOCK, modes::ACTION_SHOWNUMBER, 2, static_cast<u32>(value)));
    CHECK(g.Read32(Record(2) + hud::NUMBER) == record);
  }

  // the story's pictures: the vector shows DISTANCE
  CHECK(hud::NeedsStoryPictures(p->code) && hud::WindowFlags(*p) == hud::STORY_PICTURES);
  // the race vector awards points and runs danger zones: the points page and the story's pictures
  {
    std::ifstream file(ProtocolFile("protocol/tests/vectors/modes") / "race.mode.json");
    std::stringstream text;
    text << file.rdbuf();
    picojson::value v;
    picojson::parse(v, text.str());
    std::string why;
    const std::optional<mode::Package> race = mode::Read(v, &why);
    CHECK(race.has_value() && hud::WindowFlags(*race) == (hud::STORY_PICTURES | hud::POINTS_PAGE | hud::ROLLING));
  }
  // start_together: its one flag, read by the image's placing and preload
  {
    std::ifstream file(ProtocolFile("protocol/tests/vectors/modes") / "together.mode.json");
    std::stringstream text;
    text << file.rdbuf();
    picojson::value v;
    picojson::parse(v, text.str());
    std::string why;
    const std::optional<mode::Package> together = mode::Read(v, &why);
    CHECK(together.has_value() && together->start_together && hud::WindowFlags(*together) == hud::TOGETHER);
  }
}
