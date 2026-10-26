// The observer HUD's numbers (fzgx/observer.h says what they are and why).
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fzgx/observer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace fzgx::observer
{
namespace
{
constexpr u32 MOST_STEPS = 1u << 20;  // a clock longer than this (about five hours) starts again
constexpr const char* MACHINES[41] = {
    "Red Gazelle", "White Cat", "Golden Fox", "Iron Tiger", "Fire Stingray", "Wild Goose", "Blue Falcon",
    "Deep Claw", "Great Star", "Little Wyvern", "Mad Wolf", "Super Piranha", "Death Anchor", "Astro Robin",
    "Big Fang", "Sonic Phantom", "Green Panther", "Hyper Speeder", "Space Angler", "King Meteor", "Queen Meteor",
    "Twin Noritta", "Night Thunder", "Wild Boar", "Blood Hawk", "Wonder Wasp", "Mighty Typhoon",
    "Mighty Hurricane", "Crazy Bear", "Black Bull",
    "Dark Schneider", "Fat Shark", "Cosmic Dolphin", "Pink Spider", "Magic Seagull", "Silver Rat", "Spark Moon",
    "Bunny Flash", "Groovy Taxi", "Rolling Turtle", "Rainbow Phoenix",
};
}  // namespace

const char* MachineName(u32 id)
{
  return id < 41 ? MACHINES[id] : "Custom";
}

void FrontClock::Clear()
{
  m_front.clear();
  m_first = 0;
}

void FrontClock::Record(u32 step, float front)
{
  if (m_front.empty() || step < m_first || step - m_first >= MOST_STEPS)
  {
    // the first step shown, or one before it (the race begun again): the clock starts there
    m_first = step;
    m_front.assign(1, front);
    return;
  }
  const std::size_t i = step - m_first;
  if (i >= m_front.size())
  {
    // the steps not shown between the last one recorded and this one: a straight line
    const std::size_t from = m_front.size() - 1;
    const float last = m_front.back(), to = std::max(front, last);
    for (std::size_t j = from + 1; j < i; ++j)
      m_front.push_back(last + (to - last) * static_cast<float>(j - from) / static_cast<float>(i - from));
    m_front.push_back(to);
    return;
  }
  m_front[i] = i > 0 ? std::max(m_front[i - 1], front) : front;
  for (std::size_t j = i + 1; j < m_front.size() && m_front[j] < m_front[j - 1]; ++j)
    m_front[j] = m_front[j - 1];
}

float FrontClock::At(u32 step) const
{
  if (m_front.empty() || step < m_first)
    return NEVER;
  return m_front[std::min<std::size_t>(step - m_first, m_front.size() - 1)];
}

float FrontClock::Behind(u32 step, float distance) const
{
  if (m_front.empty() || step < m_first)
    return NEVER;
  const std::size_t last = std::min<std::size_t>(step - m_first, m_front.size() - 1);
  if (distance >= m_front[last])
    return 0;
  const auto begin = m_front.begin();
  const std::size_t k = static_cast<std::size_t>(std::lower_bound(begin, begin + static_cast<std::ptrdiff_t>(last) + 1,
                                                                  distance) - begin);
  if (k == 0)
    return NEVER;  // the front was there before the clock began
  const float lo = m_front[k - 1], hi = m_front[k];
  const float at = static_cast<float>(k - 1) + (hi > lo ? (distance - lo) / (hi - lo) : 1.0f);
  return static_cast<float>(step - m_first) - at;
}

Gap GapOf(const Standing& racer, const FrontClock& clock, u32 step, u32 winner_ms)
{
  Gap gap;
  if (!racer.present)
    return gap;
  if (racer.out)
  {
    gap.kind = GapKind::Out;
    return gap;
  }
  if (racer.rank == 0)
  {
    gap.kind = GapKind::Leader;
    return gap;
  }
  if (racer.finished)
  {
    gap.kind = GapKind::Finished;
    gap.ms = winner_ms != 0 && racer.finish_ms > winner_ms ? racer.finish_ms - winner_ms : 0;
    return gap;
  }
  const float front = clock.At(step);
  if (front == NEVER)
    return gap;
  if (racer.lap_length > 0 && front - racer.distance >= racer.lap_length)
  {
    gap.kind = GapKind::Laps;
    gap.laps = static_cast<u32>((front - racer.distance) / racer.lap_length);
    return gap;
  }
  float behind = clock.Behind(step, racer.distance);
  if (behind == NEVER)
  {
    // the clock does not reach back: the distance at the racer's own speed
    const float metres_a_second = racer.speed / 3.6f;
    if (metres_a_second < 1)
      return gap;
    behind = (front - racer.distance) / metres_a_second * STEPS_PER_SECOND;
  }
  gap.kind = GapKind::Time;
  gap.ms = static_cast<u32>(std::lround(std::max(behind, 0.0f) * 1000.0f / STEPS_PER_SECOND));
  return gap;
}

u32 WinnerTime(const Standing* racers, std::size_t count)
{
  u32 best = 0;
  for (std::size_t i = 0; i < count; ++i)
    if (racers[i].present && racers[i].finished && !racers[i].out && racers[i].finish_ms != 0 &&
        (best == 0 || racers[i].finish_ms < best))
      best = racers[i].finish_ms;
  return best;
}

u32 Racing(const Standing* racers, std::size_t count)
{
  u32 n = 0;
  for (std::size_t i = 0; i < count; ++i)
    n += racers[i].present && !racers[i].out && !racers[i].finished;
  return n;
}

u32 LapOf(u32 leader_laps_done, u32 laps)
{
  const u32 lap = leader_laps_done + 1;
  return laps != 0 ? std::min(lap, laps) : lap;
}

void GapText(const Gap& gap, char* out, std::size_t size)
{
  if (size == 0)
    return;
  out[0] = 0;
  switch (gap.kind)
  {
  case GapKind::None:
    return;
  case GapKind::Leader:
    std::snprintf(out, size, "LEADER");
    return;
  case GapKind::Out:
    std::snprintf(out, size, "OUT");
    return;
  case GapKind::Laps:
    std::snprintf(out, size, "+%u %s", gap.laps, gap.laps == 1 ? "LAP" : "LAPS");
    return;
  case GapKind::Time:
  case GapKind::Finished:
    break;
  }
  const u32 tenths = (gap.ms + 50) / 100;
  if (tenths < 600)
    std::snprintf(out, size, "+%u.%u", tenths / 10, tenths % 10);
  else
    std::snprintf(out, size, "+%u:%02u.%u", tenths / 600, tenths / 10 % 60, tenths % 10);
}

void ClockText(u32 ms, char* out, std::size_t size)
{
  if (size == 0)
    return;
  std::snprintf(out, size, "%u'%02u\"%03u", ms / 60000, ms / 1000 % 60, ms % 1000);
}
}  // namespace fzgx::observer
