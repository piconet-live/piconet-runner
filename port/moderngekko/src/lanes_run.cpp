// A lane's basics (lanes.h): one dispatch of the module's code on the lane, the CPU core's check of the code, the
// lane that gives up, and the registers and words a job moves. lanes_hooks.cpp has the hooks its state carries in
// place of the CPU core's.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "lanes_internal.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "Core/PowerPC/StaticRecomp/StaticRecompCore.h"

namespace fzgx_port::lanes
{
u64 Now()
{
  return static_cast<u64>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                              std::chrono::steady_clock::now().time_since_epoch())
                              .count());
}

// Something the lanes do not handle: said, and the game ends at once (its run then has no end line).
void Fatal(const std::string& text)
{
  s_core_host.Log(fzgx::LogLevel::Error, "fzgx: lanes: UNEXPECTED, the game ends here: " + text);
  std::_Exit(86);
}

void Note(const std::string& what)
{
  static std::vector<std::string> said;
  if (std::find(said.begin(), said.end(), what) == said.end())
  {
    said.push_back(what);
    s_core_host.Log(fzgx::LogLevel::Warning, "fzgx: lanes: (first time) " + what);
  }
}

u32 Read32(u32 address)
{
  const u8* p = s_work.game->ram + (address - table::RAM);
  return static_cast<u32>(p[0]) << 24 | static_cast<u32>(p[1]) << 16 | static_cast<u32>(p[2]) << 8 | p[3];
}

void Write32(u32 address, u32 value)
{
  u8* p = s_work.game->ram + (address - table::RAM);
  p[0] = static_cast<u8>(value >> 24);
  p[1] = static_cast<u8>(value >> 16);
  p[2] = static_cast<u8>(value >> 8);
  p[3] = static_cast<u8>(value);
}

void Save(const CPUState& cpu, Regs* regs)
{
  std::memcpy(regs->gpr, cpu.gpr, sizeof(regs->gpr));
  std::memcpy(regs->fpr, cpu.fpr, sizeof(regs->fpr));
  std::memcpy(regs->ps1, cpu.ps1, sizeof(regs->ps1));
  regs->pc = cpu.pc;
  regs->lr = cpu.lr;
  regs->ctr = cpu.ctr;
  regs->cr = cpu.cr;
  regs->xer = cpu.xer;
  regs->fpscr = cpu.fpscr;
}

void Load(const Regs& regs, CPUState* cpu)
{
  std::memcpy(cpu->gpr, regs.gpr, sizeof(regs.gpr));
  std::memcpy(cpu->fpr, regs.fpr, sizeof(regs.fpr));
  std::memcpy(cpu->ps1, regs.ps1, sizeof(regs.ps1));
  cpu->pc = regs.pc;
  cpu->lr = regs.lr;
  cpu->ctr = regs.ctr;
  cpu->cr = regs.cr;
  cpu->xer = regs.xer;
  cpu->fpscr = regs.fpscr;
}

bool Dispatch(Lane& lane)
{
  CPUState& c = lane.cpu;
  const u32 pc = c.pc;
  const s64 charged = c.downcount;
  char text[96];
  if (lane.gave_up)
    return false;
  if (pc >= HOST_CALLS && pc < HOST_CALLS_END)
  {
    GiveUp(lane, "an update called the host");
    return false;
  }
  if (!s_work.dispatch(&c, pc))
  {
    std::snprintf(text, sizeof(text), "no native code at %08X (called from %08X)", pc, c.lr);
    if (lane.plain)
      Fatal(text);
    GiveUp(lane, "no native code where an update went");
    return false;
  }
  if (lane.gave_up)
    return false;
  if (c.exception != 0)
  {
    std::snprintf(text, sizeof(text), "an exception (%X) in the code at %08X", c.exception, pc);
    if (lane.plain)
      Fatal(text);
    GiveUp(lane, "an exception in an update");
    return false;
  }
  return !(c.pc == pc && c.downcount == charged);
}

void Verify(u32 pc)
{
  if (g_static_recomp_core && g_static_recomp_core->DispatchableAt(pc))
    return;
  char text[96];
  std::snprintf(text, sizeof(text), "the code at %08X is not what the native module was built from", pc);
  Fatal(text);
}

void GiveUp(Lane& lane, const char* why)
{
  if (lane.plain)
    Fatal(why);
  if (lane.gave_up)
    return;
  lane.gave_up = why;
  lane.track.outside = true;
  lane.cpu.exception = lane.cpu.exception != 0 ? lane.cpu.exception : GAVE_UP;
  lane.cpu.cycle_budget = 0;
}

}  // namespace fzgx_port::lanes
