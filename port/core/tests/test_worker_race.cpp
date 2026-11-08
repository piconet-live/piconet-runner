// The deep-rollback worker in a paced race (test_pace.cpp's RunAPacedRace with its fake adapter, FakeWorkers): the
// official rows come late and in bursts, so that the worker's jobs chain and the second worker runs; every final step
// must have run on its official row, and its checksum must be the official race's. And the last check before a step
// becomes final (frame.cpp HeldOpenSteps): a step whose session account says "official row" while it ran on another,
// the fault a desynced game's recording showed on 2026-11-07, stays open and runs again.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "harness.h"
#include "paced_race.h"

#include <cstdlib>

void TestTheWorkersFinalRows()
{
  BEGIN_TEST();
  for (const u32 together : {0u, 3u})
  {
    Pace pace;
    pace.worker = "on";
    pace.workers = 2;
    pace.relay_back_steps = 4;
    pace.other_changes_every = 1;
    pace.relay_rate = 0.96;  // (the relay's clock slow: the game holds often, as the forced-hold gate's)
    pace.race_frames = 1500;
    pace.rows_together = together;
    pace.together_from = 0;
    pace.together_to = together != 0 ? 1000000 : 0;
    const Raced r = RunAPacedRace(pace);
    std::printf("worker race (rows %u together): final %u, other rows %u, other checksums %u\n", together,
                r.final_steps, r.other_rows, r.other_checksums);
    CHECK(r.ran);
    CHECK(r.final_steps > 1000);
    CHECK(r.other_rows == 0);
    CHECK(r.other_checksums == 0);
    CHECK(r.notices.find("adopted") != std::string::npos);  // (the worker's jobs ran and were adopted)
    const std::size_t second = r.notices.find("second jobs started ");  // (the S6 line: the second worker ran)
    CHECK(second != std::string::npos && std::atoi(r.notices.c_str() + second + 20) > 0);
    CHECK(r.notices.find("kept open") == std::string::npos);
  }
}

void TestAStepThatRanOnAnotherRowStaysOpen()
{
  BEGIN_TEST();
  for (const char* worker : {"", "on"})
  {
    Pace pace;
    pace.worker = worker;
    pace.relay_back_steps = 4;
    pace.other_changes_every = 3;
    pace.race_frames = 900;
    pace.fault_at = 600;
    const Raced r = RunAPacedRace(pace);
    std::printf("a step run on another row (worker \"%s\"): final %u, other rows %u, other checksums %u (from %u), "
                "faults %u\n",
                worker, r.final_steps, r.other_rows, r.other_checksums, r.first_other_checksum, r.faults);
    CHECK(r.ran);
    CHECK(r.final_steps > 700);
    CHECK(r.faults != 0);
    CHECK(r.other_rows == 0);
    CHECK(r.other_checksums == 0);
    // (with the worker on, a job's adoption hands the session the rows its steps ran on, which the fault then
    // overwrites again; the step is found and run again either way)
    CHECK(*worker != 0 || r.notices.find("step 600 kept open") != std::string::npos);
  }
}
