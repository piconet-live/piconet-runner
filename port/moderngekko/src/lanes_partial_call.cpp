// Per-machine rollback (lanes_partial.h): the task runner's calls of the machines' task taken, a batch run on the
// path's lane, an instruction left to the interpreter handed to the CPU core, and the game's end.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "lanes_partial.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>

#include "Core/PowerPC/PowerPC.h"

namespace fzgx_port::lanes::partial
{
namespace
{
std::map<u32, u64> s_handed;  // where an instruction was handed to the CPU core, how often

void RunBatch(CPUState* cpu, u32 step, const Kept* old)
{
  u64 began = Now();
  const u32 chosen = Choose(cpu, step, old);
  const u32 failed = RunChosen(cpu, step, old);
  const u64 part = Now() - began;
  began = Now();
  Remember(cpu, step);
  s_b.live_in_failed += failed;
  if (Logging())
    BatchLine(step, old != nullptr, chosen, 0, part, Now() - began, failed, nullptr);
}
}  // namespace
}  // namespace fzgx_port::lanes::partial

namespace fzgx_port::lanes
{
using namespace partial;

bool PartialWants(u32 address)
{
  if (address != TASK || !s_state.native)
    return false;
  const std::string& mode = s_state.options.debug.partial_mode;
  return mode.empty() || mode == "check" || mode == "writes";  // on by default
}

bool PartialHolds(u32 start, u32 end)
{
  return PartialWants(TASK) && start <= TASK && TASK < end;
}

bool PartialCall(CPUState* cpu)
{
  Batch& b = s_b;
  Work& w = s_work;
  w.game = cpu;
  const u32 step = Step();
  if (w.open)
  {
    if (cpu->gpr[3] == w.job[w.returned].machine && cpu->gpr[24] == w.job[w.returned].node && cpu->lr == TASK_RETURN)
      return Return(cpu);
    Line("X,a call of the task that the open batch does not hold, step " + std::to_string(step));
    w.open = false;
  }
  if (!Ready(cpu) || !Collect(cpu) || !CollectIndexes())
    return false;
  Save(*cpu, &b.entry);
  LayOut(cpu, std::min<u32>(cpu->ram[game::MACHINE_COUNT_AT - table::RAM], CARS), &b.layout);
  // a step's last run counts while the memory is the same moment: a race's start or end, a scrubber's jump or a
  // practice's key forget them all
  const u32 moment = fzgx::RollbackMoments();
  if (moment != b.moment)
  {
    b.moment = moment;
    b.max_step = 0;
  }
  const Kept& last = b.kept[step % KEEP];
  const Kept* old = step != 0 && step <= b.max_step && last.step == step && last.moment == moment ? &last : nullptr;
  b.max_step = std::max(b.max_step, step);
  b.runs[step % KEEP] = old != nullptr ? b.runs[step % KEEP] + 1 : 1;
  ++b.batches;
  b.again += old != nullptr ? 1 : 0;
  const fzgx::Debug& d = s_state.options.debug;
  const bool in_range = d.partial_to == 0 || (step >= d.partial_from && step <= d.partial_to);
  if (Mode("check") && in_range && old != nullptr)
    CheckBatch(cpu, step, old);
  else
    RunBatch(cpu, step, old);
  if (old != nullptr)
  {
    for (u32 i = 0; i < CARS; ++i)
      b.ran += b.run[i] ? 1 : 0;
  }
  w.open = true;
  w.returned = 0;
  return Return(cpu);
}

// An instruction the module leaves to the interpreter on the path's lane (a jump-table target with no native entry,
// as the rumble switch's bare blr at 802B1C94). The lane runs alone on the game's
// thread, so the CPU core runs the instruction as it does without the path: the lane's registers into the game's
// state, its locked cache and block-copy registers into the emulator's, the core's own fallback, and back. The job
// then runs in every later run of its step (what the interpreter read and wrote of the cache is not recorded).
void HandOff(Lane& lane, u32 raw, u32 cia)
{
  CPUState& game = *s_work.game;
  CPUState& c = lane.cpu;
  PowerPC::PowerPCState& ppc = Ppc();
  const CPUState kept = game;
  const u32 dmau = ppc.spr[DMAU], dmal = ppc.spr[DMAL];
  Regs r;
  Save(c, &r);
  Load(r, &game);
  game.downcount = 0;
  if (!lane.plain)
  {
    std::memcpy(game.exram, lane.cache.get(), SCRATCH);
    ppc.spr[DMAU] = lane.dmau;
    ppc.spr[DMAL] = lane.dmal;
  }
  game.instruction_fallback(&game, raw, cia);
  Save(game, &r);
  Load(r, &c);
  if (!lane.plain)
  {
    std::memcpy(lane.cache.get(), game.exram, SCRATCH);
    lane.dmau = ppc.spr[DMAU];
    lane.dmal = ppc.spr[DMAL];
  }
  // the game's own locked cache now holds the lane's: the run's end (EndRun) writes it whole in any case
  game = kept;
  if (!lane.plain)
  {
    ppc.spr[DMAU] = dmau;
    ppc.spr[DMAL] = dmal;
  }
  ++s_b.handed;
  if (s_handed[cia]++ == 0)
  {
    char text[160];
    std::snprintf(text, sizeof(text), "fzgx: partial rollback: an instruction left to the interpreter (%08X at %08X, "
                  "step %u) is run by the CPU core", raw, cia, Step());
    s_core_host.Log(fzgx::LogLevel::Notice, text);
  }
  if (Logging())
  {
    char text[64];
    std::snprintf(text, sizeof(text), "H,%u,%u,%08X,%08X", Step(), s_b.index[lane.job_now], cia, raw);
    Line(text);
  }
}

void PartialStop()
{
  const Batch& b = s_b;
  if (!b.ready)
    return fzgx::PartialFlush();
  char text[480];
  std::snprintf(text, sizeof(text), "fzgx: partial rollback%s%s: %llu batches, %llu run again (machines run in them: "
                "%llu; failed locked-cache or status checks %llu, of them the status %llu; run by the guard: %llu for a "
                "word written, %llu batches from the objects on), %llu instructions run by the CPU core, %llu checked, "
                "%llu differing",
                b.ready && !s_state.options.debug.partial_mode.empty() ? " " : "", s_state.options.debug.partial_mode.c_str(),
                static_cast<unsigned long long>(b.batches), static_cast<unsigned long long>(b.again),
                static_cast<unsigned long long>(b.ran), static_cast<unsigned long long>(b.live_in_failed),
                static_cast<unsigned long long>(b.fpscr_failed), static_cast<unsigned long long>(b.guard_written),
                static_cast<unsigned long long>(b.guard_diverged), static_cast<unsigned long long>(b.handed),
                static_cast<unsigned long long>(b.checked), static_cast<unsigned long long>(b.differing));
  s_core_host.Log(fzgx::LogLevel::Notice, text);
  Line(std::string("X,") + text);
  for (const auto& [cia, count] : s_handed)
  {
    std::snprintf(text, sizeof(text), "fzgx: partial rollback: the CPU core ran the instruction at %08X %llu times", cia,
                  static_cast<unsigned long long>(count));
    s_core_host.Log(fzgx::LogLevel::Notice, text);
    Line(std::string("X,") + text);
  }
  WritesSummary();
  fzgx::PartialFlush();
}
}  // namespace fzgx_port::lanes
