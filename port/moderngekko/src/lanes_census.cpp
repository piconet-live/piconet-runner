// The deep-rollback probe's census (lanes_census.h): the windows a step runs in, the counting hooks a step run
// again is dressed with, the module's access trace below the undo log, and the run's files.
//
// A window is the game's code from a step's network hook (HOST_NET, after the core's NetSync) to the next of our
// host calls that is the next step's network hook or the frame tick: the step wrapper, the step's tasks and the frame
// routine's loop to its next step. A frame's last step's window also holds the frame's once-a-frame code (the
// drawing) that follows it: such a window is told apart (kind L), and only the windows of steps run again before a
// frame's last (kind R, the steps a worker would run) are the census's own. The first run of every step (kind F) is
// watched by the access trace only, for its values.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "lanes_census.h"

#include "lanes_internal.h"

#include <algorithm>
#include <array>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "Common/RunSettings.h"
#include "Core/FzgxProbe.h"

namespace fzgx_port::lanes
{
namespace
{
// The control block's words (port/image/routines/netframe.py)
constexpr u32 MORE = table::CONTROL + 8;          // the frame's steps still to run after this one
constexpr u32 RESIMULATING = table::CONTROL + 12;  // 1 for the K steps after a frame's first
constexpr u32 STEP = table::CONTROL + 124;         // the step about to run
constexpr u32 RUN_BEFORE = table::CONTROL + 488;   // the step about to run was simulated before
// The undo log tracks from the page of main.rel's .data up (rollback.h); below it, the executable's data and bss,
// the OS, main.rel's text and read-only data, and the threads' stacks. The DOL's generator word is tracked.
constexpr u32 LOW = 0x80000000u, LOG_FROM = game::REL_DATA_AT & ~0xFFFu, WORDS = (LOG_FROM - LOW) / 4;
constexpr u32 DOL_GENERATOR = 0x801A63C0u;
constexpr u32 OS_CURRENT_THREAD = 0x800000E4u;

enum Kind : u8
{
  NONE,   // outside every window: the frame's head, the gate's wait, menus
  FIRST,  // a step's first run (with the drawing after it when it is the frame's last)
  RERUN,  // a step run again, not the frame's last: the census's own
  LAST,   // a step run again that is the frame's last (its window holds the drawing too)
};

// What one window of kind RERUN (or LAST) reached, beside the generated code's own memory and registers.
struct Counts
{
  u64 hw_reads = 0, hw_writes = 0;          // the hardware's registers (0xCC..., 0xCD..., 0xC8...)
  u64 slow_reads = 0, slow_writes = 0;      // main memory by translation (the statistics' table) or anything else
  u64 window_reads = 0, window_writes = 0;  // the mod's data window
  u64 cache_reads = 0, cache_writes = 0;    // the locked cache through the hooks (not by the module's window)
  u64 control = 0;                           // eciwx, ecowx
  u64 pointers = 0;                          // raw pointers the module asked for
  u64 fallback = 0;                          // instructions the module left to the interpreter
  u64 spr_reads = 0, spr_writes = 0, cache_ops = 0;
  u64 host_calls = 0;                        // ours (the mode point, the machines' task...) inside the window
  u64 checks = 0, failed = 0;                // the CPU core's chunk checks made, chunks failing (deltas)
  u64 live_in = 0, writes_below = 0;         // below the log: words read before written, words written
  u64 differ = 0, unseen = 0;                // of the live-ins: value other than the first run's; not read there
  // what the hooks saw while a handler ran (an interrupt's, the system call's, FP unavailable's: the chassis says)
  u64 handler_hw = 0, handler_fallback = 0, handler_slow = 0, handler_other = 0;
};

struct Site
{
  u64 count = 0;
  u32 first_pc = 0, first_step = 0, value = 0;
};

struct Word
{
  u64 rerun_reads = 0, first_reads = 0, differ = 0, unseen = 0;
  u32 first_step = 0, first_value = 0, rerun_value = 0;  // at the first difference (or the first read)
};

struct Census
{
  bool asked = false, on = false;
  std::string path;
  // the window open now
  Kind kind = NONE;
  u32 step = 0, serial = 0, frame = 0, frame_of_job = 0xFFFFFFFFu, job_step = 0;
  u64 started = 0;
  u64 core0[4] = {};
  Counts now;
  FzgxProbe::StepCensus chassis;
  // the hooks the game's state had when it was dressed
  CPUState saved{};
  bool dressed = false;
  // the run's
  Counts sum;
  u64 windows[4] = {};
  std::map<std::pair<u32, bool>, Site> hardware;  // (address, write)
  std::map<std::pair<u32, bool>, Site> handler_hardware;
  std::map<std::pair<u32, bool>, Site> slow;      // (page, write)
  std::map<std::pair<u32, bool>, Site> pointers;  // (page, any)
  std::map<std::pair<u16, bool>, Site> sprs;
  std::map<u32, Site> fallbacks, host_calls;      // by address (raw instruction in `value`)
  std::map<u8, u64> cache_ops;
  std::map<u32, FzgxProbe::StepCensus::Pc> jit, raised, taken, jit_step;
  u64 unclosed = 0;  // windows that ended with a handler still marked running
  FzgxProbe::StepCensus chassis_sum;
  std::string lines;
  // the access trace below the log
  bool traced = false;
  const u8* ram = nullptr;  // the game's main memory (the trace reads the value a load is about to read)
  u64 accesses = 0;
  std::vector<u32> written_in, read_in;  // per word: the window serial of its last write / its last read
  std::vector<u8> flags;                 // per word: Flag bits over the run
  std::unordered_map<u32, Word> words;   // the words a step run again read before writing them
  // the first run's live-ins of the last 128 steps: step, then (word, value)
  struct FirstRun
  {
    u32 step = 0xFFFFFFFFu;
    std::unordered_map<u32, u32> values;
  };
  std::array<FirstRun, 128> first;
  u32 vector_c00[8] = {};
} s_c;

enum Flag : u8
{
  READ_RERUN = 1,       // read before written by a step run again
  READ_FIRST = 2,       // read before written by a first run
  WRITE_RERUN = 4,      // written by a step run again
  WRITE_FIRST = 8,      // written in a first run's window (the drawing after a frame's last step included)
  WRITE_OUTSIDE = 16,   // written outside every window (the frame's head; interrupts there)
  DIFFERS = 32,         // a step run again read another value than the step's first run
  WRITE_LAST = 64,      // written in the window of a step run again that is the frame's last
};

u32 Big32(const u8* p)
{
  return static_cast<u32>(p[0]) << 24 | static_cast<u32>(p[1]) << 16 | static_cast<u32>(p[2]) << 8 | p[3];
}

u32 Ram32(const CPUState* cpu, u32 address)
{
  return Big32(cpu->ram + (address - LOW));
}

u32 Control(u32 address)
{
  return static_cast<u32>(fzgx::Data().Read(address, 4));
}

bool Hardware(u32 ea)
{
  const u32 top = ea >> 24;
  return top == 0xCC || top == 0xCD || top == 0xC8 || top == 0x0C;
}

bool Cache(u32 ea)
{
  return ea - game::LOCKED_CACHE < WINDOW;
}

bool InHandler()
{
  return FzgxProbe::g_step_census && FzgxProbe::g_step_census->in_handler;
}

void Note(std::map<std::pair<u32, bool>, Site>& table, u32 key, bool write, const CPUState* cpu, u32 value = 0)
{
  if (s_c.kind != RERUN)
    return;  // (the tables are the census's own windows')
  Site& s = table[{key, write}];
  if (s.count++ == 0)
  {
    s.first_pc = cpu->pc;
    s.first_step = s_c.step;
    s.value = value;
  }
}

// The counting hooks: each counts by what the access reaches and passes it to the hook the game's state had.
u64 CountRead(CPUState* cpu, u32 ea, u8 size)
{
  if (InHandler())
  {
    if (Hardware(ea))
    {
      ++s_c.now.handler_hw;
      Note(s_c.handler_hardware, ea, false, cpu);
    }
    else if (fzgx::ModData::Contains(ea, size) || Cache(ea))
      ++s_c.now.handler_other;
    else
      ++s_c.now.handler_slow;
  }
  else if (fzgx::ModData::Contains(ea, size))
    ++s_c.now.window_reads;
  else if (Cache(ea))
    ++s_c.now.cache_reads;
  else if (Hardware(ea))
  {
    ++s_c.now.hw_reads;
    Note(s_c.hardware, ea, false, cpu);
  }
  else
  {
    ++s_c.now.slow_reads;
    Note(s_c.slow, ea & ~0xFFFu, false, cpu);
  }
  return s_c.saved.external_read(cpu, ea, size);
}

void CountWrite(CPUState* cpu, u32 ea, u64 value, u8 size)
{
  if (InHandler())
  {
    if (Hardware(ea))
    {
      ++s_c.now.handler_hw;
      Note(s_c.handler_hardware, ea, true, cpu, static_cast<u32>(value));
    }
    else if (fzgx::ModData::Contains(ea, size) || Cache(ea))
      ++s_c.now.handler_other;
    else
      ++s_c.now.handler_slow;
  }
  else if (fzgx::ModData::Contains(ea, size))
    ++s_c.now.window_writes;
  else if (Cache(ea))
    ++s_c.now.cache_writes;
  else if (Hardware(ea))
  {
    ++s_c.now.hw_writes;
    Note(s_c.hardware, ea, true, cpu, static_cast<u32>(value));
  }
  else
  {
    ++s_c.now.slow_writes;
    Note(s_c.slow, ea & ~0xFFFu, true, cpu);
  }
  s_c.saved.external_write(cpu, ea, value, size);
}

u32 CountRead32(CPUState* cpu, u32 ea, u8 rid)
{
  ++(InHandler() ? s_c.now.handler_hw : s_c.now.control);
  Note(s_c.hardware, ea, false, cpu);
  return s_c.saved.external_read32(cpu, ea, rid);
}

void CountWrite32(CPUState* cpu, u32 ea, u32 value, u8 rid)
{
  ++(InHandler() ? s_c.now.handler_hw : s_c.now.control);
  Note(s_c.hardware, ea, true, cpu, value);
  s_c.saved.external_write32(cpu, ea, value, rid);
}

void* CountPointer(CPUState* cpu, u32 ea, u32 size)
{
  ++s_c.now.pointers;
  Note(s_c.pointers, ea & ~0xFFFu, false, cpu);
  return s_c.saved.external_pointer(cpu, ea, size);
}

void CountFallback(CPUState* cpu, u32 raw, u32 cia)
{
  if (InHandler())
  {
    ++s_c.now.handler_fallback;
    return s_c.saved.instruction_fallback(cpu, raw, cia);
  }
  ++s_c.now.fallback;
  Site& s = s_c.fallbacks[s_c.kind == RERUN ? cia : 0];
  if (s.count++ == 0)
  {
    s.first_step = s_c.step;
    s.value = raw;
  }
  s_c.saved.instruction_fallback(cpu, raw, cia);
}

u32 CountSprRead(CPUState* cpu, u16 spr, u32 cia)
{
  ++s_c.now.spr_reads;
  Site& s = s_c.sprs[{spr, false}];
  if (s.count++ == 0)
  {
    s.first_pc = cia;
    s.first_step = s_c.step;
  }
  return s_c.saved.spr_read(cpu, spr, cia);
}

void CountSprWrite(CPUState* cpu, u16 spr, u32 value, u32 cia)
{
  ++s_c.now.spr_writes;
  Site& s = s_c.sprs[{spr, true}];
  if (s.count++ == 0)
  {
    s.first_pc = cia;
    s.first_step = s_c.step;
    s.value = value;
  }
  s_c.saved.spr_write(cpu, spr, value, cia);
}

void CountCache(CPUState* cpu, u8 operation, u32 ea, u32 cia)
{
  ++s_c.now.cache_ops;
  ++s_c.cache_ops[operation];
  s_c.saved.cache_control(cpu, operation, ea, cia);
}

void DressCensus(CPUState* cpu)
{
  // (a hook the state does not have stays absent: the generated code's way without one is not changed)
  s_c.saved = *cpu;
  const auto dress = [](auto& hook, auto count) {
    if (hook)
      hook = count;
  };
  dress(cpu->external_read, &CountRead);
  dress(cpu->external_write, &CountWrite);
  dress(cpu->external_read32, &CountRead32);
  dress(cpu->external_write32, &CountWrite32);
  dress(cpu->external_pointer, &CountPointer);
  dress(cpu->instruction_fallback, &CountFallback);
  dress(cpu->spr_read, &CountSprRead);
  dress(cpu->spr_write, &CountSprWrite);
  dress(cpu->cache_control, &CountCache);
  s_c.dressed = true;
}

void Undress(CPUState* cpu)
{
  if (!s_c.dressed)
    return;
  cpu->external_read = s_c.saved.external_read;
  cpu->external_write = s_c.saved.external_write;
  cpu->external_read32 = s_c.saved.external_read32;
  cpu->external_write32 = s_c.saved.external_write32;
  cpu->external_pointer = s_c.saved.external_pointer;
  cpu->instruction_fallback = s_c.saved.instruction_fallback;
  cpu->spr_read = s_c.saved.spr_read;
  cpu->spr_write = s_c.saved.spr_write;
  cpu->cache_control = s_c.saved.cache_control;
  s_c.dressed = false;
}

// The module's access trace (DOLRECOMP_MEM_TRACE=1), before every guest load and store: below the log only.
void TraceAccess(unsigned address, unsigned width_and_write)
{
  ++s_c.accesses;
  const u32 physical = address & 0x3FFFFFFFu;
  if ((address & 0xB0000000u) != 0x80000000u || physical >= LOG_FROM - LOW)
    return;
  const u32 width = std::max(1u, width_and_write & 0xFFu);
  const bool write = (width_and_write & 0x100u) != 0;
  for (u32 w = physical / 4; w <= (physical + width - 1) / 4 && w < WORDS; ++w)
  {
    if (write)
    {
      if (s_c.written_in[w] != s_c.serial && (s_c.kind == RERUN || s_c.kind == LAST))
        ++s_c.now.writes_below;
      s_c.written_in[w] = s_c.serial;
      s_c.flags[w] |= s_c.kind == RERUN ? WRITE_RERUN : s_c.kind == LAST ? WRITE_LAST : s_c.kind == FIRST ? WRITE_FIRST
                                                                                                   : WRITE_OUTSIDE;
      continue;
    }
    // (a step run again that is the frame's last: its window holds the drawing too, its reads are not counted)
    if (s_c.kind == NONE || s_c.kind == LAST || s_c.written_in[w] == s_c.serial || s_c.read_in[w] == s_c.serial)
      continue;
    s_c.read_in[w] = s_c.serial;
    const u32 value = s_c.ram ? Big32(s_c.ram + w * 4) : 0;
    Census::FirstRun& f = s_c.first[s_c.step % s_c.first.size()];
    if (s_c.kind == FIRST)
    {
      s_c.flags[w] |= READ_FIRST;
      f.values.emplace(w, value);
      continue;
    }
    s_c.flags[w] |= READ_RERUN;
    ++s_c.now.live_in;
    Word& word = s_c.words[w];
    if (word.rerun_reads++ == 0)
    {
      word.first_step = s_c.step;
      word.rerun_value = value;
    }
    if (f.step != s_c.step)
    {
      ++word.unseen;
      ++s_c.now.unseen;
      continue;
    }
    const auto it = f.values.find(w);
    if (it == f.values.end())
    {
      ++word.unseen;
      ++s_c.now.unseen;
    }
    else if (it->second != value)
    {
      if (word.differ++ == 0)
      {
        word.first_step = s_c.step;
        word.first_value = it->second;
        word.rerun_value = value;
      }
      ++s_c.now.differ;
      s_c.flags[w] |= DIFFERS;
    }
  }
}

void Start()
{
  s_c.asked = true;
  const char* path = RunSettings::Get("FZGX_STEP_CENSUS");
  if (!path || !*path)
    return;
  s_c.on = true;
  s_c.path = path;
  s_c.written_in.assign(WORDS, 0);
  s_c.read_in.assign(WORDS, 0);
  s_c.flags.assign(WORDS, 0);
#ifdef _WIN32
  if (!s_work.module.empty())
  {
    const HMODULE module = GetModuleHandleW(std::filesystem::path(s_work.module).filename().c_str());
    using SetTrace = void (*)(void (*)(unsigned, unsigned));
    if (const auto set = module ? reinterpret_cast<SetTrace>(GetProcAddress(module, "fz_set_trace")) : nullptr)
    {
      set(&TraceAccess);
      s_c.traced = true;
    }
  }
#endif
  s_core_host.Log(fzgx::LogLevel::Notice,
                  std::string("fzgx: step census on (") + path + "): every step run again dressed with counting hooks" +
                      (s_c.traced ? "; the module's access trace listened to" : "; no access trace in the module"));
}

void Merge(std::map<u32, FzgxProbe::StepCensus::Pc>& into, const FzgxProbe::StepCensus::Pc* from)
{
  for (int i = 0; i < FzgxProbe::StepCensus::PCS; ++i)
  {
    if (from[i].count == 0)
      continue;
    auto& p = into[from[i].pc];
    if (p.count == 0)
    {
      p.pc = from[i].pc;
      p.from = from[i].from;
    }
    p.count += from[i].count;
  }
}

void Add(Counts& a, const Counts& b)
{
  const u64* from = reinterpret_cast<const u64*>(&b);
  u64* to = reinterpret_cast<u64*>(&a);
  for (std::size_t i = 0; i < sizeof(Counts) / sizeof(u64); ++i)
    to[i] += from[i];
}

std::string Pcs(const FzgxProbe::StepCensus::Pc* table)
{
  std::string text;
  for (int i = 0; i < FzgxProbe::StepCensus::PCS; ++i)
  {
    if (table[i].count == 0)
      continue;
    char item[40];
    std::snprintf(item, sizeof(item), "%s%08X:%" PRIu64, text.empty() ? "" : " ", table[i].pc,
                  static_cast<u64>(table[i].count));
    text += item;
  }
  return text;
}

void Write();

// The window open now ends (its census's line, when it was a step run again).
void Close(CPUState* cpu)
{
  if (s_c.kind == NONE)
    return;
  Undress(cpu);
  FzgxProbe::g_step_census = nullptr;
  const Kind kind = s_c.kind;
  s_c.kind = NONE;
  ++s_c.serial;  // (accesses until the next window are outside)
  if (kind != RERUN && kind != LAST)
    return;
  u64 core[4] = {};
  if (FzgxProbe::g_core_counters)
    FzgxProbe::g_core_counters(core);
  s_c.now.checks = core[3] - s_c.core0[3];
  s_c.now.failed = core[2];
  const double us = static_cast<double>(Now() - s_c.started) / 1000.0;
  const FzgxProbe::StepCensus& h = s_c.chassis;
  const Counts& n = s_c.now;
  if (h.in_handler)
    ++s_c.unclosed;
  char line[900];
  std::snprintf(line, sizeof(line),
                "%u,%u,%u,%c,%.1f,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64
                ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64
                ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%.1f,%" PRIu64 ",%" PRIu64 ",%" PRIu64
                ",%" PRIu64 ",%08X,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%s,%s,%s,%" PRIu64 ",%" PRIu64
                ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%d,%s\n",
                s_c.step, s_c.frame, s_c.job_step, kind == RERUN ? 'R' : 'L', us, n.hw_reads, n.hw_writes, n.control,
                n.slow_reads, n.slow_writes, n.window_reads, n.window_writes, n.cache_reads, n.cache_writes,
                n.pointers, n.fallback, n.spr_reads, n.spr_writes, n.cache_ops, n.host_calls, n.checks, n.failed,
                static_cast<u64>(h.native_exceptions), static_cast<u64>(h.delivered),
                static_cast<u64>(h.jit_entries), static_cast<double>(h.jit_tsc) / 3394.0,
                static_cast<u64>(h.interpreted), static_cast<u64>(h.advances), static_cast<u64>(h.idles),
                static_cast<u64>(h.other_thread), h.first_other_pc, n.live_in, n.writes_below, n.differ, n.unseen,
                Pcs(h.jit).c_str(), Pcs(h.raised).c_str(), Pcs(h.taken).c_str(), n.handler_hw, n.handler_fallback,
                n.handler_slow, n.handler_other, static_cast<u64>(h.handlers), static_cast<u64>(h.handler_dispatches),
                static_cast<u64>(h.jit_outside), h.in_handler ? 1 : 0, Pcs(h.jit_step).c_str());
  s_c.lines += line;
  if (++s_c.windows[kind] % 1000 == 0)
    Write();  // (now and then: a game its time limit ends keeps what it had)
  if (kind == RERUN)
    Add(s_c.sum, n);
  Merge(s_c.jit, h.jit);
  Merge(s_c.raised, h.raised);
  Merge(s_c.taken, h.taken);
  Merge(s_c.jit_step, h.jit_step);
  FzgxProbe::StepCensus& t = s_c.chassis_sum;
  t.native_exceptions += h.native_exceptions;
  t.delivered += h.delivered;
  t.jit_entries += h.jit_entries;
  t.jit_tsc += h.jit_tsc;
  t.interpreted += h.interpreted;
  t.advances += h.advances;
  t.idles += h.idles;
  t.other_thread += h.other_thread;
  t.lost += h.lost;
}
}  // namespace

void CensusHostCall(CPUState* cpu, u32 address)
{
  if (!s_c.asked)
    Start();
  if (!s_c.on)
    return;
  if (address == table::HOST_TICK)
    ++s_c.frame;
  if (address == table::HOST_NET || address == table::HOST_TICK)
    return Close(cpu);
  if (s_c.kind == RERUN || s_c.kind == LAST)
  {
    ++s_c.now.host_calls;
    ++s_c.host_calls[address].count;
  }
}

void CensusAfterNet(CPUState* cpu)
{
  if (!s_c.on)
    return;
  Close(cpu);  // (a step's network hook that came without the tick or another hook between)
  s_c.ram = cpu->ram;
  s_c.step = Control(STEP);
  const bool before = Control(RUN_BEFORE) != 0;
  const bool resimulating = Control(RESIMULATING) != 0;
  const u32 more = Control(MORE);
  const bool last = resimulating ? more <= 1 : more == 0;
  s_c.kind = !before ? FIRST : last ? LAST : RERUN;
  ++s_c.serial;
  if (s_c.kind == FIRST)
  {
    Census::FirstRun& f = s_c.first[s_c.step % s_c.first.size()];
    if (f.step != s_c.step)
    {
      f.step = s_c.step;
      f.values.clear();
    }
    return;
  }
  if (s_c.frame_of_job != s_c.frame)
  {
    s_c.frame_of_job = s_c.frame;
    s_c.job_step = 0;
  }
  else
    ++s_c.job_step;
  if (s_c.vector_c00[0] == 0)
  {
    for (u32 i = 0; i < 8; ++i)
      s_c.vector_c00[i] = Ram32(cpu, 0x80000C00u + i * 4);
  }
  s_c.now = {};
  s_c.chassis = {};
  s_c.chassis.thread = Ram32(cpu, OS_CURRENT_THREAD);
  if (FzgxProbe::g_core_counters)
    FzgxProbe::g_core_counters(s_c.core0);
  s_c.started = Now();
  DressCensus(cpu);
  FzgxProbe::g_step_census = &s_c.chassis;
}

namespace
{
// The files: the steps' lines so far appended, the words' and the sums' files written anew.
void Write()
{
  static bool begun = false;
  if (std::FILE* f = std::fopen((s_c.path + ".steps.csv").c_str(), begun ? "ab" : "wb"))
  {
    if (!begun)
      std::fputs("step,frame,job_step,kind,us,hw_reads,hw_writes,ext_control,slow_reads,slow_writes,window_reads,"
               "window_writes,cache_reads,cache_writes,pointers,fallback,spr_reads,spr_writes,cache_ops,host_calls,"
               "chunk_checks,chunks_failing,exc_raised,exc_delivered,jit_entries,jit_us,interpreted,advances,idles,"
               "other_thread,first_other_pc,below_live_in,below_written,below_differ,below_unseen,jit_pcs,raised,"
               "taken,handler_hw,handler_fallback,handler_slow,handler_other,handlers,handler_dispatches,"
               "jit_outside,handler_open,jit_step_pcs\n",
               f);
    std::fwrite(s_c.lines.data(), 1, s_c.lines.size(), f);
    std::fclose(f);
    s_c.lines.clear();
    begun = true;
  }
  if (std::FILE* f = std::fopen((s_c.path + ".words.csv").c_str(), "wb"))
  {
    std::fputs("address,flags,rerun_reads,differ,unseen,first_step,first_value,rerun_value\n", f);
    std::vector<u32> order;
    for (const auto& [w, word] : s_c.words)
      order.push_back(w);
    std::sort(order.begin(), order.end());
    for (const u32 w : order)
    {
      const Word& word = s_c.words[w];
      std::fprintf(f, "%08X,%u,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%u,%08X,%08X\n", LOW + w * 4, s_c.flags[w],
                   word.rerun_reads, word.differ, word.unseen, word.first_step, word.first_value, word.rerun_value);
    }
    std::fclose(f);
  }
  std::string text;
  char line[400];
  const Counts& s = s_c.sum;
  const FzgxProbe::StepCensus& t = s_c.chassis_sum;
  std::snprintf(line, sizeof(line),
                "windows R %" PRIu64 " L %" PRIu64 "; R sums: hw %" PRIu64 "/%" PRIu64 " control %" PRIu64
                " slow %" PRIu64 "/%" PRIu64 " window %" PRIu64 "/%" PRIu64 " cache %" PRIu64 "/%" PRIu64
                " pointers %" PRIu64 " fallback %" PRIu64 " spr %" PRIu64 "/%" PRIu64 " cacheops %" PRIu64
                " hostcalls %" PRIu64 " checks %" PRIu64 "\n",
                s_c.windows[RERUN], s_c.windows[LAST], s.hw_reads, s.hw_writes, s.control, s.slow_reads,
                s.slow_writes, s.window_reads, s.window_writes, s.cache_reads, s.cache_writes, s.pointers, s.fallback,
                s.spr_reads, s.spr_writes, s.cache_ops, s.host_calls, s.checks);
  text += line;
  std::snprintf(line, sizeof(line),
                "chassis (R and L): raised %" PRIu64 " delivered %" PRIu64 " jit %" PRIu64 " (%.1f us) interpreted %" PRIu64
                " advances %" PRIu64 " idles %" PRIu64 " other-thread %" PRIu64 " lost %" PRIu64 "\n",
                static_cast<u64>(t.native_exceptions), static_cast<u64>(t.delivered),
                static_cast<u64>(t.jit_entries), static_cast<double>(t.jit_tsc) / 3394.0,
                static_cast<u64>(t.interpreted), static_cast<u64>(t.advances), static_cast<u64>(t.idles),
                static_cast<u64>(t.other_thread), static_cast<u64>(t.lost));
  text += line;
  std::snprintf(line, sizeof(line),
                "below the log (R): live-ins %" PRIu64 " words written %" PRIu64 " differing %" PRIu64 " unseen %" PRIu64
                "; distinct words read %zu; trace %s, %" PRIu64 " accesses\n",
                s.live_in, s.writes_below, s.differ, s.unseen, s_c.words.size(), s_c.traced ? "on" : "off",
                s_c.accesses);
  text += line;
  text += "vector 0xC00:";
  for (const u32 v : s_c.vector_c00)
  {
    std::snprintf(line, sizeof(line), " %08X", v);
    text += line;
  }
  text += "\n";
  const auto sites = [&](const char* what, const auto& table) {
    for (const auto& [key, site] : table)
    {
      std::snprintf(line, sizeof(line), "%s %08X %s count %" PRIu64 " first pc %08X step %u value %08X\n", what,
                    static_cast<u32>(key.first), key.second ? "W" : "R", site.count, site.first_pc, site.first_step,
                    site.value);
      text += line;
    }
  };
  sites("hardware", s_c.hardware);
  sites("handler-hardware", s_c.handler_hardware);
  sites("slow-page", s_c.slow);
  sites("pointer-page", s_c.pointers);
  sites("spr", s_c.sprs);
  for (const auto& [cia, site] : s_c.fallbacks)
  {
    std::snprintf(line, sizeof(line), "fallback %08X raw %08X count %" PRIu64 " first step %u\n", cia, site.value,
                  site.count, site.first_step);
    text += line;
  }
  for (const auto& [address, site] : s_c.host_calls)
  {
    std::snprintf(line, sizeof(line), "hostcall %08X count %" PRIu64 "\n", address, site.count);
    text += line;
  }
  for (const auto& [op, count] : s_c.cache_ops)
  {
    std::snprintf(line, sizeof(line), "cacheop %u count %" PRIu64 "\n", op, count);
    text += line;
  }
  const auto pcs = [&](const char* what, const auto& table) {
    for (const auto& [pc, p] : table)
    {
      std::snprintf(line, sizeof(line), "%s %08X from %08X count %" PRIu64 "\n", what, pc, p.from,
                    static_cast<u64>(p.count));
      text += line;
    }
  };
  pcs("jit", s_c.jit);
  pcs("raised", s_c.raised);
  pcs("taken", s_c.taken);
  pcs("jit-step", s_c.jit_step);
  std::snprintf(line, sizeof(line), "windows that ended inside a handler: %" PRIu64 "\n", s_c.unclosed);
  text += line;
  if (std::FILE* f = std::fopen((s_c.path + ".txt").c_str(), "wb"))
  {
    std::fwrite(text.data(), 1, text.size(), f);
    std::fclose(f);
  }
}
}  // namespace

void CensusStop()
{
  if (!s_c.on)
    return;
  FzgxProbe::g_step_census = nullptr;
  s_c.kind = NONE;
  Write();
  s_core_host.Log(fzgx::LogLevel::Notice, "fzgx: step census: R windows " + std::to_string(s_c.windows[RERUN]) +
                                              ", L windows " + std::to_string(s_c.windows[LAST]) + " (" + s_c.path +
                                              ".txt)");
  s_c.on = false;
}
}  // namespace fzgx_port::lanes
