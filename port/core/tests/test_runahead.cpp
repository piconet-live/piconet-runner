// High refresh with no step ahead (runahead.cpp, AURORA_RUNAHEAD_AFTER, on by default with AURORA_REDRAW): the tick runs nothing of the
// game's, the request leaves at DRAW_ASK = 1 with the poses after the true step and the command stream's position the tick
// saw; none when no step ran since the tick (a pause, a held frame) or when the race's guards no longer hold there.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "harness.h"
#include "net_internal.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "fzgx/runahead.h"

using namespace fzgx;

namespace
{
void Float(Guest& g, u32 a, float f)
{
  u32 v;
  std::memcpy(&v, &f, 4);
  g.Write32(a, v);
}

float FloatAt(const Guest& g, u32 a)
{
  const u32 v = g.Read32(a);
  float f;
  std::memcpy(&f, &v, 4);
  return f;
}

constexpr u32 B = game::REL_BSS_AT;
constexpr u32 MACHINES = 0x80800000, OBJECT = 0x80810000, RECORDS = 0x80820000;
constexpr u32 POSE = 0x14C, PAUSE_WORD = B + 0x26B50;

// A race frame: main.rel, running, the race's phase, the chase camera of one viewport, two machines standing at x 0 and 1.
void ARace(Guest& g)
{
  g.Write32(net::MODULE_POINTER, layout::MAIN_REL);
  g.Write32(layout::CONTROL + 120, 1);
  g.Write16(game::PHASE_AT, 0x29);
  g.Write32(game::MACHINES_AT, MACHINES);
  g.Write8(game::MACHINE_COUNT_AT, 2);
  g.Write32(B + 0xF68, OBJECT);
  g.Write32(B + 0xF6C, RECORDS);
  g.Write8(OBJECT + 0x49, 1);
  for (u32 i = 0; i < 30; ++i)
    for (u32 k : {0u, 5u, 10u})
      Float(g, MACHINES + i * game::MACHINE_SIZE + POSE + 4 * k, 1.0f);
  Float(g, MACHINES + 1 * game::MACHINE_SIZE + POSE + 12, 1.0f);
  for (u32 k : {0u, 5u, 10u})
  {
    Float(g, RECORDS + 0xAC + 4 * k, 1.0f);
    Float(g, RECORDS + 0xDC + 4 * k, 1.0f);
  }
}
}  // namespace

