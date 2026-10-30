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

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "fzgx/guest.h"
#include "mode_words.g.h"
#include "picojson.h"

namespace fzgx::mode
{
// The block's layout (port/image/routines/memmap.py names the first two: the records the image's sites read, and
// the end word its race-end clause reads).
constexpr u32 BLOCK = 0x3000, PAGE = 0x1000;
constexpr u32 RECORDS = 0, RECORD = 16;   // a seat's: +0 flags, +4 accelerator, +8 brake, +12 the held speed
constexpr u32 END = RECORDS + 30 * RECORD;  // 0x1E0: 0 the race goes on; 1 a mode error; 2 the mode ended it
constexpr u32 FAULT = END + 4;            // the fault's reason (Fault), and +8 its step
constexpr u32 RUNS = END + 12;            // the runs so far: the first page's step stamp
constexpr u32 OUT_MASK = END + 16;        // the seats out at the last run (bit i: seat i)
constexpr u32 OPS = END + 20;             // the last run's operations, and +4 the most of any run
constexpr u32 GLOBALS = 0x200;            // modes::GLOBALS words
constexpr u32 EDGES = GLOBALS + 4 * modes::GLOBALS;  // a `when` rule's: bit i its condition held for seat i
constexpr u32 SEAT_PAGE_SEATS = 15;       // a page's seats: its first word is the page's step stamp
constexpr u32 SEAT_WORDS = 0x10;          // in a seat page: seat k's words at +0x10 + 0x100 k
constexpr u32 PEDALS = 1, BOOST_OFF = 2, HELD = 4;  // a record's flags
constexpr float MOST_SPEED = 9000.0f;
static_assert(EDGES + 4 * modes::RULES <= PAGE && SEAT_WORDS + SEAT_PAGE_SEATS * 0x100 <= PAGE &&
              4 * modes::SEAT_WORDS == 0x100 && PAGE + 2 * PAGE == BLOCK);

constexpr u32 SeatWord(u32 seat, u32 word)
{
  return PAGE * (1 + seat / SEAT_PAGE_SEATS) + SEAT_WORDS + 0x100 * (seat % SEAT_PAGE_SEATS) + 4 * word;
}

enum Fault : u32
{
  FAULT_NONE,
  FAULT_BUDGET,   // the step's operations went past modes::BUDGET
  FAULT_QUEUE,    // more than modes::QUEUE actions
  FAULT_REAL,     // a real that is not finite for a game field
  FAULT_CODE,     // the bytecode does what it may not (an operand out of range, the stack past its room)
  FAULT_NO_BLOCK  // the game had no room for the block
};

constexpr u32 NONE = 0xFFFFFFFF;
struct Rule
{
  std::string label;
  u8 event = 0;
  u16 locals = 1;
  u32 when = NONE, during = NONE, body = 0;  // offsets in the code (`during`: the rule's `while`)
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
  std::vector<s32> globals, seat_words;  // their start values
  std::vector<Rule> rules;
  std::vector<u8> code;
};

// setup.mode, read and checked: the hash is the package's, the api this build's or older, every parameter given and
// in its range, the package's sections whole and every rule's offsets inside the code. Nothing (and `why` says it)
// otherwise.
std::optional<Package> Read(const picojson::value& mode, std::string* why);

// The run at the mode point (`block`: the block's address; `module`: the module pointer's value).
void Run(Guest& g, const Package& p, u32 block, u32 module);
// The mode point's host call: the race's mode run, and a mode error said once in the log.
void Step(Guest& g);
// Seat `seat`'s record in the block; 0 without a block.
u32 Record(const Guest& g, u32 seat);
}  // namespace fzgx::mode
