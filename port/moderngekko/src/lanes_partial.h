// Per-machine rollback: a step run again after a rollback runs only the
// machines a late input can change. The machines' task (fn_1_83858, a call for each machine in a step) is run by the
// adapter on the game's thread, on a lane of its own (the lanes' machinery: lanes_internal.h), every job recorded
// (its locked-cache reads and writes, its registers and cycles). When a step runs again, a machine runs if
//   - its input row differs from the step's last run, or
//   - any byte of its records differs (block, progress record, the four track records, segment, driver block, car
//     object, its countdown word, its record in a game mode's block: fzgx::ModeRecord), or
//   - a locked-cache word it read last time holds another value now, or the floating-point status it started from
//     differs, or
//   - its call is the task's last (racer 0's: the stand-in routine does the frame's own work there), or
//   - its last run did what a put-back cannot give back (a block copy out of the locked cache, an instruction handed
//     to the CPU core, a word of the guard written), or
//   - the course objects' flags it starts from differ from its last run's (the guard: once they differ, every later
//     job of the batch runs).
// Every other machine is put back as the step's last run left it: its records, its locked-cache writes, its
// registers and cycles, and the task runner's return slot that a task's prologue writes.
// The guard (lanes_partial_guard.cpp; study section 9): the words outside a machine's records that its task can
// write, read before and after every job: the effects' serial count, the three random generators, the sound queue's
// next place, the mute routine's two counts, and the course objects' first words (a mine's hit).
// The contact task (fn_1_850C4) runs whole, on the CPU core, in every run of a step. Its contact jobs (its walls,
// hazards and progress calls put back by the same rule) are gone, as is the "trace" mode; the private history keeps
// both.
//
// "debug.partialRollback" (fzgx::Debug): "off" leaves the task to the CPU core, so every step run again runs whole;
// "check" runs every step run again twice from the same memory, whole and partial, and compares the two word for
// word (lanes_partial_check.cpp); "writes" notes every word of main memory a job writes outside its machine's
// records (lanes_partial_guard.cpp). The path is on with none of them. "guard": false runs without the guard (tests:
// the faults it is there for).
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <string>
#include <vector>

#include "lanes_internal.h"

namespace PowerPC
{
struct PowerPCState;
}

