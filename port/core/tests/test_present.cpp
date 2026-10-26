// A late frame's drawing skipped (present.cpp): the answer the image reads when its drawing is about to begin, by
// the time since the frame tick and the drawing's cost as timed; never two frames running; never outside a race
// frame or on course 41; the test keys; the answer taken back at the next tick.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "harness.h"
#include "net_internal.h"

using namespace fzgx;
using namespace std::chrono_literals;

namespace
{
struct Frames
{
  TestHost& host;
  Guest g;
  std::chrono::steady_clock::time_point t{};

  explicit Frames(TestHost& h) : host(h), g(h, Data()) { host.time = t; }
  void Move(std::chrono::microseconds d)
  {
    t += d;
    host.time = t;
  }
  // One frame: its tick, `race` from NormalStep, `steps` of time before the drawing, `draw` of drawing. The answer.
  bool Frame(bool race, std::chrono::microseconds steps, std::chrono::microseconds draw = 2000us)
  {
    net::PresentTick(g);
    if (race)
      net::PresentRaceFrame();
    Move(steps);
    WindowWritten(layout::DRAW_ASK, 1);
    const bool skip = g.Read32(layout::DRAW_SKIP) != 0;
    Move(skip ? 50us : draw);
    WindowWritten(layout::DRAW_ASK, 2);
    Move(1000us);
    return skip;
  }
};

void Start(TestHost& host, const Debug& debug, u32 course)
{
  Options options;
  options.debug = debug;
  Init(host, options);
  net::PresentEnd();
  Guest(host, Data()).Write32(layout::RACE + 4, course);
}
}  // namespace

void TestALateFramesDrawingIsSkipped()
{
  BEGIN_TEST();
  TestHost host;  // (one for the whole test: Init's shutdown of the start before speaks to the host before)
  {
    Start(host, Debug{}, 16);
    Frames f(host);
    // frames with room: drawn; the drawing timed at 2 ms
    for (int i = 0; i < 20; ++i)
      CHECK(!f.Frame(true, 8000us));
    // 14 ms of steps + 2 ms of drawing + the margin pass a field: skipped, and the picture held for the host
    CHECK(f.Frame(true, 14000us));
    // the answer is taken back at the next tick, before the next frame asks
    net::PresentTick(f.g);
    CHECK(f.g.Read32(layout::DRAW_SKIP) == 0);
    // never two frames running: the frame after a skip draws however late it is
    CHECK(f.Frame(true, 20000us));
    CHECK(!f.Frame(true, 20000us));
    CHECK(f.Frame(true, 20000us));
    // 13 ms of steps leave room for 2 ms of drawing: drawn
    CHECK(!f.Frame(true, 13000us));
    // a drawing that has grown to 5 ms: 11 ms of steps no longer leave room, once the estimate has followed it
    for (int i = 0; i < 40; ++i)
      CHECK(!f.Frame(true, 6000us, 5000us));
    CHECK(f.Frame(true, 11000us));
    // a frame that is no race frame (lockstep, the countdown, the results, the frame that settles the race) never
    // skips
    CHECK(!f.Frame(false, 30000us));
    CHECK(!f.Frame(false, 30000us));
    CHECK(host.notices.find("fzgx: drawing:") == std::string::npos);  // (a line every 1,800 race frames, and at the end)
    net::PresentEnd();
    CHECK(host.notices.find("fzgx: drawing: skipped in 4 of 66 race frames") != std::string::npos);
  }
  {
    // course 41: the drawing places scenery the race collides with; never skipped
    Start(host, Debug{}, 41);
    Frames f(host);
    for (int i = 0; i < 4; ++i)
      CHECK(!f.Frame(true, 30000us));
  }
  {
    // "skipDraws" 3: every third race frame, whatever its time, and late frames as well
    Debug every3;
    every3.skip_draws = 3;
    Start(host, every3, 16);
    Frames h(host);
    int skipped = 0;
    for (int i = 1; i <= 30; ++i)
    {
      const bool skip = h.Frame(true, 4000us);
      CHECK(skip == (i % 3 == 0));
      skipped += skip;
      h.Frame(false, 4000us);  // (lockstep frames do not count)
    }
    CHECK(skipped == 10);
  }
  net::PresentEnd();  // (its line goes to this test's host, which ends here: the next Init's shutdown has none to say)
  // the test key's range: refused by its name, never bent into it
  for (const char* n : {"0", "601"})
  {
    Options o;
    const auto problem = ParseLaunchFile(std::string(R"({"about": "x", "bundle": "b", "replay": {"file": "r.fzrp"},
                                                         "debug": {"skipDraws": )") + n + "}}", &o);
    CHECK(problem && problem->find("\"debug.skipDraws\" is not a whole number from 1 to 600") != std::string::npos);
  }
}
