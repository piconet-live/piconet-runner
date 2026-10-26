// The machines' task as a host call (lanes.h): the run loop's entry, the module's dispatch, and the task runner's
// calls of the task as one batch's jobs and their returns. The per-machine rollback (lanes_partial.h) runs the batch.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "lanes_internal.h"

#include <cstring>
#include <filesystem>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fzgx_port::lanes
{
Work s_work;

// The module's two functions the path calls, by the module's own names; none on a host that cannot ask.
bool FindModule(Work& w)
{
#ifdef _WIN32
  const HMODULE module = GetModuleHandleW(std::filesystem::path(w.module).filename().c_str());
  using GetModule = const StaticRecompModuleDesc* (*)();
  const auto get = module ? reinterpret_cast<GetModule>(GetProcAddress(module, STATICRECOMP_GET_MODULE_SYMBOL)) : nullptr;
  w.dispatch = get ? get()->dispatch : nullptr;
  w.set_valid = module ? reinterpret_cast<void (*)(unsigned, unsigned, int)>(GetProcAddress(module, "staticrecomp_set_native_valid")) : nullptr;
#endif
  return w.dispatch != nullptr && w.set_valid != nullptr;
}

// The tasks from the task runner's node on, as far as they are the machines' task: the batch's jobs. False: this is
// not the task runner's call as the game makes it, or its state cannot be handed to a lane.
bool Collect(CPUState* cpu)
{
  Work& w = s_work;
  const u32 ram_end = table::RAM + cpu->ram_size;
  const auto in_ram = [&](u32 address) { return address >= table::RAM && address < ram_end - 0x40 && (address & 3) == 0; };
  u32 node = cpu->gpr[24];
  w.manager = cpu->gpr[30];
  if (cpu->lr != TASK_RETURN || !in_ram(node) || !in_ram(w.manager) || !in_ram(cpu->gpr[1]) ||
      Read32(w.manager + MANAGER_NODE) != node || Read32(node + NODE_ARGUMENT) != cpu->gpr[3])
    return false;
  if ((cpu->msr & 0x2000) == 0 || cpu->exram == nullptr || cpu->exram_size < SCRATCH)
    return false;  // floating point is off (after an interrupt): the game's own path turns it on
  w.jobs = 0;
  for (; node != 0 && w.jobs < MOST_JOBS; node = Read32(node + NODE_NEXT))
  {
    if (!in_ram(node) || Read32(node + NODE_FUNCTION) != TASK || !in_ram(Read32(node + NODE_ARGUMENT)))
      break;
    w.job[w.jobs].node = node;
    w.job[w.jobs].machine = Read32(node + NODE_ARGUMENT);
    w.job[w.jobs].ret_pc = TASK_RETURN;
    w.job[w.jobs].sp_change = TASK_FRAME;
    ++w.jobs;
  }
  return w.jobs != 0;
}

// One call of the task returns: the registers a call may change are as its update left them, the others as the
// task runner holds them; the status bits that only ever get set are kept; the cycles are charged by the run loop.
bool Return(CPUState* cpu)
{
  Work& w = s_work;
  const Job& job = w.job[w.returned];
  const Regs& r = job.regs;
  if (r.gpr[1] != cpu->gpr[1] || r.pc != TASK_RETURN)
    Fatal("an update did not return to its task's caller");
  cpu->gpr[0] = r.gpr[0];
  std::memcpy(&cpu->gpr[3], &r.gpr[3], 10 * sizeof(u32));
  std::memcpy(cpu->fpr, r.fpr, 14 * sizeof(f64));
  std::memcpy(cpu->ps1, r.ps1, 14 * sizeof(f64));
  cpu->cr = (cpu->cr & 0x00FFF000u) | (r.cr & 0xFF000FFFu);
  cpu->xer = r.xer;
  cpu->ctr = r.ctr;
  cpu->lr = r.lr;
  cpu->fpscr = r.fpscr | (cpu->fpscr & 0xFFF80700u);
  cpu->pc = TASK_RETURN;
  cpu->downcount = -job.cycles;
  if (++w.returned == w.jobs)
    w.open = false;
  return true;
}
void Module(const std::string& path)
{
  s_work.module = path;
}

bool IsTask(u32 address)
{
  return PartialWants(address);
}

bool Holds(u32 start, u32 end)
{
  return PartialHolds(start, end);
}

bool TaskCall(CPUState* cpu)
{
  return PartialWants(cpu->pc) && PartialCall(cpu);
}

void Stop()
{
  PartialStop();
}
}  // namespace fzgx_port::lanes
