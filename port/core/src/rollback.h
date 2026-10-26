// The undo log of rollback and the state checksum, lifted from the Dolphin fork (dolphin-fzgx Core/FzgxNet.cpp at
// 5bdc7c640e): what every simulation step changes in the simulation's memory, kept so that the memory can be put
// back to before any step not yet final.
//
// What is tracked: main.rel's .data and .bss and the heap, from the page of main.rel's .data up to STATE_END
// (0x8155B000), the DOL's random generator word, and the locked data cache (Host::Scratch, guest 0xE0000000 on): the
// game's collision work area is there, and a step reads from it what the step before left (left out, as the
// fork left it out, the first step run again after a rollback read what the newest step had left, and its
// machines' surface word +0x21C came out one step early), less the game's records of the display hardware in it
// (SCRATCH_KEPT). Nothing else. In particular the mod's data window
// (ModData, 0x81800000 on) is not compared, logged or put back, exactly as the fork never tracked its mod memory;
// what a rollback changes there (the step word, the input rows) the session writes itself.
// The whole list, region by region, with what each is paired with outside the restored set:
// port/facts/rollback-state.tsv. The test TestRollbacksStateList runs the undo log against it: a change here to
// what is put back fails until the list says the same.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "fzgx/guest.h"
#include "game_facts.h"

namespace fzgx::net
{
// The undo log's entries are packed and dropped by units of this many steps (undo_pack.cpp; the replay scrubber).
constexpr u32 UNIT = 32;

// The game's and the mod's addresses the undo log and the checksum read.
constexpr u32 CONTROL = layout::CONTROL;
constexpr u32 STEP = CONTROL + 124;         // the step about to run, counted from the relay's "go"
constexpr u32 UNTRACKED = CONTROL + 0x700;  // set by the mod: the count of ranges never rolled back (at most 32),
constexpr u32 MAX_UNTRACKED = 32;           // then start, end pairs; read when rollback starts
constexpr u32 MODULE_POINTER = game::MODULE_LIST;
constexpr u32 BSS = 0x1BA040;            // main.rel's .bss from its header
constexpr u32 MACHINES = BSS + 0x6D838;  // the machine array pointer (0x620 bytes each)
constexpr u32 MACHINE_COUNT = BSS + 0x6D84A;
constexpr u32 DATA = 0x16D600;           // main.rel's .data from its header
constexpr u32 STATE_END = game::STATE_END;  // the simulation's memory ends below the display buffers
constexpr u32 DOL_RNG = game::DOL_RNG;
constexpr u32 PAGE = 0x1000;
// The part of the locked cache a rollback leaves as it is, as offsets into it: the game's two records of what it
// last told the hardware. Its set-up routine (0x80070158) lays out the cache as the maths area (0 to 0x1C0), the
// frame-buffer record (0x1C0, 0x1C bytes: the picture buffer the video chip shows next, which of the two is in
// turn, the command queues) and, from the next 32 bytes on, the graphics wrapper's record (0x1E0, 0xCA0 bytes:
// every setting it last made, by which it skips one it believes is made). Only display routines of the executable
// read either (0x8006FD1C to 0x80070A58, 0x80072778 to 0x80078CDC); no step does. A rollback does not put the
// hardware back, so it must not put these back either (put back, the wrapper skipped
// the portraits' copy size after a boost's screen copy and the copies wiped the face board's heap).
constexpr u32 SCRATCH_KEPT = 0x1C0;
constexpr u32 SCRATCH_KEPT_END = 0xE80;

// What the undo log did since the last TakeWork: for the log line and the mod's counters. A value, not shared state.
struct Work
{
  double track_us = 0;    // comparing memory with the shadow (SaveStep)
  double restore_us = 0;  // putting memory back (RestoreTo)
  u32 blocks = 0;         // 32-byte blocks logged
  u32 watched_pages = 0;  // pages compared at every step, now
  u32 late_pages = 0;     // pages first seen changing after the learning steps, in all
};

// Rollback as its user sees it: save a step, restore to a step, re-simulate, checksum. It knows nothing of where
// inputs come from or why a step is wrong: it is told which step to go back to and which inputs each step uses.
// (The seam of a later move: rollback becomes a service of the core mod, netcode a mod of its own.)
class Rollback
{
public:
  Rollback();
  ~Rollback();
  Rollback(const Rollback&) = delete;
  Rollback& operator=(const Rollback&) = delete;

  // The undo log is being kept (between Start and Stop).
  bool Active() const;
  // Starts the undo log from the memory as it is now (the first rollback frame of a race).
  void Start(Guest& g);
  // Ends it: every entry is dropped (the race is over; every step is final).
  void Stop();
  // The bytes of the simulation's memory the log covers (0 when not active).
  std::size_t TrackedBytes() const;

  // Save a step. Before step t runs: what changed since the previous call goes to step t - 1's undo entry.
  // frame_step: the step is a displayed frame's own (not a re-simulated one).
  void SaveStep(Guest& g, u32 t, bool frame_step);
  // Steps below `step` are final: their undo entries are dropped. No later RestoreTo may name a step below it.
  void Final(u32 step);

  // Restore to a step. The simulation's memory back to before step t, from the present (before step `from`), and the
  // one word of the mod's data window a rollback itself changes: the step about to run (control block +124).
  // Nothing else in the window is touched.
  void RestoreTo(Guest& g, u32 from, u32 t);

