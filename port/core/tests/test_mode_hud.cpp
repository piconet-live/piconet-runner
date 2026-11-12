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
#include "mode_gap.h"
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

// The time gaps from the seats' distances.
void TestTheModeTimeGaps()
{
  BEGIN_TEST();
  TestHost host;
  ModData data;
  Guest g(host, data);
  FakeDisplayRace(g);

  // seats 0 and 1 run the same road, seat 0 five steps ahead, then seat 1 gains; lap length 6400: 100 a mini-sector
  const auto at = [&](u32 seat, float distance) {
    const u32 p = PROGRESS + seat * game::PROGRESS_SIZE;
    g.Write32(p + game::PROGRESS_DISTANCE, std::bit_cast<u32>(distance));
    g.Write32(p + game::PROGRESS_LAP_LENGTH, std::bit_cast<u32>(6400.0f));
  };
  for (u32 seat = 0; seat < 30; ++seat)
    at(seat, -1.0f);  // (behind the line: none yet)
  CHECK(!hud::TimeGapOf(g, BLOCK, 1, 0).known);
  u32 run = 100;
  const auto step = [&](float zero, float one) {
    at(0, zero), at(1, one);
    hud::NoteGaps(g, BLOCK, MODULE, ++run);
  };
  // every five steps a mini-sector: seat 1 five steps behind at first
  for (u32 i = 0; i < 12; ++i)
    for (u32 k = 0; k < 5; ++k)
      step(static_cast<float>(i) * 100 + static_cast<float>(k) * 20, i == 0 ? 0.0f : static_cast<float>(i - 1) * 100 + static_cast<float>(k) * 20);
  hud::TimeGap gap = hud::TimeGapOf(g, BLOCK, 1, 0);
  CHECK(gap.known && gap.steps == 5 && gap.laps == 0 && gap.trend == 0);
  CHECK(hud::TimeGapOf(g, BLOCK, 0, 1).steps == -5);
  CHECK(!hud::TimeGapOf(g, BLOCK, 2, 0).known && !hud::TimeGapOf(g, BLOCK, 0, 0).known);
  // on the same mini-sector, whoever came first is ahead
  at(2, 300.0f);
  hud::NoteGaps(g, BLOCK, MODULE, ++run);
  at(3, 300.0f);
  hud::NoteGaps(g, BLOCK, MODULE, ++run);
  CHECK(hud::TimeGapOf(g, BLOCK, 2, 3).steps == -1 && hud::TimeGapOf(g, BLOCK, 3, 2).steps == 1);
  // driving back changes nothing
  const u32 row = g.Read16(BLOCK + hud::GapRow(0, 5));
  step(100, 1000);
  CHECK(g.Read16(BLOCK + hud::GapRow(0, 5)) == row);
  // seat 1 at half seat 0's speed: losing time
  for (u32 i = 12; i < 30; ++i)
    for (u32 k = 0; k < 5; ++k)
      step(static_cast<float>(i) * 100 + static_cast<float>(k) * 20, 1100.0f + static_cast<float>(i - 12) * 50 + static_cast<float>(k) * 10);
  gap = hud::TimeGapOf(g, BLOCK, 1, 0);
  CHECK(gap.known && gap.steps > 5 && gap.trend > 0);
  // a lap and more ahead: says so
  step(6400 * 3, 2900);
  CHECK(hud::TimeGapOf(g, BLOCK, 1, 0).laps == 2 && hud::TimeGapOf(g, BLOCK, 0, 1).laps == 2);
}

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
  CHECK(hud::Read(g, BLOCK, 0).tags[3] == 2);
  CHECK(hud::Read(g, BLOCK, 0).tags[2] == 0 && hud::Read(g, BLOCK, 0).tags[4] == 0);
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
// The owner's words of 2026-10-31: show_number off, the ghost's tint and sound, and the live gap's table.
void TestTheGhostWords()
{
  BEGIN_TEST();
  TestHost host;
  ModData data;
  Guest g(host, data);
  FakeDisplayRace(g);
  CHECK(hud::Apply(g, BLOCK, modes::ACTION_SHOWNUMBER, 2, 7));
  CHECK(g.Read32(Record(2) + hud::NUMBER) == 8);
  CHECK(hud::Apply(g, BLOCK, modes::ACTION_SHOWNUMBEROFF, 2, 0));
  CHECK(g.Read32(Record(2) + hud::NUMBER) == 0);
  // the look's defaults are Time Attack's: yellow (a word of 0) and no voice, the viewed racer always heard
  CHECK(hud::Apply(g, BLOCK, modes::ACTION_SEATLOOK, 3, modes::LOOK_GHOST));
  CHECK(hud::Apply(g, BLOCK, modes::ACTION_SEATLOOK, 4, modes::LOOK_GHOST));
  hud::Frame f = hud::Read(g, BLOCK, 4);
  CHECK(f.ghost_tints[3] == 0 && f.silent == 1u << 3);
  CHECK(hud::Apply(g, BLOCK, modes::ACTION_SEATGHOSTTINT, 3, 0x80FF80FF));
  CHECK(hud::Apply(g, BLOCK, modes::ACTION_SEATGHOSTSOUND, 3, 1));
  f = hud::Read(g, BLOCK, 4);
  CHECK(f.ghost_tints[3] == 0x80FF80FF && f.silent == 0);
  CHECK(hud::Apply(g, BLOCK, modes::ACTION_SEATGHOSTSOUND, 3, 0));
  CHECK(hud::Read(g, BLOCK, 4).silent == 1u << 3);
  CHECK(!hud::Read(g, BLOCK, 4).ghost_gap);
  CHECK(hud::Apply(g, BLOCK, modes::ACTION_SHOW, 4, modes::PART_GHOST_GAP));
  CHECK(hud::Read(g, BLOCK, 4).ghost_gap);
  // a stock race's parts are untouched by the part: it has no bit
  CHECK(hud::PART_BITS[modes::PART_GHOST_GAP] == 0 && hud::PART_BITS[modes::PART_RANK] == 0x20000000);

  // the gap: other ahead, how long ago it was where you are; you ahead, how long ago you were where it is
  hud::GapTable t;
  t.Note(0, 10, 100);
  t.Note(1, 10, 100);
  CHECK(t.Frames(0, 1, 100) == 0);
  t.Note(1, 20, 110);
  t.Note(0, 15, 110);
  CHECK(t.Frames(0, 1, 110) == 0);     // seat 1 passed 15 in the same frame: no time between them yet
  t.Note(0, 20, 130);
  t.Note(1, 20, 130);
  CHECK(t.Frames(0, 1, 130) == 0);
  t.Note(0, 30, 140);
  CHECK(t.Frames(1, 0, 140) == 10);    // seat 0 ahead: it was at 20 at step 130
  CHECK(t.Frames(0, 1, 140) == -10);
  // a step going back drops what came later; an entry from the future counts for nothing
  t.Note(0, 18, 120);
  CHECK(t.Bucket(0) == 18);
  t.Note(0, 30, 125);
  CHECK(t.Frames(1, 0, 125) == 0);
  t.Note(1, 25, 135);
  t.Note(0, 30, 135);
  CHECK(t.Frames(1, 0, 135) == 10);    // seat 0 was at 25 by the end of step 125
  const hud::GapPicture ahead = hud::GapOf(3600 + 60 + 6, false);
  CHECK(ahead.colour == 0xFF808000 && ahead.sign == 0x3A && ahead.minutes == 1 && ahead.seconds == 1 && ahead.thousandths == 100);
  CHECK(hud::GapOf(-60, false).colour == 0x8080FF00 && hud::GapOf(-60, false).sign == 0x3B && hud::GapOf(-60, true).colour == 0x4040BF00);
  CHECK(hud::GapOf(0, false).colour == 0xFFFFFF00 && hud::GapOf(0, true).colour == 0xBFBFBF00 && hud::GapOf(60, true).colour == 0xBF404000);
}

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

  // a player in seat 1 (a ghost to everyone, solid to itself); a spectator, a watched race or a playback following
  // seat 4: seat 4 as its driver sees it, solid, and seat 1 a ghost again; following no racer, everyone by seat.look
  const hud::Frame player = hud::Read(g, BLOCK, 1), watcher = hud::Read(g, BLOCK, 4), nobody = hud::Read(g, BLOCK, 0xFFFFFFFF);
  CHECK(player.parts == hud::PART_BITS[modes::PART_TIMER] && player.digit == 0);
  CHECK(player.looks[1] == modes::LOOK_NORMAL && player.looks[4] == modes::LOOK_GHOST && player.looks[0] == modes::LOOK_NORMAL);
  CHECK(watcher.looks[4] == modes::LOOK_NORMAL && watcher.looks[1] == modes::LOOK_GHOST && watcher.looks[0] == modes::LOOK_NORMAL);
  CHECK(nobody.looks[1] == modes::LOOK_GHOST && nobody.looks[4] == modes::LOOK_GHOST);
  // the danger zone's count (the race side's: mode.h DANGER, 60 x seconds + 60 down to 60): the big digit and the
  // warning, the count as the story's digit value (179: the digit 2); the sheet's last digit, 3, at most; none once
  // done or at 60
  const u32 d = BLOCK + mode::DANGER + 16 * 1;
  g.Write32(d + 12, mode::DANGER_ON | mode::DANGER_ARMED);
  g.Write32(d + 8, 179);
  const hud::Frame danger = hud::Read(g, BLOCK, 1);
  CHECK(danger.parts == (hud::PART_BITS[modes::PART_TIMER] | hud::PART_BITS[modes::PART_DIGIT] |
                         hud::PART_BITS[modes::PART_WARNING]));
  CHECK(danger.digit == 179);
  g.Write32(d + 8, 600);
  CHECK(hud::Read(g, BLOCK, 1).digit == 239);
  g.Write32(d + 8, 60);
  CHECK(hud::Read(g, BLOCK, 1).parts == hud::PART_BITS[modes::PART_TIMER]);
  g.Write32(d + 8, 179);
  g.Write32(d + 12, mode::DANGER_ON | mode::DANGER_DONE);
  CHECK(hud::Read(g, BLOCK, 1).parts == hud::PART_BITS[modes::PART_TIMER]);
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
  // FACES clears the board's bit as well as the window's word; a seat not viewed holds nothing
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
  // the game's gap popup: its bit on while shown; a part of the game's own pictures, so no story pictures to load
  CHECK(hud::Apply(g, BLOCK, modes::ACTION_SHOW, 1, modes::PART_LEADER_GAP));
  g.Write32(MODULE + game::HUD_PARTS, 0xFF9FFFFF);
  hud::Hud(g);
  CHECK(g.Read32(MODULE + game::HUD_PARTS) == ((0xFF9FFFFF & ~hud::STORY_BITS) | 0x20000 | hud::GAP_PART));
  CHECK(hud::Apply(g, BLOCK, modes::ACTION_HIDE, 1, modes::PART_LEADER_GAP));
  CHECK(hud::PART_BITS[modes::PART_LEADER_GAP] == hud::GAP_PART);
  // a racer's RANK hidden hides the rank icons too: the window's word
  CHECK(hud::Apply(g, BLOCK, modes::ACTION_HIDE, 1, modes::PART_RANK));
  g.Write32(MODULE + game::HUD_PARTS, 0xFF9FFFFF);
  hud::Hud(g);
  CHECK(g.Read32(layout::MODE_HUD + hud::WINDOW_RANK) == 1);
  CHECK(hud::Apply(g, BLOCK, modes::ACTION_SHOW, 1, modes::PART_RANK));
  g.Write32(MODULE + game::HUD_PARTS, 0xFF9FFFFF);
  hud::Hud(g);
  CHECK(g.Read32(layout::MODE_HUD + hud::WINDOW_RANK) == 0);
  CHECK(hud::Apply(g, BLOCK, modes::ACTION_HIDE, 2, modes::PART_RANK));
  CHECK(hud::Apply(g, BLOCK, modes::ACTION_HIDE, 1, modes::PART_FACES));
  g.Write32(MODULE + game::HUD_PARTS, 0xFF9FFFFF);
  hud::Hud(g);
  CHECK(g.Read32(MODULE + game::HUD_PARTS) == ((0xFF9FFFFF & ~hud::STORY_BITS & ~hud::PART_BITS[modes::PART_FACES]) | 0x20000));
  CHECK(g.Read32(layout::MODE_HUD + hud::WINDOW_FACES) == 1);
  CHECK(hud::Apply(g, BLOCK, modes::ACTION_SHOW, 1, modes::PART_FACES));
  CHECK(hud::Apply(g, BLOCK, modes::ACTION_SHOW, 2, modes::PART_RANK));
  g.Write32(MODULE + game::HUD_PARTS, 0xFF9FFFFF);
  hud::Hud(g);
  CHECK(g.Read32(layout::MODE_HUD + hud::WINDOW_FACES) == 0);
  CHECK(g.Read32(MODULE + net::BSS + 0x84) == 0);  // no DIGIT shown: no digit source
  CHECK(g.Read32(MODULE + net::BSS + 0x84 + 8) == PROGRESS + game::PROGRESS_SIZE);
  CHECK(g.Read32(MODULE + net::BSS + 0x84 + 4) == MACHINES + game::MACHINE_SIZE + game::MACHINE_SPEED);
  // the story parts' values (X3, X7): with none given, DISTANCE's source is the racer's own P and R and OBJECTS the course's
  CHECK(g.Read32(MODULE + net::BSS + 0x84 + 0xC) == MODULE + net::BSS + 0x3C30);
  CHECK(g.Read32(layout::MODE_HUD + hud::WINDOW_OBJECTS) == 0);
  {
    // show_distance: the drawer's P and R are the window's stand-ins, and lap length x laps - distance is the metres
    CHECK(hud::Apply(g, BLOCK, modes::ACTION_SHOWDISTANCE, 1, std::bit_cast<u32>(4321.5f)));
    CHECK(hud::Apply(g, BLOCK, modes::ACTION_SHOWDISTANCE, 2, std::bit_cast<u32>(777.0f)));
    hud::Hud(g);
    const u32 p = g.Read32(MODULE + net::BSS + 0x84 + 8), r = g.Read32(MODULE + net::BSS + 0x84 + 0xC);
    CHECK(p == layout::MODE_HUD + hud::STANDIN_P && r == layout::MODE_HUD + hud::STANDIN_R);
    const float shown = std::bit_cast<float>(g.Read32(p + 0xFC)) * static_cast<float>(g.Read8(r + 4)) - std::bit_cast<float>(g.Read32(p + 0xF8));
    CHECK(shown == 4321.5f);  // seat 1's, the viewed racer's; seat 2's stays in its record
    CHECK(std::bit_cast<float>(g.Read32(BLOCK + hud::STORY + 2 * hud::STORY_RECORD + hud::STORY_METRES)) == 777.0f);
    // held to the five digits, and nothing below 0
    CHECK(hud::Apply(g, BLOCK, modes::ACTION_SHOWDISTANCE, 1, std::bit_cast<u32>(250000.0f)));
    CHECK(hud::Read(g, BLOCK, 1).metres == hud::DISTANCE_MOST);
    // under `distance_label off` to six (the sixth digit has the label's room), and five again without it
    const u32 window = g.Read32(layout::MODE_HUD + hud::WINDOW_FLAGS);
    g.Write32(layout::MODE_HUD + hud::WINDOW_FLAGS, window | hud::NO_DISTANCE_LABEL);
    CHECK(hud::Apply(g, BLOCK, modes::ACTION_SHOWDISTANCE, 1, std::bit_cast<u32>(250000.0f)));
    CHECK(hud::Read(g, BLOCK, 1).metres == 250000.0f);
    CHECK(hud::Apply(g, BLOCK, modes::ACTION_SHOWDISTANCE, 1, std::bit_cast<u32>(2500000.0f)));
    CHECK(hud::Read(g, BLOCK, 1).metres == hud::DISTANCE_MOST_NO_LABEL);
    g.Write32(layout::MODE_HUD + hud::WINDOW_FLAGS, window);
    CHECK(hud::Apply(g, BLOCK, modes::ACTION_SHOWDISTANCE, 1, std::bit_cast<u32>(250000.0f)));
    CHECK(hud::Read(g, BLOCK, 1).metres == hud::DISTANCE_MOST);
    CHECK(hud::Apply(g, BLOCK, modes::ACTION_SHOWDISTANCE, 1, std::bit_cast<u32>(-5.0f)));
    CHECK(hud::Read(g, BLOCK, 1).distance_fed && hud::Read(g, BLOCK, 1).metres == 0.0f);
    CHECK(hud::Apply(g, BLOCK, modes::ACTION_SHOWDISTANCE, 1, 0x7FC00000));
    CHECK(hud::Read(g, BLOCK, 1).metres == 0.0f);
    // off: the racer's own again
    CHECK(hud::Apply(g, BLOCK, modes::ACTION_SHOWDISTANCEOFF, 1, 0));
    hud::Hud(g);
    CHECK(g.Read32(MODULE + net::BSS + 0x84 + 8) == PROGRESS + game::PROGRESS_SIZE);
    CHECK(g.Read32(MODULE + net::BSS + 0x84 + 0xC) == MODULE + net::BSS + 0x3C30);
    CHECK(!hud::Read(g, BLOCK, 1).distance_fed);
    // show_objects: of + 1 and of - n in the window for the image's two count sites, two digits each, n at most of
    CHECK(hud::Apply(g, BLOCK, modes::ACTION_SHOWOBJECTS, 1, 3, 5));
    hud::Hud(g);
    CHECK(g.Read32(layout::MODE_HUD + hud::WINDOW_OBJECTS) == 6 && g.Read32(layout::MODE_HUD + hud::WINDOW_OBJECTS + 4) == 2);
    CHECK(hud::Apply(g, BLOCK, modes::ACTION_SHOWOBJECTS, 1, 150, 120));
    hud::Hud(g);
    CHECK(g.Read32(layout::MODE_HUD + hud::WINDOW_OBJECTS) == 100 && g.Read32(layout::MODE_HUD + hud::WINDOW_OBJECTS + 4) == 0);
    CHECK(hud::Apply(g, BLOCK, modes::ACTION_SHOWOBJECTS, 1, 7, 0xFFFFFFFF));
    hud::Hud(g);
    CHECK(g.Read32(layout::MODE_HUD + hud::WINDOW_OBJECTS) == 1 && g.Read32(layout::MODE_HUD + hud::WINDOW_OBJECTS + 4) == 0);
    CHECK(hud::Apply(g, BLOCK, modes::ACTION_SHOWOBJECTSOFF, 1, 0));
    hud::Hud(g);
    CHECK(g.Read32(layout::MODE_HUD + hud::WINDOW_OBJECTS) == 0);
    // a page of zeros again (seat 2's distance off too), as the rest of this test expects
    CHECK(hud::Apply(g, BLOCK, modes::ACTION_SHOWDISTANCEOFF, 2, 0));
    for (u32 k = 0; k < 30 * hud::STORY_RECORD; ++k)
      CHECK(g.Read8(BLOCK + hud::STORY + k) == 0);
  }
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
  CHECK(hud::NeedsStoryPictures(p->code) && hud::WindowFlags(*p) == (hud::STORY_PICTURES | hud::GHOST_TASK));  // and it has a ghost
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
  // start_gap: 0 its one flag SAME_SPOT, read by the image's slot site; even SPREAD, read by the image's move
  for (const auto& [name, flags] : {std::pair{"gap_same.mode.json", hud::SAME_SPOT}, std::pair{"gap_spread.mode.json", hud::SPREAD}})
  {
    std::ifstream file(ProtocolFile("protocol/tests/vectors/modes") / name);
    std::stringstream text;
    text << file.rdbuf();
    picojson::value v;
    picojson::parse(v, text.str());
    std::string why;
    const std::optional<mode::Package> gap = mode::Read(v, &why);
    CHECK(gap.has_value() && hud::WindowFlags(*gap) == flags);
  }
}
