// When a race's steps run: the time sync against a host that paces the game as the emulator does and a relay that
// keeps and closes inputs as the real one does, in time this test moves itself (TestHost::time), so nothing here
// depends on how busy the computer is.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "harness.h"
#include "paced_race.h"

#include <map>
#include <set>
#include <sstream>

namespace
{
using Ns = std::chrono::nanoseconds;
constexpr Ns FRAME{16'666'667};

Ns Ms(double ms)
{
  return Ns(static_cast<long long>(ms * 1e6));
}

// The rollback log read back: each column by its name.
std::map<std::string, std::vector<double>> ReadLog(const std::string& path)
{
  std::map<std::string, std::vector<double>> columns;
  std::ifstream file(path);
  std::string line;
  std::vector<std::string> names;
  while (std::getline(file, line))
  {
    std::stringstream cells(line);
    std::size_t k = 0;
    for (std::string cell; std::getline(cells, cell, ','); ++k)
    {
      if (names.size() <= k && columns.empty())
        names.push_back(cell);
      else if (k < names.size())
        columns[names[k]].push_back(std::atof(cell.c_str()));
    }
    if (columns.empty())
      for (const std::string& name : names)
        columns[name];
  }
  return columns;
}

}  // namespace

double Percentile(std::vector<double> values, double p)
{
  if (values.empty())
    return 0;
  std::sort(values.begin(), values.end());
  return values[static_cast<std::size_t>(p * static_cast<double>(values.size() - 1))];
}

namespace
{

void Add32(std::string& p, u32 v)
{
  for (int i = 3; i >= 0; --i)
    p += static_cast<char>(v >> (8 * i));
}

using Record = std::array<u8, 6>;

// The relay of a paced race (RunAPacedRace), with the real one's rules, and what it saw of the local seat's inputs.
struct PacedRelay
{
  PacedRelay(const Pace& the_pace, Udp& socket, Raced& result) : pace(the_pace), relay(socket), out(result) {}
  const Pace& pace;
  Udp& relay;
  Raced& out;
  std::map<u32, Record> first_sent, last_sent, pending, official;  // seat 0's, by frame
  std::set<u32> counted;                                           // frames whose margin is counted
  Record latest{};
  long long latest_frame = -1;
  std::optional<Endpoint> game;
  std::optional<std::chrono::steady_clock::time_point> go_at;  // the relay's clock runs from here
  Ns clock_back{0};                                             // and stood this long in all
  u32 closed = 0;       // frames 0 to closed - 1 are official
  u32 game_has = 0;     // the official frames the game said it has, in its latest inputs

  // The official row of step t: seat 0's as this relay closed it, seat 1's.
  std::array<u8, 12> Row(u32 t) const
  {
    std::array<u8, 12> r{};
    if (const auto it = official.find(t); it != official.end())
      std::copy(it->second.begin(), it->second.end(), r.begin());
    for (int k = 6; k < 12; ++k)
      r[k] = static_cast<u8>(1 + t / pace.other_changes_every);
    return r;
  }

  // Its clock at this moment, in ms since go; at one frame of the race it stands (Pace::stands_at).
  double Clock(std::chrono::steady_clock::time_point now, bool racing, u32 race_frames)
  {
    if (racing && pace.stands_steps != 0 && race_frames == pace.stands_at)
      clock_back += FRAME * pace.stands_steps;
    const double clock_ms =
        go_at ? std::chrono::duration<double, std::milli>(now - *go_at - clock_back).count() * pace.relay_rate : 0;
    return clock_ms;
  }

  // The frames its clock has closed (before the race: those lockstep needs).
  void CloseFrames(u32 step, double clock_ms)
  {
    // (the countdown; and the end of a race that ends, with the results after it: closed a quarter second early,
    // since the frame that settles the race waits for them in time that stands while a frame runs)
    const bool lockstep = step < FROM || step + 16 >= pace.race_to;
    // (what its clock has closed is closed before it reads what arrived now: an input that comes at the moment its
    // frame is due is late)
    const u32 by_clock = clock_ms / FRAME_MS >= 2 ? static_cast<u32>(clock_ms / FRAME_MS) - 1 : 0;
    for (const u32 to = std::max(closed, lockstep ? std::max(step + 8, by_clock) : by_clock); closed < to; ++closed)
    {
      if (const auto it = pending.find(closed); it != pending.end())
      {
        std::tie(latest, latest_frame) = std::pair{it->second, static_cast<long long>(closed)};
        pending.erase(it);
      }
      official[closed] = latest;
    }
  }

