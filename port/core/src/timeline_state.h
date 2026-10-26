// The racer timeline's state in a playback's session (net_internal.h includes this; timeline.cpp says what it reads).
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "fzgx/host.h"

#include <array>

namespace fzgx::net
{
// What the front edge's steps showed of every racer so far, and the events.
struct TimelineState
{
  bool started = false;
  u32 racers = 0;
  u32 next = 0;                          // the next step to read (each is read once)
  std::array<u32, 30> flags{}, skulls{}, laps{};
  std::array<float, 30> distance{};      // over all laps; a racer out keeps the one it died with
  std::array<u32, 30> finish_ms{};
  u32 finished = 0;                      // racers finished so far
  int leader = -1, candidate = -1;       // the leader, and one who has led for less than the time a change needs
  u32 candidate_since = 0;
  u32 fastest_ms = 0xFFFFFFFF;
  // hits: each racer's partners in machine contact at the step before (bits); the step of the last rail hit and
  // of the last hit of each pair that was shown (the one-second rule: a repeat within REPEAT_STEPS of it is folded)
  std::array<u32, 30> touching{};
  std::array<u32, 30> rail_shown{};
  std::array<std::array<u32, 30>, 30> hit_shown{};
  u32 folded = 0;                        // hits folded by the rule
  Timeline line;                         // what the host is given
  u32 sent = 0xFFFFFFFF;                 // the version the host last had
  double us = 0, most_us = 0;            // reading the steps: the time in all, and the most for one
  u32 read = 0;
  bool said = false;
};
}  // namespace fzgx::net
