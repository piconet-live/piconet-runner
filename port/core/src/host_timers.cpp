// The scoped timers of a profiling build (host_timers.h). In every other build this file is empty.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "host_timers.h"

#ifdef FZGX_HOST_TIMERS

#include <algorithm>
#include <array>
#include <vector>

#include "util.h"

namespace fzgx::timers
{
namespace
{
std::array<Sum, Count> s_sums{};
constexpr const char* NAMES[] = {
    "hook",         "drain",        "checksum",    "frame-begin", "send-input",   "close-final", "rollback-begin",
    "predictions",  "run-steps",    "frame-end",   "extra-step",  "scrub-frame",  "scrub-extra", "undo-full",
    "undo-pool",    "undo-pool-wait", "undo-gather", "undo-watched", "undo-check", "undo-scratch", "undo-store",
    "restore",      "final"};
static_assert(std::size(NAMES) == Count, "a name for each part timed");
std::array<std::vector<u32>, 4> s_pages, s_scratch_pages;
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
  s_pages[kind].push_back(pages);
  s_scratch_pages[kind].push_back(scratch_pages);
}

Sum& At(Id id)
{
  return s_sums[id];
}

void Report(u64 frames, u64 steps)
{
  Notice("fzgx: host timers over {} displayed frames and {} steps (a profiling build)", frames, steps);
  for (int i = 0; i < Count; ++i)
  {
    const Sum& sum = s_sums[i];
    if (sum.calls == 0)
      continue;
    Notice("fzgx: host timer {}: {:.3f} ms in {} calls, {:.2f} us a call, {:.1f} us a frame", NAMES[i],
           sum.us / 1000.0, sum.calls, sum.us / static_cast<double>(sum.calls),
           frames != 0 ? sum.us / static_cast<double>(frames) : 0.0);
  }
  for (int k = 0; k < 4; ++k)
  {
    if (s_pages[k].empty())
      continue;
    Notice("fzgx: host pages changed, {}: main memory {}; locked cache {}", KINDS[k], Spread(s_pages[k]),
           Spread(s_scratch_pages[k]));
    s_pages[k].clear();
    s_scratch_pages[k].clear();
  }
  s_sums = {};
}
}  // namespace fzgx::timers

#endif
