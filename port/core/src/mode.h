// A game mode in the race (piconet-docs research/game-modes/; the package: protocol/formats.md section 9; the words
// and operations: protocol/modes.toml, mode_words.g.h): the package as the match start's setup.mode carries it, read
// and checked (files_mode.cpp), and its interpreter (mode.cpp), which the image's mode point calls once a step
// (HOST_MODE, port/image/sites_mode.py).
//
// The block (memmap.MODE_BLOCK bytes in the game's main heap, on whole pages of 4 KiB; its address at layout::MODES
// +4) holds every state the mode keeps, so the undo log puts it back with the race (a rollback, the practice's rewind
// and its saved moment: one memory, one undo log) and the state checksum covers it (checksum.cpp). Nothing the run
// needs is kept in this process between two steps: the package is read only.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "fzgx/guest.h"
#include "mode_words.g.h"
#include "picojson.h"

namespace fzgx::mode
{
// The block's layout (port/image/routines/memmap.py names the words the image's sites read: the records, the end
// word, the engine page's clock, no-contact rows, touching rows, moves and the rolling start's saved steering). Seven
// pages: the run's words, the globals, the edges, the generator and the waiting rules' bits; two of seat words; the
// wait records; the team words; the engine page (the race side's state: what the events compare with, the places,
// the points, the contact table); the displays (mode_hud.h). Every page's first word is its step stamp.
constexpr u32 BLOCK = 0x7000, PAGE = 0x1000;
constexpr u32 RECORDS = 0, RECORD = 16;   // a seat's: +0 flags, +4 accelerator, +8 brake, +12 the held speed
constexpr u32 END = RECORDS + 30 * RECORD;  // 0x1E0: 0 the race goes on; 1 a mode error; 2 the mode ended it
constexpr u32 FAULT = END + 4;            // the fault's reason (Fault), and +8 its step
constexpr u32 RUNS = END + 12;            // the runs so far: the first page's step stamp
constexpr u32 OUT_MASK = END + 16;        // the seats out at the last run (bit i: seat i)
constexpr u32 OPS = END + 20;             // the last run's operations, and +4 the most of any run
constexpr u32 GLOBALS = 0x200;            // modes::GLOBALS words
constexpr u32 EDGES = GLOBALS + 4 * modes::GLOBALS;  // a `when` rule's: bit i its condition held for seat i
constexpr u32 RANDOM = EDGES + 4 * modes::RULES;    // the generator's state, xoshiro128**: four words
constexpr u32 WAITING = RANDOM + 16;      // a rule's: bit i its instance on seat i waits (bit 0: a rule on no seat)
constexpr u32 SEAT_PAGE_SEATS = 15;       // a page's seats: its first word is the page's step stamp
constexpr u32 SEAT_WORDS = 0x10;          // in a seat page: seat k's words at +0x10 + 0x100 k
constexpr u32 WAIT_RECORDS = 3 * PAGE + 0x10;  // the wait records, WAIT bytes each (Wait in mode.cpp)
constexpr u32 WAIT = 0x3C;                // +0 its rule + 1 (0: free), +4 the instance's seat, +8 kind | live << 8,
                                          // +12 the run it ends at, +16 its length, +20 where the code goes on,
                                          // +24 its condition, +28 the locals kept
constexpr u32 TEAM_PAGE = 4 * PAGE + 0x10;  // team t's words at +0x80 t
// a record's flags: the stand-in routine reads 1, 2, 8, 32 and 64, the mode point 4, 16 and 128, site L 4
constexpr u32 PEDALS = 1, BOOST_OFF = 2, HELD = 4, BOOST = 8, EXPLODE = 16, HAND = 32, ROLL = 64, DAMAGE = 128;
// The engine page (the race side, wave 2A). The image reads CLOCK (the timer site), NC_ANY and NC (the pair gate),
// writes TOUCH (the pair gate, a contact answered), reads MOVES (site L) and ROLL_SAVE (the stand-in routine).
constexpr u32 ENGINE = 5 * PAGE;
constexpr u32 WAITS = ENGINE + 0x0;       // race_waits: not 0, the race-over test counts every seat
constexpr u32 CLOCK = ENGINE + 0x4;       // clock_limit in steps (0: none)
constexpr u32 FLAGS = ENGINE + 0x8;       // 1 race.started at the last run; 2 rolling start written
constexpr u32 NC_ANY = ENGINE + 0xC;      // the OR of the no-contact rows: 0, the pair gate takes the stock path
constexpr u32 NC = ENGINE + 0x10;         // a seat's no-contact row: bit 0x80000000 >> j, the game's own order (+0x514's)
constexpr u32 TOUCH = ENGINE + 0x88;      // a seat's contact answers this step, the same order (the pair gate ORs)
constexpr u32 TOUCH_PREV = ENGINE + 0x100;  // the same, the step before
constexpr u32 RESPAWN = ENGINE + 0x178;   // bit i: seat i respawns (flag 0x1 kept on while it is not finished)
constexpr u32 HANDED = ENGINE + 0x17C;    // bit i: the CPU drives seat i (hand_to_cpu)
constexpr u32 FROZEN = ENGINE + 0x180;    // bit i: seat i is held at speed 0 (freeze)
constexpr u32 DRIVEN = ENGINE + 0x184;    // bit i: a player seat i has driven (a record not "AI drives") and not retired since
constexpr u32 TEAM_NC = ENGINE + 0x188;   // a seat's no-contact bits from team T no contact (what ghost off goes back to)
constexpr u32 END_ORDER = ENGINE + 0x200;  // the end clause's order, a seat a byte (held every step after the end)
constexpr u32 PLACES = ENGINE + 0x220;    // seat.place, a byte a seat (0: none)
constexpr u32 POINTS = ENGINE + 0x240;    // award's points, a word a seat
constexpr u32 RANKS = ENGINE + 0x2B8;     // the standing at the last run, a byte a seat (overtake)
constexpr u32 SEAT_STATE = ENGINE + 0x300, SEAT_STATE_SIZE = 0x40;  // what a seat's events compare with (mode.cpp)
constexpr u32 MOVES = ENGINE + 0xA80;     // a seat's: +0 1 push, 2 velocity; +4 x, +8 y, +12 z (momentum, binary32)
constexpr u32 DANGER = ENGINE + 0xC60;    // a seat's danger zone: +0 below, +4 arm (km/h), +8 count, +12 flags
constexpr u32 ROLL_SAVE = ENGINE + 0xE40, ROLL_SAVE_SIZE = 8;  // a seat's steering (+0) before the rule hook while the rolling start holds it
constexpr u32 DANGER_ON = 1, DANGER_ARMED = 2, DANGER_DONE = 4;
// A seat's stat record (seat.stat.*, damage), in the display page's free room (mode_hud.h: the only page with room, and
// no page more is stamped and summed at every step; the stand-in's rule hook wrapper and the mode point read it):
// +0 the stats the mode changed, bit i for STAT_WORDS[i]; +4 their values; +STAT_BASE what each was before the first
// change; +STAT_DAMAGE `damage`'s amount this run (with the record's flag DAMAGE).
// The wrapper writes each changed value into the machine after the rule hook rebuilt it, so the machine's fields are
// the game's own from there on: the step reads them, the undo log and the checksum have them with the machine.
constexpr u32 STATS = 6 * PAGE + 0x400, STAT = 0x60, STAT_VALUES = 4, STAT_BASE = 0x2C, STAT_DAMAGE = 0x54;
static_assert(STAT_VALUES + 40 <= STAT_BASE && STAT_BASE + 40 <= STAT_DAMAGE && STAT_DAMAGE + 4 <= STAT &&
              STATS + 30 * STAT <= BLOCK);
// The live combat values, after the stat records (port/image/sites_combat.py reads them in the step; memmap.py the same
// offsets). A word of 0 is the game's own, so a mode that names none of them changes nothing. C_DAMAGE, C_KNOCK... as a
// factor of the game's; C_FACTOR and C_CAP the game's 0.002 and 20 at that factor; C_ONE 1.0 for the bounce's sum.
constexpr u32 COMBAT = STATS + 30 * STAT;
constexpr u32 C_FACTOR = 0, C_CAP = 4, C_KNOCK = 8, C_WALL = 12, C_BOUNCE = 16, C_SPIN_OFF = 20, C_SIDE_OFF = 24,
              C_IMMUNE = 28, C_ONE = 32, C_DAMAGE = 36;
constexpr u32 CLOCK_SHOWN = COMBAT + 40, CLOCK_EARNED = COMBAT + 44;  // time_bonus: what the mode wrote to the timer, and what the game added
constexpr u32 POINTS_TABLE = COMBAT + 48;  // points_table's by place, a word each
constexpr u32 BOOST_OFF_SEATS = POINTS_TABLE + 30 * 4;  // boost s off's, a bit a seat, to the race's end
static_assert(BOOST_OFF_SEATS + 4 <= BLOCK);
// By the bit: M's field, in the order of modes.toml's seat.stat.* (port/image/routines/standin_inputs.py STAT_FIELDS is
// this list, and its test holds the two equal): weight, accel, max_speed, grip, turn, strafe, drag, boost_strength,
// boost_length, body.
constexpr std::array<u32, 10> STAT_FIELDS = {0x8, 0x220, 0x22C, 0xC, 0x14, 0x1C, 0x23C, 0x230, 0x234, 0x30};
constexpr float MOST_SPEED = 9000.0f;
static_assert(WAITING + 4 * modes::RULES <= PAGE && SEAT_WORDS + SEAT_PAGE_SEATS * 0x100 <= PAGE &&
              4 * modes::SEAT_WORDS == 0x100 && 28 + 4 * modes::WAIT_LOCALS <= WAIT &&
              WAIT_RECORDS + modes::WAITS * WAIT <= 4 * PAGE && 4 * modes::TEAM_WORDS == 0x80 &&
              TEAM_PAGE + modes::TEAMS * 0x80 <= ENGINE && 7 * PAGE == BLOCK && NC + 30 * 4 <= TOUCH &&
              TOUCH + 30 * 4 <= TOUCH_PREV && TOUCH_PREV + 30 * 4 <= RESPAWN && TEAM_NC + 30 * 4 <= END_ORDER &&
              END_ORDER + 30 <= PLACES && PLACES + 30 <= POINTS && POINTS + 30 * 4 <= RANKS && RANKS + 30 <= SEAT_STATE &&
              SEAT_STATE + 30 * SEAT_STATE_SIZE <= MOVES && MOVES + 30 * 16 <= DANGER && DANGER + 30 * 16 <= ROLL_SAVE &&
              ROLL_SAVE + 30 * ROLL_SAVE_SIZE <= 6 * PAGE);

constexpr u32 SeatWord(u32 seat, u32 word)
{
  return PAGE * (1 + seat / SEAT_PAGE_SEATS) + SEAT_WORDS + 0x100 * (seat % SEAT_PAGE_SEATS) + 4 * word;
}

constexpr u32 TeamWord(u32 team, u32 word)
{
  return TEAM_PAGE + 0x80 * team + 4 * word;
}

enum Fault : u32
{
  FAULT_NONE,
  FAULT_BUDGET,   // the step's operations went past modes::BUDGET
  FAULT_QUEUE,    // more than modes::QUEUE actions
  FAULT_REAL,     // a real that is not finite for a game field
  FAULT_CODE,     // the bytecode does what it may not (an operand out of range, the stack past its room)
  FAULT_NO_BLOCK, // the game had no room for the block
  FAULT_WAITS     // more rules waiting than modes::WAITS
};

constexpr u32 NONE = 0xFFFFFFFF;
struct Rule
{
  std::string label;
  u8 event = 0;
  u16 locals = 1;
  u32 when = NONE, during = NONE, body = 0;  // offsets in the code (`during`: the rule's `while`)
};

// A `board` line (the language's section 10; the package's BORD): a variable's word on the status board.
struct BoardLine
{
  u8 scope = 0;  // board::ModeBoardScope
  u8 type = 0;   // board::ModeBoardType
  u16 word = 0;
  std::string label;
  std::vector<std::string> words;  // what the variable's whole number picks; none: it shows the number
};

// A package's code in the form the interpreter runs (Prepare in mode.cpp): one entry for each byte offset of the code,
// and one past its end, the operands decoded and checked once. Worked out from the package alone and never written
// after: it holds nothing of a race, so a rollback has nothing of it to put back. What it answers is what the code
// says; every state of the run stays in the block.
struct PreparedOp
{
  u32 arg = 0;     // the operand: a value, a jump's offset, a word's place
  u8 kind = 0;     // the operation, or one of mode.cpp's two below them
  u8 small = 0;    // a local's number, a read's word
};
struct Prepared
{
  std::vector<PreparedOp> ops;                                 // empty: the plain interpreter, from the bytes (tests)
  std::array<std::vector<u16>, modes::EVENT_COUNT> on_event;   // the rules on each event, in the file's order
  bool waits = false;                                          // the code may hold a Wait: no wait record otherwise
};

// The package as it runs: the setup words' values with the parameters put in.
struct Package
{
  std::vector<u8> bytes;
  std::string hash;  // SHA-256, lower-case hex
  u32 api = 0;
  std::string name;
  std::vector<s32> params;
  std::optional<u32> laps;         // `laps`, or `laps by course`'s else: the race's, over the match's
  std::vector<std::pair<u32, u32>> course_laps;  // `laps by course`'s rows: a course's number, its laps
  std::optional<u32> LapsFor(u32 course) const
  {
    for (const auto& [c, n] : course_laps)
      if (c == course)
        return n;
    return laps;
  }
  bool pedals = false, boost_off = false;
  u32 accel = 0, brake = 0;        // binary32 bits
  // the race side's setup words (wave 2A): the race block's rules a mode states (files_match.cpp puts them in), and
  // the engine's own
  std::optional<u32> finish_limit;  // seconds
  std::optional<u32> mine_regen;    // milliseconds, 0 off
  std::optional<u32> cpu_level;
  std::optional<bool> restore;      // restore on / off; unstated, no restore (the lobby's rule never reaches a mode's race)
  bool contact_off = false;
  u32 clock_limit = 0;              // steps, 0 none
  bool race_waits = false;          // `race_waits everyone`: the race-over test counts every seat (raceend.py)
  bool results_points = false;      // `results points`: the first results page passes by itself
  std::string results_title;        // `results_title "TEXT"`: the string for VS TOTAL RANKING; empty, the game's
  std::vector<u32> points_table;    // what a place earns when the mode ends the race (none: nothing)
  u32 time_bonus = 0;               // time_bonus's steps, 0 none
  std::array<std::optional<u32>, 4> combat{};  // damage, knockback, wall_damage, wall_bounce: binary32 bits
  bool spin_off = false, side_off = false, spin_immune = false;
  std::vector<std::string> tag_texts;  // the texts `tag` names by their place from 1 (the compiler's tag_texts)
  std::optional<u32> rolling;       // rolling_start's speed, binary32 bits
  bool start_together = false;      // start_together: every machine on slot 0, every no-contact row on from before step 0
  // a race raced alone (a practice's session, practice_session.cpp, reads them as practice::Words)
  bool cpus_allowed = false;        // `cpus on`: a player gone is driven by the AI; left out, their seat retires
  bool solo = false, pause = false, save_state = false;
  u32 rewind = 0;                   // the rewind's reach in steps, 0 none
  u32 resume_countdown = 0;         // steps
  std::vector<BoardLine> board;     // the file's board lines, in its order: presentation only (mode_board.cpp)
  u32 players = 0;                  // bit i: a player drives seat i (the match start's player seats)
  std::vector<s32> globals, seat_words, team_words;  // their start values
  std::vector<Rule> rules;
  std::vector<u8> code;
  std::array<u8, 30> teams = SeatTeams();  // each seat's team (setup.mode.teams; none given: the seat's number)
  std::array<u32, 4> seed{};               // the generator's first state (Seed)
  Prepared prepared;                       // Read does it; see Prepare

