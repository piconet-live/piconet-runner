// The undo log's guard (undo_log.cpp): pages of the tracked memory made read-only, so that the first write to each, on
// any thread, is a fault the guard's handler catches. The handler notes the page as written, makes it writable again
// and lets the write go on. The undo log then compares only the pages it watches and those written, and no longer
// compares the whole memory twice in a rollback frame to find first writes.
//
// One guard per process: the handler is the process's. It is safe on any thread and inside a signal handler: it
// reads and sets atomic bits of fixed arrays and calls the system's page protection, nothing else.
// Other handlers of the same faults (the emulator's own, for its JIT's fast memory) are chained, whichever was
// installed first: a fault that is not on a page this guard protects goes on to them untouched.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "fzgx/host.h"

namespace fzgx::net::guard
{
constexpr u32 GUARD_PAGE = 0x1000;
constexpr u32 MOST_PAGES = 8192;  // 32 MiB: the tracked memory is 18.6 MiB

// Starts a guard over `bytes` (whole pages) from `base`, with no page protected yet: the handler installed (or put
// back in front, if another was installed after it). False, with the reason, when it cannot be: the memory is not
// page-aligned, too large, the system's pages are not 4 KiB, the handler is refused. A guard already on is ended first.
bool Begin(u8* base, std::size_t bytes, std::string* why);
// The page `page` (an index from base) read-only, and guarded from now on. False when the system refuses.
bool Protect(u32 page);
// The page writable again, and no longer guarded (the undo log compares it at every step from now on).
void Open(u32 page);
// The guarded pages written since the last call, in order, each opened by its first write: into `pages`.
void TakeWritten(std::vector<u32>* pages);
// Every guarded page writable again and nothing guarded; the handler stays, and passes every fault on.
void End();
// The guarded memory is gone (the emulator let it go): nothing guarded from now on, and no page's protection touched,
// for its addresses may be another allocation's by now. The handler stays, and passes every fault on. End after it
// does nothing.
void Abandon();
// Whether a guard is on, the faults it has caught since Begin, and the pages it guards now (protected, not yet
// opened; it counts them: tests, Debug::undo_trace).
bool Active();
u64 Faults();
u32 Guarded();
// The faults caught on another thread than the one that called Begin (the game's), since Begin, and the first few
// pages they were on (at most `most` into `pages`; the count of pages given back).
u64 OtherThreadFaults();
u32 OtherThreadPages(u32* pages, u32 most);
}  // namespace fzgx::net::guard
