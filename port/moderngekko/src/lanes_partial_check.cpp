// Per-machine rollback (lanes_partial.h), "check": a step run again is run whole, then from the same memory with the
// chosen machines run and the others put back, and the two are compared word for word; the whole run's result is
// kept. The log's lines: a batch, the words that differ, a job's outputs, the watched words, the shared tables.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "lanes_partial.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "Core/PowerPC/PowerPC.h"

namespace fzgx_port::lanes::partial
{
namespace
{
// The tracked memory before the batch, the pages the whole run changed and their content after it, the rest of what
// the two runs are compared by.
struct Whole
{
  std::vector<u8> before, after;
  std::vector<u32> changed;
  std::vector<u8> window_before, window_after;
  u8 cache_before[SCRATCH] = {}, cache_after[SCRATCH] = {};
  u32 dma_before[2] = {}, dma_after[2] = {}, exceptions_before = 0, exceptions_after = 0;
  std::array<Regs, MOST_JOBS> regs;
  std::array<s64, MOST_JOBS> cycles{};
};
Whole s_whole;

u32 RamSize()
{
  return s_work.game->ram_size;
}

u32 Read32Be(const u8* p)
{
  return static_cast<u32>(p[0]) << 24 | static_cast<u32>(p[1]) << 16 | static_cast<u32>(p[2]) << 8 | p[3];
}

double Us(u64 nanoseconds)
{
  return static_cast<double>(nanoseconds) / 1000.0;
}

void Differ(Diffs* d, const std::string& text)
{
  if (d->first.size() < 12)
    d->first.push_back(text);
}

// Before the batch: the whole of main memory, the locked cache, the block copy's registers, the window.
void KeepBefore(CPUState* cpu)
{
  Whole& b = s_whole;
  b.before.assign(cpu->ram, cpu->ram + RamSize());
  std::memcpy(b.cache_before, cpu->exram, SCRATCH);
  b.dma_before[0] = Ppc().spr[DMAU];
  b.dma_before[1] = Ppc().spr[DMAL];
  b.exceptions_before = Ppc().Exceptions;
  b.window_before = fzgx::Data().Snapshot();
}

// After the whole run: the pages it changed and their content, its registers; everything back to before the batch.
void KeepWhole(CPUState* cpu)
{
  Whole& b = s_whole;
  b.changed.clear();
  b.after.clear();
  for (u32 off = 0; off < RamSize(); off += PAGE)
  {
    u8* live = cpu->ram + off;
    if (std::memcmp(live, b.before.data() + off, PAGE) == 0)
      continue;
    b.changed.push_back(off);
    b.after.insert(b.after.end(), live, live + PAGE);
    std::memcpy(live, b.before.data() + off, PAGE);
  }
  std::memcpy(b.cache_after, cpu->exram, SCRATCH);
  std::memcpy(cpu->exram, b.cache_before, SCRATCH);
  b.dma_after[0] = Ppc().spr[DMAU];
  b.dma_after[1] = Ppc().spr[DMAL];
  Ppc().spr[DMAU] = b.dma_before[0];
  Ppc().spr[DMAL] = b.dma_before[1];
  b.exceptions_after = Ppc().Exceptions;
  Ppc().Exceptions = b.exceptions_before;
  b.window_after = fzgx::Data().Snapshot();
  fzgx::Data().Restore(b.window_before);
  for (u32 k = 0; k < s_work.jobs; ++k)
  {
    b.regs[k] = s_work.job[k].regs;
    b.cycles[k] = s_work.job[k].cycles;
  }
}

// The run with the chosen machines against the whole run, word for word; the whole run's result is kept. The
// game thread's stack below the task runner's stack pointer is dead (the harness's deadstack.py): counted apart.
void CompareMemory(CPUState* cpu, Diffs* d)
{
  Whole& b = s_whole;
  const u32 thread = Read32(table::RAM + 0xE4);
  const u32 low = thread - table::RAM < RamSize() - 0x310 ? Read32(thread + 0x308) : 0, high = s_b.entry.gpr[1];
  std::size_t c = 0;
  for (u32 off = 0; off < RamSize(); off += PAGE)
  {
    const bool changed = c < b.changed.size() && b.changed[c] == off;
    const u8* expect = changed ? b.after.data() + (c++) * PAGE : b.before.data() + off;
    u8* live = cpu->ram + off;
    if (std::memcmp(live, expect, PAGE) == 0)
      continue;
    for (u32 w = 0; w < PAGE; w += 4)
    {
      if (std::memcmp(live + w, expect + w, 4) == 0)
        continue;
      const u32 address = table::RAM + off + w;
      if (address >= low && address < high)
      {
        ++d->dead;
        continue;
      }
      ++d->words;
      char text[160];
      std::snprintf(text, sizeof(text), "%s whole %02X%02X%02X%02X this %02X%02X%02X%02X", Where(s_b.layout, address).c_str(),
                    expect[w], expect[w + 1], expect[w + 2], expect[w + 3], live[w], live[w + 1], live[w + 2], live[w + 3]);
      Differ(d, text);
    }
    std::memcpy(live, expect, PAGE);
  }
}

void CompareTheRest(CPUState* cpu, Diffs* d)
{
  Whole& b = s_whole;
  for (u32 w = 0; w < SCRATCH; w += 4)
  {
    if (std::memcmp(cpu->exram + w, b.cache_after + w, 4) == 0)
      continue;
    ++d->cache;
    char text[96];
    std::snprintf(text, sizeof(text), "cache+0x%04X whole %08X this %08X", w, Read32Be(b.cache_after + w), Read32Be(cpu->exram + w));
    Differ(d, text);
  }
  std::memcpy(cpu->exram, b.cache_after, SCRATCH);
  const std::vector<u8> window = fzgx::Data().Snapshot();
  for (std::size_t i = 0; i < window.size() && i < b.window_after.size(); ++i)
    d->window += window[i] != b.window_after[i] ? 1 : 0;
  fzgx::Data().Restore(b.window_after);
  d->other += Ppc().spr[DMAU] != b.dma_after[0] || Ppc().spr[DMAL] != b.dma_after[1] ? 1 : 0;
  d->other += Ppc().Exceptions != b.exceptions_after ? 1 : 0;
  Ppc().spr[DMAU] = b.dma_after[0];
  Ppc().spr[DMAL] = b.dma_after[1];
  Ppc().Exceptions = b.exceptions_after;
  for (u32 k = 0; k < s_work.jobs; ++k)
  {
    const Regs& x = s_work.job[k].regs;
    const Regs& y = b.regs[k];
    bool same = x.gpr[0] == y.gpr[0] && std::memcmp(&x.gpr[3], &y.gpr[3], 10 * 4) == 0 &&
                std::memcmp(x.fpr, y.fpr, 14 * 8) == 0 && std::memcmp(x.ps1, y.ps1, 14 * 8) == 0 && x.cr == y.cr &&
                x.xer == y.xer && x.ctr == y.ctr && x.lr == y.lr && x.fpscr == y.fpscr && s_work.job[k].cycles == b.cycles[k];
    if (!same)
    {
      ++d->regs;
      char text[200];
      std::snprintf(text, sizeof(text), "regs job %u m%u: gpr %d fpr %d cr %08X/%08X xer %d ctr %d lr %d fpscr %08X/%08X cycles %lld/%lld",
                    k, s_b.index[k], x.gpr[0] == y.gpr[0] && std::memcmp(&x.gpr[3], &y.gpr[3], 40) == 0 ? 0 : 1,
                    std::memcmp(x.fpr, y.fpr, 112) == 0 && std::memcmp(x.ps1, y.ps1, 112) == 0 ? 0 : 1, x.cr, y.cr,
                    x.xer != y.xer, x.ctr != y.ctr, x.lr != y.lr, x.fpscr, y.fpscr,
                    static_cast<long long>(s_work.job[k].cycles), static_cast<long long>(b.cycles[k]));
      Differ(d, text);
    }
    s_work.job[k].regs = y;
    s_work.job[k].cycles = b.cycles[k];
  }
}
}  // namespace

// A job run because a locked-cache word it read last time holds another value: whether it then wrote what it wrote
// last time (its cache outputs, its records, its registers and cycles), or where not.
std::string SameOutputs(CPUState* cpu, u32 k, const Kept& old)
{
  Batch& b = s_b;
  const Record& x = s_work.job[k].unit;
  const Record& y = old.rec[k];
  std::string text = " | outputs";
  u32 differ = 0;
  for (const Word& o : x.outputs)
  {
    const auto it = std::find_if(y.outputs.begin(), y.outputs.end(), [&](const Word& w) { return w.word == o.word; });
    if (it == y.outputs.end() || it->mask != o.mask || ((it->value ^ o.value) & ByteMask(o.mask)) != 0)
    {
      if (differ++ < 6)
      {
        char part[48];
        std::snprintf(part, sizeof(part), " +0x%X %08X/%08X", o.word * 4, it == y.outputs.end() ? 0 : it->value, o.value);
        text += part;
      }
    }
  }
  text += differ == 0 && x.outputs.size() == y.outputs.size() ? " same" : " differ " + std::to_string(differ);
  std::vector<u8> end;
  Gather(cpu, b.layout[b.index[k]], &end);
  text += end == old.end[b.index[k]] ? ", records same" : ", records differ";
  text += s_work.job[k].cycles == old.cycles[k] ? ", cycles same" : ", cycles differ";
  return text;
}

void BatchLine(u32 step, bool again, u32 chosen, u64 whole, u64 part, u64 keep, u32 failed, const Diffs* d)
{
  char text[240];
  u32 mask = 0;
  u32 ran = 0;
  for (u32 i = 0; i < CARS; ++i)
  {
    mask |= s_b.run[i] ? 1u << i : 0;
    ran += s_b.run[i] ? 1 : 0;
  }
  std::snprintf(text, sizeof(text), "B,%u,%u,%u,%u,%u,%08X,%.1f,%.1f,%.1f,%u,%u,%u,%u,%u,%u,%u,%u", step, again ? 1 : 0, s_work.jobs,
                chosen, ran, mask, Us(whole), Us(part), Us(keep), failed, d ? d->words : 0, d ? d->dead : 0, d ? d->cache : 0,
                d ? d->window : 0, d ? d->regs : 0, d ? d->other : 0, s_b.runs[step % KEEP]);
  Line(text);
  for (std::size_t i = 0; d && i < d->first.size(); ++i)
    Line("D," + std::to_string(step) + "," + d->first[i]);
}

void CheckBatch(CPUState* cpu, u32 step, const Kept* old)
{
  Batch& b = s_b;
  KeepBefore(cpu);
  const u32 chosen = Choose(cpu, step, old);
  u64 began = Now();
  BeginRun(cpu, true);
  for (u32 k = 0; k < s_work.jobs; ++k)
    RunJob(k, true);
  EndRun(cpu, true);
  const u64 whole = Now() - began;
  KeepWhole(cpu);
  began = Now();
  const u32 failed = RunChosen(cpu, step, old);
  const u64 part = Now() - began;
  Diffs d;
  CompareMemory(cpu, &d);
  CompareTheRest(cpu, &d);
  ++b.checked;
  b.differing += d.words + d.cache + d.window + d.regs + d.other != 0 ? 1 : 0;
  b.live_in_failed += failed;
  began = Now();
  Remember(cpu, step);
  BatchLine(step, old != nullptr, chosen, whole, part, Now() - began, failed, &d);
}

void Watch(u32 k, const char* how)
{
  static std::vector<std::pair<u32, u32>> words;  // address, last value seen
  static bool read = false;
  if (!std::exchange(read, true))
  {
    const std::string& list = s_state.options.debug.partial_watch;
    for (std::size_t at = 0; at < list.size();)
    {
      const std::size_t comma = std::min(list.find(',', at), list.size());
      words.push_back({static_cast<u32>(std::strtoul(list.substr(at, comma - at).c_str(), nullptr, 16)), 0});
      at = comma + 1;
    }
  }
  for (auto& [address, last] : words)
  {
    const u32 now = Read32(address);
    if (now != last)
      Line("W," + std::to_string(Step()) + "," + how + ",job " + std::to_string(k) + " m" + std::to_string(s_b.index[k]) +
           "," + std::to_string(address) + "," + std::to_string(last) + "->" + std::to_string(now));
    last = now;
  }
}

// Where two copies of a machine's records first differ: "R,<part>+0x<offset>".
std::string FirstDifference(const Machine& m, const std::vector<u8>& x, const std::vector<u8>& y)
{
  std::string why = "R";
  u32 at = 0;
  for (const Span& s : m.spans)
  {
    u32 bytes = 0, first = 0, last = 0;
    for (u32 i = 0; i < s.size && at + i < x.size() && at + i < y.size(); ++i)
    {
      if (x[at + i] == y[at + i])
        continue;
      first = bytes++ == 0 ? i : first;
      last = i;
    }
    if (bytes != 0)
    {
      char text[64];
      std::snprintf(text, sizeof(text), ",%s:%u:0x%X-0x%X", KindName(s.kind), bytes, s.offset + first, s.offset + last);
      why += text;
    }
    at += s.size;
  }
  return why == "R" ? "R,?" : why;
}

// The car manager (the ranking's tables) and the drivers' globals (the rubber band) against the step's last run:
// a line of the words that differ.
void Tables(CPUState* cpu, u32 step, const Kept* old)
{
  Batch& b = s_b;
  b.tables.assign(cpu->ram + (CAR_MANAGER - table::RAM), cpu->ram + (CAR_MANAGER - table::RAM) + CAR_MANAGER_SIZE);
  b.tables.insert(b.tables.end(), cpu->ram + (CPU_GLOBALS - table::RAM), cpu->ram + (CPU_GLOBALS - table::RAM) + CPU_GLOBALS_SIZE);
  if (old == nullptr || old->tables.size() != b.tables.size())
    return;
  std::string text;
  u32 words = 0;
  for (u32 w = 0; w + 4 <= b.tables.size(); w += 4)
  {
    if (std::memcmp(b.tables.data() + w, old->tables.data() + w, 4) == 0)
      continue;
    if (++words <= 40)
    {
      char part[32];
      std::snprintf(part, sizeof(part), ",%s+0x%X", w < CAR_MANAGER_SIZE ? "carmgr" : "cpuglobals",
                    w < CAR_MANAGER_SIZE ? w : w - CAR_MANAGER_SIZE);
      text += part;
    }
  }
  if (words != 0)
    Line("G," + std::to_string(step) + "," + std::to_string(words) + text);
}
}  // namespace fzgx_port::lanes::partial