void TestHighRefreshWithNoStepAhead()
{
  BEGIN_TEST();
  TestHost host;
  Init(host, Options{});
  net::s_session = std::make_unique<net::Session>();
  Guest g(host, Data());
  ARace(g);
  setenv("AURORA_RUNAHEAD", "240", 1);
  setenv("AURORA_REDRAW", "1", 1);
  u64 position = 500;
  u32 ran = 0;
  std::vector<runahead::RedrawRequest> asked;
  runahead::Start([](float) {}, [&](u32) { ++ran; }, [&](const runahead::RedrawRequest& r) { asked.push_back(r); },
                  [&] { return position; });
  CHECK(runahead::On());
  const auto frame = [&](u32 step, bool steps, bool tick = true) {
    g.Write32(net::STEP, step);
    const bool ahead = tick && runahead::Begin();
    CHECK(!ahead);  // nothing runs at the tick
    CHECK(!runahead::Inside());
    if (steps)
      g.Write32(net::STEP, step + 1);
  };
  // the first tick has no tick before it: no pictures
  frame(100, true);
  runahead::TimeTheDrawing(1);
  runahead::TimeTheDrawing(2);
  CHECK(asked.empty());
  // the next: the true step moves machine 1 from x 1 to 3, and the stream has moved on by its drawing's start
  frame(101, false);
  position = 520;
  Float(g, MACHINES + 1 * game::MACHINE_SIZE + POSE + 12, 3.0f);
  g.Write32(net::STEP, 102);
  runahead::TimeTheDrawing(1);
  CHECK(asked.size() == 1);
  if (asked.size() == 1)
  {
    const runahead::RedrawRequest& r = asked[0];
    CHECK(r.position == 500);  // the tick's: the request belongs to the copy just before it
    CHECK(r.machines == 3u);
    CHECK(r.pictures == 3);
    CHECK(r.picture[0].fraction == 0.25f && r.picture[2].fraction == 0.75f);
    // machine 0 stood: the camera's correction (no turn, no move: the identity); machine 1 a quarter of its move, x + 0.5
    CHECK(std::memcmp(r.picture[0].machine[0].data(), r.picture[0].camera.data(), sizeof(runahead::Matrix)) == 0);
    CHECK(r.picture[0].camera[0] == 1.0f && r.picture[0].camera[3] == 0.0f);
    CHECK(std::fabs(r.picture[0].machine[1][3] - 0.5f) < 1e-5f);
    CHECK(std::fabs(r.picture[2].machine[1][3] - 1.5f) < 1e-5f);
    CHECK(std::fabs(r.at[1][0] - 1.0f) < 1e-5f);  // its place in the true picture's view: where the tick saw it
  }
  CHECK(FloatAt(g, MACHINES + 1 * game::MACHINE_SIZE + POSE + 12) == 3.0f);  // nothing of the game's written
  runahead::TimeTheDrawing(1);  // (one request a tick)
  CHECK(asked.size() == 1);
  runahead::TimeTheDrawing(2);
  // a frame that ran no step (a pause pressed, a frame held): none
  frame(102, false);
  runahead::TimeTheDrawing(1);
  CHECK(asked.size() == 1);
  // a frame paused by its drawing: none
  frame(103, true);
  g.Write32(PAUSE_WORD, 1);
  runahead::TimeTheDrawing(1);
  CHECK(asked.size() == 1);
  g.Write32(PAUSE_WORD, 0);
  // a tick out of the race (here: its drawing skipped): DRAW_ASK = 1 asks nothing
  g.Write32(layout::DRAW_SKIP, 1);
  frame(105, true);
  runahead::TimeTheDrawing(1);
  CHECK(asked.size() == 1);
  g.Write32(layout::DRAW_SKIP, 0);
  // two steps in one frame (catching up): the pictures go to the picture it shows, still with the tick's position
  position = 900;
  frame(106, false);
  position = 950;
  g.Write32(net::STEP, 108);
  runahead::TimeTheDrawing(1);
  CHECK(asked.size() == 2);
  if (asked.size() == 2)
    CHECK(asked[1].position == 900);
  CHECK(ran == 0);  // the per-machine rollback is never told of a step run ahead
  const std::string report = runahead::Report();
  CHECK(report.find("2 requests at the drawing") != std::string::npos);
  CHECK(report.find("1 passes with no step since the tick, 1 out of the race by then") != std::string::npos);
  CHECK(report.find("0/1/2/3/more 1/2/1/0/0") != std::string::npos);
  CHECK(report.find("against the next tick: 1 compared, machines differed 0, camera differed 0") != std::string::npos);
  runahead::Stop();
  // AURORA_RUNAHEAD_AFTER=0: the step ahead again (here it finds no step wrapper in the image, and goes off), and none without REDRAW
  setenv("AURORA_RUNAHEAD_AFTER", "0", 1);
  net::s_session->watch = net::Watch::Drifting;  // (no input of a seat of this test's to send at the tick)
  runahead::Start([](float) {}, [&](u32) { ++ran; }, [&](const runahead::RedrawRequest& r) { asked.push_back(r); },
                  [&] { return position; });
  g.Write32(net::STEP, 200);
  CHECK(!runahead::Begin());
  g.Write32(net::STEP, 201);
  CHECK(!runahead::Begin());
  CHECK(!runahead::On());
  runahead::Stop();
  unsetenv("AURORA_RUNAHEAD_AFTER");
  unsetenv("AURORA_REDRAW");
  runahead::Start([](float) {}, [&](u32) { ++ran; }, [&](const runahead::RedrawRequest& r) { asked.push_back(r); },
                  [&] { return position; });
  g.Write32(net::STEP, 300);
  runahead::Begin();
  g.Write32(net::STEP, 301);
  CHECK(!runahead::Begin());
  CHECK(!runahead::On());
  runahead::Stop();
  unsetenv("AURORA_RUNAHEAD");
  unsetenv("AURORA_REDRAW");
  unsetenv("AURORA_RUNAHEAD_AFTER");
  runahead::Start(nullptr, nullptr, nullptr, nullptr);
  CHECK(!runahead::On());
  net::s_session.reset();
}

