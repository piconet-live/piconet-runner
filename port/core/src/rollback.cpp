// Rollback as its user sees it (rollback.h): the facade over the undo log (undo_log.cpp) and the state checksum
// (checksum.cpp).
// SPDX-License-Identifier: GPL-3.0-or-later

#include "rollback.h"

#include <algorithm>
#include <array>
#include <iterator>
#include <map>
#include <cstddef>
#include <cstring>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include "undo_log.h"

namespace fzgx::net
{
namespace
{
// The mod's words a rollback's user reaches through Rollback (netframe.py, standin.py).
constexpr u32 EXTRA_STEPS = CONTROL + 8;    // steps the frame routine runs in this displayed frame after the first
constexpr u32 RESIMULATING = CONTROL + 12;  // 1 while those run
constexpr u32 CHECKSUMS = layout::CHECKSUMS;
constexpr u32 INPUTS = layout::INPUTS;
constexpr int RECORD = 6;

// Rollback::Moments per memory: every change of a memory to another moment than RestoreTo's, by any log. A few
// memories at most (the game's, the worker's, a test's); the worker's thread (S2) counts its own memory's.
std::mutex s_moments_mutex;
std::vector<std::pair<const Host*, u32>> s_moments;

u32& MomentsOf(const Host* memory)
{
  for (auto& [host, count] : s_moments)
  {
    if (host == memory)
      return count;
  }
  return s_moments.emplace_back(memory, 0).second;
}
}  // namespace

struct Rollback::Impl : Tracking
{
  // the memory `g` became another moment: this log's count when it is its own, and the memory's
  void Moved(const Guest& g)
  {
    if (&g.GetHost() == home)
      ++moments;
    const std::lock_guard lock(s_moments_mutex);
    ++MomentsOf(&g.GetHost());
  }
};

u32 Rollback::Moments() const
{
  return m->moments;
}

u32 Rollback::Moments(const Host& memory)
{
  const std::lock_guard lock(s_moments_mutex);
  return MomentsOf(&memory);
}


Rollback::Rollback(bool tells) : m(std::make_unique<Impl>())
{
  m->tells = tells;
}
Rollback::~Rollback()
{
  TakeOffGuard(*m);
  ForgetPutBack(m.get());
}

bool Rollback::Active() const
{
  return m->tracking;
}

void Rollback::Start(Guest& g)
{
  m->home = &g.GetHost();
  m->Moved(g);
  StartTracking(g, *m);
}

void Rollback::Stop()
{
  if (m->home != nullptr)
  {
    ++m->moments;
    const std::lock_guard lock(s_moments_mutex);
    ++MomentsOf(m->home);
  }
  if (m->tracking)
    ReportWatched(*m);
  TakeOffGuard(*m);
  m->tracking = false;
  m->undo.clear();
  m->spare.clear();
  m->spare_bytes = 0;
  ForgetPutBack(m.get());
  m->undo_bytes = 0;
  m->keys.clear();
  m->key_bytes = 0;
  m->units.clear();
  m->hot.clear();
  m->packed_bytes = m->packed_content = 0;
  m->last_plain = Undo{};
  m->last_plain_step = 0xFFFFFFFE;
}

std::size_t Rollback::TrackedBytes() const
{
  return m->tracking ? m->shadow.size() + m->scratch_shadow.size() : 0;
}

void Rollback::SaveStep(Guest& g, u32 t, bool frame_step)
{
  PreStep(g, *m, t, frame_step);
}

void Rollback::SaveAgain(u32 t)
{
  m->save_again = t;
}

void Rollback::Final(u32 step)
{
  FZGX_TIME(Final);
  for (auto it = m->undo.begin(); it != m->undo.end() && it->first < step;)
    it = EraseEntry(*m, it);
  m->final_below = std::max(m->final_below, step);
}

u32 Rollback::Oldest() const
{
  return std::max(m->first_step, m->final_below);
}

std::size_t Rollback::LogBytes() const
{
  return static_cast<std::size_t>(m->undo_bytes) + m->spare_bytes;
}

void Rollback::Keep(const Guest& g, Moment* moment) const
{
  moment->ram.resize(m->shadow.size());
  g.CopyFrom(moment->ram.data(), m->region, moment->ram.size());
  moment->scratch.clear();
  if (const u8* scratch = m->scratch_shadow.empty() ? nullptr : g.GetHost().Scratch(); scratch != nullptr)
    moment->scratch.assign(scratch, scratch + m->scratch_shadow.size());
  g.CopyFrom(moment->rng, DOL_RNG, 4);
}

void Rollback::Exchange(Guest& g, const Rollback& other, Moment* moment, bool copy)
{
  m->Moved(g);
  m->all_dirty = true;
  ExchangeMoment(g, *m, *other.m, *moment, copy);
}

void Rollback::Apply(Guest& g, u32 from, u32 t) const
{
  m->Moved(g);
  if (&g.GetHost() == m->home)
    m->all_dirty = true;
  ApplyEntries(g, *m, from, t);
}

void Rollback::Adopt(Guest& g, const Rollback& other, u32 step)
{
  m->Moved(g);
  AdoptMemory(g, *m, *other.m, step);
}

void Rollback::KeepWatching()
{
  m->keep_watching = true;
}

void Rollback::Discard(Guest& g)
{
  m->Moved(g);
  m->all_dirty = true;
  DiscardChanges(g, *m);
}

Rollback::Held Rollback::Holds() const
{
  Held held;
  for (const auto& [step, entry] : m->undo)
  {
    const std::size_t blocks = entry.blocks.size() + entry.scratch.size();
    ++held.steps;
    held.blocks += blocks;
    held.content += blocks * 32;
    // (a map's node: three pointers and a colour beside the key and the entry; an allocation's own header, 16)
    held.allocated += EntryBytes(entry) + sizeof(entry) + 48 + 32;
  }
  return held;
}

void Rollback::PackStep(Guest& g, u32 step)
{
  fzgx::net::PackStep(g.GetHost(), *m, step);
}

bool Rollback::Ready(Guest& g, u32 from, u32 to)
{
  return EnsureEntries(g.GetHost(), *m, from, to);
}

std::size_t Rollback::DropFarthest(u32 step, u32 near)
{
  return DropFarthestUnit(*m, step, near);
}

std::size_t Rollback::DropFarKey(u32 step, u32 near, u32 first, u32 keep_every)
{
  return DropFarthestKey(*m, step, near, first, keep_every);
}

Rollback::Packed Rollback::PackedUnits() const
{
  Packed p;
  p.bytes = static_cast<std::size_t>(m->packed_bytes);
  p.content = static_cast<std::size_t>(m->packed_content);
  for (const auto& [first, unit] : m->units)
    p.units += unit.dropped ? 0 : 1;
  p.hot = static_cast<u32>(m->hot.size());
  p.dropped = m->dropped;
  p.keys_dropped = m->keys_dropped;
  p.packs = m->packs;
  p.unpacks = m->unpacks;
  p.pack_us = m->pack_us;
  p.unpack_us = m->unpack_us;
  return p;
}

u32 Rollback::KeyBelow(u32 step) const
{
  auto it = m->keys.upper_bound(step);
  return it == m->keys.begin() ? 0xFFFFFFFF : (--it)->first;
}

void Rollback::KeepKey(const Guest& g, u32 step, const Moment& base)
{
  KeepAKey(g, *m, step, base);
}

u32 Rollback::KeyAbove(u32 step) const
{
  const auto it = m->keys.lower_bound(step);
  return it == m->keys.end() ? 0xFFFFFFFF : it->first;
}

void Rollback::TakeKey(Guest& g, const Rollback& other, u32 key, const Moment& base) const
{
  m->Moved(g);
  m->all_dirty = true;
  if (const auto it = m->keys.find(key); it != m->keys.end())
    TakeAKey(g, *m, *other.m, it->second, base);
}

std::size_t Rollback::KeyBytes() const
{
  return static_cast<std::size_t>(m->key_bytes);
}

void Rollback::RestoreTo(Guest& g, u32 from, u32 t)
{
  Restore(g, *m, from, t);
  g.Write32(STEP, t);
}

void Rollback::UseInputs(Guest& g, u32 t, const std::vector<int>& seats, const u8* records)
{
  for (std::size_t j = 0; j < seats.size(); ++j)
    g.CopyTo(INPUTS + ((t & 1023) * 30 + static_cast<u32>(seats[j])) * 8, records + j * RECORD, RECORD);
}

void Rollback::UsedInputs(const Guest& g, u32 t, const std::vector<int>& seats, u8* records)
{
  for (std::size_t j = 0; j < seats.size(); ++j)
    g.CopyFrom(records + j * RECORD, INPUTS + ((t & 1023) * 30 + static_cast<u32>(seats[j])) * 8, RECORD);
}

void Rollback::Resimulate(Guest& g, u32 steps)
{
  g.Write32(EXTRA_STEPS, steps);
}

bool Rollback::Resimulating(const Guest& g)
{
  return g.Read32(RESIMULATING) != 0;
}

u32 Rollback::Step(const Guest& g)
{
  return g.Read32(STEP);
}

u32 Rollback::Checksum(Guest& g)
{
  return StateChecksum(g, g.Read32(MODULE_POINTER));
}

void Rollback::KeepChecksum(Guest& g, u32 step, u32 value)
{
  g.Write32(CHECKSUMS + (step & 1023) * 4, value);
}

u32 Rollback::KeptChecksum(const Guest& g, u32 step)
{
  return g.Read32(CHECKSUMS + (step & 1023) * 4);
}

void Rollback::TakeOver(Guest& g, Rollback& worker, const Guest& gw, u32 from, u32 to)
{
  TakeOverMoment(g, *m, *worker.m, gw, from, to);
  // the checksums and the rows the worker's steps ran with (at most the rings' 1024 steps)
  for (u32 k = std::max(from, to > 1024 ? to - 1024 : 0); k < to; ++k)
  {
    u8 bytes[30 * 8];
    gw.CopyFrom(bytes, CHECKSUMS + (k & 1023) * 4, 4);
    g.CopyTo(CHECKSUMS + (k & 1023) * 4, bytes, 4);
    gw.CopyFrom(bytes, INPUTS + (k & 1023) * 30 * 8, sizeof(bytes));
    g.CopyTo(INPUTS + (k & 1023) * 30 * 8, bytes, sizeof(bytes));
  }
}

std::vector<u32> Rollback::TakeDirty()
{
  std::vector<u32> pages;
  const Tracking& s = *m;
  for (u32 page = 0; page < s.dirty.size(); ++page)
  {
    if (s.all_dirty || s.dirty[page] || s.untracked[page] || s.exact[page])
      pages.push_back(page * PAGE);
  }
  std::fill(m->dirty.begin(), m->dirty.end(), u8{0});
  m->all_dirty = false;
  return pages;
}

u32 Rollback::Region() const
{
  return m->tracking ? m->region : 0;
}

// A word (its offset in the region) in one of the kept parts a restore leaves as they are.
static bool InKept(const std::vector<std::pair<u32, u32>>& kept, u32 off)
{
  for (const auto& [at, len] : kept)
    if (off + 4 > at && off < at + len)
      return true;
  return false;
}

u32 Rollback::Compare(const Guest& g, const Guest& other, std::vector<u32>* words, std::size_t most,
                      const std::vector<u32>* ignore) const
{
  const auto ignored = [ignore](u32 address) {
    return ignore != nullptr && std::binary_search(ignore->begin(), ignore->end(), address);
  };
  const Tracking& s = *m;
  if (!s.tracking)
    return 0;
  u32 count = 0;
  const auto note = [&](u32 address) {
    if (words != nullptr && words->size() < most)
      words->push_back(address);
    ++count;
  };
  const u8* a = g.Ram(s.region, s.shadow.size());
  const u8* b = other.Ram(s.region, s.shadow.size());
  for (u32 page = 0; a != nullptr && b != nullptr && page < s.shadow.size() / PAGE; ++page)
  {
    const u32 off = page * PAGE;
    if (s.untracked[page] || std::memcmp(a + off, b + off, PAGE) == 0)
      continue;
    for (u32 w = off; w < off + PAGE; w += 4)
    {
      if (std::memcmp(a + w, b + w, 4) != 0 && !InKept(s.kept, w) && !ignored(s.region + w))
        note(s.region + w);
    }
  }
  const u8* ca = s.scratch_shadow.empty() ? nullptr : g.GetHost().Scratch();
  const u8* cb = s.scratch_shadow.empty() ? nullptr : other.GetHost().Scratch();
  for (u32 w = 0; ca != nullptr && cb != nullptr && w + 4 <= s.scratch_shadow.size(); w += 4)
  {
    if ((w < SCRATCH_KEPT || w >= SCRATCH_KEPT_END) && std::memcmp(ca + w, cb + w, 4) != 0)
      note(0xE0000000u + w);
  }
  if (g.Read32(DOL_RNG) != other.Read32(DOL_RNG))
    note(DOL_RNG);
  return count;
}

u32 Rollback::CompareMoments(const Guest& g, const Rollback& other, const Guest& other_memory, u32 from, u32 to,
                             const std::vector<u32>* ignore) const
{
  const Tracking &x = *m, &y = *other.m;
  const u8* mx = g.Ram(x.region, x.shadow.size());
  const u8* my = other_memory.Ram(x.region, x.shadow.size());
  const u8* cx = x.scratch_shadow.empty() ? nullptr : g.GetHost().Scratch();
  const u8* cy = y.scratch_shadow.empty() ? nullptr : other_memory.GetHost().Scratch();
  if (mx == nullptr || my == nullptr || x.region != y.region)
    return to;
  // every block either log names in those steps (memory and locked cache apart), with its content in each now
  std::map<u32, std::pair<std::array<u8, 32>, std::array<u8, 32>>> blocks, cache;
  for (const Tracking* t : {&x, &y})
  {
    for (u32 k = from; k < to; ++k)
    {
      const auto it = t->undo.find(k);
      if (it == t->undo.end())
        continue;
      for (const auto& block : it->second.blocks)
        blocks.try_emplace(block.first);
      for (const auto& block : it->second.scratch)
        cache.try_emplace(block.first);
    }
  }
  for (auto& [off, pair] : blocks)
  {
    std::memcpy(pair.first.data(), mx + off, 32);
    std::memcpy(pair.second.data(), my + off, 32);
  }
  for (auto& [off, pair] : cache)
  {
    if (cx != nullptr)
      std::memcpy(pair.first.data(), cx + off, 32);
    if (cy != nullptr)
      std::memcpy(pair.second.data(), cy + off, 32);
  }
  // what a restore leaves: untracked pages and the kept parts
  for (auto it = blocks.begin(); it != blocks.end();)
    it = x.untracked[it->first / PAGE] ? blocks.erase(it) : std::next(it);
  u32 rx = g.Read32(DOL_RNG), ry = other_memory.Read32(DOL_RNG);
  const auto same = [&]() {
    for (const auto& [off, pair] : blocks)
    {
      if (pair.first == pair.second)
        continue;
      for (u32 w = 0; w < 32; w += 4)
      {
        if (std::memcmp(pair.first.data() + w, pair.second.data() + w, 4) != 0 && !InKept(x.kept, off + w) &&
            !(ignore != nullptr && std::binary_search(ignore->begin(), ignore->end(), x.region + off + w)))
          return false;
      }
    }
    for (const auto& [off, pair] : cache)
    {
      if ((off < SCRATCH_KEPT || off >= SCRATCH_KEPT_END) && pair.first != pair.second)
        return false;
    }
    return rx == ry;
  };
  // a step's entry onto the copies, as a restore goes through it: from its last block to its first
  const auto back = [&](const Tracking& t, u32 k, bool first) {
    const auto it = t.undo.find(k);
    if (it == t.undo.end())
      return;
    for (auto b = it->second.blocks.rbegin(); b != it->second.blocks.rend(); ++b)
    {
      if (x.untracked[b->first / PAGE])
        continue;
      auto& pair = blocks[b->first];
      (first ? pair.first : pair.second) = b->second;
    }
    for (auto b = it->second.scratch.rbegin(); b != it->second.scratch.rend(); ++b)
    {
      auto& pair = cache[b->first];
      (first ? pair.first : pair.second) = b->second;
    }
    if (it->second.has_rng)
    {
      u32 v = 0;
      for (u8 byte : it->second.rng)
        v = v << 8 | byte;
      (first ? rx : ry) = v;
    }
  };
  if (!same())
    return to;
  for (u32 k = to; k-- > from;)
  {
    back(x, k, true);
    back(y, k, false);
    if (!same())
      return k;
  }
  return 0xFFFFFFFF;
}

Work Rollback::TakeWork()
{
  Work work;
  work.track_us = m->track_us;
  work.restore_us = m->restore_us;
  work.blocks = m->blocks_logged;
  work.watched_pages = static_cast<u32>(m->watched_list.size());
  work.late_pages = m->late_pages;
  m->track_us = m->restore_us = 0;
  m->blocks_logged = 0;
  return work;
}
}  // namespace fzgx::net
