// Practice with rewind (fzgx/practice.h): the stick's curve, the rolling buffer's reach and its keys, the save and
// its expiry, the branch on a resume, and the run kept for the replay through rewinds and loads.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "harness.h"

#include "fzgx/practice.h"

#include <cmath>
#include <map>

using namespace fzgx::practice;

namespace
{
bool Near(double a, double b)
{
  return std::abs(a - b) < 1e-9;
}

// frames with nothing pressed, the stick at `x`; the last frame's decision
Frame Hold(Rewind& r, u32 frames, int x = 0)
{
  Frame f;
  for (u32 i = 0; i < frames; ++i)
    f = r.Next({0, x});
  return f;
}

// a button pressed for one frame and let go the next
Frame Press(Rewind& r, u16 button)
{
  const Frame f = r.Next({button, 0});
  r.Next({0, 0});
  return f;
}

// the race at the front runs `steps` steps, a frame each
void Race(Rewind& r, u32 steps)
{
  for (u32 i = 0; i < steps; ++i)
  {
    const Frame f = r.Next({});
    CHECK(f.run && f.step == r.Front());
    r.Ran(f.step);
  }
}
}  // namespace

void TestTheStickScrubsOnACurve()
{
  CHECK(ScrubSpeed(0) == 0 && ScrubSpeed(STICK_DEAD) == 0 && ScrubSpeed(-STICK_DEAD) == 0);
  // just past the dead zone: about a quarter; half tilt: the race's own speed; full tilt and beyond: eight times
  CHECK(ScrubSpeed(STICK_DEAD + 1) > 0.25 && ScrubSpeed(STICK_DEAD + 1) < 0.3);
  const int half = STICK_DEAD + (STICK_FULL - STICK_DEAD) / 2;  // 28 (u = 0.4902...)
  CHECK(ScrubSpeed(half) > 0.95 && ScrubSpeed(half) <= 1.0);
  CHECK(Near(ScrubSpeed(STICK_FULL), 8.0) && Near(ScrubSpeed(127), 8.0) && Near(ScrubSpeed(-128), -8.0));
  // left goes back as fast as right goes on, and more tilt is never slower
  bool rises = true, even = true;
  for (int x = STICK_DEAD + 1; x <= 127; ++x)
  {
    rises = rises && ScrubSpeed(x) >= ScrubSpeed(x - 1);
    even = even && Near(ScrubSpeed(-x), -ScrubSpeed(x));
  }
  CHECK(rises && even);
  // the slow half is a third of the stick's travel or more: slow speeds are easy to hold
  int slow = 0;
  for (int x = 0; x <= STICK_FULL; ++x)
    slow += ScrubSpeed(x) > 0 && ScrubSpeed(x) <= 1.0 ? 1 : 0;
  CHECK(slow * 3 >= STICK_FULL);
}

void TestTheRollingBuffer()
{
  Rewind r(0);
  Race(r, 1000);
  CHECK(r.Front() == 1000 && r.Oldest() == 0 && r.TakeDropped().empty());
  Race(r, 4000);  // 5000 steps: the buffer holds the last minute
  CHECK(r.Front() == 5000 && r.Oldest() == 5000 - WINDOW && r.FinalBelow() == 1400);
  // keys before the one the oldest moment is reached from are let go, each once, in order
  const std::vector<u32> dropped = r.TakeDropped();
  CHECK(r.KeyFor(1400) == 1280 && dropped.size() == 10 && dropped.front() == 0 && dropped.back() == 1152);
  CHECK(r.TakeDropped().empty());
  CHECK(r.WantsKey(1280) && !r.WantsKey(1281) && r.KeyFor(1281) == 1280 && r.KeyFor(1279) == 1152);
  // paused, full tilt back for longer than a minute takes: the picture stops at the oldest moment, and forward at
  // the front
  CHECK(Press(r, BUTTON_START).step == 5000 && r.Now() == Mode::Paused && r.Count().pauses == 1);
  Frame f = Hold(r, 3600 / 8 - 1, -127);
  CHECK(!f.run && f.step == 5000 - (3600 / 8 - 1) * 8 && r.Front() == 5000);
  f = Hold(r, 30, -127);
  CHECK(!f.run && f.step == 1400 && r.Position() == r.Oldest());
  f = Hold(r, 1000, 127);
  CHECK(!f.run && f.step == 5000);
  // a quarter-speed scrub moves a step every fourth frame or so
  Hold(r, 1, -127);
  const u32 from = r.Position();
  Hold(r, 8, -(STICK_DEAD + 1));
  CHECK(r.Position() + 2 == from || r.Position() + 3 == from);
  // the bar: kept inside the buffer
  r.Seek(10);
  CHECK(r.Position() == 1400);
  r.Seek(4321);
  CHECK(r.Position() == 4321);
  r.Seek(9999);
  CHECK(r.Position() == 5000);
  // a step the game says it ran while paused is not the race's: counted, nothing moves
  r.Ran(5000);
  CHECK(r.Misses() == 1 && r.Front() == 5000);
  // a race shorter than a minute reaches back to its first step, not before it
  Rewind late(200);
  Race(late, 300);
  CHECK(late.Oldest() == 200 && late.KeyFor(100) == 200 && late.WantsKey(328) && !late.WantsKey(256));
}

