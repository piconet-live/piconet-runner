// When steps run at a lower input delay: the paced race of test_pace.cpp (paced_race.h) with the player's setting at
// 0 and 1 beside the usual 2. (A paced race names its delay, 2 unless the test says otherwise: the test's session
// reads the delay from the seat slot, which a race of the tests' own relay leaves at zero, so a race that named
// none would run at a delay of 0 without saying so.)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "paced_race.h"

namespace
{
// A host of a schedule test: what its race's frames take (Pace::work).
struct PacedHost
{
  const char* what;
  std::vector<double> work;
  double least = 0;  // the least time to spare its inputs must reach the relay with, in frames
};

struct Counted
{
  u32 deepest = 0;   // the deepest rollback of the race
  u32 at_limit = 0;  // frames that added a catch-up step on top of a rollback as deep as the rule allows at this delay
  u32 beyond = 0;    // ... and on top of a deeper one
  u32 deeper = 0;    // frames whose rollback was deeper than the rule allows
};

// The depth the rule allows a catch-up step on top of: 6 at the delay of 2, and one more for each frame given up.
u32 Limit(u32 delay)
{
  return 6 + (2 - delay);
}

Counted Count(const Raced& r, u32 delay)
{
  Counted c;
  for (std::size_t i = 0; i < r.depth.size() && i < r.catch_up.size(); ++i)
  {
    const u32 depth = static_cast<u32>(r.depth[i]);
    c.deepest = std::max(c.deepest, depth);
    c.at_limit += r.catch_up[i] > 0 && depth == Limit(delay) ? 1 : 0;
    c.beyond += r.catch_up[i] > 0 && depth > Limit(delay) ? 1 : 0;
    c.deeper += depth > Limit(delay) ? 1 : 0;
  }
  return c;
}
}  // namespace

// A lower input delay (the player's setting: 0 or 1 for the usual 2). The game then runs one step further ahead of
// the relay's clock for each frame of delay given up, and its rollbacks are as much deeper. The rule that adds no
// catch-up step on top of a deep rollback counted 6 steps as deep whatever the delay.
// - On the hosts of TestTheGameKeepsToItsSchedule, with a relay next door: the game keeps to its schedule at every
//   delay, no input of the local seat is late, every final step's checksum is the official race's.
// - The rule's depth, pinned at each delay (6 at 2, 7 at 1, 8 at 0): with real players most frames have a wrong
//   prediction, and with a relay a round trip away every one of them is a deep rollback. Here the other seat's
//   input changes every step and the host never makes time up (55 and 50 frames a second), so the game must add a
//   step every tenth or fifth frame to stay on its schedule.
//   - Official frames four steps late: every rollback is exactly as deep as the rule allows at that delay. The
//     steps are added on top of them, and the inputs reach the relay with the time to spare the host has with a
//     relay next door. A limit one step lower adds none there: the game falls back a step and has less to spare.
//   - Five steps late: most rollbacks are one step deeper than the rule allows. No step is added on top of those
//     (a limit one step higher adds them there); the game falls back until its rollbacks are as deep as allowed.
//   Tried with the limit one lower and one higher at each delay: each fails here, at its own delay.
//   (With 6 at every delay: at delay 0, 1.64 frames to spare for 1.82 at 55 frames a second, 1.40 for
//   1.60 at 50; no input late in this race.)
void TestTheScheduleAtALowerDelay()
{
  std::vector<PacedHost> near = {{"time to spare", {}}, {"49 and 36 ms every 15 frames", std::vector<double>(15, 0.0)},
                                 {"frames of 19 and 14.33 ms in turn", {19, 14.33}}, {"one frame of 20 ms in four", {20, 0, 0, 0}},
                                 {"one frame of 30 ms in four", {30, 0, 0, 0}}};
  near[1].work[13] = 49;
  near[1].work[14] = 36;
  const std::vector<PacedHost> far = {{"55 frames a second", {1000.0 / 55}, 1.7}, {"50 frames a second", {1000.0 / 50}, 1.5}};
  for (const u32 delay : {0u, 1u, 2u})
  {
    for (const PacedHost& host : near)
    {
      const int failed_before = s_failed;
      Pace pace;
      pace.work = host.work;
      pace.delay = delay;
      const Raced r = RunAPacedRace(pace);
      CHECK(r.ran);
      if (!r.ran)
        continue;
      CHECK(r.final_steps > 1200 && r.other_checksums == 0);
      const std::vector<double> settled(r.sched.begin() + SETTLED, r.sched.end());
      const double low = Percentile(settled, 0.10), middle = Percentile(settled, 0.5), high = Percentile(settled, 0.90);
      CHECK(middle >= -1.0 && middle <= 1.5);
      CHECK(high <= 2.25);
      CHECK(low >= -1.5);
      CHECK(r.late == 0 && r.sent_twice == 0);
      CHECK(Count(r, delay).beyond == 0);
      if (s_failed != failed_before)
        std::printf("  delay %u, a host with %s: steps ahead of the schedule from frame %u on: 10%% %.2f, median %.2f, "
                    "90%% %.2f; inputs late %u of %zu; sent twice %u; deepest rollback %u\n",
                    delay, host.what, SETTLED, low, middle, high, r.late, r.margin.size(), r.sent_twice, Count(r, delay).deepest);
    }
    for (const PacedHost& host : far)
    {
      for (const u32 back : {4u, 5u})
      {
        Pace pace;
        pace.work = host.work;
        pace.delay = delay;
        pace.other_changes_every = 1;
        pace.relay_back_steps = back;
        const Raced r = RunAPacedRace(pace);
        const Counted c = Count(r, delay);
        const double least = Percentile(r.margin, 0.0);
        bool right = r.ran && r.other_checksums == 0 && r.margin.size() > 1000 && r.late == 0 && c.at_limit > 100 && c.beyond == 0;
        // four steps late: nothing deeper than the limit at 50 frames a second, the host's whole time to spare;
        // five: most rollbacks deeper than the limit, with no step on top of them
        right = right && (back == 4 ? least >= host.least && (host.least < 1.7 ? c.deeper == 0 : true) : c.deeper > 1000);
        CHECK(right);
        if (!right)
          std::printf("  delay %u (the rule allows %u steps), official frames %u steps late, a host at %s: ran %d; inputs "
                      "late %u of %zu; the least time to spare %.2f frames (asked at 4 steps: %.2f); deepest rollback %u, "
                      "%u frames deeper than allowed; catch-up steps on top of a rollback as deep as allowed: %u, of a "
                      "deeper one: %u; final steps with another checksum %u\n",
                      delay, Limit(delay), back, host.what, r.ran, r.late, r.margin.size(), least, host.least, c.deepest,
                      c.deeper, c.at_limit, c.beyond, r.other_checksums);
      }
    }
  }
}
