// The racer timeline (timeline.cpp): its hits, read after each front step from the machines' flags, never written.
//
//   Rail      flags gains 0x00800000 (fn_1_266A8 sets it when the push +0x1B0 is over 0.5 km/h a frame and
//             fn_1_25A14's wall query found a wall, with the wall hit count +0x213; the step fn_1_236CC clears it, with
//             0x01000000 and 0x02000000, before the contact task runs again), not out and not finished. The same pass
//             sets 0x01000000 alone for a push with no wall found: a machine landing on the road, which was counted
//             as a rail hit too by a first count; it is not one. A scrape that keeps the flag is one hit; it
//             counts again once it has let go. The event's detail: 1 the wall flag, 2 the other, 4 in the air the step
//             before, 8 in the air now (flags +0x0 bit 0x2).
//   Hit       flags gains 0x02000000 (fn_1_239C4 sets it on both machines when a contact's push was applied, with
//             0x400; the step clears it as above), for the pair that begins to touch: the other machine is the one
//             also flagged in that step whose "last machine approached" +0x4C1 it is, else the nearest flagged
//             (position +0x7C) within HIT_REACH. One event for the pair, on the lower seat; none when either is out
//             (a hit that KOs is the KO).
//   The one-second rule: a rail hit of a racer, or a hit of a pair, within REPEAT_STEPS of the last one of it that
//             was shown is folded into that one (its value counts them); other events in between change nothing.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "net_internal.h"

#include <array>
#include <bit>
#include <cmath>

namespace fzgx::net
{
namespace
{
constexpr u32 NONE = 0xFFFFFFFF;
constexpr u32 BLOCK = 0x620;
constexpr u32 RAIL_FLAGS = 0x01800000;
constexpr u32 WALL = 0x00800000;       // the wall query found a wall (fn_1_25A14 non-zero)
constexpr u32 CONTACT_FLAG = 0x02000000;
constexpr u32 AIRBORNE = 0x2;          // flags +0x0: the machine is in the air
constexpr u32 DESTROYED = 0x40000000;  // (destroying sets 0x00800000 too: fn_1_25004)
constexpr u32 REPEAT_STEPS = 60;       // one second
constexpr float HIT_REACH = 12.0f;     // m: two machines' contact radii are about 2 each

float F(Guest& g, u32 at)
{
  const float v = std::bit_cast<float>(g.Read32(at));
  return std::isfinite(v) ? v : 0.0f;
}

// A rail hit or a hit, by the one-second rule: shown, or folded into the last one of it shown.
void Emit(TimelineState& t, u32 step, u8 kind, u8 a, u8 b, u8 detail)
{
  u32& last = kind == TimelineEvent::Rail ? t.rail_shown[a] : t.hit_shown[a][b];
  if (last != NONE && step - last < REPEAT_STEPS)
  {
    std::vector<TimelineEvent>& events = t.line.events;
    for (auto it = events.rbegin(); it != events.rend() && it->step + REPEAT_STEPS > last; ++it)
      if (it->step == last && it->kind == kind && it->racer == a && it->other == b)
      {
        ++it->value;
        ++t.line.version;
        break;
      }
    ++t.folded;
    return;
  }
  last = step;
  TimelineAdd(t, {step, kind, a, b, detail, 0});
}

bool Racing(const TimelineState& t, u32 i)
{
  return t.line.out[i] == 0 && (t.flags[i] & (0x10000 | DESTROYED | 0x800)) == 0;
}

// The step's hits: rails by each racer's flags; machines by the pairs that began to touch.
}  // namespace

void TimelineHits(Guest& g, TimelineState& t, u32 machines, u32 step, const std::array<u32, 30>& before,
                  const std::array<u8, 30>& approached)
{
  for (u32 i = 0; i < t.racers; ++i)
  {
    // a rail hit is the wall flag: 0x01000000 alone is a push with no wall found (a landing's, the road's)
    const u32 rail = t.flags[i] & RAIL_FLAGS;
    if (rail == 0 || (before[i] & RAIL_FLAGS) != 0 || !Racing(t, i))
      continue;
    const u8 how = static_cast<u8>((rail & WALL ? 1 : 0) | (rail & 0x01000000 ? 2 : 0) | (before[i] & AIRBORNE ? 4 : 0) |
                                   (t.flags[i] & AIRBORNE ? 8 : 0));
    if ((rail & WALL) != 0)  // (0x01000000 alone: a push with no wall, a landing's or the road's)
      Emit(t, step, TimelineEvent::Rail, static_cast<u8>(i), 0xFF, how);
  }
  u32 flagged = 0;
  std::array<std::array<float, 3>, 30> at{};
  for (u32 i = 0; i < t.racers; ++i)
    if ((t.flags[i] & CONTACT_FLAG) != 0)
    {
      flagged |= 1u << i;
      for (u32 k = 0; k < 3; ++k)
        at[i][k] = F(g, machines + i * BLOCK + 0x7C + 4 * k);
    }
  std::array<u32, 30> now{};
  for (u32 a = 0; a < t.racers; ++a)
  {
    if ((flagged & (1u << a)) == 0)
      continue;
    u32 b = approached[a];
    if (b >= t.racers || b == a || (flagged & (1u << b)) == 0)
    {
      b = 0xFF;
      float best = HIT_REACH * HIT_REACH;
      for (u32 c = 0; c < t.racers; ++c)
      {
        if (c == a || (flagged & (1u << c)) == 0)
          continue;
        const float dx = at[a][0] - at[c][0], dy = at[a][1] - at[c][1], dz = at[a][2] - at[c][2];
        if (const float d = dx * dx + dy * dy + dz * dz; d < best)
        {
          best = d;
          b = c;
        }
      }
    }
    if (b == 0xFF)
      continue;
    now[a] |= 1u << b;
    now[b] |= 1u << a;
  }
  for (u32 a = 0; a < t.racers; ++a)
    for (u32 b = a + 1; b < t.racers; ++b)
      if ((now[a] & (1u << b)) != 0 && (t.touching[a] & (1u << b)) == 0 && Racing(t, a) && Racing(t, b))
        Emit(t, step, TimelineEvent::Hit, static_cast<u8>(a), static_cast<u8>(b), 0);
  t.touching = now;
}
}  // namespace fzgx::net
