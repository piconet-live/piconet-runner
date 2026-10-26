// The replay scrubber's host side that is no bar (adapter.cpp hands these to the core): the keys, the packer of its
// undo log, the process's memory, and the game thread's clocks.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "adapter_internal.h"

#include <atomic>
#include <cstdint>

#include "Core/CoreTiming.h"
#include "Core/FzgxProbe.h"
#include "Core/System.h"
#include "fzgx/replay_keys.h"

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#endif
#ifdef FZGX_PORT_ZSTD
#include <zstd.h>
#endif

namespace fzgx_port
{
namespace
{
std::atomic<fzgx::u32> s_pressed{0};  // presses not yet taken by the core
}  // namespace

// The scrubber's keys, from the game window's own key messages (only while it has the focus; no hook of the
// system's): Space, Left, Right, Home, End, . and , and [ and ]. Left, Right, . and , repeat while held, at the
// system's key-repeat rate; the others act once a press.
bool ReplayKeyMessage(unsigned message, std::uintptr_t wparam, std::intptr_t lparam)
{
  if (!s_state.options.replay_scrubber || message != 0x0100)  // (WM_KEYDOWN)
    return false;
  const fzgx::ReplayKeyPress press =
      fzgx::ReplayKeyOf(wparam, (static_cast<std::uintptr_t>(lparam) & (std::uintptr_t{1} << 30)) != 0);
  if (press.bit != 0)
    s_pressed.fetch_or(press.bit, std::memory_order_relaxed);
  return press.ours;
}

fzgx::u32 TakeReplayKeys()
{
  return s_pressed.exchange(0, std::memory_order_relaxed);
}

// The undo log packed by the emulator's own zstd, at its fastest level (measured on real logs: zlib's fastest
// packed them 3 times; zstd as well at many times the speed). A step's bytes are packed with the step before's as
// the prefix (zstd's raw prefix: no dictionary is built). On the game's thread only.
bool PackReplayLog(const fzgx::u8* data, std::size_t size, const fzgx::u8* prefix, std::size_t prefix_size,
                   std::vector<fzgx::u8>* out)
{
#ifdef FZGX_PORT_ZSTD
  static ZSTD_CCtx* const packer = ZSTD_createCCtx();
  if (packer == nullptr || ZSTD_isError(ZSTD_CCtx_setParameter(packer, ZSTD_c_compressionLevel, 1)) ||
      ZSTD_isError(ZSTD_CCtx_refPrefix(packer, prefix, prefix_size)))
    return false;
  out->resize(ZSTD_compressBound(size));
  const std::size_t packed = ZSTD_compress2(packer, out->data(), out->size(), data, size);
  if (ZSTD_isError(packed))
    return false;
  out->resize(packed);
  return true;
#else
  (void)data, (void)size, (void)prefix, (void)prefix_size, (void)out;
  return false;
#endif
}

bool UnpackReplayLog(const fzgx::u8* data, std::size_t size, const fzgx::u8* prefix, std::size_t prefix_size,
                     fzgx::u8* out, std::size_t out_size)
{
#ifdef FZGX_PORT_ZSTD
  static ZSTD_DCtx* const unpacker = ZSTD_createDCtx();
  if (unpacker == nullptr || ZSTD_isError(ZSTD_DCtx_refPrefix(unpacker, prefix, prefix_size)))
    return false;
  const std::size_t unpacked = ZSTD_decompressDCtx(unpacker, out, out_size, data, size);
  return !ZSTD_isError(unpacked) && unpacked == out_size;
#else
  (void)data, (void)size, (void)prefix, (void)prefix_size, (void)out, (void)out_size;
  return false;
#endif
}

// The process's memory as Windows counts it: private bytes and their peak, the working set and its peak.
bool ProcessMemoryNow(fzgx::u64 out[4])
{
#ifdef _WIN32
  PROCESS_MEMORY_COUNTERS_EX counters{};
  counters.cb = sizeof(counters);
  if (!K32GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters)))
    return false;
  out[0] = counters.PrivateUsage;
  out[1] = counters.PeakPagefileUsage;
  out[2] = counters.WorkingSetSize;
  out[3] = counters.PeakWorkingSetSize;
  return true;
#else
  (void)out;
  return false;
#endif
}

// The game thread's clocks for the scrubber's budget (scrub_budget.cpp), on that thread: the time the emulator's
// throttle slept on it (our probe counts every sleep of CoreTiming::SleepUntil on the CPU thread), and the emulated
// processor's ticks and idle ticks as the emulator's timing counts them.
bool GameThreadClocks(fzgx::GameClocks* out)
{
  const CoreTiming::CoreTimingManager& timing = Core::System::GetInstance().GetCoreTiming();
  out->waited = std::chrono::nanoseconds{FzgxProbe::CpuThreadSleptNs()};
  out->ticks = timing.GetTicks();
  out->idle_ticks = timing.GetIdleTicks();
  return true;
}
}  // namespace fzgx_port
