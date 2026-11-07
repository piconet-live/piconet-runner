// The deep-rollback probe (S0 of research/study-rollback-second-core.md, section 9): what a step run again reaches
// besides the memory and the registers the generated code works on, counted on the game's thread. Off unless the
// launch file's "debug.stepCensus" names a file (the emulator's FZGX_STEP_CENSUS). With it, every step run again
// (the control block's +488 set at the step's network hook) runs with the game's state dressed with counting hooks
// that pass everything on to the CPU core's own, and the CPU core counts its exceptions, interrupts, fallback JIT
// entries, slices, idle loops and OS thread while it runs (FzgxProbe::StepCensus, the chassis). A module built with
// DOLRECOMP_MEM_TRACE=1 also reports every load and store: the census then keeps the words below the undo log's
// range that each step reads before writing them, and compares the values a step run again reads with those its
// first run read. Nothing the game computes changes: the hooks count and pass on; the module's trace only reads.
// Its files: FILE.steps.csv (a line a step run again), FILE.words.csv (the words below the log a step reads), and
// FILE.txt (the run's sums and tables), written when the game stops.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "Common/CommonTypes.h"

struct CPUState;

namespace fzgx_port::lanes
{
// One of our host calls (or the machines' task) is about to be served: an open census window counts it, and the
// network hook and the frame tick end it.
void CensusHostCall(CPUState* cpu, u32 address);
// The step's network hook has returned: the step about to run opens a window (dressed when it was run before).
void CensusAfterNet(CPUState* cpu);
// The game stops: the files.
void CensusStop();
}  // namespace fzgx_port::lanes
