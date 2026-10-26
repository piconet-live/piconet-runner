// Whether the player's controllers may rumble (rumble.cpp RumbleWanted): the launch file's "rumble", and only while
// the player drives a seat of their own (no rumble while spectating).
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

void TestTheRumbleKey();  // test_launch.cpp: the launch file's key

namespace
{
void TestRumbleOnlyWhileThePlayerDrives()
{
  const Options on;
  CHECK(on.rumble);  // a file without the key: on
  Options off;
  off.rumble = false;
  // a plain start (the menus, offline play) and a race before its session: as the switch says
  CHECK(net::RumbleWanted(on, nullptr, 0) && !net::RumbleWanted(off, nullptr, 0));
  {
    net::Session s;
    Racing(s);
    CHECK(net::RumbleWanted(on, &s, 100) && !net::RumbleWanted(off, &s, 100));
  }
  // a practice drives too
  {
    net::Session s;
    Racing(s);
    s.practice = true;
    CHECK(net::RumbleWanted(on, &s, 100));
  }
  // a spectator's start and a playback never, from the launch file on, before any session
  Options spectator;
  spectator.spectate = true;
  Options playback;
  playback.play_file = "r.fzrp";
  CHECK(!net::RumbleWanted(spectator, nullptr, 0) && !net::RumbleWanted(playback, nullptr, 0));
  // watching: none of these drives
  const std::function<void(net::Session&)> watching[] = {
      [](net::Session& s) { s.watcher = true; },                // a spectator
      [](net::Session& s) { s.playback = true; },               // a replay
      [](net::Session& s) { s.watch = net::Watch::Drifting; },  // out, or over the line
      [](net::Session& s) { s.watch = net::Watch::Behind; },    // the same, behind live
      [](net::Session& s) { s.returning = true; },              // started again: the AI drives until the hand-back
      [](net::Session& s) { s.own_from = net::NEVER; },         // the seat away
      [](net::Session& s) { s.own_from = 200; },                // given back from step 200: not yet at 100
  };
  for (const auto& change : watching)
  {
    net::Session s;
    Racing(s);
    change(s);
    CHECK(!net::RumbleWanted(on, &s, 100));
  }
  // given back: the player drives again from that step
  net::Session back;
  Racing(back);
  back.own_from = 200;
  CHECK(!net::RumbleWanted(on, &back, 199) && net::RumbleWanted(on, &back, 200));
}
}  // namespace

void TestRumble()
{
  TestTheRumbleKey();
  TestRumbleOnlyWhileThePlayerDrives();
}
