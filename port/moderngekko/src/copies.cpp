// Frame-buffer copies and the rollback: the emulator asks before a copy writes the game's memory (HostHooks
// clear_copy_memory), and a copy into memory a rollback keeps leaves it as the game had it.
// The picture's thread clears a frame-buffer copy's destination in the game's memory when it comes to the copy,
// about 2 ms after the game asked for it and after the frame's rollback has put the memory back: not where a
// rollback keeps the memory (fzgx::UnderRollback has the story). Copies into memory no rollback keeps are
// cleared as ever: the portraits, the boost's full-screen copy, the display's copy (above the simulation's
// memory), and every copy while no undo log is kept (the menus, the countdown, the results, a race that failed).
// The emulator asks before every write a copy makes to the game's memory: the clear of a copy kept as a texture
// (what pins.cpp holds the game to, and the Null backend of a headless start takes too) and the pixels of a copy
// written to memory (reachable only with debug.userSettings, or a backend that keeps no copy as a texture).
// SPDX-License-Identifier: GPL-3.0-or-later

#include "adapter_internal.h"

#include <atomic>
#include <format>

namespace fzgx_port
{
namespace
{
// copies whose destination the picture's thread did not clear (the hook below), for one line when the game stops
std::atomic<unsigned long long> s_copies_kept{0};
}  // namespace

void KeepCopiesUnderRollback(HostHooks::Hooks& hooks)
{
  s_copies_kept = 0;
  hooks.clear_copy_memory = [](u32 address, u32 size) {
    if (!fzgx::UnderRollback(table::RAM + address, size))
      return true;
    s_copies_kept.fetch_add(1, std::memory_order_relaxed);
    return false;
  };
}

void ReportCopiesKept()
{
  if (const unsigned long long kept = s_copies_kept.load(); kept != 0)
    s_core_host.Log(fzgx::LogLevel::Notice, std::format("fzgx: frame-buffer copies into memory a rollback keeps: {}, "
                                                        "their destinations left as the game had them", kept));
}
}  // namespace fzgx_port
