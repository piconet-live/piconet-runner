// The undo log's two moments, for the replay scrubber (rollback.h, "two moments of one race"): one log's entries
// applied to the memory alone, the memory exchanged with (or copied from) a kept moment, the memory taken as a log's
// present, the changes since the last step put back, and keys: whole copies of a log's moment before a step.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "undo_log.h"

#include "page_guard.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstring>
#include <utility>
#include <vector>

namespace fzgx::net
{
namespace
{
constexpr u32 BLOCK = 32;

// The parts of pages kept through a restore (Tracking::kept), as the memory holds them now; and written back.
std::vector<u8> KeptParts(const Tracking& s, const u8* live)
{
  std::vector<u8> kept;
  for (const auto& [off, len] : s.kept)
    kept.insert(kept.end(), live + off, live + off + len);
  return kept;
}

void PutKeptParts(const Tracking& s, u8* live, const std::vector<u8>& kept)
{
  u32 at = 0;
  for (const auto& [off, len] : s.kept)
  {
    std::memcpy(live + off, kept.data() + at, len);
    at += len;
  }
}

// The locked cache's two tracked stretches: before and after the part a rollback leaves as it is.
template <typename Each>
void EachScratchStretch(std::size_t size, Each&& each)
{
  each(std::size_t{0}, std::min<std::size_t>(size, SCRATCH_KEPT));
  if (size > SCRATCH_KEPT_END)
    each(std::size_t{SCRATCH_KEPT_END}, size - SCRATCH_KEPT_END);
}
}  // namespace

void ApplyEntries(Guest& g, const Tracking& s, u32 from, u32 t)
{
  u8* live = g.Ram(s.region, s.shadow.size());
  u8* rng = g.Ram(DOL_RNG, 4);
  u8* scratch = s.scratch_shadow.empty() ? nullptr : g.GetHost().Scratch();
  const std::vector<u8> kept = KeptParts(s, live);
  for (u32 k = from; k-- > t;)
  {
    const auto it = s.undo.find(k);
    if (it == s.undo.end())
      continue;
    for (auto b = it->second.blocks.rbegin(); b != it->second.blocks.rend(); ++b)
      std::memcpy(live + b->first, b->second.data(), BLOCK);
    for (auto b = it->second.scratch.rbegin(); scratch != nullptr && b != it->second.scratch.rend(); ++b)
      std::memcpy(scratch + b->first, b->second.data(), BLOCK);
    if (it->second.has_rng)
      std::memcpy(rng, it->second.rng.data(), 4);
  }
  PutKeptParts(s, live, kept);
}

void ExchangeMoment(Guest& g, const Tracking& a, const Tracking& b, Rollback::Moment& m, bool copy)
{
  u8* live = g.Ram(a.region, a.shadow.size());
  const std::vector<u8> kept = KeptParts(a, live);
  const auto take = [copy](u8* memory, u8* moment, std::size_t size) {
    if (copy)
      std::memcpy(memory, moment, size);
    else
      std::swap_ranges(memory, memory + size, moment);
  };
  for (std::size_t page = 0; page < a.watched.size(); ++page)
  {
    if (!a.untracked[page] && (a.watched[page] || b.watched[page]))
      take(live + page * PAGE, m.ram.data() + page * PAGE, PAGE);
  }
  PutKeptParts(a, live, kept);
  if (u8* scratch = a.scratch_shadow.empty() ? nullptr : g.GetHost().Scratch(); scratch != nullptr)
  {
    EachScratchStretch(std::min(a.scratch_shadow.size(), m.scratch.size()), [&](std::size_t from, std::size_t size) {
      take(scratch + from, m.scratch.data() + from, size);
    });
  }
  take(g.Ram(DOL_RNG, 4), m.rng, 4);
}

void AdoptMemory(Guest& g, Tracking& s, const Tracking& with, u32 step)
{
  const u8* live = g.Ram(s.region, s.shadow.size());
  for (std::size_t page = 0; page < s.watched.size(); ++page)
  {
    if (s.untracked[page] || !(s.watched[page] || with.watched[page]))
      continue;
    std::memcpy(s.shadow.data() + page * PAGE, live + page * PAGE, PAGE);
    Watch(s, static_cast<u32>(page * PAGE));
    if (s.way == Tracking::Way::Both)  // (the check's shadow takes the same present)
    {
      std::memcpy(s.check_shadow.data() + page * PAGE, live + page * PAGE, PAGE);
      s.check_hash[page] = g.GetHost().PageHash(live + page * PAGE, PAGE);
    }
  }
  if (const u8* scratch = s.scratch_shadow.empty() ? nullptr : g.GetHost().Scratch(); scratch != nullptr)
    std::memcpy(s.scratch_shadow.data(), scratch, s.scratch_shadow.size());
  g.CopyFrom(s.shadow_rng.data(), DOL_RNG, 4);
  for (auto it = s.undo.begin(); it != s.undo.end();)
    it = EraseEntry(s, it);
  s.first_step = step;
  s.final_below = 0;
}

void TakeOverMoment(Guest& g, Tracking& s, Tracking& w, const Guest& gw, u32 from, u32 to, const std::vector<u32>* keep)
{
  u8* live = g.Ram(s.region, s.shadow.size());
  const u8* other = gw.Ram(s.region, s.shadow.size());
  if (live == nullptr || other == nullptr || w.region != s.region || w.shadow.size() != s.shadow.size())
    return;
  // what the worker watched is watched here too (open under the guard, as a page that becomes watched is)
  for (const u32 off : w.watched_list)
  {
    if (s.watched[off / PAGE] || s.untracked[off / PAGE])
      continue;
    Watch(s, off);
    if (s.guarding)
      guard::Open(off / PAGE);
  }
  const std::vector<u8> kept = KeptParts(s, live);
  // the caller's words kept as the present has them (the deep-rollback worker: words an interrupt or the time base
  // writes, which W never has)
  std::vector<std::pair<u32, u32>> kept_words;
  for (const u32 a : keep ? *keep : std::vector<u32>())
  {
    if (a >= s.region && a - s.region + 4 <= s.shadow.size() && (a & 3) == 0 && !s.untracked[(a - s.region) / PAGE])
    {
      u32 raw;
      std::memcpy(&raw, live + (a - s.region), 4);
      kept_words.emplace_back(a - s.region, raw);
    }
  }
  // the blocks either timeline changed since the moment both held: a bit a block, then copied in order
  std::vector<u64> named((s.shadow.size() / BLOCK + 63) / 64, 0);
  const auto name = [&](const Tracking& t) {
    for (u32 k = from; k < to; ++k)
    {
      const auto it = t.undo.find(k);
      if (it == t.undo.end())
        continue;
      for (const auto& block : it->second.blocks)
        named[block.first / BLOCK / 64] |= u64{1} << (block.first / BLOCK % 64);
    }
  };
  name(s);
  name(w);
  Blocks copied;  // (for the check's shadow of Debug::undo_log "both": the content each block takes)
  for (std::size_t i = 0; i < named.size(); ++i)
  {
    for (u64 bits = named[i]; bits != 0; bits &= bits - 1)
    {
      const u32 off = static_cast<u32>((i * 64 + static_cast<std::size_t>(std::countr_zero(bits))) * BLOCK);
      if (s.untracked[off / PAGE])
        continue;
      std::memcpy(live + off, other + off, BLOCK);
      std::memcpy(s.shadow.data() + off, other + off, BLOCK);
      Dirty(s, off);
      if (s.way == Tracking::Way::Both)
      {
        auto& block = copied.emplace_back();
        block.first = off;
        std::memcpy(block.second.data(), other + off, BLOCK);
      }
    }
  }
  // the kept parts stay as the present leaves them, as a restore leaves them
  PutKeptParts(s, live, kept);
  PutKeptParts(s, s.shadow.data(), kept);
  for (const auto& [off, value] : kept_words)
  {
    std::memcpy(live + off, &value, 4);
    std::memcpy(s.shadow.data() + off, &value, 4);
  }
  if (s.way == Tracking::Way::Both)
  {
    CheckRestored(g, s, copied);
    CheckKept(g, s);
  }
  u8* scratch = s.scratch_shadow.empty() ? nullptr : g.GetHost().Scratch();
  const u8* their_scratch = s.scratch_shadow.empty() ? nullptr : gw.GetHost().Scratch();
  if (scratch != nullptr && their_scratch != nullptr)
  {
    EachScratchStretch(std::min<std::size_t>(s.scratch_shadow.size(), gw.GetHost().ScratchSize()),
                       [&](std::size_t at, std::size_t size) {
                         std::memcpy(scratch + at, their_scratch + at, size);
                         std::memcpy(s.scratch_shadow.data() + at, their_scratch + at, size);
                       });
  }
  gw.CopyFrom(s.shadow_rng.data(), DOL_RNG, 4);
  g.CopyTo(DOL_RNG, s.shadow_rng.data(), 4);
  // the history: this log's entries of those steps go, the worker's move in
  for (u32 k = from; k < to; ++k)
  {
    if (const auto it = s.undo.find(k); it != s.undo.end())
      EraseEntry(s, it);
    if (auto it = w.undo.find(k); it != w.undo.end())
    {
      w.undo_bytes -= EntryBytes(it->second);
      s.undo_bytes += EntryBytes(it->second);
      s.undo[k] = std::move(it->second);
      w.undo.erase(it);
    }
  }
}

void DiscardChanges(Guest& g, Tracking& s)
{
  u8* live = g.Ram(s.region, s.shadow.size());
  const std::vector<u8> kept = KeptParts(s, live);
  for (std::size_t page = 0; page < s.watched.size(); ++page)
  {
    if (!s.untracked[page] && std::memcmp(live + page * PAGE, s.shadow.data() + page * PAGE, PAGE) != 0)
      std::memcpy(live + page * PAGE, s.shadow.data() + page * PAGE, PAGE);
  }
  PutKeptParts(s, live, kept);
  if (u8* scratch = s.scratch_shadow.empty() ? nullptr : g.GetHost().Scratch(); scratch != nullptr)
  {
    EachScratchStretch(s.scratch_shadow.size(), [&](std::size_t from, std::size_t size) {
      std::memcpy(scratch + from, s.scratch_shadow.data() + from, size);
    });
  }
  g.CopyTo(DOL_RNG, s.shadow_rng.data(), 4);
}

void KeepAKey(const Guest& g, Tracking& s, u32 step, const Rollback::Moment& base)
{
  Key& key = s.keys[step];
  s.key_bytes -= KeyBytes(key);
  key.pages = s.watched_list;
  key.ram.resize(key.pages.size() * PAGE);
  for (std::size_t i = 0; i < key.pages.size(); ++i)
    g.CopyFrom(key.ram.data() + i * PAGE, s.region + key.pages[i], PAGE);
  key.scratch.clear();
  const u8* scratch = s.scratch_shadow.empty() ? nullptr : g.GetHost().Scratch();
  for (u32 off = 0; scratch != nullptr && off + PAGE <= s.scratch_shadow.size() && off + PAGE <= base.scratch.size(); off += PAGE)
  {
    if (std::memcmp(scratch + off, base.scratch.data() + off, PAGE) == 0)
      continue;
    auto& page = key.scratch.emplace_back();
    page.first = off;
    std::memcpy(page.second.data(), scratch + off, PAGE);
  }
  g.CopyFrom(key.rng.data(), DOL_RNG, 4);
  s.key_bytes += KeyBytes(key);
}

void TakeAKey(Guest& g, const Tracking& s, const Tracking& with, const Key& key, const Rollback::Moment& base)
{
  u8* live = g.Ram(s.region, s.shadow.size());
  const std::vector<u8> kept = KeptParts(s, live);
  // every page either log watches as the fork had it, then the key's pages over them
  for (std::size_t page = 0; page < s.watched.size(); ++page)
  {
    if (!s.untracked[page] && (s.watched[page] || with.watched[page]))
      std::memcpy(live + page * PAGE, base.ram.data() + page * PAGE, PAGE);
  }
  for (std::size_t i = 0; i < key.pages.size(); ++i)
    std::memcpy(live + key.pages[i], key.ram.data() + i * PAGE, PAGE);
  PutKeptParts(s, live, kept);
  if (u8* scratch = s.scratch_shadow.empty() ? nullptr : g.GetHost().Scratch(); scratch != nullptr)
  {
    std::array<u8, SCRATCH_KEPT_END - SCRATCH_KEPT> records;  // (the display's records stay as they are)
    std::memcpy(records.data(), scratch + SCRATCH_KEPT, records.size());
    std::memcpy(scratch, base.scratch.data(), std::min(s.scratch_shadow.size(), base.scratch.size()));
    for (const auto& [off, bytes] : key.scratch)
      std::memcpy(scratch + off, bytes.data(), PAGE);
    std::memcpy(scratch + SCRATCH_KEPT, records.data(), records.size());
  }
  g.CopyTo(DOL_RNG, key.rng.data(), 4);
}
}  // namespace fzgx::net