  static constexpr std::array<u8, 30> SeatTeams()
  {
    std::array<u8, 30> t{};
    for (u8 i = 0; i < 30; ++i)
      t[i] = i;
    return t;
  }
};

// setup.mode, read and checked: the hash is the package's, the api this build's or older, every parameter given and
// in its range, the package's sections whole and every rule's offsets inside the code. Nothing (and `why` says it)
// otherwise.
std::optional<Package> Read(const picojson::value& mode, std::string* why);

// `p.prepared` from `p`'s code, rules, parameters and variables (Read calls it): again after any of them is changed.
void Prepare(Package& p);

// The generator's first state for the race of match `match_id` (formats.md section 9): SHA-256 of the match id's
// bytes and the package's hash (its 32 bytes), its first 16 bytes as four big-endian words, each through one step of
// SplitMix32. The same in every game of the race and in its replay.
std::array<u32, 4> Seed(const std::string& match_id, const std::string& hash);

// The game's square root (main.dol 0x8006D0B4), which the bytecode's FSqrt is, worked on the host step for step as
// the processor does it (mode.cpp); the compiler folds constants with the same steps (ModeOperators.GameSqrt).
float GameSqrt(float x);

// The run at the mode point (`block`: the block's address; `module`: the module pointer's value).
void Run(Guest& g, const Package& p, u32 block, u32 module);
// The mode point's host call: the race's mode run, and a mode error said once in the log.
void Step(Guest& g);
// Seat `seat`'s record in the block; 0 without a block.
u32 Record(const Guest& g, u32 seat);
// Seat `seat`'s move (MOVES: read by the image's mode_speed in the machine's step) and its rolling start's saved
// steering (ROLL_SAVE: written and read by the stand-in's rule hook wrapper in the machine's task); 0 without a mode.
u32 Move(const Guest& g, u32 seat);
u32 RollSave(const Guest& g, u32 seat);
// Seat `seat`'s stat record (STATS: read by the wrapper in the machine's task); 0 without a mode.
u32 Stat(const Guest& g, u32 seat);
// The run with no package past its end: the end clause's order held in the standing (a race the mode ended).
}  // namespace fzgx::mode
