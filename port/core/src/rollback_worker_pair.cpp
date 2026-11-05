// The two rollback workers' rules (rollback_worker_pair.h).
// SPDX-License-Identifier: GPL-3.0-or-later

#include "rollback_worker_pair.h"

#include <string_view>

namespace fzgx::rollback_worker::pair
{
u32 WorkersFor(u32 key, const char* env, unsigned threads)
{
  if (key == 1 || key == 2)
    return key;
  if (env != nullptr && (std::string_view(env) == "1" || std::string_view(env) == "2"))
    return env[0] == '1' ? 1 : 2;
  return threads >= LEAST_THREADS ? 2 : 1;
}

const char* SecondBar(const SecondView& v)
{
  if (v.workers < 2)
    return "one worker on this PC";
  if (!v.older_open || v.newer_open)
    return "not one job open";
  if (v.late == NONE)
    return "no late row";
  if (v.late <= v.from)
    return "the late row at the job's start";  // (N would be O itself: the chain's)
  if (v.older_below)
    return "the open job's base is stale";
  if (v.late > v.at || v.late >= v.taken)
    return "the open job has not run past the late row";
  if (!v.after_images)
    return "the open job keeps no after-images";
  if (!v.slot_idle)
    return "the second slot not idle";
  if (v.slow_us != 0 && v.window_ms * 1000.0 >= v.slow_us)
    return "the pace: the last window at slowStepUs or slower";
  if (v.window_barred)
    return "the pace: W's estimate barred its starts";
  if (v.window_behind != 0)
    return "the pace: a catch-up in the last window";
  return nullptr;
}

Plan Order(const JobView& older, const JobView& newer)
{
  Plan plan;
  if (!older.open)
    return plan;
  if (older.ends)
  {
    plan.older = Fate::FALL;
    plan.newer = newer.open ? Fate::STOP : Fate::NONE;
    return plan;
  }
  if (older.ready)
  {
    plan.older = Fate::ADOPT;
    if (newer.open)
      plan.newer = newer.ends ? Fate::FALL : newer.ready ? Fate::ADOPT : Fate::RUNS;
    return plan;
  }
  plan.older = Fate::RUNS;
  if (newer.open)
    plan.newer = newer.ends ? Fate::BACK : Fate::RUNS;
  return plan;
}

bool CleanThroughNewer(u32 first_stale, u32 newer_from, bool older_below)
{
  return !older_below && (first_stale == NONE || first_stale >= newer_from);
}

u32 OwedNow(u32 owed, bool any_open)
{
  return any_open ? NONE : owed;
}
}  // namespace fzgx::rollback_worker::pair
