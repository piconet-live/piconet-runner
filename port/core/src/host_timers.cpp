// The scoped timers of a profiling build (host_timers.h). In every other build this file is empty.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "host_timers.h"

#ifdef FZGX_HOST_TIMERS

#include <algorithm>
#include <array>
#include <vector>

#include "fzgx/rollback_worker.h"
#include "util.h"

namespace fzgx::timers
{
namespace
{
// The sums of each thread that times (study section 17, U5): the game's, and each rollback worker's (one a slot: W's
// and the second worker's, study section 21) on lines of their own (its SaveStep, RestoreTo, checksum and mode
// scopes), never added into the game's nor into each other's.
struct Sums
{
  std::array<Sum, Count> sums{};
  std::array<std::vector<u32>, 4> pages, scratch_pages;
};
Sums s_game, s_workers[2];
Sums& Mine()
{
  const int slot = rollback_worker::Slot();
  return slot >= 0 ? s_workers[slot & 1] : s_game;
}
constexpr const char* NAMES[] = {
    "hook",         "drain",        "checksum",    "frame-begin", "send-input",   "close-final", "rollback-begin",
    "predictions",  "run-steps",    "frame-end",   "extra-step",  "scrub-frame",  "scrub-extra", "undo-full",
    "undo-pool",    "undo-pool-wait", "undo-gather", "undo-watched", "undo-check", "undo-scratch", "undo-store",
    "restore",      "final",        "mode"};
static_assert(std::size(NAMES) == Count, "a name for each part timed");
constexpr std::array<const char*, 4> KINDS = {"learning", "a frame's own", "after a frame's own", "run again"};

std::string Spread(std::vector<u32> v)
{
  if (v.empty())
    return "none";
  std::sort(v.begin(), v.end());
  const auto at = [&](double q) { return v[std::min(v.size() - 1, static_cast<std::size_t>(q * v.size()))]; };
  return std::format("median {}, 90% {}, 99% {}, most {} ({} steps)", at(0.5), at(0.9), at(0.99), v.back(), v.size());
}
}  // namespace

void NotePages(int kind, u32 pages, u32 scratch_pages)
{
  Sums& m = Mine();
  m.pages[kind].push_back(pages);
  m.scratch_pages[kind].push_back(scratch_pages);
}

Sum& At(Id id)
{
  return Mine().sums[id];
}

void Report(u64 frames, u64 steps)
{
  Notice("fzgx: host timers over {} displayed frames and {} steps (a profiling build)", frames, steps);
  for (Sums* m : {&s_game, &s_workers[0], &s_workers[1]})
  {
    const char* whose = m == &s_game        ? ""
                        : m == &s_workers[0] ? " (the rollback worker's thread)"
                                             : " (the second worker's thread)";
    for (int i = 0; i < Count; ++i)
    {
      const Sum& sum = m->sums[i];
      if (sum.calls == 0)
        continue;
      Notice("fzgx: host timer {}{}: {:.3f} ms in {} calls, {:.2f} us a call, {:.1f} us a frame", NAMES[i], whose,
             sum.us / 1000.0, sum.calls, sum.us / static_cast<double>(sum.calls),
             frames != 0 ? sum.us / static_cast<double>(frames) : 0.0);
    }
    for (int k = 0; k < 4; ++k)
    {
      if (m->pages[k].empty())
        continue;
      Notice("fzgx: host pages changed{}, {}: main memory {}; locked cache {}", whose, KINDS[k], Spread(m->pages[k]),
             Spread(m->scratch_pages[k]));
      m->pages[k].clear();
      m->scratch_pages[k].clear();
    }
    m->sums = {};
  }
}
}  // namespace fzgx::timers

#endif