void TestTheSaveAndItsExpiry()
{
  Rewind r(0);
  Race(r, 100);
  // D-pad left with no save does nothing
  CHECK(Press(r, BUTTON_LEFT).run && r.Now() == Mode::Racing && r.Count().loads == 0);
  Frame f = Press(r, BUTTON_RIGHT);
  CHECK(f.saved && r.Save() == 100u && r.Count().saves == 1);
  r.Ran(f.step);  // the frame that saved ran its step as usual: the save is the moment before it
  // a minute later it is still there; one step more and it is gone
  Race(r, WINDOW - 1);
  CHECK(r.Front() == 100 + WINDOW && r.Oldest() == 100 && r.Save() == 100u);
  Race(r, 1);
  CHECK(!r.Save() && r.Count().saves_expired == 1);
  CHECK(Press(r, BUTTON_LEFT).run && r.Count().loads == 0);
  // a save while paused marks the moment on screen
  Press(r, BUTTON_START);
  Hold(r, 30, -127);
  const u32 at = r.Position();
  CHECK(Press(r, BUTTON_RIGHT).saved && r.Save() == at && at < r.Front());
}

void TestABranchOnResume()
{
  Rewind r(0);
  Race(r, 2000);
  Press(r, BUTTON_RIGHT);  // a save at 2000, which the branch below drops
  r.Ran(2000);
  Race(r, 99);             // the front at 2100; keys 0 to 2048 taken
  Press(r, BUTTON_START);
  Hold(r, 50, -127);       // back 400 steps: 1700
  CHECK(r.Position() == 1700 && r.Now() == Mode::Paused);
  // Start: a second's countdown on the picture where it stands, then the race runs on from there
  Frame f = Press(r, BUTTON_START);
  CHECK(!f.run && r.Now() == Mode::Countdown && r.CountdownLeft() == COUNTDOWN - 2);
  f = Hold(r, COUNTDOWN - 3);
  CHECK(!f.run && f.step == 1700 && r.CountdownLeft() == 1 && r.Front() == 2100);
  f = r.Next({});
  CHECK(f.run && f.branch && f.step == 1700 && r.Front() == 1700 && r.Now() == Mode::Racing);
  CHECK(r.Count().branches == 1 && !r.Save() && r.Count().saves_dropped == 1);
  // keys after the branch point are let go (1792, 1920, 2048); 1664 still holds 1700's moment
  const std::vector<u32> dropped = r.TakeDropped();
  CHECK(dropped == (std::vector<u32>{1792, 1920, 2048}));
  CHECK(r.KeyFor(1700) == 1664);
  r.Ran(f.step);
  Race(r, 99);
  CHECK(r.Front() == 1800);
  // Start in the countdown stands again; a resume at the front is no branch
  Press(r, BUTTON_START);
  Press(r, BUTTON_START);
  CHECK(r.Now() == Mode::Countdown);
  Press(r, BUTTON_START);
  CHECK(r.Now() == Mode::Paused && r.CountdownLeft() == 0);
  Press(r, BUTTON_START);
  f = Hold(r, COUNTDOWN);
  CHECK(f.run && !f.branch && f.step == 1800 && r.Count().branches == 1);
}

void TestALoadRunsTheCountdown()
{
  Rewind r(0);
  Race(r, 500);
  Press(r, BUTTON_RIGHT);
  r.Ran(500);
  Race(r, 299);
  // D-pad left while racing: the saved moment at once, a second's countdown, then a branch there
  Frame f = r.Next({BUTTON_LEFT, 0});
  CHECK(f.loaded && !f.run && f.step == 500 && r.Now() == Mode::Countdown && r.CountdownLeft() == COUNTDOWN);
  f = Hold(r, COUNTDOWN);
  CHECK(f.run && f.branch && f.step == 500 && r.Front() == 500);
  // the save stays: loaded again and again
  CHECK(r.Save() == 500u);
  r.Ran(500);
  Race(r, 20);
  r.Next({BUTTON_LEFT, 0});
  Hold(r, 10);
  r.Next({BUTTON_LEFT, 0});  // again inside the countdown: the second starts over
  CHECK(r.CountdownLeft() == COUNTDOWN && r.Count().loads == 3);
  // a load while paused far back
  f = Hold(r, COUNTDOWN);
  r.Ran(f.step);
  Press(r, BUTTON_START);
  Hold(r, 30, -127);
  f = Press(r, BUTTON_LEFT);
  CHECK(f.loaded && r.Position() == 500);
}

