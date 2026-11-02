// The mod's data window and the host calls: what the emulator is given so that the image's data lives in this
// process and the game's calls to the host's five addresses arrive in the core.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "adapter_internal.h"
#include "lanes.h"

#include <algorithm>
#include <cstdio>

#include "Core/HostHooks.h"
#include "fzgx/hfr.h"
#include "threads.h"
#include "Core/PowerPC/StaticRecomp/StaticRecompModuleSource.h"

namespace fzgx_port
{
namespace
{
bool IsOurs(u32 address)
{
  if (s_state.native)
    return address >= HOST_CALLS && address < HOST_CALLS_END && (address & 3) == 0;
  return address == NET_HOOK;
}
}  // namespace

// The data window's accesses per frame and per step, for the log: at every 600th frame tick and at the stop.
void ReportWindow(bool last)
{
  if (s_state.frames == 0 || (!last && s_state.frames % 600 != 0))
    return;
  char text[240];
  std::snprintf(text, sizeof(text),
                "fzgx: data window: %llu frames, %.1f accesses a frame (most %llu), %.1f a step over %llu steps; "
                "%llu reads and %llu writes in all",
                static_cast<unsigned long long>(s_state.frames),
                static_cast<double>(s_state.frame_accesses) / static_cast<double>(s_state.frames),
                static_cast<unsigned long long>(s_state.frame_most),
                s_state.steps ? static_cast<double>(s_state.frame_accesses) / static_cast<double>(s_state.steps) : 0.0,
                static_cast<unsigned long long>(s_state.steps), static_cast<unsigned long long>(s_state.window_reads),
                static_cast<unsigned long long>(s_state.window_writes));
  s_core_host.Log(fzgx::LogLevel::Notice, text);
}

namespace
{
// One game frame of a race or replay start has begun (the frame tick): what the frame before cost in accesses.
void CountFrame()
{
  const fzgx::u64 now = s_state.window_reads + s_state.window_writes;
  const fzgx::u64 before = s_state.counted_reads + s_state.counted_writes;
  if (s_state.frames != 0)
  {
    s_state.frame_accesses += now - before;
    s_state.frame_most = std::max(s_state.frame_most, now - before);
  }
  s_state.counted_reads = s_state.window_reads;
  s_state.counted_writes = s_state.window_writes;
  ++s_state.frames;
  ReportWindow(false);
}

// The host's services as host calls of the StaticRecomp core: taken from the run loop at a call boundary, with the
// CPU state complete; each returns to its caller. None changes a guest register (the rule for hooks: a host call
// is an exit, and the code is entered again) but the drawn frames' call, whose answer is r3: the image's routine
// that calls it (port/image/sites_hfr.py) reads it.
bool HostCall(CPUState* cpu, u32 address, void*)
{
  if (lanes::IsTask(address))  // the machines' task, with the per-machine rollback on (lanes.h)
    return lanes::TaskCall(cpu);
  if (IsOurs(address))
  {
    if (!s_state.native || address == table::HOST_NET)
    {
      ++s_state.steps;
      fzgx::NetSync();
    }
    else if (address == table::HOST_TICK)
    {
      threads::KnowThisThread(threads::Which::Game);  // (for the freeze report: one flag read from the second call on)
      CountFrame();
      fzgx::FrameTick();
    }
    else if (address == table::HOST_HFR)
    {
      // the frames drawn between two steps (fzgx/hfr.h): the operation in r3, its frame in r4, the answer in r3
      cpu->gpr[3] = fzgx::HfrCall(cpu->gpr[3], cpu->gpr[4]);
    }
    else if (address == table::HOST_LINKED)
    {
      fzgx::Linked(cpu->gpr[30]);  // the loader holds the module in r30 (the patched site is `lwz r12, 0x34(r30)`)
    }
    else if (address == table::HOST_MODE)
    {
      fzgx::ModeCall();  // the game mode's rules, at the mode point (port/image/sites_mode.py)
    }
    cpu->pc = cpu->lr;
    return true;
  }
  return s_state.next_call && s_state.next_call(cpu, address, s_state.next_user);
}

bool HostCallContains(u32 address, void*)
{
  return IsOurs(address) || lanes::IsTask(address) || (s_state.next_contains && s_state.next_contains(address, s_state.next_user));
}

bool HostCallRangeContains(u32 start, u32 end, void*)
{
  const bool ours = s_state.native ? (start < HOST_CALLS_END && HOST_CALLS < end) : (start <= NET_HOOK && NET_HOOK < end);
  return ours || lanes::Holds(start, end) || (s_state.next_range_contains && s_state.next_range_contains(start, end, s_state.next_user));
}

}  // namespace

// The native form: the mod's data window is this process's buffer (fzgx::ModData). The emulator answers every
// access of it, and every instruction fetched from the image, from these two functions before any translation
// (Core/HostHooks.h), and the StaticRecomp core counts the module's pieces inside the window as checked.
void RegisterWindow(HostHooks::Hooks& hooks)
{
  hooks.window_start = table::WINDOW;
  hooks.window_end = table::WINDOW_END;
  hooks.window_read = [](u32 address, u32 size) {
    ++s_state.window_reads;
    return fzgx::Data().Read(address, size);
  };
  hooks.window_write = [](u32 address, u64 value, u32 size) {
    ++s_state.window_writes;
    fzgx::Data().Write(address, value, size);
    fzgx::WindowWritten(address, value);  // (the replay scrubber: one of the image's stores is a call to the core)
  };
}

// The host calls go in front of the ones `source` holds (the mod loader's).
void TakeHostCalls(StaticRecompModuleSource& source)
{
  s_state.next_call = source.host_call;
  s_state.next_contains = source.host_call_contains;
  s_state.next_range_contains = source.host_call_range_contains;
  s_state.next_user = source.host_call_user;
  source.host_call = &HostCall;
  source.host_call_contains = &HostCallContains;
  source.host_call_range_contains = &HostCallRangeContains;
}
}  // namespace fzgx_port
