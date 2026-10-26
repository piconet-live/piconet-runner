// A spectator's game against a relay that is the test itself, on the loopback: it joins with a spectator's token, never sends an input record, and watches the
// race behind live on official frames only.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "harness.h"

// The relay answers the Join with Joined's flag 4 (a spectator), seat 0 of the player seats 0 and 1 to watch first,
// says go and sends the official frames up to a few steps past the game's. The game sends acknowledgements only (an
// Input of no record), says it is a spectator, and watches the race phase in lockstep (no rollback, no prediction).
// When the relay later answers "over" (it dropped the race), the game stops.
void TestASpectatorSendsNoInputAndWatchesBehindLive()
{
  Udp relay;
  CHECK(relay.Open());
  Options options;
  options.debug.relay = "127.0.0.1:" + std::to_string(relay.LocalPort()) + ":1f2e3d4c5b6a7988";
  options.spectate = true;
  TestHost host;
  host.time = std::chrono::steady_clock::now();
  Init(host, options);
  FakeRace race(host, 10, 60, false);
  const auto put32 = [](std::string& p, u32 v) {
    for (int i = 3; i >= 0; --i)
      p += static_cast<char>(v >> (8 * i));
  };
  std::optional<Endpoint> game;
  u32 inputs = 0, records = 0;
  u32 frames = 0;
  for (; frames < 90; ++frames)
  {
    u8 buffer[512];
    std::size_t n = 0;
    Endpoint from;
    while (relay.Receive(buffer, sizeof(buffer), &n, &from))
    {
      game = from;
      if (n >= 29 && buffer[3] == 7)  // an Input: token, cookie, first, next official, count
      {
        ++inputs;
        records += buffer[28];
      }
    }
    if (game)
    {
      const u32 step = race.g.Read32(net::STEP);
      std::string joined = FromRelay('\x04');
      put32(joined, 1);
      joined += '\0';
      put32(joined, 3);
      joined += std::string(8, '\0');  // (no cookie)
      joined += '\x04';                // a spectator, the race not running yet
      put32(joined, 0);
      relay.Send(joined.data(), joined.size(), *game);
      std::string go = FromRelay('\x06');
      put32(go, 1);
      put32(go, 0);
      relay.Send(go.data(), go.size(), *game);
      for (u32 first = step > 2 ? step - 2 : 0; first < step + 14; first += 6)
      {
        std::string official = FromRelay('\x08');
        put32(official, first);
        official += static_cast<char>(6);
        put32(official, 3);
        official += std::string(6 * 12, static_cast<char>(first % 5));
        relay.Send(official.data(), official.size(), *game);
      }
    }
    if (!race.Frame())
      break;
  }
  CHECK(game.has_value() && host.stops == 0);
  CHECK(race.g.Read32(net::STEP) > 20);
  CHECK(inputs > 0);
  CHECK(records == 0);  // a spectator drives nothing
  CHECK(host.notices.find("fzgx: joined the race as a spectator, watching seat 0 of 2 player seats") != std::string::npos);
  CHECK(host.notices.find("a spectator: watching in lockstep on official frames") != std::string::npos);
  CHECK(host.notices.find("fzgx: rollback on") == std::string::npos);  // never a prediction
  // the relay dropped the race (every racer left): its answer to the spectator's Join says "over"; the game stops
  std::string over = FromRelay('\x04');
  put32(over, 1);
  over += '\0';
  put32(over, 3);
  over += std::string(8, '\0');
  over += '\x06';  // a spectator, over
  put32(over, 4660);
  relay.Send(over.data(), over.size(), *game);
  race.Frame();
  CHECK(host.stops == 1);
  CHECK(host.notices.find("fzgx: the relay has dropped the race this spectator watched (frame 4660); stopping") !=
        std::string::npos);
  Shutdown();
  Init(host, Options{});
}