  // What the game sent since the last frame: a pong for a ping; the local seat's inputs, and how early each came.
  void Hear(bool racing, u32 race_frames, double clock_ms)
  {
    u8 buffer[1024];
    std::size_t n = 0;
    Endpoint from;
    while (relay.Receive(buffer, sizeof(buffer), &n, &from))
    {
      game = from;
      if (n == 24 && buffer[3] == 1)  // (a ping: nonce, the time, the token)
      {
        std::string pong = FromRelay('\x02') + std::string(4, '\0');
        pong.append(reinterpret_cast<const char*>(buffer) + 8, 8);
        Add32(pong, static_cast<u32>(clock_ms));
        relay.Send(pong.data(), pong.size(), *game);
      }
      if (n >= 29 && buffer[3] == 7)  // inputs: the token, the cookie, the first frame, the frames in hand, a count
      {
        const u32 first = (u32(buffer[20]) << 24) | (u32(buffer[21]) << 16) | (u32(buffer[22]) << 8) | buffer[23];
        game_has = (u32(buffer[24]) << 24) | (u32(buffer[25]) << 16) | (u32(buffer[26]) << 8) | buffer[27];
        for (u32 k = 0; k < buffer[28] && 29 + (k + 1) * 6 <= n; ++k)
        {
          const u32 f = first + k;
          Record record;
          std::memcpy(record.data(), buffer + 29 + k * 6, 6);
          last_sent[f] = record;
          if (!first_sent.emplace(f, record).second)
            continue;  // sent before: the relay keeps the first
          if (racing && race_frames > SETTLED)
          {
            counted.insert(f);
            out.margin.push_back(static_cast<double>(f + 2) - clock_ms / FRAME_MS);
            out.late += f < closed ? 1 : 0;
          }
          if (f >= closed)
            pending[f] = record;
          else if (f > latest_frame)
            std::tie(latest, latest_frame) = std::pair{record, static_cast<long long>(f)};
        }
      }
    }
  }

  // Joined, go, and the frames closed last.
  void Say(std::chrono::steady_clock::time_point now)
  {
    if (game)
    {
      std::string joined = FromRelay('\x04');   // seat 0 of the player seats 0 and 1
      Add32(joined, 1), joined += '\0';
      Add32(joined, 3);
      joined.append(13, '\0');   // (no cookie, no flags, no frames closed)
      relay.Send(joined.data(), joined.size(), *game);
      std::string go = FromRelay('\x06');
      Add32(go, 1);
      Add32(go, 0);
      relay.Send(go.data(), go.size(), *game);
      if (!go_at)
        go_at = now;
      // (the last twelve; from the game's own count when it has fewer, as the real relay sends again: closing
      // early at a race's end, this relay can close more than twelve frames at once)
      // (what the game is told of: the frames closed, less those still on their way back to it)
      const u32 told = closed > pace.relay_back_steps ? closed - pace.relay_back_steps : 0;
      for (u32 first = std::min(game_has, told > 12 ? told - 12 : 0); first < told; first += 6)
      {
        const u32 count = std::min<u32>(6, told - first);
        std::string packet = FromRelay('\x08');
        Add32(packet, first);
        packet += static_cast<char>(count);
        Add32(packet, 3);
        for (u32 t = first; t < first + count; ++t)
        {
          const std::array<u8, 12> r = Row(t);
          packet.append(r.begin(), r.end());
        }
        relay.Send(packet.data(), packet.size(), *game);
      }
    }
  }

  void CompareWithTheOfficialRace(FakeRace& race)
  {
    // every final step's checksum against the official race's (the ring keeps the last 1024)
    TestHost plain_host;
    FakeRace plain(plain_host, FROM, 1000000, false);
    plain.finish_goal = pace.finish_goal;
    plain.knock_out_at = pace.knock_out_by_input_at;
    for (u32 t = 0; t < out.final_steps; ++t)
    {
      const std::array<u8, 12> inputs = Row(t);
      net::Rollback::UseInputs(plain.g, t, {0, 1}, inputs.data());
      plain.Step(t);
      if (t + 1000 >= out.final_steps && net::Rollback::KeptChecksum(race.g, t) != net::StateChecksum(plain.g, MODULE))
        ++out.other_checksums;
    }
  }

