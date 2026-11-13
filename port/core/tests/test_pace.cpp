// When a race's steps run: the time sync against a host that paces the game as the emulator does and a relay that
// keeps and closes inputs as the real one does, in time this test moves itself (TestHost::time), so nothing here
// depends on how busy the computer is.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "harness.h"
#include "paced_race.h"
#include "fzgx/freeze.h"
#include "fzgx/rollback_worker.h"

#include <condition_variable>
#include <mutex>

#include <map>
#include <numeric>
#include <set>
#include <sstream>

namespace
{
using Ns = std::chrono::nanoseconds;
constexpr Ns FRAME{16'683'334};  // the host's displayed frame: the console's field (FIELD_MS), rounded up

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
  bool ended = false;   // the relay has ended the race (Pace::relay_ends_at)

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
  double Clock(std::chrono::steady_clock::time_point now, bool racing, u32 race_frames, bool stands = true)
  {
    if (stands && racing && pace.stands_steps != 0 && race_frames == pace.stands_at)
      clock_back += FRAME * pace.stands_steps;
    const double clock_ms =
        go_at ? std::chrono::duration<double, std::milli>(now - *go_at - clock_back).count() * pace.relay_rate : 0;
    return clock_ms;
  }

  // The frames its clock has closed (before the race: those lockstep needs).
  void CloseFrames(u32 step, double clock_ms)
  {
    if (ended)
      return;
    // (the countdown; and the end of a race that ends, with the results after it: closed a quarter second early,
    // since the frame that settles the race waits for them in time that stands while a frame runs)
    const bool lockstep = step < FROM || step + 16 >= pace.race_to;
    // (what its clock has closed is closed before it reads what arrived now: an input that comes at the moment its
    // frame is due is late)
    const u32 by_clock = clock_ms / FIELD_MS >= 2 ? static_cast<u32>(clock_ms / FIELD_MS) - 1 : 0;
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
            out.margin.push_back(static_cast<double>(f + 2) - clock_ms / FIELD_MS);
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
    if (game && ended)
    {
      // the race ended: the frames closed before the end (all of them reach the game), then "over" with the relay's
      // count, which may be past them (Pace::relay_end_extra)
      for (u32 first = std::min(game_has, closed > 12 ? closed - 12 : 0); first < closed; first += 6)
      {
        const u32 count = std::min<u32>(6, closed - first);
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
      std::string over = FromRelay('\x04');   // seat 0 of the player seats 0 and 1, no cookie, "over"
      Add32(over, 1), over += '\0';
      Add32(over, 3);
      over.append(8, '\0');
      over += '\x02';
      Add32(over, closed + pace.relay_end_extra);
      relay.Send(over.data(), over.size(), *game);
      return;
    }
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
      u32 told = closed > pace.relay_back_steps ? closed - pace.relay_back_steps : 0;
      if (pace.rows_together != 0 && told >= pace.together_from && told < pace.together_to)
        told -= (told - pace.together_from) % pace.rows_together;
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
    // The rings hold 1024 steps, and the game's own steps past its final ones (its predictions, the steps a rollback
    // worker's open job still runs) have written their slots: step t's entries stand while t + 1024 > the game's step.
    // The last 1000 final steps are compared, less those whose slots a step 1024 later has taken: with the worker
    // slow (a PC of few cores), the game ran more than 24 steps past its final ones, and the slots of the first steps
    // of the 1000 held the checksums of the game's newest steps, not theirs (seen on GitHub's 4-core runner, 2026-11-13).
    const u32 from = std::max<int64_t>(0, std::max<int64_t>(static_cast<int64_t>(out.final_steps) - 1000,
                                                             static_cast<int64_t>(out.step_end) - 1024));
    for (u32 t = 0; t < out.final_steps; ++t)
    {
      const std::array<u8, 12> inputs = Row(t);
      net::Rollback::UseInputs(plain.g, t, {0, 1}, inputs.data());
      plain.Step(t);
      if (t < from)
        continue;
      ++out.compared;
      if (net::Rollback::KeptChecksum(race.g, t) != net::StateChecksum(plain.g, MODULE))
      {
        if (out.other_checksums++ == 0)
          out.first_other_checksum = t;
      }
      std::array<u8, 12> ran{};
      net::Rollback::UsedInputs(race.g, t, {0, 1}, ran.data());
      if (ran != inputs && out.other_rows++ == 0)
        out.first_other_row = t;
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
namespace
{
// The deep-rollback worker's adapter, as port/moderngekko's runs it (RunRollbackWorker, ThreadMain), on a fake race:
// after each network hook a job begun for a slot wakes that slot's thread, which makes the job's base (TakeBase) and
// runs the race's steps on the slot's memory and window (FakeRace::StepOn), the worker's hook before each, until the
// hook says the job is over.
struct FakeWorkers
{
  explicit FakeWorkers(FakeRace& r) : race(r)
  {
    rollback_worker::SetPipePerThread(true);
    for (int k = 0; k < 2; ++k)
      slots[k].thread = std::thread([this, k] { Main(k); });
    race.after_net = [this] { AfterTheHook(); };
  }
  ~FakeWorkers()
  {
    rollback_worker::Stop();
    for (Slot& slot : slots)
    {
      {
        const std::lock_guard lock(slot.m);
        slot.quit = true;
      }
      slot.cv.notify_all();
      slot.thread.join();
    }
    rollback_worker::SetPipePerThread(false);
    race.after_net = nullptr;
  }
  void AfterTheHook()
  {
    for (int k = 0; k < 2; ++k)
    {
      rollback_worker::Memory memory;
      if (!rollback_worker::Begin(k, &memory) || !memory.threaded)
        continue;
      {
        const std::lock_guard lock(slots[k].m);
        slots[k].go = true;
      }
      slots[k].cv.notify_all();
    }
  }
  void Main(int k)
  {
    rollback_worker::ThreadStarts(k);
    Slot& slot = slots[k];
    std::unique_lock lock(slot.m);
    for (;;)
    {
      slot.cv.wait(lock, [&] { return slot.go || slot.quit; });
      if (slot.quit)
        return;
      slot.go = false;
      lock.unlock();
      if (rollback_worker::TakeBase(k))
      {
        Guest gw(*rollback_worker::OwnHost(), rollback_worker::Window());
        std::vector<u8> no_cache;
        do
        {
          const u32 t = gw.Read32(net::STEP);
          race.StepOn(gw, no_cache, t);
          gw.Write32(net::STEP, t + 1);
          ++steps;
        } while (rollback_worker::Hook());
      }
      rollback_worker::Left(k);
      lock.lock();
    }
  }
  struct Slot
  {
    std::thread thread;
    std::mutex m;
    std::condition_variable cv;
    bool go = false, quit = false;
  };
  FakeRace& race;
  Slot slots[2];
  std::atomic<u64> steps{0};
};
}  // namespace

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
  if (!pace.worker.empty())
  {
    options.debug.worker_mode = pace.worker;
    options.debug.worker_rule = "always";
    options.debug.worker_depth = pace.worker_depth;
    options.debug.worker_workers = pace.workers;
    options.debug.worker_from = FROM + 1;
  }
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
  std::optional<FakeWorkers> workers;
  if (!pace.worker.empty())
    workers.emplace(race);
  if (pace.fault_at != 0)
    race.after_net = [&, before = race.after_net] {
      if (before)
        before();
      // the session's account of step fault_at says "the official row" while the step ran on another (a prediction)
      // and has not run again since: the fault is made at every hook until the step is final or ran on that row
      net::Session* session = net::s_session.get();
      const u32 t = pace.fault_at;
      if (session == nullptr || t < session->checked || !session->used.contains(t) || !session->my_inputs.contains(t))
        return;
      net::Row official(12);
      std::copy_n(session->my_inputs.at(t).begin(), 6, official.begin());
      const std::array<u8, 12> relays = r.Row(t);
      std::copy(relays.begin() + 6, relays.end(), official.begin() + 6);
      std::array<u8, 12> ran{};
      net::Rollback::UsedInputs(race.g, t, {0, 1}, ran.data());
      if (std::equal(ran.begin(), ran.end(), official.begin()))
        return;
      session->used[t] = official;
      ++out.faults;
    };
  auto due = now;       // the host's pacing: when the next displayed frame is due
  bool undone = false;  // (Pace::undo_at_behind)
  u32 race_frames = 0, frames = 0;
  bool racing = false;
  // The session waiting for official frames (session.cpp WaitFor: the game MAX_AHEAD steps past the frames it holds, a
  // lockstep frame whose official frame is not here): it sleeps 500 us, reads the time and drains the relay's packets,
  // over and again, until the frame is here or the time is up. This relay is on the game's thread and the time is this
  // test's: nothing would come and the time would stand, for ever (seen on GitHub's 4-core runner, 2026-11-13, and on
  // four cores here, about one run in twelve: with the worker's thread short of time the game's steps fall otherwise,
  // and a lockstep frame of the watching game found its official frame not yet told, the relay's frames coming four
  // steps back and in bursts of three; one more closed frame ends such a wait). Here, at each read of the time while
  // the core says it waits (the freeze pulse), the time moves on by
  // the wait's sleep, and when the relay's clock reaches another frame the relay does what the real one does: closes it
  // and sends what it holds (then only: Drain reads the time at each packet it takes, and a relay that sent at every
  // read would never let it empty the socket). The wait ends as it does in a game; how often the game read the time
  // waiting, and at which step first, is in what came of the race (Raced::relay_waits).
  const std::thread::id game_thread = std::this_thread::get_id();
  host.on_now = [&] {
    if (std::this_thread::get_id() != game_thread || !freeze::ThePulse().waits_for_relay.load(std::memory_order_relaxed))
      return;
    const u32 step = race.g.Read32(net::STEP);
    if (out.relay_waits++ == 0)
    {
      out.first_relay_wait = step;
      const net::Session* s = net::s_session.get();
      std::printf("  (the game waited for the relay at step %u, frame %u of the race: %zu official frames, the relay had "
                  "closed %u, watch %d)\n", step, race_frames, s ? s->official.size() : 0, r.closed,
                  s ? static_cast<int>(s->watch) : -1);
    }
    now += std::chrono::microseconds(500);
    host.time = now;
    const double clock_ms = r.Clock(now, racing, race_frames, false);
    const u32 closed = r.closed;
    r.CloseFrames(step, clock_ms);
    if (r.closed != closed)
    {
      r.Hear(racing, race_frames, clock_ms);
      r.Say(now);
    }
  };
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
    racing = step >= FROM && race.g.Read32(layout::CONTROL + 120) != 0;
    // the pad: another record in every displayed frame
    ++frames;
    const Record pad = {0, static_cast<u8>(frames % 100), static_cast<u8>(frames / 100), 0, 0, 0};
    race.g.CopyTo(MODULE + net::BSS + 0xA88, pad.data(), 6);
    BeforeTheFrame(race, pace, out, step, pad, racing, race_frames, undone);

    // the relay, at this moment: what the game sent since the last frame, a pong for a ping, and the frames its
    // clock has closed (before the race: those lockstep needs)
    const double clock_ms = r.Clock(now, racing, race_frames);
    if (pace.relay_ends_at != 0 && racing && race_frames >= pace.relay_ends_at && !r.ended)
    {
      r.ended = true;
      out.relay_closed = r.closed;
    }
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

  host.on_now = nullptr;
  out.final_steps = race.g.Read32(layout::CONTROL + 232);
  out.step_end = race.g.Read32(net::STEP);
  r.CompareWithTheOfficialRace(race);
  workers.reset();
  out.next_input_end = net::s_session ? net::s_session->next_input : 0;
  out.stops = static_cast<u32>(host.stops);
  Shutdown();
  out.exit_code = ExitCode();
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

// A host with time to spare shows a frame every field, 59.94 a second, and the schedule runs at that rate (FIELD_MS,
// and the relay's frames): over six thousand frames the game adds no step to catch up and holds none, and stays
// where it is against its schedule. (The schedule ran at 60 a second: every 1,001 frames the game was a step behind
// it and ran two steps in one frame, a jump in the picture at a high refresh rate.) The control is that relay, its
// clock 1001/1000 fast: a catch-up step about every 1,001 frames.
void TestTheScheduleRunsAtTheConsolesFieldRate()
{
  const auto count = [](const std::vector<double>& column) {
    return std::accumulate(column.begin() + SETTLED, column.end(), 0.0);
  };
  Pace pace;
  pace.race_frames = 6200;
  const Raced r = RunAPacedRace(pace);
  CHECK(r.ran && r.final_steps > 6000 && r.other_checksums == 0 && r.late == 0);
  if (r.ran)
  {
    const auto [low, high] = std::minmax_element(r.sched.begin() + SETTLED, r.sched.end());
    CHECK(count(r.catch_up) == 0 && count(r.held) == 0);
    CHECK(*high - *low < 0.5);
    if (count(r.catch_up) != 0 || count(r.held) != 0 || *high - *low >= 0.5)
      std::printf("  at the field rate: %.0f catch-up steps, %.0f frames held, %.2f to %.2f steps ahead of the schedule\n",
                  count(r.catch_up), count(r.held), *low, *high);
  }
  pace.relay_rate = 1.001;
  const Raced at_60 = RunAPacedRace(pace);
  CHECK(at_60.ran && at_60.other_checksums == 0);
  if (at_60.ran)
    CHECK(count(at_60.catch_up) >= 5 && count(at_60.catch_up) <= 7 && count(at_60.held) == 0);
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

// The relay ends the race (the operator's end from the dashboard, or the relay's time limit): it sends nothing more,
// and answers the game's Join "over" with its count of frames, here one past those it sent (a frame closed as it
// ended, sent to no game). Every game ends its race at the last frame it was sent, the same step in every game: a
// racer ahead of the official frames on predictions goes back to it (running again on the official rows a step it
// predicted wrong), a racer who is out watching behind in lockstep plays up to it. There every step before it is final
// on its official row, the results are taken, and the game stops as leaving the results does (exit code 0: nothing
// kept for a rejoin). (The race m211, 2026-11-07, ended from the dashboard: every racer's game waited for frames that
// never came, stood still and was closed by its player, its replay never closed.)
void TestARaceTheRelayEndsEndsAtOneStep()
{
  struct Case
  {
    const char* what;
    u32 lag, changes_every, out_at;
    bool predicted_wrong;
  };
  for (const Case& c : {Case{"a racer ahead of the official frames", 3, 7, 0, false},
                        Case{"a racer whose every prediction is wrong", 3, 1, 0, true},
                        Case{"a racer who is out, watching behind", 0, 7, 300, false}})
  {
    const int failed_before = s_failed;
    Pace pace;
    pace.race_frames = 900;
    pace.record = true;
    pace.relay_back_steps = c.lag;
    pace.other_changes_every = c.changes_every;
    pace.out_at = c.out_at;
    pace.relay_ends_at = 600;
    const Raced r = RunAPacedRace(pace);
    const u32 end = r.relay_closed;
    CHECK(end > FROM + 500);
    CHECK(r.stops == 1 && r.exit_code == 0);
    CHECK(r.final_steps == end);
    CHECK(r.other_checksums == 0 && r.other_rows == 0);
    CHECK(!r.results.empty());
    CHECK(r.warnings.empty());
    CHECK(r.notices.find(std::format("fzgx: the relay ended the race at its frame {} ({} official frames here", end + 1,
                                     end)) != std::string::npos);
    CHECK(r.notices.find(std::format("fzgx: the race ended at step {}, where the relay ended it", end)) !=
          std::string::npos);
    CHECK(r.notices.find("the race is kept for a rejoin") == std::string::npos);
    CHECK(r.notices.find("no official inputs") == std::string::npos);
    if (c.predicted_wrong)
      CHECK(r.notices.find("ran on a predicted row; from it to the end again on the official rows") != std::string::npos);
    if (s_failed != failed_before)
      std::printf("  %s: the relay's end at %u; final steps %u, stops %u, exit code %d, other checksums %u, rows %u\n%s\n%s\n",
                  c.what, end, r.final_steps, r.stops, r.exit_code, r.other_checksums, r.other_rows, r.warnings.c_str(),
                  r.notices.substr(r.notices.size() > 3000 ? r.notices.size() - 3000 : 0).c_str());
  }
}
