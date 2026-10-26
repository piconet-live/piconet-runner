// A game whose racer is out watches the race behind live, on official frames only (port/core/src/spectate.cpp): a
// paced race (test_pace.cpp's RunAPacedRace) whose local seat goes out, on a host that keeps the emulation speed the
// game sets.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "harness.h"
#include "net_internal.h"
#include "paced_race.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr u32 OUT_AT = 400;  // the race frame the local seat goes out at
using Record = std::array<u8, 6>;

// The first frame of a watch state at or after `from` (the log's index), or the log's length.
std::size_t FirstOf(const Raced& r, double watch, std::size_t from = 0)
{
  for (std::size_t i = from; i < r.watch.size(); ++i)
    if (r.watch[i] == watch)
      return i;
  return r.watch.size();
}

// The harsh case of run cam-r2c: the other seat's input changes in every step, and official frames come back four
// steps after the relay closed them, so a racing game rolls back in every frame.
Pace HarshRace()
{
  Pace pace;
  pace.race_frames = 2900;
  pace.other_changes_every = 1;
  pace.relay_back_steps = 4;
  pace.out_at = OUT_AT;
  pace.follow_speed = true;
  return pace;
}

// Official frames of the local seat from `from` to `to` (not past what the relay closed) that are the neutral record.
u32 Neutral(const Raced& r, std::size_t from, std::size_t to)
{
  u32 n = 0;
  for (std::size_t f = from; f < std::min(to, r.official_mine.size()); ++f)
    n += r.official_mine[f] == Record{} ? 1 : 0;
  return n;
}

// From `from` on (the log's index): each displayed frame ran exactly one new step (none stood, none jumped); no
// rollback in lockstep; the speeds within [least, most]; once the slack was in hand (the index, returned), its
// frames in hand within 4 and `in_hand_most` from `settled` frames on (in_hand_most 0: not looked at).
std::size_t TheWatching(const Raced& r, std::size_t from, double least, double most, std::size_t settled = 0,
                        double in_hand_most = 16)
{
  u32 stood = 0, jumped = 0, rolled = 0;
  for (std::size_t i = from; i + 1 < r.step.size(); ++i)
  {
    stood += r.step[i + 1] == r.step[i] ? 1 : 0;
    jumped += r.step[i + 1] > r.step[i] + 1 ? 1 : 0;
  }
  double slowest = 1, fastest = 1, in_least = 1e9, in_most = -1e9;
  std::size_t full = FirstOf(r, 2, from);
  while (full < r.buffer.size() && r.buffer[full] < 9)
    ++full;
  for (std::size_t i = from; i < r.watch.size(); ++i)
  {
    rolled += r.watch[i] == 2 && r.depth[i] > 0 ? 1 : 0;
    slowest = std::min(slowest, r.speed[i]);
    fastest = std::max(fastest, r.speed[i]);
    if (i >= full + settled)
    {
      in_least = std::min(in_least, r.buffer[i]);
      in_most = std::max(in_most, r.buffer[i]);
    }
  }
  const int failed_before = s_failed;
  CHECK(stood == 0 && jumped == 0 && rolled == 0);
  CHECK(slowest >= least - 1e-6 && fastest <= most + 1e-6);
  CHECK(full < r.buffer.size() && (in_hand_most == 0 || (in_least >= 4 && in_most <= in_hand_most)));
  CHECK(r.notices.find("watching waited for an official frame") == std::string::npos);
  if (s_failed != failed_before)
    std::printf("  from frame %zu: lockstep from %zu, the slack in hand from %zu; frames stood %u, jumped %u; rollbacks "
                "in lockstep %u; speeds %.3f to %.3f; in hand then %.1f to %.1f\n  the session said:\n%s",
                from, FirstOf(r, 2, from), full, stood, jumped, rolled, slowest, fastest, in_least, in_most,
                r.notices.c_str());
  return full;
}

