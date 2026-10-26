// A race recorded for the tests of a playback (test_recording.cpp, test_scrubber.cpp): the fake game run plainly,
// its inputs and every step's checksum, as a replay file holds them.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "harness.h"

namespace recorded
{
// The recorded race's steps, and where its race phase lies.
constexpr u32 STEPS = 420, RACE_FROM = 60, RACE_TO = 380;

enum class Game { Plain, Leaking, Scratch, ScratchKeptOut };

// One of the four games, and what follows from which it is.
struct Ways
{
  Game game;
  bool leak, with_scratch, fails;
};

// The recording: the game run plainly, its inputs and every step's checksum; `end_state`: the simulation's memory
// at its end.
inline std::string RecordTheRace(std::mt19937& rng, const Ways& ways, std::vector<u8>& end_state, u32 finish_goal = 0,
                                 u32 knock_out_at = 0)
{
  const auto [game, leak, with_scratch, fails] = ways;
  std::string steps;
  TestHost host;
  host.scratch.assign(with_scratch ? 0x4000 : 0, 0);
  Init(host, Options{});
  FakeRace race(host, RACE_FROM, RACE_TO, leak);
  race.finish_goal = finish_goal;
  race.knock_out_at = knock_out_at;
  for (u32 t = 0; t < STEPS; ++t)
  {
    u8 row[12];
    for (u8& b : row)
      b = static_cast<u8>(rng() % 5);   // inputs change every few steps
    net::Rollback::UseInputs(race.g, t, {0, 1}, row);
    race.Step(t);
    steps.append(reinterpret_cast<const char*>(row), 12);
    const u32 sum = net::Rollback::Checksum(race.g);
    for (int i = 3; i >= 0; --i)
      steps += static_cast<char>(sum >> (8 * i));
  }
  race.g.Write16(MODULE + net::BSS + 0x3C2A, 0x2A);  // the phase word as the frame routine leaves it after the race
  end_state.assign(host.ram.begin() + (race.first - layout::RAM), host.ram.begin() + (net::STATE_END - layout::RAM));
  Shutdown();
  return steps;
}
}  // namespace recorded
