// A game mode's displays (piconet-docs research/game-modes/vocabulary.md section 5; protocol/modes.toml's display
// words): what the mode's rules ask the race's pictures to show, kept in the mode's block, and the drawing's side,
// which reads it once a frame.
//
// Presentation only. The display words write the block's display page at the mode point, like every action (SIM: the
// undo log puts the page back with the race, the checksum covers it, so every game holds the same page). Nothing a
// step reads is among these words (the page's room past them holds the seats' stat records, mode.h STATS, which are not
// the display's). The drawing reads it every frame and keeps nothing of its own: the mode's HUD pass
// (Hud: the HUD part mask's mode bits, the story parts' source block .bss 0x84, the looks this game draws) and our
// course map (MapRacers) rewrite their outputs whole from the page, so a rollback, a practice's rewind or the
// scrubber simply redraws. The image's sites read the page or the window words the pass writes: the spare-machine
// icon's getter (show_number), the machine's draw (the looks), the results' page choice (the points page) and the
// banners raised after the mode point (port/image/sites_modehud.py).
//
// The page (DISPLAY, the block's seventh page): +0 its step stamp; a seat's record of SEAT_DISPLAY bytes at +0x10 +
// 0x20 x seat:
//   +0  the parts, bit p for modes::PART_* p: the story's parts shown (show sets, hide clears), the standard parts
//       (PART_RANK on) hidden (hide sets, show clears), so a page of zeros draws what the game draws
//   +4  show_number's number + 1 (0: none shown)
//   +8  seat.look (modes::LOOK_*), +9 seat.own_look + 1 (0: as seat.look), +10 stock_banner's banner + 1 asked at
//       this run (0: none; every run begins with none, so a banner is asked once), +11 the last GAP or WRONG_WAY asked
//   +12 seat.colour, 0xRRGGBBAA (0: the machine's own)
//   +16 map_hide's seats: the course maps of these seats leave the seat's dot off (bit i: seat i)
//   +20 map_mark: 0x01000000 | 0xRRGGBB (0: the machine's own colour)
//   +24 GAP's time, packed as the game's lap words (the seat's last lap line, P+0x1E0)
//   +28 the race clock when +11 was asked: the HUD pass shows the gap popup's part and starts the wrong-way sign from it
// At +TAGS_AT (after the records), a byte a seat: `tag`'s text, its number in the package's tag_texts from 1 (0: none).
// The danger zone's display reads the race side's state (mode.h DANGER: its count, as story chapter 6's).
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <vector>

#include "fzgx/guest.h"
#include "mode.h"
#include "mode_words.g.h"