// The local seat goes out at race frame 400, on a host whose clock runs as the relay's, or slow or fast against it.
// - From there each displayed frame runs exactly one new step: no frame stands (a wait, a held frame) and none
//   jumps (a catch-up step), until the race's end.
// - It falls behind playing a little slower (0.99; on a host whose clock is slow against the relay's lower, to
//   0.95), is in lockstep (no rollback, no prediction), then has the slack in hand and keeps it, faster than 1.02
//   never; and waits for no official frame.
// - The race is the official one (every final step's checksum), and the relay sees nothing new of the watching
//   seat: no input late, none sent twice. Its records are the pad while it falls behind and the neutral record once
//   in lockstep.
void WatchingBehind(double relay_rate)
{
  Pace pace = HarshRace();
  pace.relay_rate = relay_rate;
  pace.race_frames = relay_rate < 1 ? 4000 : 2900;
  const Raced r = RunAPacedRace(pace);
  CHECK(r.ran);
  if (!r.ran)
  {
    std::printf("  relay's clock at %.3f: warnings: %s\n", relay_rate, r.warnings.c_str());
    return;
  }
  CHECK(r.other_checksums == 0 && r.final_steps > 2500);
  CHECK(r.late == 0 && r.sent_twice == 0);
  u32 racing_rollbacks = 0;
  for (std::size_t i = 200; i < OUT_AT; ++i)
    racing_rollbacks += r.depth[i] > 0 ? 1 : 0;
  CHECK(racing_rollbacks > (OUT_AT - 200) * 9 / 10);
  const std::size_t drifting = FirstOf(r, 1), behind = FirstOf(r, 2);
  CHECK(drifting >= OUT_AT && drifting <= OUT_AT + 2);
  CHECK(behind > drifting && behind < r.watch.size() - 600);
  if (behind >= r.watch.size())
    return;
  TheWatching(r, drifting, relay_rate < 1 ? 0.95 : 0.99, 1.02);
  // the pad up to lockstep, the neutral record from the frames sent once in it
  const u32 at_behind = static_cast<u32>(r.step[behind]);
  CHECK(Neutral(r, 200, at_behind) == 0);
  CHECK(Neutral(r, at_behind + 60, r.official_mine.size()) + at_behind + 60 >= r.official_mine.size() &&
        r.official_mine.size() > at_behind + 300);
}

// A knock-out that was only a prediction: the image's count goes back to 0 when a rollback takes it back. The game
// falls behind for those frames and races again, at the host's speed; it sends the pad all along, never the neutral
// record, and never goes into lockstep. Once made by a predicted input of the other seat (the race's own way:
// FakeRace::knock_out_at, the count kept from the machine's flags as the image keeps it); and, by the count alone,
// once 30 frames on and once in the very frame it would have gone into lockstep (the drifting frame before ran only
// official steps, so the seat is found back on the official race). Racing again, it sends by the schedule until it
// is within a frame of it, then stops (next_input 0): the pad of each displayed frame for its step + the delay.
void APredictedKnockOutTakenBack()
{
  enum How { ByInput, After30, AtBehind };
  for (const How how : {ByInput, After30, AtBehind})
  {
    Pace pace = HarshRace();
    pace.race_frames = 2000;
    if (how == ByInput)
    {
      pace.out_at = 0;
      pace.knock_out_by_input_at = FROM + OUT_AT;
    }
    else if (how == AtBehind)
      pace.undo_at_behind = true;
    else
      pace.out_until = OUT_AT + 30;
    const Raced r = RunAPacedRace(pace);
    CHECK(r.ran && r.other_checksums == 0 && r.late == 0 && r.sent_twice == 0);
    const std::size_t drifting = FirstOf(r, 1), back = FirstOf(r, 0, drifting);
    CHECK(how == ByInput ? drifting > OUT_AT - 20 && drifting < OUT_AT + 20 : drifting >= OUT_AT && drifting <= OUT_AT + 2);
    CHECK(back < r.watch.size() && FirstOf(r, 2) == r.watch.size());
    CHECK(how == AtBehind ? back > OUT_AT + 200 : back <= drifting + 32);
    CHECK(r.next_input_end == 0);
    u32 pads = 0, other = 0;
    for (std::size_t i = back + 120; i < r.step.size(); ++i)
    {
      const u32 t = static_cast<u32>(r.step[i]);
      if (const auto it = r.pads.find(t); it != r.pads.end() && t + 2 < r.official_mine.size())
      {
        ++pads;
        other += r.official_mine[t + 2] != it->second ? 1 : 0;
      }
    }
    CHECK(pads > 500 && other == 0);
    CHECK(FirstOf(r, 1, back) == r.watch.size());  // racing to the end
    CHECK(Neutral(r, 200, r.official_mine.size()) == 0);
    CHECK(r.notices.find("the local racer races again") != std::string::npos);
    for (std::size_t i = back; i < r.speed.size(); ++i)
      CHECK(r.speed[i] == 1.0);
  }
}

// The local seat over the line from the race's first step (its machine's flags; FakeRace::finish_goal): the game
// watches from there, the core's own count, and goes into lockstep as for a knock-out.
void AFinishedSeatWatches()
{
  Pace pace = HarshRace();
  pace.out_at = 0;
  pace.finish_goal = 100000000;
  const Raced r = RunAPacedRace(pace);
  CHECK(r.ran && r.other_checksums == 0 && r.late == 0 && r.sent_twice == 0);
  const std::size_t drifting = FirstOf(r, 1);
  CHECK(drifting <= 2 && FirstOf(r, 2) < r.watch.size());
  TheWatching(r, drifting, 0.99, 1.02);
  CHECK(r.notices.find("the local racer is over the line") != std::string::npos);
}

