// A game mode's displays (piconet-docs research/game-modes/vocabulary.md section 5; protocol/modes.toml's display
// words): what the mode's rules ask the race's pictures to show, kept in the mode's block, and the drawing's side,
// which reads it once a frame.
//
// Presentation only. The display words write the block's display page at the mode point, like every action (SIM: the
// undo log puts the page back with the race, the checksum covers it, so every game holds the same page). Nothing a
// step reads is on this page. The drawing reads it every frame and keeps nothing of its own: the mode's HUD pass
// (Hud: the HUD part mask's mode bits, the story parts' source block .bss 0x84, the looks this game draws) and our
// course map (MapRacers) rewrite their outputs whole from the page, so a rollback, a practice's rewind or the
// scrubber simply redraws. The image's sites read the page or the window words the pass writes: the spare-machine
// icon's getter (show_number), the machine's draw (the looks), the results' page choice (the points page) and the
// banners raised after the mode point (port/image/sites_modehud.py).
//
// The page (DISPLAY, the block's sixth page): +0 its step stamp; +4 the page's flags (POINTS_USED); a seat's record
// of SEAT_DISPLAY bytes at +0x10 + 0x20 x seat:
//   +0  the parts shown, bit p for modes::PART_* p (show / hide)
//   +4  show_number's number + 1 (0: none shown)
//   +8  seat.look (modes::LOOK_*), +9 seat.own_look + 1 (0: as seat.look), +10 stock_banner's banner + 1 asked at
//       this run (0: none; every run begins with none, so a banner is asked once), +11 0
//   +12 seat.colour, 0xRRGGBBAA (0: the machine's own)
//   +16 map_hide's seats: the course maps of these seats leave the seat's dot off (bit i: seat i)
//   +20 map_mark: 0x01000000 | 0xRRGGBB (0: the machine's own colour)
//   +24 the danger zone's count: steps left of its countdown (0: none running), +28 bit 0 armed. Written by the
//       danger_zone word (the race side's: its state is its own, this is what it shows), read by the HUD pass
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <vector>

#include "fzgx/guest.h"
#include "mode.h"
#include "mode_words.g.h"

namespace fzgx::mode::hud
{
constexpr u32 DISPLAY = 0x5000;          // the block's display page (mode.h BLOCK holds it)
constexpr u32 FLAGS = DISPLAY + 4;       // the page's flags
constexpr u32 POINTS_USED = 1;           // the mode awards points: the results show the game's points page
constexpr u32 SEATS_AT = DISPLAY + 0x10, SEAT_DISPLAY = 0x20;
constexpr u32 PARTS = 0, NUMBER = 4, LOOK = 8, OWN_LOOK = 9, BANNER = 10, COLOUR = 12, MAP_HIDE = 16, MAP_MARK = 20,
              DANGER = 24, DANGER_FLAGS = 28;
constexpr u32 MARKED = 0x01000000;
static_assert(SEATS_AT + 30 * SEAT_DISPLAY <= DISPLAY + 0x1000);

// The window words the pass writes for the image (memmap.MODE_HUD; port/image/sites_modehud.py): +0 the image's
// flags (POINTS_PAGE: the results go from their first page to the points page, the site at main.rel 0x14A37C;
// STORY_PICTURES: the story's picture group 0xBA is loaded at the race's set-up), +4 the DIGIT part's value (the
// source block's +0 points here), +0x10 a byte a seat: the look this game draws the seat's machine with.
constexpr u32 WINDOW_FLAGS = 0, WINDOW_DIGIT = 4, WINDOW_LOOKS = 0x10;
constexpr u32 POINTS_PAGE = 1, STORY_PICTURES = 2;

// The race HUD's part bits (the part mask .data 0x3D544, a word a viewport) of the parts a mode can show, by
// modes::PART_*: the drawers fn_1_CF5B0 (TIMER), fn_1_CDC8C (DISTANCE, SPEED_GAUGE, OBJECTS) and fn_1_CEB38 (the
// others), read from their mask tests and the pictures each draws (group 0xBA, bmp_sto: 0xBA0A the digits 0 to 3,
// 0xBA0B DANGER, 0xBA09 MISSION CLEAR!, 0xBA0C MISSION FAILED, 0xBA15 Time is up..., 0xBA0D the object icon).
constexpr std::array<u32, modes::PART_COUNT> PART_BITS = {0x20000, 0x1000, 0x8, 0x400, 0x200, 0x100, 0x80, 0x2000,
                                                          0x4000};
// Every bit of the story's parts (the two drawers' tests and TIMER's), which the game's default mask 0xFF9FFFFF has
// on: once group 0xBA is loaded they would draw, DIGIT a "0" from a source of none, so the pass clears every one
// the mode does not show.
constexpr u32 STORY_BITS = 0x2 | 0x8 | 0x10 | 0x20 | 0x40 | 0x80 | 0x100 | 0x200 | 0x400 | 0x800 | 0x1000 | 0x2000 |
                           0x4000 | 0x20000;
constexpr u32 STORY_GROUP = 0xBA;

// Whether action `id` is a display word: the interpreter applies it at once, in the order the rules run (a mode's
// variable is written so too), without a place in the step's queue of actions on the game.
bool IsDisplay(u8 id);
// A display word into the seat's record: true when `id` is one.
bool Apply(Guest& g, u32 block, u8 id, u32 seat, u32 value);
// The start of every run: the banners asked at the last one are gone.
void BeginRun(Guest& g, u32 block);
// The window's flags for a race of package `p` (the core writes them with the block's request, boot.cpp): the
// story's pictures when its code shows a story part.
u32 WindowFlags(const Package& p);
// Whether a package's code uses a word that shows a story part (the picture group 0xBA must then be loaded): any
// show of a part other than TIMER.
bool NeedsStoryPictures(const std::vector<u8>& code);

// The points (the race side's `award`, at the mode point): the mode uses points, so the results go to the game's
// points page (VS TOTAL RANKING) after their first page; and a seat's total as that page shows it, its results
// record's +0x4 (0 to 9999), written at every run so that the game's own Grand Prix points (added at a finish, in a
// race type 6 as in a Grand Prix) never stand there.
void UsePoints(Guest& g, u32 block);
void WritePoints(Guest& g, u32 module, u32 seat, u32 points);

// What the HUD pass asks of the race, read from the block for one viewer (`viewed`: the racer the picture follows;
// `own`: the seat whose own look applies, or none): pure, for the tests.
struct Frame
{
  u32 parts = 0;                     // the part mask's mode bits to set
  s32 digit = 0;                     // the DIGIT part's value (steps, as the story's: seconds = value / 60)
  std::array<u8, 30> looks{};        // the look each seat's machine is drawn with in this game
};
Frame Read(const Guest& g, u32 block, u32 viewed, u32 own);

// The mode's HUD pass: once a frame, as the frame's drawing begins (after the frame routine's `hudstate` and
// `spechud`), for the racer this game's picture follows. Nothing without a mode.
void Hud(Guest& g);
// The course map's dots (minimap_frame.cpp): `you` the racer the map is drawn for, `racing` whether that is this
// game's own seat in the race (a spectator's map and a replay's show every dot). Hides the dots map_hide hides from
// it and colours the dots map_mark marks; `present` and `colour` as the map reads them.
void MapRacers(const Guest& g, u32 you, bool racing, std::array<bool, 30>& present, std::array<u32, 30>& colour);
}  // namespace fzgx::mode::hud
