// The exit decision when the game stops by no stop of its own (rejoin.cpp LeftWhileRacing): EXIT_LEFT_RACING only
// while the player's seat still races; every other close is a normal leave.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "harness.h"
#include "net_internal.h"

#include <functional>

namespace
{
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