// A host that stalls for two seconds while the game watches behind: about 120 frames more in hand. It makes them up
// faster the further behind it is, at 1.25 at most, one step a frame all along, and has the slack in hand again
// within some ten seconds (at 1.02 it took a hundred). And a host whose clock is slow against the relay's (the
// relay's 1.03 times as fast): it keeps up at a speed above 1.02.
void AWatcherCatchesUp()
{
  Pace pace = HarshRace();
  pace.out_at = 100;
  pace.race_frames = 3400;
  pace.stall_at = 2400;
  pace.stall_ms = 2000;
  const Raced r = RunAPacedRace(pace);
  CHECK(r.ran && r.other_checksums == 0);
  const std::size_t stall = std::min<std::size_t>(pace.stall_at, r.buffer.size() - 1);
  CHECK(FirstOf(r, 2) < stall);
  double most = 0;
  std::size_t again = r.buffer.size();
  for (std::size_t i = stall; i < r.buffer.size(); ++i)
  {
    most = std::max(most, r.buffer[i]);
    if (again == r.buffer.size() && i > stall + 5 && r.buffer[i] <= 13)
      again = i;
  }
  double fastest = 0;
  for (const double v : r.speed)
    fastest = std::max(fastest, v);
  CHECK(most > 100 && again < stall + 700 && fastest <= 1.25 + 1e-6 && fastest > 1.2);
  if (s_failed != 0)
    std::printf("  a stall at frame %zu: %.0f frames in hand at most, back near the slack after %zu frames; fastest "
                "%.3f\n", stall, most, again - stall, fastest);
  // (from the stall on: one step a frame, no rollback; the stall's own frame is a frame like any other)
  TheWatching(r, FirstOf(r, 2), 0.99, 1.25, 0, 0);

  Pace fast = HarshRace();
  fast.relay_rate = 1.03;
  const Raced f = RunAPacedRace(fast);
  CHECK(f.ran && f.other_checksums == 0);
  const std::size_t full = TheWatching(f, FirstOf(f, 1), 0.99, 1.25, 300, 20);
  double after = 0;
  for (std::size_t i = full; i < f.speed.size(); ++i)
    after = std::max(after, f.speed[i]);
  CHECK(after > 1.02);
}

// A wait for an official frame while watching behind: the slack goes 3 higher (18 at most), the slack is no longer
// in hand and the drift's window starts over.
void AWaitRaisesTheSlack()
{
  TestHost host;
  Init(host, Options{});
  net::Session s;
  s.watch = net::Watch::Behind;
  s.buffer_full = true;
  s.drift_frames = 100;
  s.drift_sum = 50;
  s.drift_before = 3;
  s.official.resize(500);
  net::BeforeTheWatchersWait(s, 499);  // its frame is here: nothing
  CHECK(s.slack == net::SPECTATE_SLACK && s.watch_waits == 0 && s.buffer_full);
  net::BeforeTheWatchersWait(s, 500);
  CHECK(s.slack == net::SPECTATE_SLACK + 3 && s.watch_waits == 1 && !s.buffer_full);
  CHECK(s.drift_frames == 0 && s.drift_sum == 0 && std::isnan(s.drift_before));
  for (int k = 0; k < 5; ++k)
    net::BeforeTheWatchersWait(s, 500);
  CHECK(s.slack == net::SPECTATE_SLACK_MOST && s.watch_waits == 6);
  s.watch = net::Watch::Drifting;  // only a game in lockstep waits so
  net::BeforeTheWatchersWait(s, 500);
  CHECK(s.watch_waits == 6);
  CHECK(host.notices.find("slack 12 from here") != std::string::npos);
}

// The race's end while the game still falls behind: it stops watching ("the race is over"), at the host's speed,
// and the race and its end are the official ones. (This relay closes the race's last 16 frames early, which would
// put the game in lockstep before the end: its frames come back 6 steps after it closed them here, not 4. Inputs
// come late in the results, as every game's in lockstep there.)
void TheRaceEndsWhileFallingBehind()
{
  Pace pace = HarshRace();
  pace.race_frames = 1100;
  pace.race_to = 800;
  pace.relay_back_steps = 6;
  const Raced r = RunAPacedRace(pace);
  CHECK(r.ran && r.other_checksums == 0);
  CHECK(FirstOf(r, 1) < r.watch.size() && FirstOf(r, 2) == r.watch.size());
  CHECK(r.notices.find("no longer watching: the race is over") != std::string::npos);
}
}  // namespace

void TestAWatchingGameRunsBehindOnOfficialFrames()
{
  for (const double relay_rate : {1.0, 0.988})
    WatchingBehind(relay_rate);
  APredictedKnockOutTakenBack();
  AFinishedSeatWatches();
  AWatcherCatchesUp();
  AWaitRaisesTheSlack();
  TheRaceEndsWhileFallingBehind();
}
