// The undo log: what every step changed in the simulation's memory, found by comparing with a shadow copy
// (PreStep), and put back (Restore). What is tracked and why: rollback.h.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "undo_log.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <format>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include "internal.h"
#include "page_guard.h"

namespace fzgx::net
{
using Clock = std::chrono::steady_clock;

// Undo tracking. Before every step (the displayed frame's own and each re-simulated one) the memory is compared with a
// shadow copy of it as it was before the previous step; the 32-byte blocks that changed since (the previous step's
// changes, and the drawing's between displayed frames) go to the previous step's undo entry with their old content,
// and the shadow takes the present. A rollback so puts the whole of the memory back to one moment, consistently
// (putting back only what the steps changed left structures the drawing shares, such as the heap's, inconsistent:
// the game crashed). Only the watched pages are compared before every step: the first 120 tracked steps compare every
// page and a changed page joins them; after that, once a displayed frame, the unwatched pages are compared too, and a
// changed one (a late page) is recorded the same way and watched from then on.
//
// Cost: the whole-memory comparisons (18.6 MiB, two a displayed frame
// with a rollback) were 85% of it, bound by reading the shadow, which nothing else keeps in the cache. An unwatched
// page is therefore compared by a hash of the live page against the hash of its shadow (XXH3, 64 bits), the shadow
// itself read only when they differ; and the pages are split over a few threads. A page's shadow changes only when it
// is compared (and it is then watched; with the guard, a page that rests and stops being watched is hashed again) or
// put back by a restore (blocks of the undo entries, which are watched pages or, with the guard, pages a restore's
// write makes watched again, and the kept parts, whose pages are always compared in full), so the hashes stay those
// of the shadow.
constexpr u32 LEARN_STEPS = 120;
// The pages of a piece of a whole-memory comparison (ScanPool): about 20 us of hashing, 73 pieces of 18.6 MiB.
constexpr u32 SCAN_PIECE = 64;
constexpr u32 BLOCK = 32;
// the undo log keeps whole blocks: the locked cache's kept part must be whole blocks to stay out of every entry
static_assert(SCRATCH_KEPT % BLOCK == 0 && SCRATCH_KEPT_END % BLOCK == 0 && SCRATCH_KEPT < SCRATCH_KEPT_END);

// The blocks of page `off` that differ from the shadow, appended to `out` with their old content, and the shadow
// brought up to date; false when the page is unchanged.
bool ComparePage(const u8* live, u8* shadow, u32 off, Blocks& out)
{
  if (std::memcmp(live + off, shadow + off, PAGE) == 0)
    return false;
  for (u32 b = off; b < off + PAGE; b += BLOCK)
  {
    u64 x[4], y[4];
    std::memcpy(x, live + b, BLOCK);
    std::memcpy(y, shadow + b, BLOCK);
    if (((x[0] ^ y[0]) | (x[1] ^ y[1]) | (x[2] ^ y[2]) | (x[3] ^ y[3])) != 0)
    {
      auto& block = out.emplace_back();
      block.first = b;
      std::memcpy(block.second.data(), shadow + b, BLOCK);
      std::memcpy(shadow + b, live + b, BLOCK);
    }
  }
  return true;
}

void Watch(Tracking& s, u32 off)
{
  if (s.watched[off / PAGE])
    return;
  s.watched[off / PAGE] = 1;
  s.watched_list.insert(std::lower_bound(s.watched_list.begin(), s.watched_list.end(), off), off);
}

namespace
{
#ifdef FZGX_HOST_TIMERS
// The profiling build's count of the pages an entry holds blocks of (host_timers.h).
u32 PagesOf(const Blocks& blocks)
{
  std::vector<u32> pages;
  pages.reserve(blocks.size());
  for (const auto& block : blocks)
    pages.push_back(block.first / PAGE);
  std::sort(pages.begin(), pages.end());
  return static_cast<u32>(std::unique(pages.begin(), pages.end()) - pages.begin());
}

void NotePages(int kind, const Undo& entry)
{
  FZGX_NOTE_PAGES(kind, PagesOf(entry.blocks), PagesOf(entry.scratch));
}
#endif

// Before a frame's own step and the step after it, and in the learning steps: every page compared, the watched ones
// with their shadow and the others by their hash first, split over the scan pool; the pages first seen changing are
// watched from here on.
void CompareEveryPage(Guest& g, Tracking& s, u32 t, bool learning, Undo& entry)
{
  u8* live = g.Ram(s.region, s.shadow.size());
  u8* shadow = s.shadow.data();
  FZGX_TIME(UndoFull);
  const u32 pages = static_cast<u32>(s.shadow.size() / PAGE);
  const u32 pieces = (pages + SCAN_PIECE - 1) / SCAN_PIECE;
  s.scan_parts.resize(pieces);
  const ScanPool::Job compare = [&](u32 piece) {
    ScanPart& out = s.scan_parts[piece];
    out.blocks.clear();
    out.pages.clear();
    for (u32 page = piece * SCAN_PIECE; page < std::min(pages, (piece + 1) * SCAN_PIECE); ++page)
    {
      if (s.untracked[page])
        continue;
      if (s.watched[page])
      {
        ComparePage(live, shadow, page * PAGE, out.blocks);
        continue;
      }
      if (!s.exact[page])
      {
        const u64 hash = g.GetHost().PageHash(live + page * PAGE, PAGE);
        if (hash == s.page_hash[page])
          continue;
        s.page_hash[page] = hash;  // when unchanged after all (never: the shadow's hash was out of date)
      }
      if (ComparePage(live, shadow, page * PAGE, out.blocks))
        out.pages.push_back(page * PAGE);
    }
  };
  s.scan_pool->Run(compare, pieces);
  FZGX_TIME(UndoGather);
  size_t blocks = 0;
  for (const ScanPart& part : s.scan_parts)
    blocks += part.blocks.size();
  entry.blocks.reserve(blocks);
  for (const ScanPart& part : s.scan_parts)
  {
    entry.blocks.insert(entry.blocks.end(), part.blocks.begin(), part.blocks.end());
    for (const u32 off : part.pages)
    {
      Watch(s, off);
      if (!learning)
        ++s.late_pages;
      if (s.trace)
        TraceLatePage(s, off, t, learning);
    }
  }
}
}  // namespace

void PreStep(Guest& g, Tracking& s, u32 t, bool frame_step)
{
  const auto started = Clock::now();
  u8* live = g.Ram(s.region, s.shadow.size());
  u8* rng = g.Ram(DOL_RNG, 4);
  u8* shadow = s.shadow.data();
  if (s.trace)
  {
    TraceHeaps(g, s, std::format("before step {} ({})", t, frame_step ? "a frame's own" : "run again"));
    TraceCopy(s, live, t);
  }
  const bool learning = s.tracked_steps < LEARN_STEPS;
  s.first_step = s.tracked_steps++ == 0 ? t : s.first_step;
  // The guard goes on at the first step after the learning ones, and that step compares every page once more: what
  // was written before the protection landed is found there, what is written after it faults.
  const bool guard_on = !learning && s.way != Tracking::Way::Scan && !s.guarding && PutOnGuard(g, s);
  // Without the guard the whole memory is compared before a frame's own step and before the step after it:
  // presentation tasks run in the frame's own step only, and one touching a page for the first time (the face board
  // loading a portrait model) would otherwise be put down to a later step, so a rollback in between left that page
  // newer than the rest. With it, a first write is caught when it is made, whatever step it is in.
  const bool full = guard_on || (!s.guarding && (learning || frame_step || s.after_frame_step));
  s.after_frame_step = frame_step;
  Undo entry;
  entry.blocks = TakeSpare(s);
  entry.scratch = TakeSpare(s);
  if (full)
    CompareEveryPage(g, s, t, learning, entry);
  else if (s.guarding)
    CompareGuarded(g, s, t, entry);
  else
  {
    FZGX_TIME(UndoWatched);
    for (const u32 off : s.watched_list)
      ComparePage(live, shadow, off, entry.blocks);
  }
  if (guard_on)
  {
    // the pages the whole comparison just found are watched now, and open; what was caught meanwhile is compared
    // again at the next step (watched pages are, in any case)
    for (const u32 off : s.watched_list)
      guard::Open(off / PAGE);
  }
  if (s.way == Tracking::Way::Both)
    CheckEntry(g, s, t, entry);
  if (s.trace)
    TraceCompared(s, t, full || s.guarding);  // with the guard, every first write since the last one is caught
  // the locked cache, whole, before every step: a few pages, and the game's collision work area changes in every step.
  // Its kept part is never seen changing: the shadow takes it first, so no entry holds a block of it and no restore
  // writes one.
  if (!s.scratch_shadow.empty())
  {
    FZGX_TIME(UndoScratch);
    const u8* scratch = g.GetHost().Scratch();
    if (scratch != nullptr && s.scratch_shadow.size() >= SCRATCH_KEPT_END)
      std::memcpy(s.scratch_shadow.data() + SCRATCH_KEPT, scratch + SCRATCH_KEPT, SCRATCH_KEPT_END - SCRATCH_KEPT);
    for (u32 off = 0; scratch != nullptr && off < s.scratch_shadow.size(); off += PAGE)
      ComparePage(scratch, s.scratch_shadow.data(), off, entry.scratch);
  }
  if (std::memcmp(rng, s.shadow_rng.data(), 4) != 0)
  {
    entry.has_rng = true;
    std::memcpy(entry.rng.data(), s.shadow_rng.data(), 4);
    std::memcpy(s.shadow_rng.data(), rng, 4);
  }
  s.blocks_logged += static_cast<u32>(entry.blocks.size() + entry.scratch.size());
#ifdef FZGX_HOST_TIMERS
  NotePages(learning ? 0 : frame_step ? 1 : full ? 2 : 3, entry);
#endif
  if (t > 0)
  {
    FZGX_TIME(UndoStore);
    Undo& kept = s.undo[t - 1];  // (an entry there already is replaced: its lists go to the spare ones)
    s.undo_bytes -= EntryBytes(kept);
    DropEntry(s, kept);
    kept = std::move(entry);
    s.undo_bytes += EntryBytes(kept);
  }
  s.track_us += std::chrono::duration<double, std::micro>(Clock::now() - started).count();
}

// Memory (and the shadow) back to before step t, from the present (before step `from`).
void Restore(Guest& g, Tracking& s, u32 from, u32 t)
{
  FZGX_TIME(Restore);
  const auto started = Clock::now();
  u8* live = g.Ram(s.region, s.shadow.size());
  u8* rng = g.Ram(DOL_RNG, 4);
  u8* scratch = s.scratch_shadow.empty() ? nullptr : g.GetHost().Scratch();
  std::vector<u8> kept;
  for (const auto& [off, len] : s.kept)
    kept.insert(kept.end(), live + off, live + off + len);
  std::vector<u8> stale_before;
  const std::vector<Tracking::LateNote> stale =
      s.trace ? TraceStale(s, live, from, t, &stale_before) : std::vector<Tracking::LateNote>();
  for (u32 k = from; k-- > t;)
  {
    const auto it = s.undo.find(k);
    if (it == s.undo.end())
      continue;
    for (auto b = it->second.blocks.rbegin(); b != it->second.blocks.rend(); ++b)
    {
      std::memcpy(live + b->first, b->second.data(), BLOCK);
      std::memcpy(s.shadow.data() + b->first, b->second.data(), BLOCK);
    }
    if (s.way == Tracking::Way::Both)
      CheckRestored(g, s, it->second.blocks);
    for (auto b = it->second.scratch.rbegin(); scratch != nullptr && b != it->second.scratch.rend(); ++b)
    {
      std::memcpy(scratch + b->first, b->second.data(), BLOCK);
      std::memcpy(s.scratch_shadow.data() + b->first, b->second.data(), BLOCK);
    }
    if (it->second.has_rng)
    {
      std::memcpy(rng, it->second.rng.data(), 4);
      std::memcpy(s.shadow_rng.data(), it->second.rng.data(), 4);
    }
    EraseEntry(s, it);
  }
  u32 at = 0;
  for (const auto& [off, len] : s.kept)
  {
    std::memcpy(live + off, kept.data() + at, len);
    std::memcpy(s.shadow.data() + off, kept.data() + at, len);
    at += len;
  }
  if (s.way == Tracking::Way::Both)
    CheckKept(g, s);
  if (s.trace)
  {
    TraceRestored(s, live, stale, stale_before, from, t);
    TraceFaithful(s, live, from, t);
    TraceHeaps(g, s, std::format("after the restore from before step {} to before step {}", from, t));
  }
  s.restore_us += std::chrono::duration<double, std::micro>(Clock::now() - started).count();
}

// The spare lists kept: the entries a frame's rollback can hold, twice over, and at most SPARE_BYTES of room in all
// (a race's lists hold about 0.1 MiB each). A list of more than SPARE_LARGEST bytes (a step that changed some 1.7
// MiB) is let go: it would hold its memory for the rest of the race. A PC may have no page file.
constexpr std::size_t SPARE_LISTS = 64;
constexpr std::size_t SPARE_LARGEST = 2u << 20;
constexpr std::size_t SPARE_BYTES = 16u << 20;

void DropEntry(Tracking& s, Undo& entry)
{
  for (Blocks* blocks : {&entry.blocks, &entry.scratch})
  {
    const std::size_t bytes = blocks->capacity() * sizeof(Blocks::value_type);
    if (s.spare.size() >= SPARE_LISTS || bytes == 0 || bytes > SPARE_LARGEST || s.spare_bytes + bytes > SPARE_BYTES)
      continue;
    blocks->clear();
    s.spare.push_back(std::move(*blocks));
    s.spare_bytes += bytes;
  }
}

Blocks TakeSpare(Tracking& s)
{
  if (s.spare.empty())
    return {};
  Blocks blocks = std::move(s.spare.back());
  s.spare.pop_back();
  s.spare_bytes -= blocks.capacity() * sizeof(Blocks::value_type);
  return blocks;
}

std::map<u32, Undo>::iterator EraseEntry(Tracking& s, std::map<u32, Undo>::iterator it)
{
  s.undo_bytes -= EntryBytes(it->second);
  DropEntry(s, it->second);
  return s.undo.erase(it);
}

void ReportWatched([[maybe_unused]] const Tracking& s)
{
#ifdef FZGX_HOST_TIMERS
  std::vector<u32> pages(s.watched_list.begin(), s.watched_list.end());
  std::sort(pages.begin(), pages.end());
  std::string runs;
  for (std::size_t i = 0; i < pages.size();)
  {
    std::size_t j = i + 1;
    while (j < pages.size() && pages[j] == pages[j - 1] + PAGE)
      ++j;
    runs += std::format(" {:08x}-{:08x}({})", s.region + pages[i], s.region + pages[j - 1] + PAGE, j - i);
    i = j;
  }
  Notice("fzgx: host pages watched: {} of {} ({} late), region from {:08x}:{}", pages.size(), s.shadow.size() / PAGE,
         s.late_pages, s.region, runs);
#endif
}

// What a restore of this log writes in main memory: the tracked memory less the ranges the mod lists as never
// rolled back (whole pages left out of tracking, and the parts of other pages kept as the present leaves them),
// and the executable's generator word. (The locked cache, which a restore writes too, is no main memory: no
// address asked of fzgx::UnderRollback reaches it.)
namespace
{
std::vector<std::pair<u32, u32>> PutBack(Guest& g, const Tracking& s)
{
  const u32 end = s.region + static_cast<u32>(s.shadow.size());
  std::vector<std::pair<u32, u32>> left;
  const u32 count = std::min(g.Read32(UNTRACKED), MAX_UNTRACKED);
  for (u32 r = 0; r < count; ++r)
  {
    const u32 lo = std::max(g.Read32(UNTRACKED + 4 + 8 * r), s.region);
    const u32 hi = std::min(g.Read32(UNTRACKED + 8 + 8 * r), end);
    if (lo < hi)
      left.emplace_back(lo, hi);
  }
  std::sort(left.begin(), left.end());
  std::vector<std::pair<u32, u32>> ranges;
  u32 at = s.region;
  for (const auto& [lo, hi] : left)
  {
    if (lo > at)
      ranges.emplace_back(at, lo);
    at = std::max(at, hi);
  }
  if (at < end)
    ranges.emplace_back(at, end);
  ranges.emplace_back(DOL_RNG, DOL_RNG + 4);  // the one word below the log that a restore writes
  return ranges;
}

std::mutex s_put_back_mutex;
const Tracking* s_put_back_log = nullptr;
std::vector<std::pair<u32, u32>> s_put_back;
}  // namespace

void TellPutBack(const Tracking* log, std::vector<std::pair<u32, u32>> ranges)
{
  const std::lock_guard lock(s_put_back_mutex);
  s_put_back_log = log;
  s_put_back = std::move(ranges);
}

void ForgetPutBack(const Tracking* log)
{
  const std::lock_guard lock(s_put_back_mutex);
  if (s_put_back_log != log)
    return;
  s_put_back_log = nullptr;
  s_put_back.clear();
}
}  // namespace fzgx::net

