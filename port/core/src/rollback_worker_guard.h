// The rollback worker's guards from the thread audit (research/study-rollback-second-core.md section 17), for
// rollback_worker.cpp: the preconditions of a job's start (U2, U6, U7) and the CPU core's code generation (U1).
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <map>
#include <string>

#include "fzgx/guest.h"

namespace fzgx::rollback_worker::guard
{
// What bars every job now, or nullptr: a module whose pipe is one for every thread (U2), an instrumented module (U6),
// the mod loader's interception active (U7). `pipe_per_thread` is SetPipePerThread's.
const char* StartBar(bool pipe_per_thread);
// U1: the generation at a job's start (TakeGeneration), and whether it has moved since (Moved, at the adoption); per
// worker slot (study section 20.4: taken per job).
void TakeGeneration(int slot = 0);
bool Moved(int slot = 0);
// The starts each bar of StartBar stopped, for the log's line.
std::map<std::string, u64>& Barred();
}  // namespace fzgx::rollback_worker::guard
