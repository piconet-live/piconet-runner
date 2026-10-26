// The replay scrubber's lines at a playback's end, the memory it holds, and its test script's reader (scrub.cpp has
// the scrubber itself).
// SPDX-License-Identifier: GPL-3.0-or-later

#include "net_internal.h"

#include <algorithm>
#include <format>
#include <string>
#include <utility>
#include <vector>

namespace fzgx::net
{
namespace
{
// Steps as ranges, for a line: " (steps 10, 12-15)"; empty for none.
std::string Ranges(std::vector<u32> steps)
{
  std::sort(steps.begin(), steps.end());
  steps.erase(std::unique(steps.begin(), steps.end()), steps.end());
  std::string text;
  u32 ranges = 0;
  for (std::size_t i = 0; i < steps.size();)
  {
    std::size_t j = i;
    while (j + 1 < steps.size() && steps[j + 1] == steps[j] + 1)
      ++j;
    if (++ranges <= 24)
      text += (text.empty() ? "" : ", ") + (i == j ? std::format("{}", steps[i]) : std::format("{}-{}", steps[i], steps[j]));
    i = j + 1;
  }
  if (ranges > 24)
    text += std::format(", and {} more ranges", ranges - 24);
  return text.empty() ? text : " (steps " + text + ")";
}
}  // namespace

void ReadScrubScript(ScrubState& a)
{
  a.script_read = true;
  for (const std::string& entry : SplitString(TheOptions().debug.replay_script, ','))
  {
    const std::vector<std::string> parts = SplitString(entry, ':');
    u32 frame = 0, n = 0;
    if (entry.empty())
      continue;
    if (parts.size() < 2 || parts.size() > 3 || parts[1].size() != 1 || !TryParse(parts[0], &frame) ||
        (parts.size() == 3 && !TryParse(parts[2], &n)))
      Warn("fzgx: scrubber: the script's entry \"{}\" is not FRAME:WHAT[:N]: left out", entry);
    else
      a.script.push_back({frame, static_cast<u32>(parts[1][0]), n});
  }
}

std::size_t ScrubBytes(const Session& s)
{
  const ScrubState& a = s.scrub;
  return s.front.LogBytes() + s.front.PackedUnits().bytes + s.front.KeyBytes() + s.rollback.LogBytes() +
         2 * s.rollback.TrackedBytes() + a.other.ram.size() + a.other.scratch.size() + a.base.ram.size() +
         a.base.scratch.size();
}

void ScrubSummary(Session& s)
{
  ScrubState& a = s.scrub;
  if (!s.playback || !TheOptions().replay_scrubber || a.said || a.frames == 0)
    return;
  a.said = true;
  const auto rate = [&a](int kind) { return a.kind_ms[kind] > 0 ? a.kind_steps[kind] * 1000.0 / a.kind_ms[kind] : 0.0; };
  const auto per = [&a](int kind) {
    return a.kind_frames[kind] ? static_cast<double>(a.kind_steps[kind]) / a.kind_frames[kind] : 0.0;
  };
  Notice("fzgx: scrubber: steps buffered a second: {:.0f} while playing ({:.1f} a frame over {} frames), {:.0f} while "
         "the picture stood ({:.1f} a frame over {} frames); {} front steps in all, the front edge {} at step {}",
         rate(0), per(0), a.kind_frames[0], rate(2), per(2), a.kind_frames[2], a.front_total,
         a.front_done ? "stood" : "was", a.front);
  Notice("fzgx: scrubber: exchanging the two moments took {:.0f} us a frame (both ways; {:.0f} us at most one way), "
         "keeping the front's log {:.0f} us a step; a front step took {:.2f} ms on the host and {:.0f} ticks of "
         "emulated time ({:.1f} % of a field)",
         a.swaps ? a.swap_us * 2 / a.swaps : 0.0, a.swap_most_us, a.front_total ? a.log_us / a.front_total : 0.0,
         a.step_ms, a.step_ticks, a.step_ticks * 100 / FIELD_TICKS);
  Notice("fzgx: scrubber: frames longer than a field and a tenth: {} of {} playing beside front steps (longest {:.1f} "
         "ms, mean {:.2f} ms; {} of them ran front steps, and {} frames ran none while buffering), {} of {} playing "
         "alone (longest {:.1f} ms, mean {:.2f} ms)",
         a.kind_long[0], a.kind_frames[0], a.kind_longest_ms[0], a.kind_frames[0] ? a.kind_ms[0] / a.kind_frames[0] : 0.0,
         a.long_with, a.frames_without, a.kind_long[1], a.kind_frames[1], a.kind_longest_ms[1],
         a.kind_frames[1] ? a.kind_ms[1] / a.kind_frames[1] : 0.0);
  Notice("fzgx: scrubber: a frame's own work (its play step and drawing) took {:.2f} ms on the host and {:.0f} "
         "ticks of emulated time ({:.1f} % of a field) at the end, smoothed{}", a.own_ms, a.own_ticks,
         a.own_ticks * 100 / FIELD_TICKS,
         a.own_clocked ? "" : " (the host gives no clocks: its waits counted as work)");
  Notice("fzgx: scrubber: checksums: {} of {} front steps differed from the recording{}; {} of {} steps run at the "
         "play position differed{}",
         a.front_differed.size(), a.front_total, Ranges(a.front_differed), a.play_differed.size(), a.play_checked,
         Ranges(a.play_differed));
  const u32 jumps = a.jumps_front + a.jumps_key;
  Notice("fzgx: scrubber: {} moves from a key and {} from the front edge ({:.2f} ms on average, {:.2f} ms at most, "
         "over {} steps of the front's log at most), {} moves by the logs alone, {} frames cut at the race's end",
         a.jumps_key, a.jumps_front, jumps ? a.jump_ms / jumps : 0.0, a.jump_most_ms, a.jump_steps_most,
         a.jumps_play, a.cuts);
  Notice("fzgx: scrubber: frame steps: {} on, {} back ({} of them from a key or the front edge); moving the memory "
         "for a step back took {:.2f} ms on average, {:.2f} ms at most; {} changes of the racer watched",
         a.frames_ahead, a.frames_back, a.frames_back_keyed, a.frames_back ? a.frame_back_ms / a.frames_back : 0.0,
         a.frame_back_most_ms, a.racer_changes);
  const ScrubCount& all = a.all;
  Notice("fzgx: scrubber: {} scrubs: {} frames held, {} of them showed another step ({:.1f} pictures a second over "
         "{:.0f} ms), {} with the pointer beyond what can be shown; moving the memory took {:.2f} ms a frame, {:.2f} "
         "ms at most",
         all.scrubs, all.frames, all.pictures, all.ms > 0 ? all.pictures * 1000.0 / all.ms : 0.0, all.ms, all.clamped,
         all.frames ? all.move_ms / all.frames : 0.0, all.move_most_ms);
  ScrubMemorySummary(s);
}

void ScrubMemorySummary(Session& s)
{
  constexpr double MIB = 1048576.0;
  const ScrubState& a = s.scrub;
  if (u64 m[4] = {}; TheHost().ProcessMemory(m))
  {
    Notice("fzgx: scrubber: the process holds {:.0f} MiB of its own ({:.0f} MiB at most) and has {:.0f} MiB in use "
           "({:.0f} MiB at most)", m[0] / MIB, m[1] / MIB, m[2] / MIB, m[3] / MIB);
  }
  const Rollback::Held front = s.front.Holds();
  const Rollback::Packed packed = s.front.PackedUnits();
  Notice("fzgx: scrubber: the front's log holds {:.1f} MiB unpacked ({} steps) and {:.1f} MiB packed for {:.1f} MiB "
         "of content ({:.2f} times less) in {} units of {} steps, {} dropped; the keys, one every {} steps, {:.1f} "
         "MiB ({} keys dropped); the play position's log {:.1f} MiB at most; the replay held {:.1f} MiB at most in all "
         "(its budget {} MiB; {} frames over it with nothing left that may go); {} steps of the play position were "
         "run again from a key in {} moves",
         front.allocated / MIB, front.steps, packed.bytes / MIB, packed.content / MIB,
         packed.bytes ? static_cast<double>(packed.content) / packed.bytes : 0.0, packed.units, UNIT, packed.dropped,
         KEY_EVERY, s.front.KeyBytes() / MIB, packed.keys_dropped, a.play_bytes / MIB, a.replay_most / MIB,
         TheOptions().debug.replay_memory, a.over_budget, a.catch_steps, a.catch_frames);
}
}  // namespace fzgx::net