namespace fzgx
{
bool UnderRollback(u32 address, u32 size)
{
  const std::lock_guard lock(net::s_put_back_mutex);
  const u64 end = static_cast<u64>(address) + size;
  for (const auto& [lo, hi] : net::s_put_back)
  {
    if (size != 0 && address < hi && end > lo)
      return true;
  }
  return false;
}
}  // namespace fzgx

namespace fzgx::net
{
void StartTracking(Guest& g, Tracking& s)
{
  const u32 module = g.Read32(MODULE_POINTER);
  s.region = (module + DATA) & ~(PAGE - 1);
  s.shadow.resize(STATE_END - s.region);
  s.watched.assign(s.shadow.size() / PAGE, 0);
  s.untracked.assign(s.shadow.size() / PAGE, 0);
  s.kept.clear();
  u32 untracked_pages = 0;
  const u32 ranges = std::min(g.Read32(UNTRACKED), MAX_UNTRACKED);
  for (u32 r = 0; r < ranges; ++r)
  {
    // clipped to the tracked region; whole pages are left out of tracking, the parts of others kept through a restore
    const u32 lo = std::max((g.Read32(UNTRACKED + 4 + 8 * r)), s.region);
    const u32 hi = std::min((g.Read32(UNTRACKED + 8 + 8 * r)),
                            s.region + static_cast<u32>(s.shadow.size()));
    for (u32 a = lo; a < hi;)
    {
      const u32 page = a & ~(PAGE - 1);
      const u32 end = std::min(page + PAGE, hi);
      if (a == page && end == page + PAGE)
      {
        untracked_pages += s.untracked[(page - s.region) / PAGE] ? 0 : 1;
        s.untracked[(page - s.region) / PAGE] = 1;
      }
      else
      {
        s.kept.emplace_back(a - s.region, end - a);
      }
      a = end;
    }
  }
  TellPutBack(&s, PutBack(g, s));
  s.watched_list.clear();
  s.keep_watching = false;
  s.tracked_steps = 0;
  OpenUndoTrace(s);
  s.first_step = s.final_below = 0;
  s.undo_bytes = 0;
  s.keys.clear();
  s.key_bytes = 0;
  s.units.clear();
  s.hot.clear();
  s.packed_bytes = s.packed_content = 0;
  s.last_plain = Undo{};
  s.last_plain_step = 0xFFFFFFFE;
  g.CopyFrom(s.shadow.data(), s.region, s.shadow.size());
  g.CopyFrom(s.shadow_rng.data(), DOL_RNG, 4);
  // the locked cache (tests: Options::keep_scratch leaves it out, the fault it shows)
  s.scratch_shadow.clear();
  if (const u8* scratch = g.GetHost().Scratch(); scratch != nullptr && !TheOptions().debug.keep_scratch)
    s.scratch_shadow.assign(scratch, scratch + (g.GetHost().ScratchSize() & ~(PAGE - 1)));
  s.exact.assign(s.shadow.size() / PAGE, 0);
  for (const auto& [off, len] : s.kept)
  {
    for (u32 page = off / PAGE; page <= (off + len - 1) / PAGE; ++page)
      s.exact[page] = 1;
  }
  s.page_hash.resize(s.shadow.size() / PAGE);
  for (size_t page = 0; page < s.page_hash.size(); ++page)
    s.page_hash[page] = g.GetHost().PageHash(s.shadow.data() + page * PAGE, PAGE);
  // a quarter of the hardware threads, at most 4: the graphics thread runs meanwhile
  const u32 threads = std::clamp<u32>(std::thread::hardware_concurrency() / 4, 1, 4);
  if (!s.scan_pool || s.scan_pool->Threads() != threads)
    s.scan_pool = std::make_unique<ScanPool>(threads);
  ChooseWay(g, s);
  s.tracking = true;
  Notice("fzgx: rollback on, tracking {} KiB of the simulation's memory and {} KiB of the locked cache, {} pages "
         "and {} parts never rolled back, nor the locked cache from {:#x} to {:#x}; compared by {} threads",
         s.shadow.size() / 1024, s.scratch_shadow.size() / 1024, untracked_pages, s.kept.size(), SCRATCH_KEPT,
         SCRATCH_KEPT_END, s.scan_pool->Threads());
}
}  // namespace fzgx::net
