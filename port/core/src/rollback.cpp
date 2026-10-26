// Rollback as its user sees it (rollback.h): the facade over the undo log (undo_log.cpp) and the state checksum
// (checksum.cpp).
// SPDX-License-Identifier: GPL-3.0-or-later

#include "rollback.h"

#include <algorithm>
#include <cstddef>
#include <memory>
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

// Rollback::Moments: every change of the memory to another moment than RestoreTo's
u32 s_moments = 0;
}  // namespace

u32 Rollback::Moments()
{
  return s_moments;
}

struct Rollback::Impl : Tracking
{
};

Rollback::Rollback() : m(std::make_unique<Impl>())
{
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
  ++s_moments;
  StartTracking(g, *m);
}

void Rollback::Stop()
{
  ++s_moments;
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
  ++s_moments;
  ExchangeMoment(g, *m, *other.m, *moment, copy);
}

void Rollback::Apply(Guest& g, u32 from, u32 t) const
{
  ++s_moments;
  ApplyEntries(g, *m, from, t);
}

void Rollback::Adopt(Guest& g, const Rollback& other, u32 step)
{
  ++s_moments;
  AdoptMemory(g, *m, *other.m, step);
}

void Rollback::KeepWatching()
{
  m->keep_watching = true;
}

void Rollback::Discard(Guest& g)
{
  ++s_moments;
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
  ++s_moments;
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