  // Re-simulate. The inputs step t runs with: `records` holds one 6-byte control record per entry of `seats` (the
  // race's player seats), in that order. Written for the step about to run and for each step run again.
  static void UseInputs(Guest& g, u32 t, const std::vector<int>& seats, const u8* records);
  // The inputs step t last ran with, as UseInputs took them (kept for the last 1024 steps): for a final step, the
  // official ones. What a replay records.
  static void UsedInputs(const Guest& g, u32 t, const std::vector<int>& seats, u8* records);
  // The game's frame routine runs `steps` more steps in this displayed frame after the one about to run, asking
  // before each (the hook) for its inputs.
  static void Resimulate(Guest& g, u32 steps);
  // The hook was called before one of those further steps (not before a displayed frame's first).
  static bool Resimulating(const Guest& g);
  // The step about to run, counted from "go".
  static u32 Step(const Guest& g);
  // A count that grows whenever the memory becomes another moment than a rollback's RestoreTo makes it: a race's
  // undo log started or stopped, the scrubber's exchanges, applied entries, adoptions, keys and discards. The
  // per-machine rollback keeps a step's last run only while it holds (the adapter's lanes_partial.h).
  static u32 Moments();

  // Checksum. Of the simulation's state as it is now (the machines' blocks and progress records).
  static u32 Checksum(Guest& g);
  // The checksum kept for a step (a ring of 1024 steps): stored by the caller before the next step runs, read back
  // by whoever needs a final step's (the relay report, a replay's recording and its check).
  static void KeepChecksum(Guest& g, u32 step, u32 value);
  static u32 KeptChecksum(const Guest& g, u32 step);
  // Whether the checksum covers the word at this byte offset of a machine's block (0x620 bytes) or, with `progress`,
  // of its progress record (0x1FC bytes).
  static bool InChecksum(bool progress, u32 offset);

  Work TakeWork();

  // The lowest step RestoreTo may name now (the log holds every step from it on), and the memory the log's entries
  // take now.
  u32 Oldest() const;
  std::size_t LogBytes() const;

  // The replay scrubber (scrub.cpp): two moments of one race over one memory, each with a log of its own, both
  // started from the same memory. The memory holds one of them; `Moment` holds the other's content of every page
  // either log has seen change, of the locked cache and of the generator word.
  struct Moment
  {
    std::vector<u8> ram, scratch;
    u8 rng[4] = {};
  };
  // The memory as it is, whole, into `moment` (when the two moments are still one).
  void Keep(const Guest& g, Moment* moment) const;
  // The memory and `moment` change places, on the pages this log or `other`'s watches; with `copy` the memory
  // takes the moment's content and the moment keeps it. Memory never rolled back stays as it is, in both.
  void Exchange(Guest& g, const Rollback& other, Moment* moment, bool copy = false);
  // The memory back from before step `from` to before step t by this log's entries, which stay; the log's own
  // shadow is not touched (the memory then is no longer this log's moment).
  void Apply(Guest& g, u32 from, u32 t) const;
  // The memory as it is now is this log's moment before step `step`: every entry is dropped, and the log holds
  // nothing from before that step.
  void Adopt(Guest& g, const Rollback& other, u32 step);
  // Whatever changed in the memory since the last SaveStep is put back (a displayed frame that ran no step of
  // this moment).
  void Discard(Guest& g);

  // From now on every page this log has seen change stays watched (with the guard, none rests): called on both
  // logs of two moments, whose exchanges, keys and adoptions take the pages either log watches as every page where
  // the two moments may differ. Until the next Start.
  void KeepWatching();

  // Keys: whole copies of this log's moment, kept every so many steps, so that a moment far from the log's present
  // is reached from the nearest key and not from the present. KeepKey: the memory as it is now, on the pages this
  // log watches, is the moment before `step` (the locked cache's pages only where they differ from `base`'s).
  // KeyAbove, KeyBelow: the nearest key at or above, or at or below, a step (0xFFFFFFFF: none). TakeKey: the memory
  // becomes that key's moment, on every page this log or `other`'s watches (a page the key has not is `base`'s:
  // this log had not changed it yet). KeyBytes: the memory the keys take.
  void KeepKey(const Guest& g, u32 step, const Moment& base);
  u32 KeyAbove(u32 step) const;
  u32 KeyBelow(u32 step) const;
  void TakeKey(Guest& g, const Rollback& other, u32 key, const Moment& base) const;
  std::size_t KeyBytes() const;

  // Packing (undo_pack.cpp): a step's entry packed into its unit of UNIT steps (Host::Pack) as it is logged; steps
  // `to` to `from` - 1 made ready for RestoreTo or Apply, unpacking what is packed (false: some were dropped); the
  // packed unit farthest from a step dropped (the bytes freed; 0: none left); what the packed units hold. And what
  // the log's entries hold unpacked: steps, blocks, the bytes of content in them (32 a block), and as allocated.
  struct Packed
  {
    std::size_t bytes = 0, content = 0;
    u32 units = 0, hot = 0, dropped = 0, packs = 0, unpacks = 0, keys_dropped = 0;
    double pack_us = 0, unpack_us = 0;
  };
  struct Held
  {
    std::size_t steps = 0, blocks = 0, content = 0, allocated = 0;
  };
  void PackStep(Guest& g, u32 step);
  bool Ready(Guest& g, u32 from, u32 to);
  std::size_t DropFarthest(u32 step, u32 near);
  // A key that may go, farthest from `step` (undo_pack.cpp, DropFarthestKey): the bytes freed (0: none).
  std::size_t DropFarKey(u32 step, u32 near, u32 first, u32 keep_every);
  Packed PackedUnits() const;
  Held Holds() const;

private:
  struct Impl;
  std::unique_ptr<Impl> m;
};

// FNV-1a over every machine's block and progress record, less the pointer and drawing-only words (Rollback::Checksum
// with main.rel's address given).
u32 StateChecksum(Guest& g, u32 module);
}  // namespace fzgx::net
