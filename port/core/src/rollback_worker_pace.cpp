// The rollback worker's pace: the slow worker's switch and the trial of W's step estimate (rollback_worker_pace.h).
// SPDX-License-Identifier: GPL-3.0-or-later

#include "rollback_worker_pace.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace fzgx::rollback_worker::pace
{
void Pace::Restart(const Now& now, std::vector<float>& window)
{
  switched = false;
  strikes = 0;
  window_at = now.s;
  window_behind = now.behind;
  window_barred = now.barred_late;
  barred_since = -1;
  window.clear();
}

Verdict Pace::Watch(const Now& now, std::vector<float>& window)
{
  if (switched)
  {
    new_race = now.starts != switched_starts;
    if (!new_race && now.s - switched_at < RETRY_S)
      return Verdict::NONE;
    Restart(now, window);
    ++retries;
    return Verdict::RETRY;
  }
  if (window_at < 0)
  {
    Restart(now, window);
    return Verdict::NONE;
  }
  if (now.s - window_at < WINDOW_S)
    return Verdict::NONE;
  const bool full = window.size() >= WINDOW;
  // W ran too few steps for a median, and a start was barred by its pace: the estimate that bars it is its step (it
  // moves only while W runs, so a spike would otherwise keep W from ever running again, with no window and no switch)
  const bool barred = !full && now.barred_late > window_barred;
  if (!full && !barred)
    return Verdict::NONE;  // (a quiet window goes on until W has run its steps)
  if (full)
  {
    const auto mid = window.begin() + static_cast<std::ptrdiff_t>(window.size() / 2);
    std::nth_element(window.begin(), mid, window.end());
    step_ms = *mid;
    step_n = window.size();
    barred_since = -1;
  }
  else
  {
    step_ms = now.estimate_ms;
    step_n = 0;
    ++barred_windows;
    if (barred_since < 0)
      barred_since = window_at;
  }
  window.clear();
  const u64 at_start = std::exchange(window_behind, now.behind);
  window_barred = now.barred_late;
  window_at = now.s;
  rate_high = std::max(rate_high, step_ms);
  if (now.limit_us == 0)
  {
    // no switch: a worker barred by its estimate for RETRY_S gets a fresh one all the same
    strikes = 0;
    if (!barred || now.s - barred_since < RETRY_S)
      return Verdict::NONE;
    barred_since = -1;
    ++reseeds;
    return Verdict::RESEED;
  }
  if (step_ms * 1000.0 < now.limit_us)
  {
    strikes = 0;
    return Verdict::NONE;
  }
  // the run of slow windows: catch-ups (and starts barred late) at each one's start, the last STRIKES kept
  std::rotate(strike_behind.begin(), strike_behind.begin() + 1, strike_behind.end());
  strike_behind.back() = at_start;
  strikes = std::min(strikes + 1, STRIKES);
  if (strikes < STRIKES || now.behind - strike_behind.front() < CATCH_UPS)
    return Verdict::NONE;
  switched = true;
  switched_at = now.s;
  switched_starts = now.starts;
  strikes = 0;
  barred_since = -1;
  ++switches;
  return Verdict::SWITCH;
}
}  // namespace fzgx::rollback_worker::pace