namespace fzgx::mode::hud
{
constexpr u32 DISPLAY = 0x6000;          // the block's display page (mode.h BLOCK holds it)
constexpr u32 SEATS_AT = DISPLAY + 0x10, SEAT_DISPLAY = 0x20;
constexpr u32 PARTS = 0, NUMBER = 4, LOOK = 8, OWN_LOOK = 9, BANNER = 10, COLOUR = 12, MAP_HIDE = 16, MAP_MARK = 20,
              ASKED = 11, GAP_TIME = 24, ASKED_AT = 28;
constexpr u32 MARKED = 0x01000000;
constexpr u32 TAGS_AT = SEATS_AT + 30 * SEAT_DISPLAY;
// seat.ghost_sound's seats, a bit a seat (0: Time Attack's, no voice), in the room after the tags; seat.ghost_tint's word,
// 0xRRGGBBAA (0: Time Attack's yellow), in the room at the end of the seat's stat record (mode.h STATS).
constexpr u32 GHOST_SOUNDS = TAGS_AT + 32, STAT_GHOST_TINT = 0x58;
static_assert(TAGS_AT + 30 <= GHOST_SOUNDS && GHOST_SOUNDS + 4 <= STATS && STATS >= DISPLAY && STATS + 30 * STAT <= DISPLAY + 0x1000 &&
              STAT_DAMAGE + 4 <= STAT_GHOST_TINT && STAT_GHOST_TINT + 4 <= STAT);

// The window words the pass writes for the image (memmap.MODE_HUD; port/image/sites_modehud.py): +0 the image's
// flags (POINTS_PAGE, award or a results title: the results go from their first page to the points page, the site at main.rel 0x14A37C;
// STORY_PICTURES: the story's picture group 0xBA is loaded at the race's set-up), +4 the DIGIT part's value (the
// source block's +0 points here), +0x10 a byte a seat: the look this game draws the seat's machine with (a
// modes::LOOK_*, or COLOURED: normal, tinted by the seat's word at +0x30, seat.colour's 0xRRGGBBAA).
// +8, not 0: the viewed racer's RANK is hidden, and the rank icons above the machines with it (routines/rankicons.py).
// +0xC, not 0: the wrong-way sign starts this frame (the image's mode_wrong clears it).
constexpr u32 WINDOW_FLAGS = 0, WINDOW_DIGIT = 4, WINDOW_RANK = 8, WINDOW_WRONG = 0xC, WINDOW_LOOKS = 0x10, WINDOW_TINTS = 0x30;
constexpr u8 COLOURED = modes::LOOK_COUNT;
static_assert(WINDOW_TINTS + 30 * 4 <= 0x100);
// ROLLING: rolling_start: the machines' tasks run from the countdown's first step (the site at main.rel 0x34988).
constexpr u32 POINTS_PAGE = 1, STORY_PICTURES = 2, POINTS_ONLY = 4, ROLLING = 8;
// TOGETHER: start_together: the grid's placing puts every machine on slot 0 (sites_grid.py grid_place).
constexpr u32 TOGETHER = 32;
// GHOST_TASK: the code asks the look ghost or GHOST_GAP: the race's set-up makes the ghost task (sites_ghostdraw.py ghost_make).
constexpr u32 GHOST_TASK = 64;
// HUD_MODERN: `hud modern`: the core's own (modern_hud_frame.cpp); no site reads it.
constexpr u32 HUD_MODERN = 128;
constexpr u32 FAULT_SHOWN = 16;  // (the core's own: ShowFault has written MODE ERROR; no site reads it)
// +0xB0: the results title, 24 bytes, NUL-terminated; empty: the game's own (the site at main.rel 0x146304).
constexpr u32 WINDOW_TITLE = 0xB0, WINDOW_TITLE_SIZE = 24;
static_assert(modes::RESULTS_TITLE + 1 <= WINDOW_TITLE_SIZE);  // (with its NUL: the api's limit fits the window)
// +0xC8: not 0, the viewed racer's faces board is not drawn (the site at fn_1_D27B8's entry).
constexpr u32 WINDOW_FACES = WINDOW_TITLE + WINDOW_TITLE_SIZE;
// +0xCC the seats this game gives no machine voice (a bit a seat: the ghosts but the viewed racer, the site in fn_1_A3D5C).
// +0xD0 GHOST_GAP as the ghost task draws it: its colour (0: not drawn), its sign's character, minutes, seconds, thousandths.
constexpr u32 WINDOW_SILENT = WINDOW_FACES + 4, WINDOW_GAP = WINDOW_SILENT + 4;
static_assert(WINDOW_TINTS + 30 * 4 <= WINDOW_TITLE && WINDOW_GAP + 20 <= 0x100);
// +0x100: `tag`, for the name tags (port/image/routines/nametags.py): a byte a seat, the page's tag byte, written by the
// pass once a frame; +0x120 the package's texts, WINDOW_TAG_TEXT bytes each with the NUL, written at the start (boot.cpp).
// A race without tags holds zeros, so the name tags draw what they always did.
constexpr u32 WINDOW_TAGS = 0x100, WINDOW_TAG_TEXTS = 0x120, WINDOW_TAG_TEXT = 12;
// +0x180: eight bytes a seat, the image's own (mode_look to the ghost task), between the texts and the window's end.
constexpr u32 WINDOW_GHOSTS = 0x180;
static_assert(modes::TAG_TEXT + 1 <= WINDOW_TAG_TEXT && WINDOW_TAGS + 30 <= WINDOW_TAG_TEXTS &&
              WINDOW_TAG_TEXTS + modes::TAG_TEXTS * WINDOW_TAG_TEXT <= WINDOW_GHOSTS &&
              WINDOW_GHOSTS + 30 * 8 <= 0x700);  // (0x700: RACE, the next in the window)

// The race HUD's part bits (the part mask .data 0x3D544, a word a viewport) of the parts a mode can show, by
// modes::PART_*: LEADER_GAP the gap popup's own bit (fn_1_15EFEC's test, GAP_PART below), which needs no story picture; the drawers fn_1_CF5B0 (TIMER), fn_1_CDC8C (DISTANCE, SPEED_GAUGE, OBJECTS) and fn_1_CEB38 (the
// others), read from their mask tests and the pictures each draws (group 0xBA, bmp_sto: 0xBA0A the digits 0 to 3,
// 0xBA0B DANGER, 0xBA09 MISSION CLEAR!, 0xBA0C MISSION FAILED, 0xBA15 Time is up..., 0xBA0D the object icon).
// From PART_RANK on the standard parts, read from the drawers' tests of the same mask (rank, laps and speed in their drawers; energy
// fn_1_C8F34, race time 0xCBC24, lap times 0xCB424 and 0xCB028); FACES has no bit (WINDOW_FACES).
// GHOST_GAP has no bit: the ghost task draws it (WINDOW_GAP).
constexpr std::array<u32, modes::PART_COUNT> PART_BITS = {0x20000, 0x1000, 0x8, 0x400, 0x200, 0x100, 0x80, 0x2000,
                                                          0x4000, 0x400000, 0, 0x20000000, 0x04000000, 0x08000000, 0x40000000,
                                                          0x01000000, 0x02000000, 0};
// Every bit of the story's parts (the two drawers' tests and TIMER's), which the game's default mask 0xFF9FFFFF has
// on: once group 0xBA is loaded they would draw, DIGIT a "0" from a source of none, so the pass clears every one
// the mode does not show.
constexpr u32 STORY_BITS = 0x2 | 0x8 | 0x10 | 0x20 | 0x40 | 0x80 | 0x100 | 0x200 | 0x400 | 0x800 | 0x1000 | 0x2000 |
                           0x4000 | 0x20000;
constexpr u32 STORY_GROUP = 0xBA;
constexpr u32 PHASE_INTRO = 0x27;  // the course's intro: from it on the race's HUD and the block are there
// The gap popup's part (fn_1_15EFEC's test of the mask, off in the default mask), for its 60 frames after a GAP.
constexpr u32 GAP_PART = 0x400000;
constexpr u32 GAP_STEPS = 60;

// The game's mask bits the modern HUD draws itself: the energy meter (and its lightning, inside the meter's test), rank,
// lap counter, race time, lap list. Speed, the skulls, the spare icon, wrong way, banners and the face board stay the game's.
constexpr u32 MODERN_PARTS = PART_BITS[modes::PART_ENERGY] | PART_BITS[modes::PART_RANK] | PART_BITS[modes::PART_LAPS] |
                             PART_BITS[modes::PART_RACE_TIME] | PART_BITS[modes::PART_LAP_TIMES];

// Whether action `id` is a display word: the interpreter applies it at once, in the order the rules run (a mode's
// variable is written so too), without a place in the step's queue of actions on the game.
bool IsDisplay(u8 id);
// The seat's parts word (modes::PART_*, bit p), or 0 with no mode: `hide` of a standard part sets its bit.
u32 SeatParts(const Guest& g, u32 seat);
// A display word into the seat's record: true when `id` is one.
bool Apply(Guest& g, u32 block, u8 id, u32 seat, u32 value);
// The start of every run: the banners asked at the last one are gone.
void BeginRun(Guest& g, u32 block);
// The window's flags for a race of package `p` (the core writes them with the block's request, boot.cpp; nothing
// changes them in the race): the story's pictures when its code shows a story part; the points page when it awards
// points (`award`: the race side writes each record's +0x4 at the end, mode.cpp).
u32 WindowFlags(const Package& p);
// A mode's break on the results (D6): the points page under the title MODE ERROR, every run of the step, so a step run
// again that does not break puts the mode's own back. Every game breaks at the same step.
void ShowFault(Guest& g, const Package& p, bool broken);
// Whether a package's code uses a word that shows a story part (the picture group 0xBA must then be loaded): any
// show of a part other than TIMER, or danger_zone (its digit and warning).
bool NeedsStoryPictures(const std::vector<u8>& code);
// Whether the package's code uses show_clock: the timed rule from the race's start, with the game's count off.
bool UsesClock(const Package& p);

// What the HUD pass asks of the race, read from the block for one viewer (`viewed`: the racer the picture follows;
// `own`: the seat whose own look applies, or none): pure, for the tests.
// GHOST_GAP's source (Time Attack's fn_1_F0E00): per seat, the step each bucket of the course's distance was first reached.
// Host memory, written once a frame by the HUD pass: outside the block, the undo log and the checksum, and no step reads
// it (the rollback presentation rule). A rollback rewrites what it sees: an entry later than the frame's step is dropped when the
// step goes back, and buckets passed only in steps run again within one frame keep their earlier step, at most a rollback's
// depth off, for the picture alone.
class GapTable
{
public:
  static constexpr u32 BUCKETS_PER_LAP = 2048;
  void Clear();
  // Seat `seat` is in `bucket` at step `step`.
  void Note(u32 seat, u32 bucket, u32 step);
  // Frames you are behind (above 0) or ahead (below 0) of `other` at step `now`: other ahead, how long ago it was where you
  // are; you ahead, how long ago you were where it is. Both in one bucket, or an entry the table has not: 0.
  s32 Frames(u32 you, u32 other, u32 now) const;
  u32 Bucket(u32 seat) const { return m_last[seat]; }

private:
  std::array<std::vector<u32>, 30> m_at{};  // the step + 1, by bucket (0: not reached)
  std::array<u32, 30> m_last{};
  std::array<u32, 30> m_step{};
};
// What the gap draws: the colour (TA's: red ahead, white level, blue behind, dimmer once the other has finished), the
// sign's character (0x3A, 0x3B or a space), minutes, seconds and thousandths of |frames| x 1000 / 60.
struct GapPicture
{
  u32 colour, sign, minutes, seconds, thousandths;
};
GapPicture GapOf(s32 frames, bool other_finished);

struct Frame
{
  u32 parts = 0;                     // the part mask's mode bits to set
  u32 hidden = 0;                    // the part mask's standard bits to clear
  bool faces_hidden = false;         // the faces board, which has no bit
  s32 digit = 0;                     // the DIGIT part's value (steps, as the story's: seconds = value / 60)
  std::array<u8, 30> looks{};        // the look each seat's machine is drawn with in this game
  std::array<u32, 30> colours{};     // each seat's seat.colour (0: its own)
  std::array<u8, 30> tags{};         // each seat's `tag` text, from 1 (0: none): the same for every viewer
  std::array<u32, 30> ghost_tints{}; // each seat's seat.ghost_tint (0: Time Attack's yellow)
  u32 silent = 0;                    // the seats given no machine voice: drawn as ghosts, unheard, not the viewed racer
  bool ghost_gap = false;            // the viewed racer sees GHOST_GAP
};
Frame Read(const Guest& g, u32 block, u32 viewed, u32 own);

// The mode's HUD pass: once a frame, as the frame's drawing begins (after the frame routine's `hudstate` and
// `spechud`), for the racer this game's picture follows. Nothing without a mode.
void Hud(Guest& g);
// The mode's values for the launcher's status board (mode_board.cpp; the language's section 10). Once a picture, after
// Hud, for every start with a mode and a board that was taken. Reads the block, writes nothing of the game: nothing a
// step reads comes from here. The first picture writes the head, each picture the values (a mode with lines).
void Board(const Guest& g);
// The course map's dots (minimap_frame.cpp): `you` the racer the map is drawn for, `racing` whether that is this
// game's own seat in the race (a spectator's map and a replay's show every dot). Hides the dots map_hide hides from
// it and colours the dots map_mark marks; `present` and `colour` as the map reads them.
void MapRacers(const Guest& g, u32 you, bool racing, std::array<bool, 30>& present, std::array<u32, 30>& colour);
}  // namespace fzgx::mode::hud