namespace
{
// A race that is a function of its inputs: the checksum after a step from the one before and the step's row.
u32 Next(u32 before, const std::vector<u8>& row)
{
  u32 h = before ^ 0x9E3779B9u;
  for (u8 b : row)
    h = (h ^ b) * 16777619u;
  return h;
}
std::vector<u8> RowAt(u32 step, u32 run)
{
  return {static_cast<u8>(step), static_cast<u8>(step >> 8), static_cast<u8>(run), 0, 0, 0};
}
}  // namespace

// A session's bookkeeping: the race at the front, rewinds and loads, the replay written from the line. The replay
// played from the start gives every checksum the session kept; a step run again after a rewind with the same input
// gives the checksum it had the first time.
void TestTheRunKeptForTheReplay()
{
  BEGIN_TEST();
  Rewind r(0);
  Line line(0);
  std::map<u32, u32> state;  // the checksum after each step of the run as it stands (state[s]: after step s)
  std::vector<std::pair<u32, Line::Step>> written;
  u32 run = 0;
  std::map<u32, u32> first_time;  // a step's checksum the first time it ran with the first run's input
  bool same_again = true;
  u32 again = 0;
  const auto frame = [&](const Pad& pad) {
    const Frame f = r.Next(pad);
    if (f.branch)
    {
      line.Cut(f.step);
      ++run;
    }
    if (!f.run)
      return;
    // the run's input: the first run's for steps below 600 after the first rewind (the same inputs follow), else
    // this run's own
    const std::vector<u8> row = RowAt(f.step, f.step < 600 ? 0 : run);
    const u32 before = f.step == 0 ? 0 : line.At(f.step - 1)->checksum;
    const u32 after = Next(before, row);
    if (f.step < 600 && first_time.contains(f.step))
    {
      same_again = same_again && first_time[f.step] == after;
      ++again;
    }
    first_time.emplace(f.step, after);
    CHECK(line.Ran(f.step, row, after));
    r.Ran(f.step);
    for (auto& s : line.TakeFinal(r.FinalBelow()))
      written.push_back(std::move(s));
  };
  for (u32 i = 0; i < 700; ++i)
    frame({});
  // pause, back 200 steps, resume: steps 500 to 599 run again with the same inputs
  frame({BUTTON_START, 0});
  for (u32 i = 0; i < 25; ++i)
    frame({0, -127});
  frame({BUTTON_START, 0});
  for (u32 i = 0; i < COUNTDOWN + 400; ++i)
    frame({});
  // a save, more race, a load, more race: four times; then a minute and more so that much of it is final
  for (u32 k = 0; k < 4; ++k)
  {
    frame({BUTTON_RIGHT, 0});
    for (u32 i = 0; i < 300; ++i)
      frame({});
    frame({BUTTON_LEFT, 0});
    for (u32 i = 0; i < COUNTDOWN + 500; ++i)
      frame({});
  }
  for (u32 i = 0; i < WINDOW + 100; ++i)
    frame({});
  CHECK(r.Count().branches == 5 && r.Count().loads == 4 && again == 100 && same_again);
  CHECK(!written.empty() && written.back().first + 1 == r.FinalBelow());
  for (auto& s : line.TakeAll())
    written.push_back(std::move(s));
  // the replay: every step from 0 once, in order, up to the front; played from the start it gives every checksum
  bool unbroken = written.size() == r.Front(), plays = true;
  u32 checksum = 0;
  for (u32 i = 0; i < written.size(); ++i)
  {
    unbroken = unbroken && written[i].first == i;
    checksum = Next(checksum, written[i].second.row);
    plays = plays && checksum == written[i].second.checksum;
  }
  CHECK(unbroken && plays);
  // nothing handed to the recorder is changed or cut afterwards
  CHECK(!line.Ran(0, RowAt(0, 9), 0));
  line.Cut(0);
  CHECK(line.End() == line.Written());
  // a step run again without a cut first replaces it, and what followed it goes
  Line l(10);
  CHECK(l.Ran(10, RowAt(10, 0), 1) && l.Ran(11, RowAt(11, 0), 2) && l.Ran(12, RowAt(12, 0), 3) && !l.Ran(14, {}, 0));
  CHECK(l.Ran(11, RowAt(11, 1), 7) && l.End() == 12 && l.At(11)->checksum == 7 && l.At(12) == nullptr);
}

// The six above, in order (main.cpp).
void TestPracticeWithRewind()
{
  TestTheStickScrubsOnACurve();
  TestTheRollingBuffer();
  TestTheSaveAndItsExpiry();
  TestABranchOnResume();
  TestALoadRunsTheCountdown();
  TestTheRunKeptForTheReplay();
}
