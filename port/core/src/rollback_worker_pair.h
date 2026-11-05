// The two rollback workers' rules (research/study-rollback-second-core.md section 20.2 to 20.4), for
// rollback_worker.cpp: how many workers this PC runs, when the second job N starts from the open job O's log, which
// of the two is adopted in a frame and in what order, what O's end does to N, and when a step the chain owes is
// returned. Plain values in and out, so the tests drive every rule.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "fzgx/guest.h"

namespace fzgx::rollback_worker::pair
{
constexpr u32 NONE = 0xFFFFFFFF;
// The second worker needs a third thread beside the game's and W's, and the drawing's beside those: on from this many
// usable hardware threads (study section 20.2)
constexpr unsigned LEAST_THREADS = 6;

// The workers this game runs (1 or 2): the launch file's "workers" when it names 1 or 2 (`key`, 0: none), else
// FZGX_ROLLBACK_WORKERS's "1" or "2" (`env`, nullptr when unset; another value is not taken), else 2 from
// LEAST_THREADS usable threads (`threads`: the hardware's count, and on Windows the process's affinity mask), else 1.
u32 WorkersFor(u32 key, const char* env, unsigned threads);

// What N's start asks of the moment (the game's thread, at the hook): the open job O and the slot N would take.
struct SecondView
{
  u32 workers = 1;            // WorkersFor now
  bool older_open = false;    // O is open, begun and has not ended
  bool newer_open = false;    // a second job is open already
  bool slot_idle = false;     // the free slot's thread has left its last job
  bool older_below = false;   // a correction below O's R was left to the chain (O's base is not the session's)
  bool after_images = false;  // O's log keeps after-images (KeepAfter at O's start)
  u32 from = 0;               // O's R
  u32 late = NONE;            // the first row of O that changed under a step O has run: N's c (NONE: none)
  u32 at = 0;                 // the step O's worker stands at (its log has saved every step to it)
  u32 taken = 0;              // O's steps below this have taken their rows (R + its ran rows)
  // the pace (20.2, "W2 backs off first"): the last complete window's median step (0: none yet), whether that window
  // was W's estimate barring its starts (no median), the catch-ups (and starts barred as foreseen ones) in it, and
  // "slowStepUs" (0: no pace bar)
  double window_ms = 0;
  bool window_barred = false;
  u64 window_behind = 0;
  u32 slow_us = 0;
};
// Why N does not start now, or nullptr: it may (the bars every job's start passes, the countdown, a finish, the
// guards, the depth bound with N's own span from c, are the caller's, after these).
const char* SecondBar(const SecondView& v);

// A job's state at a frame's hook, as the game's thread reads it.
struct JobView
{
  bool open = false;
  bool ends = false;   // it ends without an adoption in this frame (given up, not begun, the race's end checked,
                       // caught up, the switch to sync)
  bool ready = false;  // its worker is parked at the present with every step run: it may be adopted
};
// What a frame does with each job.
enum class Fate
{
  NONE,   // no job
  RUNS,   // it runs on (N parked first waits for O)
  ADOPT,  // adopted in this frame (O's first, then N's)
  FALL,   // ended without an adoption: one sync rollback from its R (or, for N with O open, below)
  STOP,   // N stopped with O (its base is O's): O's sync rollback covers it
  BACK,   // N closed while O runs on: its late row goes back to the chain (O's adoption is then provisional)
};
struct Plan
{
  Fate older = Fate::NONE, newer = Fate::NONE;
};
// The order (20.3) and the cascade (20.4): O first; N only once O is closed; O's end without an adoption stops N;
// N's end with O open sends its row back to the chain, with O adopted it is today's single-job path.
Plan Order(const JobView& older, const JobView& newer);

// O's adoption with N open is clean as far as N carries it: every row of O that differs from the session's now is at
// or above N's c (`first_stale`: the first, NONE when none) and no correction went below O's R. Its correction
// delay is then N's: no new Late, and the chain goes on in N.
bool CleanThroughNewer(u32 first_stale, u32 newer_from, bool older_below);

// A step the chain owes a rerun from (a forced rollback, tests, on a step a job has run): returned (the next job
// starts there) only when no job is left open after the frame's adoptions; else NONE, and it stays owed.
u32 OwedNow(u32 owed, bool any_open);
}  // namespace fzgx::rollback_worker::pair