  // What the game sent for the frames counted, against what the relay kept.
  void CountTheInputs()
  {
    for (const u32 f : counted)
    {
      out.sent_twice += last_sent[f] != first_sent[f] ? 1 : 0;
      if (const auto it = official.find(f); it != official.end())
        out.not_stamped += it->second != last_sent[f] ? 1 : 0;
    }
  }
};

// The rollback log's columns into what came of the race.
void TakeTheLog(const std::string& path, const Pace& pace, Raced& out)
{
  auto log = ReadLog(path);
  out.sched = log["sched"];
  out.held = log["held"];
  out.step = log["step"];
  out.depth = log["depth"];
  out.catch_up = log["catch_up"];
  out.watch = log["watch"];
  out.speed = log["speed"];
  out.buffer = log["buffer"];
  // (a race that ends has fewer frames in the race phase than it was given)
  out.ran = out.ran && (pace.race_to != Pace().race_to || out.sched.size() + 2 >= pace.race_frames) &&
            out.held.size() == out.sched.size() && out.step.size() == out.sched.size();
}

}  // namespace

// A race against a relay with a clock, on a host that keeps the game at real time the way the emulator does: a
// displayed frame begins when it is due, or, when the frame before took longer, at once, until the time lost is
// made up (beyond 100 ms it is given up). The local seat (0) holds a pad that changes in every displayed frame;
// seat 1's input changes every seventh step. The relay is the real one's rules (Online.Server RaceTimeline,
// RaceSession): frame f closes when its clock is at f + 2; a seat's first input for a frame is the one it keeps;
// a frame that closes without one holds the seat's latest; an input for a closed frame is late.
Raced RunAPacedRace(const Pace& pace)
{
  Raced out;
  Udp relay;
  if (!relay.Open())
    return out;
  const std::string folder = Temp();
  Options options;
  options.debug.relay = "127.0.0.1:" + std::to_string(relay.LocalPort()) + ":1f2e3d4c5b6a7988";
  options.debug.rollback_log = folder + "/rb.csv";
  if (pace.record)
    options.replay_file = folder + "/race.fzrp";
  TestHost host;
  auto now = std::chrono::steady_clock::now();  // this test's time, from here on
  host.time = now;
  Init(host, options);
  if (pace.summary)
    host.user_folder = folder;
  host.user_folder_throws = pace.summary_throws;
  FakeRace race(host, FROM, pace.race_to, false);
  race.finish_goal = pace.finish_goal;
  race.knock_out_at = pace.knock_out_by_input_at;
  race.frame_time = Ns(0);  // the pacing below moves the time
  race.g.Write32(layout::SLOT + 44, pace.delay);  // the seat slot's input delay, which the session reads when it begins

  PacedRelay r(pace, relay, out);
  auto due = now;       // the host's pacing: when the next displayed frame is due
  bool undone = false;  // (Pace::undo_at_behind)
  u32 race_frames = 0, frames = 0;
  while (race_frames < pace.race_frames && host.stops == 0)
  {
    // the host: the frame begins when due; later, when the one before ran over, and what it ran over is made up by
    // the frames after it (beyond 100 ms it is given up, as the emulator does)
    if (now < due)
      now = due;
    else if (now - due > Ms(100))
      due = now - Ms(100);
    due += pace.follow_speed ? Ns(static_cast<long long>(static_cast<double>(FRAME.count()) / host.speed)) : FRAME;
    host.time = now;
    const u32 step = race.g.Read32(net::STEP);
    const bool racing = step >= FROM && race.g.Read32(layout::CONTROL + 120) != 0;
    // the pad: another record in every displayed frame
    ++frames;
    const Record pad = {0, static_cast<u8>(frames % 100), static_cast<u8>(frames / 100), 0, 0, 0};
    race.g.CopyTo(MODULE + net::BSS + 0xA88, pad.data(), 6);
    BeforeTheFrame(race, pace, out, step, pad, racing, race_frames, undone);

    // the relay, at this moment: what the game sent since the last frame, a pong for a ping, and the frames its
    // clock has closed (before the race: those lockstep needs)
    const double clock_ms = r.Clock(now, racing, race_frames);
    r.CloseFrames(step, clock_ms);
    r.Hear(racing, race_frames, clock_ms);
    r.Say(now);

    if (racing && pace.picture_us != 0)
      host.pictures.push_back(pace.picture_us + (pace.picture_late_every != 0 && race_frames % pace.picture_late_every == 0 ?
                                                     pace.picture_late_us : 0));
    if (!race.Frame())
      break;
    // what the frame took
    double work = 5 + 1.5 * race.g.Read32(layout::CONTROL + 8);
    if (racing && !pace.work.empty() && pace.work[race_frames % pace.work.size()] != 0)
      work = pace.work[race_frames % pace.work.size()];
    work = racing && pace.stall_ms != 0 && race_frames == pace.stall_at ? pace.stall_ms : work;
    race_frames += racing ? 1 : 0;
    now += Ms(work);
  }
  out.ran = host.stops == 0 && host.log.empty() && race_frames == pace.race_frames;

  out.final_steps = race.g.Read32(layout::CONTROL + 232);
  r.CompareWithTheOfficialRace(race);
  out.next_input_end = net::s_session ? net::s_session->next_input : 0;
  Shutdown();
  r.CountTheInputs();
  for (const auto& [frame, record] : r.official)  // (every frame from 0 that the relay closed, in their order)
    out.official_mine.push_back(record);
  TakeTheLog(options.debug.rollback_log, pace, out);
  if (pace.summary)
  {
    std::ifstream file(folder + "/race-summary.txt", std::ios::binary);
    out.summary.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
  }
  if (pace.record)
    out.results = ResultsOf(folder + "/race.fzrp");
  out.warnings = host.log;
  out.notices = host.notices;
  Init(host, Options{});
  return out;
}

