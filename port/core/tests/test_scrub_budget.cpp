// Tests of the replay scrubber's budget for front steps (scrub_budget.cpp): the rule by its numbers, frames of a
// heavy course run by the rule, and a playback whose host's frames leave no room for a while.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "harness.h"
#include "net_internal.h"
#include "recorded_race.h"
#include "scrub_host.h"

namespace
{
using namespace recorded;
using net::FrontBudget;
using net::FrontStepFits;

// Course 37's numbers: a front step 2.7 ms and 5.2 % of a field emulated.
constexpr double STEP_MS = 2.7;
constexpr double STEP_TICKS = net::FIELD_TICKS * 0.052;

FrontBudget Budget(u32 committed, double since_ms, double own_ms, double own_ticks = 0)
{
  FrontBudget b;
  b.committed = committed;
  b.since_ms = since_ms;
  b.step_ms = STEP_MS;
  b.own_ms = own_ms;
  b.step_ticks = STEP_TICKS;
  b.own_ticks = own_ticks;
  b.share = 0.4;
  return b;
}

// The rule: the whole frame within 90 % of a field, the share as the upper bound after the first step, a frame
// with no room for any, emulated time the same way, and what is done before a step's time is known.
void TestTheRule()
{
  BEGIN_TEST();
  // a light frame (own work 5 ms): the first, and a second; a third only while the share allows
  CHECK(FrontStepFits(Budget(0, 0.3, 5.0)) && FrontStepFits(Budget(1, 0.3, 5.0)));
  CHECK(!FrontStepFits(Budget(2, 3.4, 5.0)));  // (3.4 + 1.25 x 2.7 is over 40 % of a field: the share as before)
  // course 37's heavy places (own work 10 ms): one front step, not two
  CHECK(FrontStepFits(Budget(0, 0.3, 10.0)) && !FrontStepFits(Budget(1, 0.3, 10.0)));
  // own work 12 ms: 0.3 + 12 + 3.4 is over 15 ms, so none: the frame goes straight to its play step
  CHECK(!FrontStepFits(Budget(0, 0.3, 12.0)));
  // the whole frame: 15 ms (90 % of a field) is the line, the steps set to run but not run counted
  CHECK(FrontStepFits(Budget(0, 0.5, 15.0 - 0.5 - 1.25 * STEP_MS - 0.01)));
  CHECK(!FrontStepFits(Budget(0, 0.5, 15.0 - 0.5 - 1.25 * STEP_MS + 0.01)));
  CHECK(FrontStepFits(Budget(1, 0.5, 15.0 - 0.5 - 2.25 * STEP_MS - 0.01)));
  CHECK(!FrontStepFits(Budget(1, 0.5, 15.0 - 0.5 - 2.25 * STEP_MS + 0.01)));
  // with no own work known, after the first step the rule is the share's as it was (since + 1.25 steps)
  for (double since = 0; since < 10; since += 0.1)
    CHECK(FrontStepFits(Budget(1, since, 0)) == (since + 1.25 * STEP_MS <= net::FRAME_MS * 0.4));
  // the first step is not held to the share (it was always run; a small share still buffers)
  FrontBudget small = Budget(0, 0.3, 5.0);
  small.share = 0.05;
  CHECK(FrontStepFits(small));
  small.committed = 1;
  CHECK(!FrontStepFits(small));
  // emulated time: own work of 85 % of a field leaves room for one step (85 + 1.25 x 5.2 > 90: none)
  CHECK(FrontStepFits(Budget(0, 0.3, 1.0, net::FIELD_TICKS * 0.8)));
  CHECK(!FrontStepFits(Budget(0, 0.3, 1.0, net::FIELD_TICKS * 0.85)));
  CHECK(!FrontStepFits(Budget(3, 0.3, 1.0, net::FIELD_TICKS * 0.72)));  // (0.72 + 4.25 x 0.052 > 0.9)
  // the front edge less than LEAST_LEAD steps ahead of the play position: the first step runs whatever the time
  // (the play position never passes it); the second is held to the rule as ever
  FrontBudget close = Budget(0, 0.3, 14.0, net::FIELD_TICKS * 0.95);
  close.lead = net::LEAST_LEAD - 1;
  CHECK(FrontStepFits(close));
  close.committed = 1;
  CHECK(!FrontStepFits(close));
  close.committed = 0;
  close.lead = net::LEAST_LEAD;
  CHECK(!FrontStepFits(close));
  // a step's time not known yet: the frame's first step runs (to learn it), no second
  FrontBudget unknown = Budget(0, 0.3, 14.0);
  unknown.step_ms = 0;
  CHECK(FrontStepFits(unknown));
  unknown.committed = 1;
  CHECK(!FrontStepFits(unknown));
}

// Frames run by the rule as the scrubber asks it (a step is given one ahead: before the step set to run before it
// has run): the own work of course 37 (7 ms, heavy places 10 ms, a stretch at 13 ms) and of a light course (5 ms).
// After the first frame (no own work known yet) no frame that runs front steps goes past 1.1 fields, frames whose
// own work leaves no room run none, and the lighter the frame the more it buffers.
void TestFramesByTheRule()
{
  BEGIN_TEST();
  struct Case
  {
    double own_ms;
    u32 least_steps, most_steps;  // a frame's front steps
  };
  for (const Case c : {Case{5.0, 3, 3}, Case{7.0, 2, 2}, Case{10.0, 1, 1}, Case{13.0, 0, 0}})
  {
    u32 over = 0, least = 99, most = 0;
    double own = 0;  // (smoothed, as the scrubber does: it starts from nothing)
    for (int frame = 0; frame < 300; ++frame)
    {
      double since = 0.3;  // (the frame's first hook: the play log's entry and the move)
      u32 steps = 0;
      if (FrontStepFits(Budget(0, since, own)))
      {
        steps = 1;
        for (; FrontStepFits(Budget(steps, since, own)); ++steps)
          since += STEP_MS;  // (the step set to run before it runs)
        since += STEP_MS;    // (the last one)
      }
      const double ms = since + c.own_ms;
      over += frame > 0 && steps != 0 && ms > net::FRAME_MS * 1.1 ? 1 : 0;
      own = own == 0 ? c.own_ms : own * 0.8 + c.own_ms * 0.2;
      if (frame >= 30)
      {
        least = std::min(least, steps);
        most = std::max(most, steps);
      }
    }
    CHECK(over == 0 && least == c.least_steps && most == c.most_steps);
  }
}

// A host whose frames' own work fills its emulated time (95 % of a field) for HEAVY_FRAMES frames once the front
// edge is 100 steps ahead of the picture: the heavy frames run no front step until the lead is down to LEAST_LEAD,
// then one each, so that the play position never passes the front edge; the light frames after them buffer the rest,
// every step at the front edge and every picture's step still gives the recording's checksum, and the replay is
// buffered to its end.
constexpr u32 HEAVY_FRAMES = 200;

class HeavyHost : public scrub_test::ScrubHost
{
public:
  u32 heavy_left = HEAVY_FRAMES;
  bool heavy = false;
  std::size_t heavy_bars_from = 0, heavy_bars_to = 0;  // the bars shown while the frames were heavy
  u64 ticks = 0;
  u32 asks = 0;
  void ReplayBar(const ScrubBar& bar) override
  {
    ScrubHost::ReplayBar(bar);
    if (!heavy && heavy_left == HEAVY_FRAMES && bar.first == 60 && bar.buffered >= bar.played + 100)
    {
      heavy = true;
      heavy_bars_from = bars.size();
    }
  }
  bool ThreadClocks(GameClocks* out) override
  {
    // asked twice a frame: where the frame's own work begins (after its front steps) and at the next frame's start
    const bool own_work = ++asks % 2 == 0;
    if (own_work && heavy && --heavy_left == 0)
    {
      heavy = false;
      heavy_bars_to = bars.size();
    }
    if (own_work)
      ticks += static_cast<u64>((heavy ? 0.95 : 0.2) * net::FIELD_TICKS * 12);
    out->ticks = ticks;
    out->idle_ticks = 0;
    out->waited = std::chrono::nanoseconds{0};
    return true;
  }
};

void TestAPlaybackWithHeavyFrames()
{
  BEGIN_TEST();
  const std::string dir = Temp();
  std::string image(0x100, '\0');
  image.replace(0, 4, "FZMD");
  image.replace(0x10, 4, "FZSI");
  WriteText(dir + "/mod.bin", image);
  WriteText(dir + "/manifest.json", "{\"commit\": \"abc1234\"}");
  WriteText(dir + "/codes.txt", "$Boot to race\n48000000 800030C8\n$Replay boot\n04000000 00000001\n");
  std::mt19937 rng(15);
  const Ways ways{Game::Plain, false, false, false};
  std::vector<u8> end_state;
  WriteReplay(dir + "/race.fzrp", {1, 0}, "", "", 0, RecordTheRace(rng, ways, end_state));
  HeavyHost host;
  Options options;
  options.bundle_dir = dir;
  options.play_file = dir + "/race.fzrp";
  options.replay_scrubber = true;
  options.debug.replay_end_quits = true;
  Init(host, options);
  FakeRace race(host, RACE_FROM, 100000, false);
  race.image_loop = true;
  for (u32 frames = 0; frames < 1200 && host.stops == 0 && race.Frame(); ++frames)
  {
  }
  Shutdown();
  const auto said = [&](const std::string& text) { return host.notices.find(text) != std::string::npos; };
  const auto number_after = [&](const std::string& text) {
    const std::size_t at = host.notices.find(text);
    return at == std::string::npos ? -1L : std::atol(host.notices.c_str() + at + text.size());
  };
  CHECK(host.stops == 1 && said("the check's playback stops here") && host.log.empty());
  CHECK(said("playback: 420 of 420 steps compared with the recording, 0 differed"));
  CHECK(said("scrubber: checksums: 0 of ") && said(" front steps differed from the recording; 0 of "));
  CHECK(said("the replay ends before step 419 (buffered in "));
  // the heavy frames ran none while the lead lasted (after the smoothing's few frames), then one each, which held
  // the lead at LEAST_LEAD; the front edge was never behind the picture
  CHECK(host.heavy_left == 0 && host.heavy_bars_to > host.heavy_bars_from + 150);
  const long without = number_after("of them ran front steps, and ");
  CHECK(without >= 20 && without < static_cast<long>(HEAVY_FRAMES) - 20);
  u32 least = 0xFFFFFFFF;
  for (std::size_t i = host.heavy_bars_from + 130; i < host.heavy_bars_to; ++i)
    least = std::min(least, host.bars[i].buffered - host.bars[i].played);
  CHECK(least + 3 >= net::LEAST_LEAD && least <= net::LEAST_LEAD);
  u32 behind = 0, race_bars = 0;
  for (const ScrubBar& bar : host.bars)
  {
    race_bars += bar.first == 60 ? 1 : 0;
    behind += bar.first == 60 && bar.buffered + 1 < bar.played ? 1 : 0;  // (the front edge at least the step shown)
  }
  CHECK(race_bars > 300 && behind == 0);
  CHECK(said("ticks of emulated time (20.0 % of a field) at the end, smoothed\n"));
  TestHost last;
  Init(last, Options{});
  std::filesystem::remove_all(dir);
}
}  // namespace

// The scrubber's budget's tests, in one call (TestTheReplayScrubber, test_scrubber.cpp).
void TestTheScrubbersBudget()
{
  TestTheRule();
  TestFramesByTheRule();
  TestAPlaybackWithHeavyFrames();
}
