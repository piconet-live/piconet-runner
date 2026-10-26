// The observer HUD's numbers (fzgx/observer.h): the front's clock, the gaps to the leader (time, laps, out, a
// finish), the race's lap and the racers still racing, and the texts the tower and the card draw.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "harness.h"

#include "fzgx/observer.h"

#include <cmath>
#include <string>

using namespace fzgx::observer;

namespace
{
std::string Text(const Gap& gap)
{
  char words[24];
  GapText(gap, words, sizeof(words));
  return words;
}

std::string Clock(u32 ms)
{
  char words[24];
  ClockText(ms, words, sizeof(words));
  return words;
}

bool Near(float a, float b)
{
  return std::fabs(a - b) < 0.01f;
}

// a front that moves 10 m a step from step 100
FrontClock Steady(u32 to)
{
  FrontClock c;
  for (u32 step = 100; step <= to; ++step)
    c.Record(step, 10.0f * static_cast<float>(step - 100));
  return c;
}
}  // namespace

void TestTheFrontsClock()
{
  BEGIN_TEST();
  FrontClock c = Steady(199);
  CHECK(Near(c.At(150), 500) && Near(c.At(199), 990) && c.At(99) == NEVER && Near(c.At(400), 990));
  // a racer at 500 m at step 199: the front was there at step 150
  CHECK(Near(c.Behind(199, 500), 49));
  CHECK(Near(c.Behind(199, 505), 48.5f));  // between two steps
  CHECK(c.Behind(199, 990) == 0 && c.Behind(199, 2000) == 0);  // at or beyond the front: none
  CHECK(c.Behind(199, -5) == NEVER && c.Behind(50, 10) == NEVER);  // before the clock began
  // asked at an earlier step (a scrubber moved back): only what the front had done by then
  CHECK(Near(c.Behind(160, 500), 10) && c.Behind(140, 500) == 0);
  // steps not shown: a straight line between the two shown
  FrontClock gaps;
  gaps.Record(200, 1000);
  gaps.Record(210, 1100);
  CHECK(Near(gaps.At(205), 1050) && Near(gaps.Behind(210, 1050), 5));
  // the front never goes back, and a step shown again (the scrubber) keeps the later ones in order
  gaps.Record(211, 1090);
  CHECK(Near(gaps.At(211), 1100));
  gaps.Record(205, 1200);
  CHECK(Near(gaps.At(205), 1200) && Near(gaps.At(210), 1200) && Near(gaps.At(211), 1200));
  // shown again from before its first step (the race begun again): it starts there
  gaps.Record(150, 7);
  CHECK(Near(gaps.At(150), 7) && gaps.At(149) == NEVER && Near(gaps.At(200), 7));
  gaps.Clear();
  CHECK(gaps.Empty());
}

void TestTheGapsToTheLeader()
{
  BEGIN_TEST();
  const FrontClock c = Steady(399);  // the front at 2,990 m at step 399
  Standing leader{.present = true, .rank = 0, .distance = 2990, .lap_length = 2000, .speed = 2160};
  Standing second{.present = true, .rank = 1, .distance = 2690, .lap_length = 2000, .speed = 2160};
  CHECK(GapOf(leader, c, 399, 0).kind == GapKind::Leader);
  // 300 m behind a front that does 10 m a step: 30 steps, half a second
  Gap g = GapOf(second, c, 399, 0);
  CHECK(g.kind == GapKind::Time && g.ms == 500 && Text(g) == "+0.5");
  // a lap or more behind the front: lapped
  Standing slow = second;
  slow.rank = 5;
  slow.distance = 900;
  g = GapOf(slow, c, 399, 0);
  CHECK(g.kind == GapKind::Laps && g.laps == 1 && Text(g) == "+1 LAP");
  // out: says so, whatever its distance
  Standing out = second;
  out.out = true;
  CHECK(GapOf(out, c, 399, 0).kind == GapKind::Out && Text(GapOf(out, c, 399, 0)) == "OUT");
  // finished: its time less the winner's
  Standing done = second;
  done.finished = true;
  done.finish_ms = 95'300;
  g = GapOf(done, c, 399, 93'100);
  CHECK(g.kind == GapKind::Finished && g.ms == 2'200 && Text(g) == "+2.2");
  // the clock does not reach back that far (a spectator who came in mid-race): the distance at its own speed
  FrontClock late;
  late.Record(1000, 5000);
  late.Record(1001, 5010);
  Standing behind{.present = true, .rank = 3, .distance = 4400, .lap_length = 8000, .speed = 1080};  // 300 m/s
  g = GapOf(behind, late, 1001, 0);
  CHECK(g.kind == GapKind::Time && g.ms == 2033);  // 610 m at 300 m/s
  // at rest, nothing to tell; absent, nothing at all
  behind.speed = 0;
  CHECK(GapOf(behind, late, 1001, 0).kind == GapKind::None);
  CHECK(GapOf(Standing{}, c, 399, 0).kind == GapKind::None && Text(Gap{}).empty());
}

void TestTheTowersHeadAndTexts()
{
  BEGIN_TEST();
  Standing field[4] = {
      {.present = true, .rank = 0},
      {.present = true, .finished = true, .rank = 1, .finish_ms = 120'500},
      {.present = true, .out = true, .rank = 2},
      {.present = true, .finished = true, .rank = 3, .finish_ms = 119'900},
  };
  CHECK(Racing(field, 4) == 1 && WinnerTime(field, 4) == 119'900 && WinnerTime(field, 1) == 0);
  CHECK(LapOf(1, 3) == 2 && LapOf(3, 3) == 3 && LapOf(7, 0) == 8);
  CHECK(Text({GapKind::Leader}) == "LEADER" && Text({GapKind::Laps, 0, 3}) == "+3 LAPS");
  CHECK(Text({GapKind::Time, 940}) == "+0.9" && Text({GapKind::Time, 12'340}) == "+12.3");
  CHECK(Text({GapKind::Time, 59'960}) == "+1:00.0" && Text({GapKind::Time, 62'440}) == "+1:02.4");
  CHECK(Clock(38'512) == "0'38\"512" && Clock(83'005) == "1'23\"005");
  // the machines by id, as the launcher names them
  CHECK(std::string(MachineName(0)) == "Red Gazelle" && std::string(MachineName(6)) == "Blue Falcon");
  CHECK(std::string(MachineName(29)) == "Black Bull" && std::string(MachineName(40)) == "Rainbow Phoenix");
  CHECK(std::string(MachineName(41)) == "Custom");
}
