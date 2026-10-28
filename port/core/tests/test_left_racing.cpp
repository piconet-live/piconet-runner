// The exit decision when the game stops by no stop of its own (rejoin.cpp LeftWhileRacing): EXIT_LEFT_RACING only
// while the player's seat still races; every other close is a normal leave.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "harness.h"
#include "net_internal.h"

#include <functional>

namespace
{
bool Says(const std::string& text, const std::string& part)
{
  return text.find(part) != std::string::npos;
}

// A racer's session that had the relay's go and races.
void Racing(net::Session& s)
{
  s.phase = net::Phase::Running;
  s.go = true;
  s.my_seat = 0;
  s.my_index = 0;
  s.watch = net::Watch::Racing;
}
}  // namespace

void TestTheExitWhenTheGameStopsWhileItsSeatRaces()
{
  TestHost host;
  Init(host, Options{});
  {
    net::Session s;
    Racing(s);
    CHECK(net::LeftWhileRacing(s));
  }
  // each of these is a normal leave: the process ends with 0, and the launcher tells the server the race is over
  const std::function<void(net::Session&)> normal[] = {
      [](net::Session& s) { s.phase = net::Phase::Joining; },   // before the relay answered
      [](net::Session& s) { s.phase = net::Phase::Joined; },    // loading, before the go
      [](net::Session& s) { s.phase = net::Phase::Failed; },    // the relay gone, a refused stat pack, the race over
      [](net::Session& s) { s.watcher = true; },                // a spectator
      [](net::Session& s) { s.playback = true; },               // a replay
      [](net::Session& s) { s.practice = true; },               // a practice
      [](net::Session& s) { s.my_index = -1; },                 // no seat of its own
      [](net::Session& s) { s.exit_sent = true; },              // the game's own stop: the results left, a desync
      [](net::Session& s) { s.desyncs = 1; },                   // desynced: never rejoined
      [](net::Session& s) { s.watch = net::Watch::Drifting; },  // out, or over the line: watching
      [](net::Session& s) { s.watch = net::Watch::Behind; },    // the same, behind live
      [](net::Session& s) { s.settled = true; },                // the race's end checked: the results
      [](net::Session& s) { s.overs_sent = 1; },                // the image's race-over test said "over"
  };
  for (const auto& change : normal)
  {
    net::Session s;
    Racing(s);
    change(s);
    CHECK(!net::LeftWhileRacing(s));
  }
  Shutdown();
  Init(host, Options{});
}

// A racer's game leaves every racer's results page by itself 30 s after it opened (session.cpp
// LeaveTheResultsInTime): A pressed for the player through the image (+0xA34), whose results then ask to leave as
// after a player's A; a stop of its own only if that A was not taken. Never in a playback, a practice or a spectator.
void TestTheResultsAreLeftThirtySecondsAfterEveryRacersPage()
{
  TestHost host;
  auto now = std::chrono::steady_clock::time_point{} + std::chrono::hours(1);
  host.time = now;
  Init(host, Options{});
  Guest g(host, Data());
  LinkMainRel(g);
  const auto page = [&](u8 state) {
    g.Write16(MODULE + net::PHASE, 0x2A);
    g.Write8(MODULE + net::BSS + 0x8E51D, state);
  };
  const auto at = [&](std::chrono::milliseconds t) { *host.time = now + t; };
  {
    net::Session s;
    Racing(s);
    page(0);  // the race's own results page: nothing is timed yet
    net::SayResultsLeavable(g, s);
    at(std::chrono::seconds(100));
    net::LeaveTheResultsInTime(g, s);
    CHECK(!s.leavable_said && g.Read32(net::PRESS_A) == 0 && host.stops == 0);
    page(7);  // every racer's page, seen at 100 s
    net::SayResultsLeavable(g, s);
    CHECK(s.leavable_said && Says(host.notices, "the results can be left from step"));
    at(std::chrono::milliseconds(129'999));
    net::LeaveTheResultsInTime(g, s);
    CHECK(g.Read32(net::PRESS_A) == 0 && !s.leave_pressed);
    at(std::chrono::seconds(130));
    net::LeaveTheResultsInTime(g, s);
    CHECK(g.Read32(net::PRESS_A) == 1 && s.leave_pressed && host.stops == 0 && !s.exit_sent);
    CHECK(Says(host.notices, "fzgx: every racer's page stood 30 s: A pressed for the player"));
    // the press is made once: the image writes 0 after its frame, and nothing presses again
    g.Write32(net::PRESS_A, 0);
    at(std::chrono::milliseconds(132'000));
    net::LeaveTheResultsInTime(g, s);
    CHECK(g.Read32(net::PRESS_A) == 0 && host.stops == 0);
    // the game did not take it: the stop comes 3 s after the press, once
    at(std::chrono::seconds(133));
    net::LeaveTheResultsInTime(g, s);
    net::LeaveTheResultsInTime(g, s);
    CHECK(s.exit_sent && host.stops == 1 && Says(host.notices, "the results were not left after the A pressed"));
  }
  // a playback, a practice and a spectator wait for no one: no press, no stop
  for (const auto& other : {+[](net::Session& s) { s.playback = true; }, +[](net::Session& s) { s.practice = true; },
                            +[](net::Session& s) { s.watcher = true; }})
  {
    net::Session s;
    Racing(s);
    other(s);
    at(std::chrono::seconds(200));
    net::SayResultsLeavable(g, s);
    at(std::chrono::seconds(300));
    net::LeaveTheResultsInTime(g, s);
    CHECK(g.Read32(net::PRESS_A) == 0 && !s.exit_sent);
  }
  // a player who left before the 30 s: nothing more
  {
    net::Session s;
    Racing(s);
    at(std::chrono::seconds(400));
    net::SayResultsLeavable(g, s);
    s.exit_sent = true;
    at(std::chrono::seconds(500));
    net::LeaveTheResultsInTime(g, s);
    CHECK(g.Read32(net::PRESS_A) == 0);
  }
  Shutdown();
  Init(host, Options{});
}
