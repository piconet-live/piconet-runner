// The deep-rollback worker, stage S1 (research/study-rollback-second-core.md, sections 1 to 3 and 10): a deep
// rollback's steps run again by a second CPU context over a private memory W, with a Rollback log of its own, on the
// game's thread for now (the thread is S2). The launch file's "debug.rollbackWorker" (off by default; with it off
// nothing here runs and nothing the game computes changes):
//   - "compare": every rollback at least `depth` steps deep (7 by default) is run on W first: at the frame's network
//     hook, before the restore, W takes the pages of the game's memory either log has seen change since the last job
//     (Rollback::TakeDirty) and the memory outside the log whole, is rewound to the rollback's step R by the game's
//     log (Rollback::Apply) and taken as the worker log's moment there (Adopt); the context runs the image's own
//     frame routine on W from that hook to the network hook of the frame's step L, its own hooks serving each step's
//     words as ExtraStep does. Then the game's thread runs the same steps as it always does, and at the hook of step
//     L the two are compared: the memory a restore puts back (Rollback::Compare), the two logs' entries of steps R to
//     L - 1 (CompareEntries), the checksums and input rows of those steps; and, as a named class, the words outside
//     the log. The first differences go to the log, the sums at the end.
//   - "check": the same, and the game's memory outside the log is then left as an adoption would leave it: the
//     words outside the log the worker's steps changed take back the values they had at the job's start (the
//     predicted timeline's, which the worker does not hand over), but for the running thread's stack. A playback
//     that stays exact shows these words do not reach the checksum.
// Nothing is adopted (Rollback::TakeOver is S2's): the game's thread goes on with its own steps.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

#include "fzgx/guest.h"

namespace fzgx::rollback_worker
{
// The switch is on (compare or check).
bool On();

// The CPU context's memory for a job about to run.
struct Memory
{
  u8* ram = nullptr;
  u32 ram_size = 0;
  u8* cache = nullptr;  // the locked cache's window
  u32 cache_size = 0;
};
// After the game's network hook has returned (the adapter's host call): a job prepared in this hook is to run now.
// False: none. The context starts from the game's registers at this hook.
bool Begin(Memory* memory);
// W's data window: the context's slow accesses to the window are served from it.
ModData& Window();
// The context reached the network hook: the step about to run's words are written into W (or not, at the job's last
// hook). True: go on; false: the job's steps are run (the context stops here).
bool Hook();
// The context reached the mode point (the image's HOST_MODE): the game mode's rules run on W (mode.cpp keeps every
// state between two steps in the guest's block; a race without a mode returns at once).
void ModePoint();
// The context met what it does not handle: the job gives up (and the game's thread runs the rollback as ever).
void GiveUp(const std::string& why);
// A count for the log's last line (the adapter's: system calls replicated, block copies, dispatches).
void Count(const char* what, u64 n);
// The game stops: the last line.
void Stop();
}  // namespace fzgx::rollback_worker
