// The machines' task as a host call. The game's task runner (main.rel fn_1_407C) calls one task a machine
// (fn_1_83858), from the last racer down to racer 0. While the per-machine rollback (lanes_partial.h) is on, that
// task's address is a host call: the first call of a step arrives
// here with the game's state complete, the path runs the step's jobs on a lane of its own on the game's thread
// (lanes_run.cpp, lanes_hooks.cpp: a lane's state, its hooks and the record of its locked cache), and the step's other
// calls of the task return at once, each with the registers and cycles its own update left.
// The multicore physics that this machinery was first built for (the updates on several threads) is parked on branch
// parked/lanes: it comes back only rebuilt on the Rollback library (port/core/src/rollback.h).
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

#include "Common/CommonTypes.h"

struct CPUState;

namespace fzgx_port::lanes
{
// The native module's file, as the CPU core is given it (before boot).
void Module(const std::string& path);
// `address` is a task the per-machine rollback takes, a host call; a range of code holds one.
bool IsTask(u32 address);
bool Holds(u32 start, u32 end);
// The task's call, from the run loop. False: left to the normal path (the task then runs on the CPU core).
bool TaskCall(CPUState* cpu);
// When the game stops: the log gets its lines.
void Stop();
}  // namespace fzgx_port::lanes
