// The rollback worker's pace (S4 (b), research/study-rollback-second-core.md sections 14, 15 and 19): the slow
// worker's switch and the trial of W's step estimate, for rollback_worker.cpp. Plain state and a clock passed in, so
// the tests drive it.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <vector>

#include "fzgx/guest.h"

namespace fzgx::rollback_worker::pace
{
// W's step time by the median of each window of at least WINDOW steps and WINDOW_S; STRIKES slow windows in a row
// with CATCH_UPS catch-ups in their span switch the race to sync, and the worker is tried again RETRY_S later
constexpr u32 WINDOW = 240, STRIKES = 3, CATCH_UPS = 2;
constexpr double WINDOW_S = 1.0, RETRY_S = 10.0;
constexpr double FIELD_MS = 1000.0 * 1001.0 / 60000.0;
constexpr double START_MS = 0.5;  // c of the study's section 4: a job's start and the thread's wake
constexpr double MARGIN_MS = 1.5;  // run-ahead's MARGIN_MS: room kept before the field (the rule "fit")
constexpr u32 FORESEEN_NEVER = 99;  // (an estimate at a frame or more: the job would never catch the game)

// A worker's step estimate after one more measured step (its first sets it)
inline double Smoothed(double r_ms, double ran_ms)
{
  return r_ms == 0 ? ran_ms : r_ms + (ran_ms - r_ms) / 8;
}

// j: the frames a job `d` steps deep is foreseen to take to its adoption at `r_ms` a step (study section 4), a step a
// frame
u32 Foreseen(u32 d, double r_ms);

// The rule "fit" (S2): a rollback `d` steps deep runs sync on the game's thread when the frame fits it (the frame's own
// cost lately `c_ms`, `d` steps at `r_ms`, MARGIN_MS before the field); a chain's next correction (`chained`: after an
// adoption on rows that changed since) as any other when `chain_fits` ("chained": "fit", S7), else on the worker
bool SyncFits(u32 d, double c_ms, double r_ms, bool chained, bool chain_fits);

// What a Watch asks of the worker
enum class Verdict
{
  NONE,
  SWITCH,  // the race's rollbacks run sync from now (W too slow): log it
  RETRY,   // the worker again (RETRY_S after the switch, or a new race): its step estimate starts afresh
  RESEED,  // no switch ("slowStepUs" 0), but W's estimate has barred its starts for RETRY_S: it starts afresh
};

// One Watch's measurements (the game's thread, at every frame)
struct Now
{
  double s = 0;             // the clock, in seconds
  u32 starts = 0;           // the game's log's Starts (a new count: a new race)
  u64 behind = 0;           // catch-ups, and starts barred as they would be caught up (barred late), so far
  u64 barred_late = 0;      // the starts barred by W's pace alone (its depth within "catchUp"), so far
  double estimate_ms = 0;   // W's step estimate as the start bar takes it (r_ms)
  u32 limit_us = 0;         // "slowStepUs" (0: no switch)
  u64 second_steps = 0;     // the second worker's steps (slot 1) taken into the windows, so far
};

struct Pace
{
  bool switched = false;
  u32 switched_starts = 0;
  double window_at = -1, switched_at = 0;
  u64 window_behind = 0, window_barred = 0;
  std::array<u64, STRIKES> strike_behind{};
  u32 strikes = 0;
  double barred_since = -1;  // the first of a run of barred windows (W ran too few steps; its starts barred late)
  // for the log
  u64 switches = 0, retries = 0, reseeds = 0, barred_windows = 0;
  double rate_high = 0;  // the slowest window's step (its median, or the estimate that barred it)
  double step_ms = 0;    // the last window's step (for a SWITCH's line)
  std::size_t step_n = 0;  // its steps (0: a barred window, the estimate's)
  bool new_race = false;   // (a RETRY's: a new race, not the time)
  u64 last_behind = 0;     // catch-ups (and starts barred late) in the last window that ended (the second worker's
                           // bar, study section 20.2)
  u64 window_second = 0;   // the second worker's steps at the window's start
  bool second_idle = false;  // this Watch ended a window (full, or barred) in which the second worker ran no step: its
                             // estimate, which moves only while it runs, is reseeded from W's (the window's median, or
                             // W's own estimate in a barred window); one slow step of slot 1 would otherwise bar every
                             // later start on slot 1 for good (study section 21)

  // At every frame. `window`: the worker's step times since the window's start (the caller holds the mutex that
  // guards it), emptied when a window ends. A window ends after WINDOW_S with WINDOW steps (their median is W's
  // step), or after WINDOW_S with fewer and a start barred late in it (W barred by its own estimate, which only moves
  // while W runs: the estimate is the window's step, so a worker kept from running still strikes and is switched,
  // and the retry gives it a fresh estimate). A window with neither goes on.
  Verdict Watch(const Now& now, std::vector<float>& window);
  // A window starts now (`window` emptied)
  void Restart(const Now& now, std::vector<float>& window);
};
}  // namespace fzgx::rollback_worker::pace
