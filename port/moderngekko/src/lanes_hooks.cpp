// The hooks a lane's state carries in place of the CPU core's (lanes.h): what an update reaches that is no register
// and no memory the generated code reads by itself, and the record of the lane's locked cache.
//
// The record needs no change of the emitter, and the module is the released one byte for byte: the generated code
// reaches the locked cache (its second window) directly only when the state's `exram` is set. A tracked lane's is
// empty, so every locked-cache access of the lane takes the slow path to the lane's hooks, which serve it from the
// lane's own cache and record it: per word, a read before the part's own write of that word is a live-in (its value
// kept once); a write marks the word written, whatever the value (a word rewritten with the value it held is a
// write too). A write of part of a word the part has not written yet reads the rest of the word first.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "lanes_internal.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

namespace fzgx_port::lanes
{
namespace
{
Lane& Of(CPUState* cpu)
{
  return *static_cast<Lane*>(cpu->external_user_data);
}

u32 WordAt(const u8* cache, u32 word)
{
  const u8* p = cache + word * 4;
  return static_cast<u32>(p[0]) << 24 | static_cast<u32>(p[1]) << 16 | static_cast<u32>(p[2]) << 8 | p[3];
}

// The offset of an access inside the locked cache; false: not in it. Inside it but past the part the game uses: the
// lanes' result cannot be kept (served from the lane's cache all the same, which holds only that part: zero).
bool InScratch(Lane& lane, u32 ea, u32 size, u32* offset)
{
  *offset = ea - SCRATCH_AT;
  if (*offset >= 0x40000u || size > 0x40000u - *offset)
    return false;
  if (*offset + size > SCRATCH)
    lane.track.outside = true;
  return true;
}

// The bytes of the access that lie in `word`, as a nibble.
u32 Covered(u32 offset, u32 size, u32 word)
{
  const u32 from = std::max(offset, word * 4) - word * 4, to = std::min(offset + size, word * 4 + 4) - word * 4;
  return ((1u << (to - from)) - 1u) << from;
}

void Reading(Lane& lane, u32 offset, u32 size)
{
  Track& t = lane.track;
  for (u32 word = offset / 4; word <= (offset + size - 1) / 4 && word < WORDS; ++word)
  {
    const u32 fresh = Covered(offset, size, word) & ~Nibble(t.written, word) & ~Nibble(t.read, word) & 0xFu;
    if (fresh == 0)
      continue;
    SetNibble(t.read, word, fresh);
    t.reads.push_back({static_cast<u16>(word), static_cast<u8>(fresh), WordAt(lane.cache.get(), word)});
  }
}

void Writing(Lane& lane, u32 offset, u32 size)
{
  Track& t = lane.track;
  for (u32 word = offset / 4; word <= (offset + size - 1) / 4 && word < WORDS; ++word)
  {
    if (Nibble(t.written, word) == 0)
      t.touched.push_back(static_cast<u16>(word));
    SetNibble(t.written, word, Covered(offset, size, word));
  }
}

u64 FromCache(const Lane& lane, u32 offset, u8 size)
{
  u64 value = 0;
  for (u8 i = 0; i < size; ++i)
    value = value << 8 | (offset + i < SCRATCH ? lane.cache[offset + i] : 0);
  return value;
}

// The module's call (DOLRECOMP_MEM2_TRACK) before an access to its second window, on a tracked lane.
void RecordAccess(Track* track, u8*, u32 offset, u32 size_and_write)
{
  Lane& lane = *track->lane;
  const u32 size = size_and_write & 0xFFu;
  ++track->accesses;
  if (size == 0 || offset + size > SCRATCH)
  {
    track->outside = true;
    return;
  }
  if ((size_and_write & 0x100u) != 0)
    Writing(lane, offset, size);
  else
    Reading(lane, offset, size);
}

// A read in neither memory the generated code reaches itself. The data window is the core's buffer; the locked
// cache is the lane's (recorded). Anything else is a slow read: the game's thread asks the CPU core, as the game
// does without the lane; a lane on a thread of its own has no answer and gives up.
u64 HookRead(CPUState* cpu, u32 ea, u8 size)
{
  Lane& lane = Of(cpu);
  if (fzgx::ModData::Contains(ea, size))
  {
    ++lane.window_reads;
    return fzgx::Data().Read(ea, size);
  }
  if (u32 offset = 0; !lane.plain && InScratch(lane, ea, size, &offset))
  {
    Reading(lane, offset, size);
    return FromCache(lane, offset, size);
  }
  ++lane.slow_reads;
  if (lane.on_thread)
  {
    GiveUp(lane, "a slow read on a lane's own thread");
    return 0;
  }
  return s_work.game->external_read(s_work.game, ea, size);
}

void HookWrite(CPUState* cpu, u32 ea, u64 value, u8 size)
{
  Lane& lane = Of(cpu);
  if (fzgx::ModData::Contains(ea, size))
  {
    ++lane.window_writes;
    fzgx::Data().Write(ea, value, size);
    return;
  }
  if (u32 offset = 0; !lane.plain && InScratch(lane, ea, size, &offset))
  {
    Writing(lane, offset, size);
    for (u8 i = size; i > 0; --i, value >>= 8)
    {
      if (offset + i - 1 < SCRATCH)
        lane.cache[offset + i - 1] = static_cast<u8>(value);
    }
    return;
  }
  if (lane.on_thread)
    return GiveUp(lane, "an update on a lane's own thread wrote to hardware");
  lane.track.outside |= !lane.plain;
  s_work.game->external_write(s_work.game, ea, value, size);
}

// A raw pointer into the locked cache passes the record by: counted, and the lanes' result is not kept.
void* HookPointer(CPUState* cpu, u32 ea, u32 size)
{
  Lane& lane = Of(cpu);
  u32 offset = 0;
  if (lane.plain)
    return s_work.game->external_pointer ? s_work.game->external_pointer(s_work.game, ea, size) : nullptr;
  if (size == 0 || !InScratch(lane, ea, size, &offset) || offset + size > SCRATCH)
    return nullptr;
  ++lane.pointers;
  lane.track.outside = true;
  return lane.cache.get() + offset;
}

// The rest reach the emulator's own state. On the game's thread they are the CPU core's, as without lanes (counted
// by kind); on a lane's own thread there is no answer.
bool Other(CPUState* cpu, const char* what)
{
  if (Of(cpu).on_thread)
  {
    GiveUp(Of(cpu), "an update on a lane's own thread used what only the game's thread has");
    return false;
  }
  Note(std::string("on the game's thread: ") + what);
  return true;
}

u32 HookRead32(CPUState* cpu, u32 ea, u8 rid)
{
  if (!Other(cpu, "an external-control read"))
    return 0;
  return s_work.game->external_read32(s_work.game, ea, rid);
}

void HookWrite32(CPUState* cpu, u32 ea, u32 value, u8 rid)
{
  if (Other(cpu, "an external-control write"))
    s_work.game->external_write32(s_work.game, ea, value, rid);
}

u32 HookSprRead(CPUState* cpu, u16 spr, u32 cia)
{
  if (!Other(cpu, "a special register's read"))
    return 0;
  return s_work.game->spr_read(s_work.game, spr, cia);
}

// The locked cache's block copy, as the emulator does it (MMU::DMA_LCToMemory, DMA_MemoryToLC), between the lane's
// own locked cache and main memory: 32 bytes a block, started by the write of DMAL with its bit 1 set. Into the
// cache: the words are written (recorded). Out of the cache: the words are read (recorded), and main memory is
// written, which the lanes do not keep apart.
void BlockCopy(Lane& lane, CPUState* cpu)
{
  u32 blocks = (lane.dmau & 0x1Fu) << 2 | (lane.dmal >> 2 & 3u);
  blocks = blocks == 0 ? 128 : blocks;
  const u32 size = blocks * 32, memory = (lane.dmau & ~0x1Fu) & 0x3FFFFFFFu, cache = (lane.dmal & ~0x1Fu) & 0x3FFFFu;
  if (memory >= cpu->ram_size || cpu->ram_size - memory < size || cache + size > SCRATCH)
    return GiveUp(lane, "a block copy of the locked cache that is not between its first 16 KiB and main memory");
  ++lane.block_copies;
  if ((lane.dmal & 0x10u) != 0)
  {
    Writing(lane, cache, size);
    std::memcpy(lane.cache.get() + cache, cpu->ram + memory, size);
    return;
  }
  Reading(lane, cache, size);
  ++lane.copies_out;
  lane.last_copy_out = memory;
  lane.copies.push_back({lane.job_now, memory, size, static_cast<u32>(lane.copied_over.size())});
  lane.copied_over.insert(lane.copied_over.end(), cpu->ram + memory, cpu->ram + memory + size);
  std::memcpy(cpu->ram + memory, lane.cache.get() + cache, size);
}

// A tracked lane does the block copy itself, on its own cache, and keeps the two registers (its record holds what
// it left: Close); it has no other register. A plain lane passes every write to the CPU core, as the game does.
void HookSprWrite(CPUState* cpu, u16 spr, u32 value, u32 cia)
{
  Lane& lane = Of(cpu);
  if (lane.plain)
    return s_work.game->spr_write(s_work.game, spr, value, cia);
  if (spr != DMAU && spr != DMAL)
  {
    char text[64];
    std::snprintf(text, sizeof(text), "a write of special register %u", spr);
    lane.track.outside = true;
    if (Other(cpu, text))
      s_work.game->spr_write(s_work.game, spr, value, cia);
    return;
  }
  (spr == DMAU ? lane.dmau : lane.dmal) = value;
  lane.dma_written = true;
  if (spr == DMAL && (value & 2u) != 0)
    BlockCopy(lane, cpu);
  lane.dmal &= ~2u;
}

// A cache instruction changes nothing the game can read here: the emulator forgets what it compiled of that line.
// A lane on its own thread keeps it for the game's thread, which passes it on after the batch.
void HookCache(CPUState* cpu, u8 operation, u32 ea, u32 cia)
{
  Lane& lane = Of(cpu);
  ++lane.cache_ops;
  if (!lane.on_thread)
    return s_work.game->cache_control(s_work.game, operation, ea, cia);
  if (lane.cache_count == lane.cache_kept.size())
    return GiveUp(lane, "more cache instructions in a batch than a lane keeps");
  lane.cache_kept[lane.cache_count++] = {operation, ea, cia};
}

// The CPU core's own works on the game's registers, which a lane's are not; the per-machine rollback's lane, alone on
// the game's thread, hands the instruction to it (HandOff).
void HookFallback(CPUState* cpu, u32 raw, u32 cia)
{
  if (Of(cpu).hand_off)
    return HandOff(Of(cpu), raw, cia);
  char text[96];
  std::snprintf(text, sizeof(text), "an instruction left to the interpreter (%08X at %08X)", raw, cia);
  if (Of(cpu).plain)
    Fatal(text);
  GiveUp(Of(cpu), "an instruction left to the interpreter");
}

// The module's dispatcher asks before every piece of code; a lane intercepts nothing.
bool HookHostCall(CPUState*, u32)
{
  return false;
}
}  // namespace

void Dress(Lane& lane, const CPUState& from, bool plain)
{
  CPUState& c = lane.cpu;
  c = from;
  lane.plain = plain;
  c.external_read = &HookRead;
  c.external_write = &HookWrite;
  c.external_read32 = &HookRead32;
  c.external_write32 = &HookWrite32;
  c.external_pointer = &HookPointer;
  c.instruction_fallback = &HookFallback;
  c.host_call = &HookHostCall;
  c.spr_read = &HookSprRead;
  c.spr_write = &HookSprWrite;
  c.cache_control = &HookCache;
  c.external_user_data = &lane;
  c.cycle_budget = s64{1} << 60;  // no piece of code leaves for the emulator's clock: the batch is one unit of time
  c.exception = 0;
  lane.gave_up = nullptr;
  if (!plain)
  {
    if (!lane.cache)
    {
      lane.cache = std::make_unique<u8[]>(WINDOW);
      std::memset(lane.cache.get(), 0, WINDOW);
    }
    // the module's own record: the window is the lane's cache, its control word names the lane's record; else every
    // locked-cache access goes to the hooks (an empty window), which record it
    lane.track.record = &RecordAccess;
    lane.track.lane = &lane;
    Track* const control = s_work.inline_record ? &lane.track : nullptr;
    std::memcpy(lane.cache.get() + WINDOW - 8, &control, sizeof(control));
    c.exram = s_work.inline_record ? lane.cache.get() : nullptr;
    c.exram_size = WINDOW;
    lane.track.outside = false;
    lane.copies.clear();
    lane.copied_over.clear();
  }
  lane.dma_written = false;
}

void Close(Lane& lane, Record* record)
{
  Track& t = lane.track;
  record->reads.swap(t.reads);
  t.reads.clear();
  record->outputs.clear();
  for (const u16 w : t.touched)
    record->outputs.push_back({w, static_cast<u8>(Nibble(t.written, w)), WordAt(lane.cache.get(), w)});
  for (const u16 w : t.touched)
    t.written[w >> 1] = 0;
  t.touched.clear();
  for (const Word& r : record->reads)
    t.read[r.word >> 1] = 0;
  record->dma = std::exchange(lane.dma_written, false);
  record->dmau = lane.dmau;
  record->dmal = lane.dmal;
}
}  // namespace fzgx_port::lanes
