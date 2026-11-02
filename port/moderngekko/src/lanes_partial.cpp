// Per-machine rollback (lanes_partial.h): the machines' task run on the game's thread by its own lane, every machine
// whose input or records differ from the step's last run run again, the others put back.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "lanes_partial.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>

#include "Core/PowerPC/PowerPC.h"
#include "Core/System.h"
#include "fzgx/core.h"

namespace fzgx_port::lanes::partial
{
namespace
{
const fzgx::Debug& D()
{
  return s_state.options.debug;
}

u32 WordAt(const u8* cache, u32 word)
{
  const u8* p = cache + word * 4;
  return static_cast<u32>(p[0]) << 24 | static_cast<u32>(p[1]) << 16 | static_cast<u32>(p[2]) << 8 | p[3];
}

bool InRam(const CPUState* cpu, u32 address, u32 size)
{
  return address >= table::RAM && address - table::RAM <= cpu->ram_size - size;
}

constexpr const char* KIND_NAMES[KINDS] = {"block", "progress", "track0", "track1", "track2",
                                          "track3", "segment", "driver", "car", "countdown", "mode"};
}  // namespace

Batch s_b;

bool Mode(const char* mode)
{
  return D().partial_mode == mode;
}

bool Logging()
{
  return !D().partial_log.empty();
}

void Line(const std::string& text)
{
  fzgx::PartialLine(text);
}

u32 Step()
{
  return static_cast<u32>(fzgx::Data().Read(table::STEP_AT, 4));
}

PowerPC::PowerPCState& Ppc()
{
  return Core::System::GetInstance().GetPPCState();
}

const char* KindName(u32 kind)
{
  return kind < KINDS ? KIND_NAMES[kind] : "?";
}

void LayOut(const CPUState* cpu, u32 count, Layout* layout)
{
  const u32 blocks = Read32(game::MACHINES_AT), cars = Read32(game::CARS_AT);
  for (u32 i = 0; i < CARS; ++i)
  {
    Machine& m = (*layout)[i];
    m.spans.clear();
    m.bytes = 0;
    const u32 block = blocks + i * game::MACHINE_SIZE;
    if (i >= count || !InRam(cpu, block, game::MACHINE_SIZE))
      continue;
    const auto add = [&](u32 address, u32 size, u8 kind, u32 offset = 0) {
      if (address != 0 && InRam(cpu, address, size))
      {
        m.spans.push_back({address, size, kind, offset});
        m.bytes += size;
      }
    };
    add(block, game::MACHINE_SIZE, BLOCK);
    add(Read32(block + game::MACHINE_PROGRESS), game::PROGRESS_SIZE, PROGRESS);
    for (u32 k = 0; k < 4; ++k)
      add(Read32(block + game::MACHINE_WALL_QUERY + 4 * k), 0xC0, static_cast<u8>(TRACK0 + k));
    add(Read32(block + game::MACHINE_PATH), 0x194, SEGMENT);
    add(Read32(block + game::MACHINE_CPU), 0x88, DRIVER);
    const u32 car = cars + i * game::CAR_SIZE, after = CAR_PRESENTATION + CAR_PRESENTATION_SIZE;
    add(car, CAR_PRESENTATION, CAR);
    add(car + after, game::CAR_SIZE - after, CAR, after);
    add(CAR_MANAGER + 0xD4 + 4 * i, 4, COUNTDOWN);
    add(fzgx::ModeRecord(i), 16, MODE);  // (the image's mode_speed reads it in the machine's step)
    add(fzgx::ModeMove(i), 16, MODE);    // (mode_speed's push and velocity)
    add(fzgx::ModeRollSave(i), 8, MODE); // (the rule hook wrapper's saved steering in a rolling start)
    add(fzgx::ModeStat(i), 0x60, MODE);  // (the rule hook wrapper's changed stats, mode.h STAT)
  }
}

void Gather(const CPUState* cpu, const Machine& m, std::vector<u8>* to)
{
  to->resize(m.bytes);
  u32 at = 0;
  for (const Span& s : m.spans)
  {
    std::memcpy(to->data() + at, cpu->ram + (s.address - table::RAM), s.size);
    at += s.size;
  }
}

void Scatter(CPUState* cpu, const Machine& m, const std::vector<u8>& from)
{
  u32 at = 0;
  for (const Span& s : m.spans)
  {
    if (at + s.size <= from.size())
      std::memcpy(cpu->ram + (s.address - table::RAM), from.data() + at, s.size);
    at += s.size;
  }
}

bool Ready(CPUState* cpu)
{
  Batch& b = s_b;
  if (std::exchange(b.tried, true))
    return b.ready;
  Work& w = s_work;
  if (!FindModule(w))
  {
    s_core_host.Log(fzgx::LogLevel::Warning, "fzgx: partial rollback: off, the native module's dispatch was not found");
    return false;
  }
  u64 control = 0;
  std::memcpy(&control, cpu->exram + WINDOW - 8, sizeof(control));
  w.inline_record = cpu->exram_size == WINDOW && control == 0;
  w.game = cpu;
  Verify(TASK);
  w.set_valid(TASK, TASK + 4, 3);
  b.lane.hand_off = true;
  const std::string text = std::string("on") + (D().partial_mode.empty() ? "" : ": " + D().partial_mode) +
                           (w.inline_record ? ", the module records the locked cache" : ", the hooks record the locked cache");
  s_core_host.Log(fzgx::LogLevel::Notice, "fzgx: partial rollback: " + text);
  Line("X," + text);
  b.ready = true;
  return true;
}

// Each job's machine, by its block's place among the machines' blocks. False: a job that is not a machine's.
bool CollectIndexes()
{
  const u32 blocks = Read32(game::MACHINES_AT);
  for (u32 k = 0; k < s_work.jobs; ++k)
  {
    const u32 machine = s_work.job[k].machine;
    const u32 i = (machine - blocks) / game::MACHINE_SIZE;
    if (machine < blocks || i >= CARS || (machine - blocks) % game::MACHINE_SIZE != 0)
      return false;
    s_b.index[k] = i;
  }
  return true;
}

// A run of the batch's jobs on the path's lane: plain (the game's own locked cache) or recorded (the lane's).
void BeginRun(CPUState* cpu, bool plain)
{
  Lane& lane = s_b.lane;
  Dress(lane, *cpu, plain);
  lane.on_thread = false;
  lane.alone = true;
  if (!plain)
  {
    std::memcpy(lane.cache.get(), cpu->exram, SCRATCH);
    lane.dmau = Ppc().spr[DMAU];
    lane.dmal = Ppc().spr[DMAL];
    // the record starts empty
    Record discard;
    Close(lane, &discard);
  }
}

void RunJob(u32 k, bool plain)
{
  Batch& b = s_b;
  Lane& lane = b.lane;
  CPUState& c = lane.cpu;
  Job& job = s_work.job[k];
  Regs entry = b.entry;
  entry.gpr[3] = job.machine;
  entry.gpr[24] = job.node;
  entry.fpscr = c.fpscr;  // what the jobs before it left of the floating-point status
  b.fpscr_in[k] = c.fpscr;
  Load(entry, &c);
  c.downcount = 0;
  Write32(s_work.manager + MANAGER_NODE, job.node);
  lane.job_now = k;
  const std::size_t copies = lane.copies.size();
  const u64 handed = b.handed;
  for (u32 turns = 0; c.pc != TASK_RETURN; ++turns)
  {
    if (turns > 400000)
      Fatal("a machine's task that does not return (partial rollback)");
    if (Dispatch(lane))
      continue;
    if (lane.gave_up)
      Fatal(std::string("a machine's task stopped (partial rollback): ") + lane.gave_up);
    Verify(c.pc);
    if (!Dispatch(lane))
      Fatal("code that the CPU core has checked does not run (partial rollback)");
  }
  if (c.gpr[1] != b.entry.gpr[1])
    Fatal("a machine's task returned with another stack pointer (partial rollback)");
  Save(c, &job.regs);
  job.cycles = -c.downcount;
  if (!plain && s_work.inline_record && std::exchange(lane.track.accesses, 0) == 0)
  {
    // a module built without the record (DOLRECOMP_MEM2_TRACK): its locked-cache accesses went unseen; from the
    // next job on the lane's hooks record them, and this job runs in every later run of the step
    s_work.inline_record = false;
    b.whole[k] = true;
    s_core_host.Log(fzgx::LogLevel::Notice, "fzgx: partial rollback: the module records no locked-cache access itself: the hooks do");
  }
  if (!plain)
    Close(lane, &job.unit);
  // a block copy out of the cache into main memory, or an instruction the CPU core ran: no put-back gives it back
  b.whole[k] = b.whole[k] || lane.copies.size() != copies || b.handed != handed;
  if (!D().partial_watch.empty())
    Watch(k, plain ? "plain" : "recorded");
}

void EndRun(CPUState* cpu, bool plain)
{
  Lane& lane = s_b.lane;
  if (!plain)
  {
    std::memcpy(cpu->exram, lane.cache.get(), SCRATCH);
    Ppc().spr[DMAU] = lane.dmau;
    Ppc().spr[DMAL] = lane.dmal;
  }
  Write32(s_work.manager + MANAGER_NODE, s_work.job[0].node);
}

u32 Choose(CPUState* cpu, u32 step, const Kept* old)
{
  Batch& b = s_b;
  const bool log = Logging() && old != nullptr;
  if (Mode("check"))
    Tables(cpu, step, old);
  u32 count = 0;
  for (u32 i = 0; i < CARS; ++i)
  {
    Gather(cpu, b.layout[i], &b.starts[i]);
    fzgx::Data().CopyFrom(b.rows[i].data(), table::INPUTS + ((step & 1023) * 30 + i) * 8, ROW);
    // (the stand-in routine's button edges are this step's row and not the step before's: both rows are the job's inputs)
    fzgx::Data().CopyFrom(b.prev_rows[i].data(), table::INPUTS + (((step - 1) & 1023) * 30 + i) * 8, ROW);
    bool run = old == nullptr || b.rows[i] != old->rows[i] || b.prev_rows[i] != old->prev_rows[i] ||
               b.starts[i] != old->start[i];
    // the task's last call (racer 0's) also does the frame's own work in the stand-in routine (the seat switch,
    // viewport 0's camera record, [.bss 0xF6C] +0x2): it always runs (found by "check" at step 1810)
    const bool last = s_work.jobs != 0 && i == b.index[s_work.jobs - 1];
    b.run[i] = run || last;
    count += run ? 1 : 0;
    if (!log || !b.run[i])
      continue;
    std::string why = b.rows[i] != old->rows[i] ? "I" : "";
    if (b.prev_rows[i] != old->prev_rows[i])
      why += why.empty() ? "P" : ",P";
    if (b.starts[i] != old->start[i])
      why += (why.empty() ? "" : ",") + FirstDifference(b.layout[i], b.starts[i], old->start[i]);
    Line("S," + std::to_string(step) + "," + std::to_string(i) + "," + (why.empty() ? "F" : why));
  }
  return count;
}

u32 PutBack(CPUState* cpu, u32 k, const Kept& old, std::string* first)
{
  Batch& b = s_b;
  Lane& lane = b.lane;
  const Record& r = old.rec[k];
  const bool log = first != nullptr;
  u32 failed = 0;
  for (const Word& w : r.reads)
  {
    if (((WordAt(lane.cache.get(), w.word) ^ w.value) & ByteMask(w.mask)) == 0)
      continue;
    if (log && failed < 6)
    {
      char text[96];
      std::snprintf(text, sizeof(text), "+0x%04X read %08X now %08X;", w.word * 4, w.value, WordAt(lane.cache.get(), w.word));
      *first += text;
    }
    ++failed;
    if (!log)
      break;
  }
  // the floating-point status the job started from is read by its first rounding (the inexact bit FI, the
  // study's section 8): a job put back must have started from the same
  if (lane.cpu.fpscr != old.fpscr_in[k])
  {
    ++b.fpscr_failed;
    if (log)
    {
      char text[64];
      std::snprintf(text, sizeof(text), "fpscr %08X now %08X;", old.fpscr_in[k], lane.cpu.fpscr);
      *first += text;
    }
    ++failed;
  }
  if (log && failed != 0)
  {
    // the order the job first read the cache in: which routine reads the word, by what it reads around it
    *first += " first reads:";
    for (std::size_t i = 0; i < r.reads.size() && i < 16; ++i)
    {
      char text[24];
      std::snprintf(text, sizeof(text), " %X/%X", r.reads[i].word * 4, r.reads[i].mask);
      *first += text;
    }
  }
  if (failed != 0 && !(Mode("check") && D().partial_widen))
    return failed;
  for (const Word& o : r.outputs)
  {
    u8* p = lane.cache.get() + o.word * 4;
    for (u32 i = 0; i < 4; ++i)
    {
      if ((o.mask >> i & 1) != 0)
        p[i] = static_cast<u8>(o.value >> (24 - 8 * i));
    }
  }
  if (r.dma)
  {
    lane.dmau = r.dmau;
    lane.dmal = r.dmal;
  }
  Job& job = s_work.job[k];
  Write32(s_work.manager + MANAGER_NODE, job.node);
  Scatter(cpu, b.layout[old.index[k]], old.end[old.index[k]]);
  b.fpscr_in[k] = lane.cpu.fpscr;
  lane.cpu.fpscr = old.regs[k].fpscr;
  job.regs = old.regs[k];
  // a condition-register field the task left as its caller had it is this call's caller's
  for (u32 f = 0; f < 8; ++f)
  {
    const u32 field = 0xF0000000u >> (4 * f);
    if ((old.regs[k].cr & field) == (old.entry_cr & field))
      job.regs.cr = (job.regs.cr & ~field) | (b.entry.cr & field);
  }
  job.cycles = old.cycles[k];
  job.unit = r;
  b.whole[k] = false;
  // the task's prologue saves its return address in its caller's frame (the task runner's sp + 4)
  Write32(b.entry.gpr[1] + 4, TASK_RETURN);
  if (!D().partial_watch.empty())
    Watch(k, "put back");
  return failed;
}

u32 RunChosen(CPUState* cpu, u32 step, const Kept* old)
{
  Batch& b = s_b;
  const bool log = Logging();
  const bool guard = GuardOn();
  const bool writes = WritesOn(step);
  u32 failed = 0;
  bool diverged = false;
  BeginRun(cpu, false);
  if (guard)
    ReadGuard(cpu, &b.guard);
  if (writes)
    WritesBegin(cpu);
  for (u32 k = 0; k < s_work.jobs; ++k)
  {
    const u32 i = b.index[k];
    b.whole[k] = false;
    if (guard)
      b.guard_in[k] = b.guard;
    bool same_job = old != nullptr && k < old->jobs && old->index[k] == i && !old->whole[k];
    // the guard: the course objects as the job found them in the step's last run; once they differ, every later job
    // runs (the mine walk reads them on every machine's path)
    if (guard && same_job && !diverged && !SameObjects(b.guard, old->guard_in[k]))
    {
      diverged = true;
      ++b.guard_diverged;
      if (log)
        Line("U," + std::to_string(step) + "," + std::to_string(i) + ",objects," + GuardDifference(old->guard_in[k], b.guard));
    }
    if (diverged && !b.run[i])
    {
      same_job = false;
      b.run[i] = true;
    }
    std::string first;
    if (!b.run[i] && same_job)
    {
      const u32 bad = PutBack(cpu, k, *old, log ? &first : nullptr);
      if (bad == 0)
        continue;
      if (Mode("check") && D().partial_widen)
      {
        ++failed;
        Line("L," + std::to_string(step) + "," + std::to_string(i) + "," + first + ",put back all the same");
        continue;
      }
    }
    const bool live_in = !b.run[i];
    if (live_in)
    {
      ++failed;
      b.run[i] = true;
    }
    RunJob(k, false);
    if (writes)
      WritesAfter(cpu, "car", i);
    if (guard)
    {
      // a job that wrote a word of the guard runs in every later run of the step: no put-back gives it back
      Guard after;
      ReadGuard(cpu, &after);
      if (!SameGuard(b.guard, after))
      {
        b.whole[k] = true;
        ++b.guard_written;
        if (log)
          Line("U," + std::to_string(step) + "," + std::to_string(i) + ",wrote," + GuardDifference(b.guard, after));
        b.guard = std::move(after);
      }
    }
    if (live_in && log)
      Line("L," + std::to_string(step) + "," + std::to_string(i) + "," +
           (same_job ? first + (Mode("check") ? SameOutputs(cpu, k, *old) : std::string()) : "not the same job"));
  }
  EndRun(cpu, false);
  return failed;
}

void Remember(CPUState* cpu, u32 step)
{
  Batch& b = s_b;
  Kept& k = b.kept[step % KEEP];
  k.step = step;
  k.moment = b.moment;
  k.jobs = s_work.jobs;
  k.rows = b.rows;
  k.prev_rows = b.prev_rows;
  k.entry_cr = b.entry.cr;
  if (Mode("check"))
    k.tables.swap(b.tables);
  for (u32 i = 0; i < CARS; ++i)
  {
    k.start[i].swap(b.starts[i]);
    Gather(cpu, b.layout[i], &k.end[i]);
  }
  for (u32 j = 0; j < s_work.jobs; ++j)
  {
    const Job& job = s_work.job[j];
    if (GuardOn())
      k.guard_in[j] = b.guard_in[j];
    k.index[j] = b.index[j];
    k.rec[j] = job.unit;
    k.regs[j] = job.regs;
    k.fpscr_in[j] = b.fpscr_in[j];
    k.cycles[j] = job.cycles;
    k.whole[j] = b.whole[j];
  }
}

}  // namespace fzgx_port::lanes::partial