namespace fzgx_port::lanes::partial
{
constexpr u32 CARS = 30;
constexpr u32 CAR_MANAGER = game::MACHINES_AT - 0x18;    // .bss 0x6D820; +0xD4: a countdown word a machine
constexpr u32 CPU_GLOBALS = game::REL_BSS_AT + 0x7AC48;  // the computer drivers' globals (rubber band at +0x49)
constexpr u32 CAR_MANAGER_SIZE = 0xA00, CPU_GLOBALS_SIZE = 0x60;

// One part of a machine's records: block, progress, the four track records, segment, driver, car object, countdown.
enum Kind : u8 { BLOCK, PROGRESS, TRACK0, TRACK1, TRACK2, TRACK3, SEGMENT, DRIVER, CAR, COUNTDOWN, MODE, KINDS };
struct Span
{
  u32 address = 0, size = 0;
  u8 kind = 0;
  u32 offset = 0;  // where it starts in its part (the car object's second span)
};
// The car object's presentation halfword (+0x3BA, +0x3BB seen changing): the drawing after a displayed frame's step
// writes it, and no code the machines' task reaches reads or writes it (study section 8). It is no part of the
// records: neither compared nor put back (port/facts/rollback-state.tsv, car-presentation).
constexpr u32 CAR_PRESENTATION = 0x3BA, CAR_PRESENTATION_SIZE = 2;
struct Machine
{
  std::vector<Span> spans;
  u32 bytes = 0;
};
using Layout = std::array<Machine, CARS>;

// The steps kept: more than the deepest rollback a race makes (net_internal.h MAX_AHEAD, 24 steps ahead of the last
// official step). A step further back (a practice rewind, a scrubber jump) finds no last run and runs whole.
constexpr u32 KEEP = 32, NONE = 0xFFFFFFFF, PAGE = 0x1000, ROW = 6;
using Row = std::array<u8, ROW>;

// The last run of a step: what every machine started from and ended with, each job's locked-cache record, the
// floating-point status it started from, its registers and cycles.
// The guard's words (lanes_partial_guard.cpp): their values, and the course objects' first words.
constexpr u32 GUARD_WORDS = 8;
struct Guard
{
  std::array<u32, GUARD_WORDS> words{};
  std::vector<u32> objects;
};

struct Kept
{
  u32 step = NONE, jobs = 0, moment = 0;
  std::array<u32, MOST_JOBS> index{};
  std::array<Row, CARS> rows{};
  std::array<std::vector<u8>, CARS> start, end;
  std::array<Record, MOST_JOBS> rec;
  std::array<Regs, MOST_JOBS> regs;
  std::array<u32, MOST_JOBS> fpscr_in{};
  std::array<s64, MOST_JOBS> cycles{};
  std::array<bool, MOST_JOBS> whole{};  // it runs in every later run of the step (no_put_back)
  std::vector<u8> tables;  // "check": the car manager's and the drivers' globals at the batch's start
  u32 entry_cr = 0;        // the task runner's condition register at the batch's first call
  std::array<Guard, MOST_JOBS> guard_in;  // the guard as each job found it
};

// The batch: the jobs themselves are the lanes' (s_work.job, jobs, manager, open, returned).
struct Batch
{
  bool tried = false, ready = false;
  u32 max_step = 0, moment = 0;
  std::array<u32, MOST_JOBS> index{};  // each job's machine
  std::array<bool, MOST_JOBS> whole{};  // this run did what a put-back cannot give back
  std::array<u32, MOST_JOBS> fpscr_in{};
  std::array<bool, CARS> run{};
  Regs entry;
  Layout layout;
  Lane lane;
  std::vector<Kept> kept = std::vector<Kept>(KEEP);
  std::array<u32, KEEP> runs{};
  std::vector<u8> tables;
  std::array<Row, CARS> rows;
  std::array<std::vector<u8>, CARS> starts;
  std::array<Guard, MOST_JOBS> guard_in;
  Guard guard;              // as the last job left it
  // counted since the game began
  u64 batches = 0, again = 0, chosen = 0, ran = 0, live_in_failed = 0, fpscr_failed = 0, handed = 0;
  u64 checked = 0, differing = 0, guard_written = 0, guard_diverged = 0;
};
extern Batch s_b;

// lanes_partial.cpp: the path itself
bool Mode(const char* mode);
bool Logging();  // "debug.partialRollback.log" names a file
u32 Step();
PowerPC::PowerPCState& Ppc();
bool Ready(CPUState* cpu);
bool CollectIndexes();
void BeginRun(CPUState* cpu, bool plain);
void RunJob(u32 k, bool plain);
void EndRun(CPUState* cpu, bool plain);
// The machines this run of the step runs (b.run), against the step's last run (`old`; none: all of them): the rows
// and the records' starts kept for Remember. The count chosen.
u32 Choose(CPUState* cpu, u32 step, const Kept* old);
// Job k not run: as the step's last run left it. Its locked-cache reads and its floating-point status are checked
// first: the words that hold another value now (0: put back), and with a log, where they are in `first`.
u32 PutBack(CPUState* cpu, u32 k, const Kept& old, std::string* first);
// The chosen machines run, the others put back (a put-back whose check fails runs); the count that ran so.
u32 RunChosen(CPUState* cpu, u32 step, const Kept* old);
void Remember(CPUState* cpu, u32 step);
// The records as spans (lanes_partial.cpp)
void LayOut(const CPUState* cpu, u32 count, Layout* layout);
void Gather(const CPUState* cpu, const Machine& m, std::vector<u8>* to);
void Scatter(CPUState* cpu, const Machine& m, const std::vector<u8>& from);
const char* KindName(u32 kind);
void Line(const std::string& text);

// lanes_partial_guard.cpp: the guard, and "writes"
bool GuardOn();
void ReadGuard(const CPUState* cpu, Guard* g);
bool SameObjects(const Guard& a, const Guard& b);
bool SameGuard(const Guard& a, const Guard& b);
std::string GuardDifference(const Guard& a, const Guard& b);  // "<word>:<was>-><now>,..." for the log
// "writes": main memory as the batch found it, then after each job the words it wrote that are not its machine's
// records, the stack or the task runner's node word, noted (`kind`: car; nullptr: what the
// code between two jobs wrote is taken in without a note)
bool WritesOn(u32 step);
void WritesBegin(const CPUState* cpu);
void WritesAfter(const CPUState* cpu, const char* kind, u32 machine);
void WritesSummary();
// A machine's records against a copy (Gather's order), in place
bool SameRecords(const CPUState* cpu, const Machine& m, const std::vector<u8>& copy);
// The machine and part an address is in ("m12 block+0x94"), the car manager's or the drivers' globals' offset, or
// "global 80xxxxxx".
std::string Where(const Layout& layout, u32 address);
int MachineOf(const Layout& layout, u32 address);  // -1: none

// lanes_partial_check.cpp: "check", and the log's lines
// What a check found differing between the whole run and the partial one: words of main memory (and of the dead
// stack, counted apart), of the locked cache, of the data window, jobs' registers, and the rest (block copy, flags)
struct Diffs
{
  u32 words = 0, dead = 0, cache = 0, window = 0, regs = 0, other = 0;
  std::vector<std::string> first;
};
void CheckBatch(CPUState* cpu, u32 step, const Kept* old);
void BatchLine(u32 step, bool again, u32 chosen, u64 whole, u64 part, u64 keep, u32 failed, const Diffs* d);
void Watch(u32 k, const char* how);  // "watch": the words a job changed
// The words a job run because of a failed check wrote, against its last run's ("check" with a log)
std::string SameOutputs(CPUState* cpu, u32 k, const Kept& old);
// Every part of a machine's records that differs between two copies: "R,<part>:<bytes>:<first>-<last>,..."
std::string FirstDifference(const Machine& m, const std::vector<u8>& x, const std::vector<u8>& y);
// The car manager's tables and the drivers' globals kept for the batch, a "G" line of the words that differ
void Tables(CPUState* cpu, u32 step, const Kept* old);

}  // namespace fzgx_port::lanes::partial