namespace
{
// A host of a schedule test: what its race's frames take (Pace::work).
struct PacedHost
{
  const char* what;
  std::vector<double> work;
};

// Hosts that hold 60 frames a second unevenly: a long frame, and the host makes its time up in the frame after.
// They never have four frames in a row without a long one (PACE_CALM_FRAMES).
const std::vector<PacedHost> UNEVEN_HOSTS = {
    {"frames of 19 and 14.33 ms in turn", {19, 14.33}},
    {"one frame of 20 ms in four", {20, 0, 0, 0}},
    {"one frame of 30 ms in four", {30, 0, 0, 0}},
};
}  // namespace

// A host that holds 60 frames a second, with a deep rollback's frames now and then: every 15th frame takes 49 ms and
// the one after it 36 (they often come in twos). The game is to stay between 1 step behind its schedule and 2
// ahead of it. It did not: after each long frame it added catch-up steps while the host made the same time up, and
// ended that many steps ahead. With the time sync as it was this race sits 3.0 steps ahead at the median; now 1.0.
// Nothing a step computes changes: every final step's checksum is the one a game given only the official inputs
// has after that step.
// The same of the hosts that hold the pace unevenly (UNEVEN_HOSTS).
void TestTheGameKeepsToItsSchedule()
{
  std::vector<PacedHost> hosts = {{"49 and 36 ms every 15 frames", std::vector<double>(15, 0.0)}};
  hosts[0].work[13] = 49;
  hosts[0].work[14] = 36;
  hosts.insert(hosts.end(), UNEVEN_HOSTS.begin(), UNEVEN_HOSTS.end());
  for (const PacedHost& host : hosts)
  {
    const int failed_before = s_failed;
    Pace pace;
    pace.work = host.work;
    const Raced r = RunAPacedRace(pace);
    CHECK(r.ran);
    if (!r.ran)
      continue;
    CHECK(r.final_steps > 1200 && r.other_checksums == 0);
    const std::vector<double> settled(r.sched.begin() + SETTLED, r.sched.end());
    const double low = Percentile(settled, 0.10), middle = Percentile(settled, 0.5), high = Percentile(settled, 0.90);
    CHECK(middle >= -1.0 && middle <= 1.5);
    CHECK(high <= 2.25);
    CHECK(low >= -1.5);  // (a long frame's own dip is below it)
    CHECK(r.late == 0 && r.sent_twice == 0);
    if (s_failed != failed_before)
      std::printf("  a host with %s: steps ahead of the schedule from frame %u on: 10%% %.2f, median %.2f, 90%% %.2f; "
                  "inputs late %u of %zu; frames the game sent two inputs for: %u\n",
                  host.what, SETTLED, low, middle, high, r.late, r.margin.size(), r.sent_twice);
  }
}

