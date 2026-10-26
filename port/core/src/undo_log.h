// The undo log's state and its three operations (undo_log.cpp), as the facade (rollback.cpp) reaches them. Nothing
// else includes this: the rest of the core has rollback.h.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "host_timers.h"
#include "rollback.h"
#include "util.h"

namespace fzgx::net
{
// 32-byte blocks: each its offset and its content before a step.
using Blocks = std::vector<std::pair<u32, std::array<u8, 32>>>;

struct Undo
{
  Blocks blocks;   // offset in the region, content before the step
  Blocks scratch;  // the same of the locked cache
  bool has_rng = false;
  std::array<u8, 4> rng{};
};

// The whole-memory comparisons of the undo tracking (PreStep), split over this thread and worker threads: the job is
// cut into pieces (contiguous ranges of pages), which every thread takes in turn until none is left, so that a worker
// that wakes late takes fewer and this thread does not wait for it to wake. The emulated CPU waits in the hook
// meanwhile, so the memory holds still. What each piece finds goes to a place of its own, and is read in the pieces'
// order: the result does not depend on which thread took which piece.
class ScanPool
{
public:
  using Job = std::function<void(u32 piece)>;
  explicit ScanPool(u32 threads) : m_threads_count(threads)
  {
    for (u32 i = 1; i < threads; ++i)
      m_threads.emplace_back([this] { Work(); });
  }
  ~ScanPool()
  {
    {
      std::lock_guard lock(m_mutex);
      m_stop = true;
    }
    m_wake.notify_all();
    for (std::thread& thread : m_threads)
      thread.join();
  }
  ScanPool(const ScanPool&) = delete;
  ScanPool& operator=(const ScanPool&) = delete;
  u32 Threads() const { return m_threads_count; }
  // job(0) to job(pieces - 1), each once, taken in turn by this thread and the workers; returns when all are done
  // and no worker is inside the job any more
  void Run(const Job& job, u32 pieces)
  {
    FZGX_TIME(UndoPool);
    {
      std::lock_guard lock(m_mutex);
      m_job = &job;
      m_pieces = pieces;
      m_next.store(0, std::memory_order_relaxed);
      m_finished.store(0, std::memory_order_relaxed);
      ++m_generation;
    }
    m_wake.notify_all();
    Take(job);
    FZGX_TIME(UndoPoolWait);
    std::unique_lock lock(m_mutex);
    m_done.wait(lock, [this] { return m_finished.load(std::memory_order_acquire) == m_pieces && m_inside == 0; });
    m_job = nullptr;  // a worker that wakes from here on finds no job, and goes back to sleep
  }

private:
  // the pieces not yet taken, one after the other
  void Take(const Job& job)
  {
    for (u32 piece; (piece = m_next.fetch_add(1, std::memory_order_relaxed)) < m_pieces;)
    {
      job(piece);
      if (m_finished.fetch_add(1, std::memory_order_acq_rel) + 1 == m_pieces)
      {
        std::lock_guard lock(m_mutex);
        m_done.notify_one();
      }
    }
  }
  void Work()
  {
    u64 seen = 0;
    while (true)
    {
      const Job* job;
      {
        std::unique_lock lock(m_mutex);
        m_wake.wait(lock, [&] { return m_stop || m_generation != seen; });
        if (m_stop)
          return;
        seen = m_generation;
        job = m_job;
        if (job == nullptr)
          continue;  // that run ended before this thread woke
        ++m_inside;
      }
      Take(*job);
      {
        std::lock_guard lock(m_mutex);
        if (--m_inside == 0)
          m_done.notify_one();
      }
    }
  }
  const u32 m_threads_count;
  std::vector<std::thread> m_threads;
  std::mutex m_mutex;
  std::condition_variable m_wake, m_done;
  const Job* m_job = nullptr;  // the run's job, while it runs (m_mutex)
  u32 m_pieces = 0;            // the run's pieces (set under m_mutex before the run's generation is)
  std::atomic<u32> m_next{0};      // the next piece to take
  std::atomic<u32> m_finished{0};  // the pieces done
  u32 m_inside = 0;                // workers inside the job (m_mutex)
  u64 m_generation = 0;
  bool m_stop = false;
};

// One piece's result of a whole-memory comparison: the blocks that changed (as in Undo) and the pages first seen
// changing.
struct ScanPart
{
  Blocks blocks;
  std::vector<u32> pages;
};

// A key (the replay scrubber): the log's moment before a step, whole: the pages it watched then (`pages`, offsets
// in the region; the watched list is kept in page order, so a later page may come before them) and their content,
// the locked cache's pages that differed from the fork's, the generator word.
struct Key
{
  std::vector<u32> pages;
  std::vector<u8> ram;
  std::vector<std::pair<u32, std::array<u8, 4096>>> scratch;
  std::array<u8, 4> rng{};
};

// The memory a key takes.
inline u64 KeyBytes(const Key& key)
{
  return key.ram.size() + key.pages.size() * sizeof(u32) + key.scratch.size() * (4096 + 4);
}

// UNIT steps' entries packed, or dropped (undo_pack.cpp; rollback.h: UNIT).
struct PackedUnit
{
  std::vector<u8> bytes;     // its steps one after the other, each made flat (undo_pack.cpp) and then packed by the
                             // host: the flat size, the packed size, whether packed, the bytes
  std::size_t content = 0;   // the bytes of content its entries hold (32 a block)
  bool hot = false;          // its entries are in the map too, unpacked
  bool dropped = false;      // nothing of it is kept: its steps are run again when they are wanted
};

// The undo log's state.
struct Tracking
{
  bool tracking = false;
  u32 region = 0;  // physical start of the tracked memory
  std::vector<u8> shadow;
  std::vector<u8> scratch_shadow;  // the locked cache (Host::Scratch) as it was before the previous step; empty: none
  std::array<u8, 4> shadow_rng{};
  std::map<u32, Undo> undo;  // undo[t]: memory before step t changed it
  // The lists of entries dropped (put back or final), emptied, their room kept: a new entry takes one, so that a step
  // logged allocates nothing once the race has run a while (DropEntry).
  std::vector<Blocks> spare;
  std::size_t spare_bytes = 0;  // the room the spare lists hold, in bytes
  std::vector<u8> watched;   // per page: compared at every step (it changed during the race so far)
  std::vector<u32> watched_list;
  std::vector<u8> untracked;  // per page: never compared, logged or put back
  std::vector<std::pair<u32, u32>> kept;  // parts of other pages (offset, length): kept as they are through a restore
  std::vector<u64> page_hash;  // per page not watched: the hash of its shadow (PreStep)
  std::vector<u8> exact;       // per page: compared with its shadow even when not watched (it holds a kept part)
  std::unique_ptr<ScanPool> scan_pool;
  std::vector<ScanPart> scan_parts;  // a place for each piece of a whole-memory comparison
  bool after_frame_step = false;  // the previous tracked step was a displayed frame's own
  // How a first write to a page not watched is found (Debug::undo_log; undo_guard.cpp). Scan: every page compared
  // before a frame's own step and the step after it. Guard: every page not watched write-protected once the learning
  // steps are over (page_guard.h), its first write caught when it is made, and only the watched and the written
  // pages compared. Both: the guard, and at every step every page compared against a shadow of the check's own;
  // the two logs must be the same, block for block.
  enum class Way
  {
    Scan,
    Guard,
    Both,
  };
  Way way = Way::Scan;
  bool guarding = false;   // the guard is on
  std::vector<u32> written;  // the pages the guard caught before this step (its room kept)
  std::vector<u16> quiet;    // per watched page, with the guard: the comparisons in a row that found it unchanged
  std::vector<u8> rested;    // per page, with the guard: the times it rested (each doubles the quiet it needs)
  std::vector<u32> resting;  // the watched pages protected again before this step's comparison (its room kept)
  u32 rests = 0;             // the times a watched page rested (protected again, and no longer watched)
  // Two moments over one memory (Rollback::KeepWatching): a page this log has seen change stays watched, for the
  // moments' exchanges, keys and adoptions read the two logs' watched pages as every page where they may differ. No
  // page rests.
  bool keep_watching = false;
  // Both: the check's own shadow and its pages' hashes, its blocks of this step, and its count
  std::vector<u8> check_shadow;
  std::vector<u64> check_hash;
  Blocks check_blocks;
  u32 checked_steps = 0;
  u32 differing_steps = 0;
  u32 tracked_steps = 0;
  u32 late_pages = 0;
  double track_us = 0;    // time spent comparing, in microseconds, since the last report
  double restore_us = 0;  // time spent putting memory back, since the last report
  u32 blocks_logged = 0;  // blocks logged since the last report
  // Tests (Debug::undo_trace, undo_trace.cpp), written only while `trace` is there: the file; the first step since
  // the last comparison that accounted for every changed page (of every page, or any with the guard on) and the
  // comparisons of watched pages only since; the late pages whose
  // change may be any of several steps'; whether each OS heap added up when last walked.
  std::unique_ptr<File> trace;
  u32 span_first = 0;
  u32 span_partial = 0;
  struct LateNote
  {
    u32 page, first, entry;  // offset in the region; the steps its change lies in, first to entry (its undo entry)
  };
  std::vector<LateNote> late_notes;
  u64 guard_faults = 0;  // the guard's faults when its last line was written
  std::array<int, 8> heap_state{};  // 0: not walked yet, 1: adds up, 2: does not
  u32 bad_dumps = 0;
  std::map<u32, std::vector<u8>> copies;  // the whole memory as it was before a step last ran ("check")
  // where the log begins, and its size
  u32 first_step = 0;        // the first step saved since StartTracking: the log holds nothing from before it
  u32 final_below = 0;       // entries below this step were dropped as final
  u64 undo_bytes = 0;        // the memory the entries' blocks take now (what their lists hold room for)
  std::map<u32, Key> keys;   // keys, by the step each is the moment before
  u64 key_bytes = 0;
  // packing: the units that are packed or dropped, by their first step (a unit not here: its entries are
  // in the map as the steps left them); the unpacked ones, the one used last at the end
  std::map<u32, PackedUnit> units;
  std::vector<u32> hot;
  Undo last_plain;                    // the step packed last, plain: the next step is XORed with it,
  std::vector<u8> last_flat;          // and packed with its flat bytes as the packer's prefix
  u32 last_plain_step = 0xFFFFFFFE;
  bool last_plain_sorted = false;
  u64 packed_bytes = 0, packed_content = 0;
  u32 packs = 0, unpacks = 0, dropped = 0, keys_dropped = 0;
  double pack_us = 0, unpack_us = 0;
};

// The memory an entry's blocks take.
inline u64 EntryBytes(const Undo& entry)
{
  return (entry.blocks.capacity() + entry.scratch.capacity()) * sizeof(entry.blocks[0]);
}

// undo_log.cpp: the log started from the memory as it is; before step t, what changed since the call before into
// step t - 1's entry; the memory back to before step t.
void StartTracking(Guest& g, Tracking& s);
void PreStep(Guest& g, Tracking& s, u32 t, bool frame_step);
void Restore(Guest& g, Tracking& s, u32 from, u32 t);
// undo_guard.cpp: the guard's way and, for Both, the check's shadow, when the log starts; the guard put on once the
// learning steps are over (false: it could not be, and the log scans from here); the watched and the written pages
// compared, in page order (a page written becomes watched); Both's check of a step's entry; a restore's blocks
// written into the check's shadow too; the guard off, and its lines, when the log stops.
void ChooseWay(Guest& g, Tracking& s);
bool PutOnGuard(Guest& g, Tracking& s);
void CompareGuarded(Guest& g, Tracking& s, u32 t, Undo& entry);
void CheckEntry(Guest& g, Tracking& s, u32 t, const Undo& entry);
void CheckRestored(Guest& g, Tracking& s, const Blocks& blocks);
void CheckKept(Guest& g, Tracking& s);
void TakeOffGuard(Tracking& s);
// A page into the watched ones, the list kept in page order (undo_log.cpp).
void Watch(Tracking& s, u32 off);
bool ComparePage(const u8* live, u8* shadow, u32 off, Blocks& out);

// An entry no longer needed (put back, its step final, packed, or a replay's unit let go): its lists go to the
// spare ones. TakeSpare: a list for a new entry, a spare one with the room an earlier entry left in it. EraseEntry:
// an entry out of the map, its bytes off the log's count (Tracking::undo_bytes) and its lists to the spare ones; the
// entry after it.
void DropEntry(Tracking& s, Undo& entry);
Blocks TakeSpare(Tracking& s);
std::map<u32, Undo>::iterator EraseEntry(Tracking& s, std::map<u32, Undo>::iterator it);
// The profiling build (host_timers.h): the watched pages as runs of guest addresses, a line when the log stops.
void ReportWatched(const Tracking& s);
// What this log puts back, as ranges of guest addresses, for the host's other threads (fzgx::UnderRollback): told
// when the log starts, forgotten when it stops or goes. One log's at a time, the last one started.
void TellPutBack(const Tracking* log, std::vector<std::pair<u32, u32>> ranges);
void ForgetPutBack(const Tracking* log);

// undo_trace.cpp (tests, Debug::undo_trace): the file opened; a late page found by a comparison of every page; the
// late pages a restore to step t is about to put back to an older moment than the rest (their bytes kept in
// `before`), and what it made of them; the OS heaps walked, a line when one stops or starts adding up.
void OpenUndoTrace(Tracking& s);
void TraceLatePage(Tracking& s, u32 page, u32 t, bool learning);
// the comparison before step t is done; complete: it accounts for every page that changed since the one before (a
// comparison of every page, or any with the guard on), so a late page it finds changed in step t - 1
void TraceCompared(Tracking& s, u32 t, bool complete);
// the guard's counts before step t (a line when it caught a first write or a page rested), and its other lines
void TraceGuardStep(Tracking& s, u32 t, u32 caught, u32 late, u32 woke, u32 rested, u64 faults, u32 guarded);
void TraceGuard(Tracking& s, const std::string& text);
std::vector<Tracking::LateNote> TraceStale(Tracking& s, const u8* live, u32 from, u32 t, std::vector<u8>* before);
void TraceRestored(Tracking& s, const u8* live, const std::vector<Tracking::LateNote>& stale,
                   const std::vector<u8>& before, u32 from, u32 t);
void TraceHeaps(Guest& g, Tracking& s, const std::string& when);
// With "check": the whole tracked memory kept before each step of that span, and after a restore to such a step
// compared with it, a line for every page that did not come back as it was.
void TraceCopy(Tracking& s, const u8* live, u32 t);
void TraceFaithful(Tracking& s, const u8* live, u32 from, u32 t);
// undo_moments.cpp (rollback.h, "two moments"): the entries of steps `from` - 1 down to t applied to the memory
// alone; the pages either log watches exchanged with (or copied from) a kept moment; the memory taken as the log's
// present; the changes since the last PreStep put back.
void ApplyEntries(Guest& g, const Tracking& s, u32 from, u32 t);
void ExchangeMoment(Guest& g, const Tracking& a, const Tracking& b, Rollback::Moment& m, bool copy);
void AdoptMemory(Guest& g, Tracking& s, const Tracking& with, u32 step);
void DiscardChanges(Guest& g, Tracking& s);
// undo_pack.cpp
void PackStep(Host& host, Tracking& s, u32 step);
bool EnsureEntries(Host& host, Tracking& s, u32 from, u32 to);
std::size_t DropFarthestUnit(Tracking& s, u32 step, u32 near);
std::size_t DropFarthestKey(Tracking& s, u32 step, u32 near, u32 first, u32 keep_every);
void KeepAKey(const Guest& g, Tracking& s, u32 step, const Rollback::Moment& base);
void TakeAKey(Guest& g, const Tracking& s, const Tracking& with, const Key& key, const Rollback::Moment& base);
}  // namespace fzgx::net
