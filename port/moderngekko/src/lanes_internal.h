// What the files of the machines' task as a host call have of each other (lanes.h): lanes.cpp, the task runner's
// calls of the task as one batch's jobs and their returns; lanes_run.cpp, a lane's dispatch and the registers a job
// moves; lanes_hooks.cpp, the hooks a lane's state carries and the record of its locked-cache reads and writes;
// lanes_partial*.cpp, the per-machine rollback that runs the batch (lanes_partial.h). The multicore physics these
// were first built for is parked on branch parked/lanes.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "Core/PowerPC/StaticRecomp/StaticRecompABI.h"

#include "adapter_internal.h"
#include "lanes.h"

namespace fzgx_port::lanes
{
// main.rel, read in its code (offsets from its .text): the machines' task fn_1_83858; the instruction after the task
// runner's call (fn_1_407C, its bctrl at +0x411C).
constexpr u32 TASK = game::REL_TEXT_AT + 0x83858;
constexpr u32 TASK_RETURN = game::REL_TEXT_AT + 0x4120;
constexpr u32 TASK_FRAME = 0x20;  // the task's stack frame; the word above it but one holds its return address
constexpr u32 MOST_JOBS = 32;
// the task runner's registers at its call: r24 the task's node, r30 the manager; a node: +0 the function, +4 its
// argument (the machine's block), +8 the next node; the manager: +8 the node it is running
constexpr u32 NODE_FUNCTION = 0, NODE_ARGUMENT = 4, NODE_NEXT = 8, MANAGER_NODE = 8;
// The locked cache as far as the game uses it (16 KiB): its words are what a lane records. An access past it is
// what a lane does not keep apart (Track::outside).
constexpr u32 SCRATCH = 0x4000, WORDS = SCRATCH / 4;
constexpr u32 SCRATCH_AT = game::LOCKED_CACHE;  // its first guest address
constexpr u32 WINDOW = 0x40000;  // the module's second window as the emulator gives it: the locked cache's 256 KiB
constexpr u16 DMAU = 922, DMAL = 923;    // the special registers of the locked cache's block copy
constexpr u32 GAVE_UP = 1u << 31;  // the exception bit a lane that gave up sets: generated code leaves at its next check

// The registers a unit starts from and leaves.
struct Regs
{
  u32 gpr[32];
  f64 fpr[32], ps1[32];
  u32 pc, lr, ctr, cr, xer, fpscr;
};

// A locked-cache word (its index), which of its four bytes count (bit i: the word's byte i, the most significant
// first), and its value. The record is kept by the byte: a part that writes one byte of a word has not read the
// other three, and its output is that byte alone.
struct Word
{
  u16 word = 0;
  u8 mask = 0;
  u32 value = 0;
};
// The bytes of a word that `mask` names, as a mask of its value.
inline u32 ByteMask(u32 mask)
{
  return ((mask & 1) ? 0xFF000000u : 0) | ((mask & 2) ? 0x00FF0000u : 0) | ((mask & 4) ? 0x0000FF00u : 0) |
         ((mask & 8) ? 0x000000FFu : 0);
}
// A nibble a word in a bitmap of the cache's bytes: word w's four bytes are bits 4w to 4w + 3.
inline u32 Nibble(const u8* bits, u32 word)
{
  return bits[word >> 1] >> ((word & 1) * 4) & 0xFu;
}
inline void SetNibble(u8* bits, u32 word, u32 mask)
{
  bits[word >> 1] |= static_cast<u8>(mask << ((word & 1) * 4));
}

// What one task's part (a serial part or a unit) did to the locked cache: the words it read before it wrote them,
// with the value read (its live-ins), and the words it wrote, with the value each held at the part's end.
struct Record
{
  std::vector<Word> reads, outputs;
  bool dma = false;  // it wrote the block copy's registers: their values at its end
  u32 dmau = 0, dmal = 0;
  void Clear()
  {
    reads.clear();
    outputs.clear();
    dma = false;
  }
};

// One machine's update of the batch.
struct Job
{
  u32 machine = 0, node = 0;
  Regs regs;         // as the update left them
  s64 cycles = 0;    // what the update charged
  Record unit;       // its locked-cache record
  u32 ret_pc = TASK_RETURN;  // where the update is done: the task's return
  u32 sp_change = TASK_FRAME;  // and its stack pointer there against its start
};

// The record a lane keeps of the part it runs now (lanes_hooks.cpp).
struct Lane;
struct Track
{
  // A module built with DolRecomp's DOLRECOMP_MEM2_TRACK=1 calls `record` from the generated code on every access to
  // its second window whose control word (the window's last eight bytes, WINDOW - 8) is not 0: that word is this
  // record's address, and `record` must stay its first member (forks/dolrecomp/4-lanes).
  void (*record)(Track* track, u8* window, u32 offset, u32 size_and_write) = nullptr;
  Lane* lane = nullptr;
  u64 accesses = 0;  // recorded by the module itself
  u8 written[SCRATCH / 8] = {}, read[SCRATCH / 8] = {};  // a bit a byte of the cache
  std::vector<Word> reads;
  std::vector<u16> touched;  // the words written, in the order first written (Close clears only those)
  bool outside = false;  // it reached something the lanes do not keep apart
};
// the generated code tests the two bitmaps itself at these places (forks/dolrecomp/4-lanes)
static_assert(offsetof(Track, written) == 24 && offsetof(Track, read) == 24 + 2048, "the module's record layout");

// A lane: a state of its own, on the game's thread, with a locked cache of its own while the batch is tracked.
struct Lane
{
  CPUState cpu;
  bool on_thread = false;    // a thread of its own runs it (never now: the multicore physics is parked)
  bool plain = false;        // no record: the game's own locked cache and the CPU core's hooks
  bool alone = false;        // nothing else runs
  bool hand_off = false;     // alone on the game's thread: an instruction left to the interpreter goes to the CPU
                             // core (the per-machine rollback's lane: lanes_partial.cpp, HandOff); else it gives up
  const char* gave_up = nullptr;  // it met what a lane does not handle (GiveUp)
  std::unique_ptr<u8[]> cache;  // its locked cache (WINDOW bytes; the game uses the first SCRATCH)
  Track track;
  // counted since the game began
  u64 slow_reads = 0, window_reads = 0, window_writes = 0, cache_ops = 0;
  // cache instructions of a lane on its own thread, for the game's thread to pass to the CPU core after the batch
  struct Cache
  {
    u8 operation;
    u32 ea, cia;
  };
  std::array<Cache, 64> cache_kept;
  u32 cache_count = 0;
  u32 dmau = 0, dmal = 0;    // the block copy's registers as this lane last wrote them
  bool dma_written = false;  // in the part it runs now
  // block copies out of the cache into main memory in the batch: where, for which job, and what main memory held
  // there before
  struct CopyOut
  {
    u32 job, memory, size, at;
  };
  std::vector<CopyOut> copies;
  std::vector<u8> copied_over;
  u32 job_now = 0;          // the job whose part the lane runs now
  u64 block_copies = 0, pointers = 0, copies_out = 0;
  u32 last_copy_out = 0;  // main memory's address of the last block copy out of the cache
};

struct Work
{
  std::string module;
  int (*dispatch)(CPUState*, u32) = nullptr;
  void (*set_valid)(unsigned, unsigned, int) = nullptr;
  bool inline_record = true;  // the module records the locked cache itself (DOLRECOMP_MEM2_TRACK); else the hooks do
  // the batch
  CPUState* game = nullptr;  // the game's own state, for the whole of a batch
  u32 jobs = 0, returned = 0, manager = 0;
  bool open = false;
  std::array<Job, MOST_JOBS> job;
};

extern Work s_work;

// lanes.cpp: the module's dispatch and its entry bytes' switch; the task runner's call of the machines' task as the
// batch's jobs (s_work.job); one call of the task returns as its job left the registers
bool FindModule(Work& w);
bool Collect(CPUState* cpu);
bool Return(CPUState* cpu);

// lanes_run.cpp
u64 Now();  // nanoseconds
[[noreturn]] void Fatal(const std::string& text);
void Note(const std::string& what);  // said in the log the first time
u32 Read32(u32 address);
void Write32(u32 address, u32 value);
void Save(const CPUState& cpu, Regs* regs);
void Load(const Regs& regs, CPUState* cpu);
// A tracked lane met what it does not handle: it stops, its record is not kept, and the generated code is made to
// leave soon (an exception set, no cycle budget left). A plain lane cannot give up: that ends the game.
void GiveUp(Lane& lane, const char* why);
// One dispatch at the lane's pc. False: nothing ran (the function's entry refused it, or the lane gave up).
bool Dispatch(Lane& lane);
// The CPU core checks the code at `pc`, as its run loop does before a dispatch; fatal when it is not the game's.
void Verify(u32 pc);
// lanes_hooks.cpp: the lane's state takes the game's (all of it but the registers a job brings), with hooks of the
// lane's own; `plain`: the game's locked cache and no record (the serial run)
void Dress(Lane& lane, const CPUState& from, bool plain);
// The part the lane runs now ends: its record into `record`, the lane's own cleared for the next.
void Close(Lane& lane, Record* record);
// lanes_partial*.cpp: the per-machine rollback (lanes_partial.h): the machines' task taken by it; the game's end;
// an instruction left to the interpreter on its lane, run by the CPU core
bool PartialWants(u32 address);
bool PartialHolds(u32 start, u32 end);
bool PartialCall(CPUState* cpu);
void PartialStop();
void HandOff(Lane& lane, u32 raw, u32 cia);
}  // namespace fzgx_port::lanes
