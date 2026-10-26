// The undo log's guard (undo_log.h, Tracking::Way): how the log learns of the first write to a page it does not
// watch. With the guard on, every tracked page that is not watched is write-protected (page_guard.h), and its first
// write, from whatever thread, is caught when it is made: the log then compares only the watched pages and the
// written ones, and the whole memory is no longer compared twice in a rollback frame. The whole comparison stays:
// in the learning steps, at the step the guard goes on, as the way when the guard cannot be had, and, in Both, as the
// check of every step.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "undo_log.h"

#include <algorithm>
#include <cstring>
#include <format>
#include <string>

#include "host_timers.h"
#include "internal.h"
#include "page_guard.h"

namespace fzgx::net
{
namespace
{
constexpr u32 BLOCK = 32;
constexpr u32 SHOWN_DIFFERENCES = 10;  // Both: the steps whose difference is told line by line
// With the guard, a watched page found unchanged this many comparisons in a row is protected again and no longer
// watched (it rests): a page the game writes now and then, or writes with the same values (each write faults once,
// changed or not), is not compared at every step for the rest of the race. A second of steps, doubled at each rest
// of the same page, ten times at most: a rest and the fault that ends it cost some 5 to 10 us, a comparison of a
// watched page about 0.16, so a page written again soon after each rest must stop resting. In rollback frames 6
// deep, at a constant 60, about 60 pages rested and faulted again every few frames (6 times a frame).
constexpr u32 QUIET_STEPS = 60;
constexpr u8 MOST_DOUBLINGS = 10;
}  // namespace

void ChooseWay(Guest& g, Tracking& s)
{
  TakeOffGuard(s);
  const std::string& asked = TheOptions().debug.undo_log;
  s.way = asked == "scan" ? Tracking::Way::Scan : asked == "both" ? Tracking::Way::Both : Tracking::Way::Guard;
  std::string why;
  if (s.way != Tracking::Way::Scan && !g.GetHost().RamGuardable(&why))
  {
    Notice("fzgx: undo log: no guard ({}): every page is compared before a frame's own step and the one after", why);
    s.way = Tracking::Way::Scan;
  }
  s.checked_steps = s.differing_steps = 0;
  s.quiet.assign(s.shadow.size() / PAGE, 0);
  s.rested.assign(s.shadow.size() / PAGE, 0);
  s.rests = 0;
  s.check_shadow.clear();
  s.check_hash.clear();
  if (s.way == Tracking::Way::Both)
  {
    s.check_shadow = s.shadow;
    s.check_hash = s.page_hash;
  }
}

bool PutOnGuard(Guest& g, Tracking& s)
{
  u8* live = g.Ram(s.region, s.shadow.size());
  std::string why;
  if (guard::Active())
  {
    // the process's one guard is another log's (the replay scrubber's other moment): its faults are that log's
    Notice("fzgx: undo log: the guard is another log's: this one compares every page before a frame's own step and "
           "the one after");
    s.way = Tracking::Way::Scan;
    return false;
  }
  if (!guard::Begin(live, s.shadow.size(), &why))
  {
    Warn("fzgx: undo log: the guard cannot be had ({}): every page is compared before a frame's own step and the "
         "one after",
         why);
    s.way = Tracking::Way::Scan;
    return false;
  }
  u32 pages = 0;
  for (u32 page = 0; page < s.shadow.size() / PAGE; ++page)
  {
    if (s.untracked[page] || s.watched[page])
      continue;
    if (!guard::Protect(page))
    {
      guard::End();
      Warn("fzgx: undo log: the system refused to protect page {:#x}: every page is compared before a frame's own "
           "step and the one after",
           s.region + page * PAGE);
      s.way = Tracking::Way::Scan;
      return false;
    }
    ++pages;
  }
  s.guarding = true;
  TraceGuard(s, std::format("on at the log's step {}: {} pages write-protected, {} watched", s.tracked_steps, pages,
                            s.watched_list.size()));
  Notice("fzgx: undo log: guard on at its step {}: {} pages write-protected, {} watched{}", s.tracked_steps, pages,
         s.watched_list.size(), s.way == Tracking::Way::Both ? "; every step checked against every page compared" : "");
  return true;
}

void CompareGuarded(Guest& g, Tracking& s, u32 t, Undo& entry)
{
  FZGX_TIME(UndoWatched);
  u8* live = g.Ram(s.region, s.shadow.size());
  guard::TakeWritten(&s.written);
  u32 late = 0, woke = 0;
  for (const u32 page : s.written)
  {
    if (s.watched[page] || s.untracked[page])
      continue;
    Watch(s, page * PAGE);
    if (s.rested[page] != 0)
    {
      ++woke;
      continue;  // written again after a rest: watched again, no late page
    }
    ++late;
    ++s.late_pages;
    if (s.trace)
      TraceLatePage(s, page * PAGE, t, false);
  }
  // The pages quiet long enough are protected again first, then compared with the rest: a write that landed before
  // the protection is in this comparison, one after it faults. One that changed after all stays watched, and open.
  s.resting.clear();
  for (const u32 off : s.watched_list)
  {
    if (!s.keep_watching && s.quiet[off / PAGE] >= QUIET_STEPS << std::min(s.rested[off / PAGE], MOST_DOUBLINGS) &&
        guard::Protect(off / PAGE))
      s.resting.push_back(off);
  }
  for (const u32 off : s.watched_list)
  {
    u16& quiet = s.quiet[off / PAGE];
    quiet = ComparePage(live, s.shadow.data(), off, entry.blocks) ? 0 : static_cast<u16>(std::min(quiet + 1, 0xFFFF));
  }
  for (const u32 off : s.resting)
  {
    if (s.quiet[off / PAGE] == 0)
    {
      guard::Open(off / PAGE);
      continue;
    }
    ++s.rests;
    s.rested[off / PAGE] = static_cast<u8>(std::min<u32>(s.rested[off / PAGE] + 1u, 0xFF));
    s.quiet[off / PAGE] = 0;
    s.watched[off / PAGE] = 0;
    // the hash of its shadow, as an unwatched page's is (a whole comparison reads it)
    s.page_hash[off / PAGE] = g.GetHost().PageHash(s.shadow.data() + off, PAGE);
    s.watched_list.erase(std::lower_bound(s.watched_list.begin(), s.watched_list.end(), off));
  }
  if (s.trace)
  {
    const u64 faults = guard::Faults();
    const u32 rested = static_cast<u32>(std::count_if(s.resting.begin(), s.resting.end(),
                                                      [&](u32 off) { return !s.watched[off / PAGE]; }));
    TraceGuardStep(s, t, static_cast<u32>(s.written.size()), late, woke, rested, faults - s.guard_faults,
                   guard::Guarded());
    s.guard_faults = faults;
  }
}

void CheckEntry(Guest& g, Tracking& s, u32 t, const Undo& entry)
{
  FZGX_TIME(UndoCheck);
  const u8* live = g.Ram(s.region, s.shadow.size());
  s.check_blocks.clear();
  for (u32 page = 0; page < s.shadow.size() / PAGE; ++page)
  {
    if (s.untracked[page])
      continue;
    const u64 hash = g.GetHost().PageHash(live + page * PAGE, PAGE);
    if (hash == s.check_hash[page])
      continue;
    ComparePage(live, s.check_shadow.data(), page * PAGE, s.check_blocks);
    s.check_hash[page] = hash;
  }
  ++s.checked_steps;
  const Blocks& got = entry.blocks;
  const Blocks& want = s.check_blocks;
  std::size_t i = 0;
  while (i < got.size() && i < want.size() && got[i].first == want[i].first && got[i].second == want[i].second)
    ++i;
  if (i == got.size() && i == want.size())
    return;
  if (++s.differing_steps <= SHOWN_DIFFERENCES)
  {
    const u32 at = i < want.size() ? want[i].first : got[i].first;
    Warn("fzgx: undo log check: before step {} the log holds {} blocks and every page compared gives {}; first "
         "difference at {:#x} ({} in the log, {} in the check)",
         t, got.size(), want.size(), s.region + at, i < got.size() ? "a block" : "none",
         i < want.size() ? "a block" : "none");
  }
}

void CheckRestored(Guest& g, Tracking& s, const Blocks& blocks)
{
  for (auto b = blocks.rbegin(); b != blocks.rend(); ++b)
    std::memcpy(s.check_shadow.data() + b->first, b->second.data(), BLOCK);
  u32 last = ~0u;
  for (const auto& block : blocks)
  {
    const u32 page = block.first / PAGE;
    if (page == last)
      continue;
    last = page;
    s.check_hash[page] = g.GetHost().PageHash(s.check_shadow.data() + page * PAGE, PAGE);
  }
}

void CheckKept(Guest& g, Tracking& s)
{
  const u8* live = g.Ram(s.region, s.shadow.size());
  for (const auto& [off, len] : s.kept)
  {
    std::memcpy(s.check_shadow.data() + off, live + off, len);
    for (u32 page = off / PAGE; page <= (off + len - 1) / PAGE; ++page)
      s.check_hash[page] = g.GetHost().PageHash(s.check_shadow.data() + page * PAGE, PAGE);
  }
}

void TakeOffGuard(Tracking& s)
{
  if (s.way == Tracking::Way::Both && s.checked_steps != 0)
    Notice("fzgx: undo log check: {} steps compared with every page: {}", s.checked_steps,
           s.differing_steps == 0 ? std::string("the log the same at every step")
                                  : std::format("the log different at {} steps", s.differing_steps));
  s.checked_steps = s.differing_steps = 0;
  if (!s.guarding)
    return;
  u32 pages[8];
  const u32 shown = guard::OtherThreadPages(pages, 8);
  std::string where;
  for (u32 i = 0; i < shown; ++i)
    where += std::format("{}{:#x}", i == 0 ? " (pages " : ", ", s.region + pages[i] * PAGE);
  TraceGuard(s, std::format("off: {} faults, {} of them on other threads than the game's; {} pages watched; a "
                            "watched page rested {} times",
                            guard::Faults(), guard::OtherThreadFaults(), s.watched_list.size(), s.rests));
  Notice("fzgx: undo log: guard off: {} first writes caught, {} of them on other threads than the game's{}; {} pages "
         "watched; a watched page rested {} times",
         guard::Faults(), guard::OtherThreadFaults(), shown != 0 ? where + ")" : std::string(), s.watched_list.size(),
         s.rests);
  guard::End();
  s.guarding = false;
}
}  // namespace fzgx::net