// Offline play (a plain start: no match, no replay, no session): high refresh's pictures between come in the game's own
// races by the game's words alone (MD_GAME, the race's phase, one screen, the chase camera; the race clock as the step),
// from the frame tick and DRAW_ASK the image makes there only with PLAIN_DRAW, which the core sets each field while the
// pictures between may be drawn (PlainField); with them, the game's screen option and true widescreen's word follow the
// launch file. Nothing of the game's simulation is written.
void TestHighRefreshInAPlainStart()
{
  BEGIN_TEST();
  TestHost host;
  Options options;
  CHECK(!ParseLaunchFile(R"({"about": "offline play", "widescreen": true, "trueWidescreen": true})", &options));
  CHECK(options.widescreen && options.true_widescreen);
  Init(host, options);
  CHECK(!Active());
  Guest g(host, Data());
  ARace(g);
  LinkMainRel(g);  // (main.rel at its pinned place: MODULE_POINTER is MAIN_REL again)
  g.Write32(layout::CONTROL + 120, 0);  // none of the image's race words
  g.Write16(game::GAME_MODE_AT, game::MODE_GAME);
  g.Write8(B + 0xF75, 1);
  // without high refresh: no PLAIN_DRAW; the screen option and true widescreen's word as the launch file says
  runahead::Start(nullptr, nullptr, nullptr, nullptr);
  g.Write32(MODULE + 0x229630, 0);  // the player's save said Normal
  PlainField();
  CHECK(g.Read32(layout::PLAIN_DRAW) == 0);
  CHECK(g.Read32(MODULE + 0x229630) == 1);
  float aspect;
  {
    const u32 v = g.Read32(layout::TRUE_ASPECT);
    std::memcpy(&aspect, &v, 4);
  }
  CHECK(aspect == 16.0f / 9.0f);
  runahead::Stop();
  setenv("AURORA_RUNAHEAD", "240", 1);
  setenv("AURORA_REDRAW", "1", 1);
  u64 position = 700;
  std::vector<runahead::RedrawRequest> asked;
  runahead::Start([](float) {}, [](u32) {}, [&](const runahead::RedrawRequest& r) { asked.push_back(r); },
                  [&] { return position; });
  PlainField();
  CHECK(g.Read32(layout::PLAIN_DRAW) == 1);
  const auto tick = [&](u32 clock) {
    g.Write32(game::RACE_CLOCK_AT, clock);
    CHECK(!runahead::Begin());  // nothing runs at the tick
  };
  tick(40);
  g.Write32(game::RACE_CLOCK_AT, 41);
  runahead::TimeTheDrawing(1);
  CHECK(asked.empty());  // no tick before the first
  tick(41);
  Float(g, MACHINES + 1 * game::MACHINE_SIZE + POSE + 12, 3.0f);
  g.Write32(game::RACE_CLOCK_AT, 42);
  runahead::TimeTheDrawing(1);
  CHECK(asked.size() == 1);
  if (asked.size() == 1)
    CHECK(asked[0].position == 700 && asked[0].pictures == 3 && std::fabs(asked[0].picture[0].machine[1][3] - 0.5f) < 1e-5f);
  runahead::TimeTheDrawing(2);
  // Versus with two screens, a pause, the menus: none
  for (int k = 0; k < 3; ++k)
  {
    tick(42 + k);
    if (k == 0)
      g.Write8(B + 0xF75, 2);
    else if (k == 1)
      g.Write32(PAUSE_WORD, 1);
    else
      g.Write16(game::GAME_MODE_AT, 1);
    g.Write32(game::RACE_CLOCK_AT, 43 + k);
    runahead::TimeTheDrawing(1);
    CHECK(asked.size() == 1);
    g.Write8(B + 0xF75, 1);
    g.Write32(PAUSE_WORD, 0);
    g.Write16(game::GAME_MODE_AT, game::MODE_GAME);
  }
  CHECK(FloatAt(g, MACHINES + 1 * game::MACHINE_SIZE + POSE + 12) == 3.0f);
  runahead::Stop();
  // the step ahead (AURORA_RUNAHEAD_AFTER=0) needs a race's undo log: no PLAIN_DRAW, nothing at the tick
  setenv("AURORA_RUNAHEAD_AFTER", "0", 1);
  runahead::Start([](float) {}, [](u32) {}, [&](const runahead::RedrawRequest& r) { asked.push_back(r); },
                  [&] { return position; });
  PlainField();
  CHECK(g.Read32(layout::PLAIN_DRAW) == 0);
  tick(60);
  tick(61);
  CHECK(!runahead::Inside());
  runahead::Stop();
  unsetenv("AURORA_RUNAHEAD_AFTER");
  unsetenv("AURORA_RUNAHEAD");
  unsetenv("AURORA_REDRAW");
  // Widescreen off in the launcher: Normal in memory every field, whatever the save says (a save the game wrote with
  // Wide Screen after the player changed an option in its own menu), and no true widescreen word
  Options off;
  CHECK(!ParseLaunchFile(R"({"about": "offline play", "widescreen": false})", &off));
  Init(host, off);
  runahead::Start(nullptr, nullptr, nullptr, nullptr);
  g.Write32(MODULE + 0x229630, 1);  // the save said Wide Screen
  g.Write32(layout::TRUE_ASPECT, 0x3FE38E39);  // (left from before)
  PlainField();
  CHECK(g.Read32(MODULE + 0x229630) == 0);
  CHECK(g.Read32(layout::TRUE_ASPECT) == 0);
  g.Write32(MODULE + 0x229630, 1);  // the game set it again (its menu): the next field puts it back
  PlainField();
  CHECK(g.Read32(MODULE + 0x229630) == 0);
  runahead::Stop();
  // the start that opens the game's own controller options keeps the save's option, either way, and no true
  // widescreen word
  Options setup;
  CHECK(!ParseLaunchFile(R"({"about": "the game's controller options", "controllerSetup": true})", &setup));
  Init(host, setup);
  runahead::Start(nullptr, nullptr, nullptr, nullptr);
  for (u32 saved : {0u, 1u})
  {
    g.Write32(MODULE + 0x229630, saved);
    PlainField();
    CHECK(g.Read32(MODULE + 0x229630) == saved);
    CHECK(g.Read32(layout::TRUE_ASPECT) == 0);
  }
  runahead::Stop();
}