// A host with time to spare, and a relay whose clock stands for five steps at frame 600 (as when the game's
// estimate of it moves): the game is five steps ahead of a schedule it was on.
// - It falls back by frames that run no new step, and is within 2 again; it must not stay where it was, to the
//   race's end (the wait it made in the hook the host made up).
// - A held frame and the frame after it begin at the same step, and each reads the pad. The game keeps for that
//   step's input what it sent first, as the relay does. (It kept the second: its own machine then ran on an input
//   the relay never used, and was put right a round trip later.)
// The same on the hosts that hold the pace unevenly (UNEVEN_HOSTS). (There the game was never held. The hold's
// limit keeps the lead a host's long frames need: every long frame's over-length was added to it, the short frame
// after took nothing off, and such a host never has the four frames without a long one that emptied it. So the
// limit stood at its most, 5.5 steps, for as long as the host went on so, and the game stayed up to 4.9, 5.0 and 5.2
// steps ahead to the race's end with no frame held. A short frame now takes off what it makes up.)
void TestAGameAheadOfItsScheduleFallsBack()
{
  constexpr u32 STANDS_AT = 600;
  std::vector<PacedHost> hosts = {{"time to spare", {}}};
  hosts.insert(hosts.end(), UNEVEN_HOSTS.begin(), UNEVEN_HOSTS.end());
  for (const PacedHost& host : hosts)
  {
    const int failed_before = s_failed;
    Pace pace;
    pace.race_frames = 1000;
    pace.work = host.work;
    pace.stands_at = STANDS_AT;
    pace.stands_steps = 5;
    const Raced r = RunAPacedRace(pace);
    CHECK(r.ran);
    if (!r.ran)
      continue;
    CHECK(r.other_checksums == 0);
    const std::vector<double> before(r.sched.begin() + SETTLED, r.sched.begin() + STANDS_AT);
    CHECK(Percentile(before, 0.0) >= -1.5 && Percentile(before, 1.0) <= 2.0);
    // five steps ahead at first, and within 2 again from a second on
    const double jumped = *std::max_element(r.sched.begin() + STANDS_AT, r.sched.begin() + STANDS_AT + 60);
    const std::vector<double> after(r.sched.begin() + STANDS_AT + 60, r.sched.end());
    const double after_most = Percentile(after, 1.0);
    CHECK(jumped > 3.5);
    CHECK(after_most <= 2.1);
    double holds = 0;
    for (std::size_t i = STANDS_AT; i < r.held.size(); ++i)
      holds += r.held[i];
    CHECK(holds >= 2);
    // a held frame ran no new step: the frame after it begins at the same step; any other begins at a later one
    u32 held_wrong = 0;
    for (std::size_t i = 0; i + 1 < r.step.size(); ++i)
      held_wrong += (r.held[i] != 0) != (r.step[i + 1] == r.step[i]);
    CHECK(held_wrong == 0);
    // and the game never sent two inputs for one frame: what it runs its own seat on is what the relay has
    CHECK(r.sent_twice == 0 && r.late == 0);
    if (s_failed != failed_before)
      std::printf("  a host with %s: steps ahead of the schedule: after the relay's clock stood: at most %.2f, and "
                  "from 1 s on at most %.2f; %.0f frames held; frames the game sent two inputs for: %u; inputs late "
                  "%u\n",
                  host.what, jumped, after_most, holds, r.sent_twice, r.late);
  }
}

// A host that does not hold 60 frames a second must not pay for the above with its inputs. Its frames are long one
// after the other; it makes up nothing, so nothing it lost is counted as coming back, and every step lost is caught
// up at once. And it is held back, when it gets ahead, only beyond the lead its long frames need: that lead is what
// keeps its inputs in time after the next of them.
// For each host: no input of the local seat reaches the relay after its frame closed, and the least time to spare
// (in frames) is what the time sync gave before it kept an account. (With every long frame counted, the account of
// e1f82c4: 0.27 at 55 frames a second for 1.82, 0.20 at 50 for 1.60, and at 40 two inputs in three late. With a
// hold whatever the host: one input in five late when half the frames take 30 ms.)
void TestASlowHostSendsItsInputsInTime()
{
  struct Host
  {
    const char* what;
    std::vector<double> work;
    double least;  // the least margin asked, a little under the one measured
  };
  const std::vector<Host> hosts = {
      {"55 frames a second", {1000.0 / 55}, 1.7},
      {"50 frames a second", {1000.0 / 50}, 1.5},
      {"40 frames a second", {1000.0 / 40}, 0.9},
      {"50 frames a second, unevenly: 26 and 14 ms", {26, 14}, 0.9},
      {"four frames with time to spare, then six of 30 ms", {0, 0, 0, 0, 30, 30, 30, 30, 30, 30}, 0.3},
      {"three long frames together (44, 68, 25 ms) every 15", {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 44, 68, 25}, 1.5},
  };
  for (const Host& host : hosts)
  {
    Pace pace;
    pace.work = host.work;
    const Raced r = RunAPacedRace(pace);
    const double least = Percentile(r.margin, 0.0);
    const bool in_time = r.ran && r.other_checksums == 0 && r.margin.size() > 1000 && r.late == 0 && least >= host.least;
    CHECK(in_time);
    if (!in_time)
      std::printf("  a host at %s: ran %d; inputs late %u of %zu; the least time to spare %.2f frames (asked: %.2f); "
                  "final steps with another checksum %u\n",
                  host.what, r.ran, r.late, r.margin.size(), least, host.least, r.other_checksums);
  }
}
