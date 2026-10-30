// What every test file of the core uses: the count of checks, the host that is 24 MiB of plain memory, a game image
// and a game for a session to run, this run's temporary folder, and a replay file made by hand. The tests are in
// test_*.cpp; main.cpp runs them and holds their time limit.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdio>
#include <algorithm>
#include <array>
#include <atomic>
#include <memory>
#include <bit>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "fzgx/core.h"
#include "page_memory.h"
#include "internal.h"
#include "replay.h"
#include "rollback.h"
#include "udp.h"
#include "from_relay.h"

using namespace fzgx;

extern int s_failed;
extern int s_checks;

// Tells the time limit (main.cpp) which test is running and where its latest check is.
void AtCheck(const char* function, const char* file, int line);

#define CHECK(condition)                                                                                               \
  do                                                                                                                   \
  {                                                                                                                    \
    ++s_checks;                                                                                                        \
    AtCheck(__func__, __FILE__, __LINE__);                                                                             \
    if (!(condition))                                                                                                  \
    {                                                                                                                  \
      ++s_failed;                                                                                                      \
      std::printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #condition);                                               \
    }                                                                                                                  \
  } while (0)

// The first line of a test whose first check is in one of its steps (a function of another name): the test's time
// begins here, and the time limit names this test.
#define BEGIN_TEST() AtCheck(__func__, __FILE__, __LINE__)

class TestHost : public Host
{
public:
  TestHost() : ram(layout::RETAIL_RAM_SIZE, 0) {}
  u8* Ram() override { return ram.data(); }
  u32 RamSize() override { return static_cast<u32>(ram.size()); }
  u8* Scratch() override { return scratch.empty() ? nullptr : scratch.data(); }
  u32 ScratchSize() override { return static_cast<u32>(scratch.size()); }
  void RequestStop() override { ++stops; }
  float EmulationSpeed() override { return speed; }
  float CpuClock() override { return clock; }
  void SetPace(float s, float c) override { speed = s; clock = c; }
  void Log(LogLevel level, const std::string& text) override
  {
    (level != LogLevel::Notice ? log : notices) += text + "\n";
  }
  bool ReadGameFile(const std::string&, std::vector<u8>*) override { return false; }
  void CodeChanged(u32 address, u32 size) override { changed.emplace_back(address, size); }
  void Recorder(std::string* e, std::string* m) override
  {
    *e = engine;
    *m = module;
  }
  std::chrono::steady_clock::time_point Now() override
  {
    ++*clock_reads;
    return time ? *time : std::chrono::steady_clock::now();
  }
  std::string CardFolder() override { return card_folder; }
  // The pictures this host handed to the screen since it was last asked: the times between them, in microseconds
  // (a test puts them into `pictures`; a host that shows none answers 0, as the core's own).
  u32 PictureIntervals(u32* out, u32 most) override
  {
    const u32 n = std::min<u32>(static_cast<u32>(pictures.size()), most);
    std::copy(pictures.begin(), pictures.begin() + n, out);
    pictures.erase(pictures.begin(), pictures.begin() + n);
    return n;
  }
  std::vector<u32> pictures;
  std::string UserFolder() override
  {
    if (user_folder_throws)
      throw std::runtime_error("the test's host has no user folder to name");
    return user_folder;
  }

  // The time the session reads. Empty: the steady clock. Set: this test's own; every displayed frame of a FakeRace
  // then moves it on by FakeRace::frame_time, as a host that paces the game at 60 frames a second does. A race
  // whose frames pass as fast as the test can run them is far ahead of any relay's clock, and a game ahead of its
  // schedule runs no new steps.
  std::optional<std::chrono::steady_clock::time_point> time;
  // How often the session has read the time. A session that waits for official frames reads it in a loop, and one
  // that runs a frame reads it a few times a step: a test's relay tells a waiting game from a busy one by this,
  // and not by how long a frame has stood on the wall clock, which says nothing on a loaded PC.
  // (shared, so that a host can still be copied and assigned: a copy counts with its original)
  std::shared_ptr<std::atomic<unsigned long long>> clock_reads = std::make_shared<std::atomic<unsigned long long>>(0);

  std::string engine, module;
  std::string card_folder;  // the folder of the player's saves; empty: the host has none
  std::string user_folder;  // the folder a race's summary line goes to; empty: the host has none
  bool user_folder_throws = false;  // asked for that folder, the host throws (a summary that fails while it is made)

  std::vector<std::pair<u32, u32>> changed;
  std::vector<u8, PageMemory<u8>> ram;
  std::vector<u8> scratch;  // the locked cache; empty: the host has none
  bool guardable = false;  // every write into `ram` goes through ram.data(): the undo log may guard it
  bool RamGuardable(std::string* why) override { return guardable || Host::RamGuardable(why); }
  int stops = 0;
  float speed = 1.0f, clock = 6.0f;
  std::string log, notices;
};

// A game image for the undo log: main.rel's header pointer, and noise where the simulation's memory is.
constexpr u32 MODULE = 0x801BA660;

// main.rel linked, as the game's loader leaves it: the head of the system's module list is its header, and the
// header's first word is its module number.
inline void LinkMainRel(Guest& g)
{
  g.Write32(net::MODULE_POINTER, MODULE);
  g.Write32(MODULE, game::MAIN_REL_ID);
}

inline void FakeGame(TestHost& host, Guest& g, std::mt19937& rng)
{
  for (u8& b : host.ram)
    b = static_cast<u8>(rng());
  LinkMainRel(g);
}

// This run's own folder under the temporary folder, made once and removed when the run ends. One shared folder
// (<temp>/fzgx-core-test) for every run fails: two runs at once each empty it under the other (73 of 452 checks
// failed that way).
inline const std::filesystem::path& TempRoot()
{
  static const struct Root
  {
    Root()
    {
      std::random_device random;
      do
      {
        path = std::filesystem::temp_directory_path() / std::format("fzgx-core-test-{:08x}{:08x}", random(), random());
      } while (!std::filesystem::create_directory(path));  // false: the name is taken (by another run)
    }
    ~Root()
    {
      std::error_code ignored;
      std::filesystem::remove_all(path, ignored);
    }
    std::filesystem::path path;
  } root;
  return root.path;
}

// An empty folder for one test (this run's own, emptied).
inline std::string Temp()
{
  const auto& dir = TempRoot();
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  return dir.string();
}

inline void WriteText(const std::string& path, const std::string& text)
{
  std::ofstream(path, std::ios::binary) << text;
}

// A replay file of `racers` racers (all of them player seats), with the starting ranks given: the header, the frame
// preamble, the lineup the recording game appended at go, and two steps.
inline void WriteReplay(const std::string& path, const std::vector<u8>& ranks, const std::string& rules = "",
                 const std::string& more = "", size_t cut = 0, const std::string& steps = "")
{
  const size_t racers = ranks.size();
  std::string seats;
  for (size_t i = 0; i < racers; ++i)
    seats += std::string(i ? "," : "") + "{\"seat\": " + std::to_string(i) + ", \"machine\": " + std::to_string(i + 3) +
             ", \"playerId\": \"p" + std::to_string(i) + "\", \"name\": \"P" + std::to_string(i) + "\"}";
  const u32 mask = racers == 30 ? 0x3FFFFFFF : (1u << racers) - 1;
  const std::string header = "{\"match\": {\"matchId\": \"m9\", \"setup\": {\"course\": 7, \"laps\": 3, " + rules + "\"seats\": [" + seats +
                             "]}}, \"seat\": 1, \"mask\": " + std::to_string(mask) + ", \"delay\": 2" + more + "}";
  std::string file = "FZRP";
  const auto put32 = [&](u32 v) {
    for (int i = 3; i >= 0; --i)
      file += static_cast<char>(v >> (8 * i));
  };
  file += std::string("\0\2", 2);
  put32(static_cast<u32>(header.size()));
  file += header + "FZRF";
  put32(mask);
  put32(1);
  file += "FZRL";
  put32(124);
  std::string lineup(124, '\0');
  for (size_t i = 0; i < racers; ++i)
  {
    lineup[i] = static_cast<char>(i + 3);
    lineup[30 + i] = static_cast<char>(ranks[i]);
  }
  file += lineup + (steps.empty() ? std::string(2 * (racers * 6 + 4), '\0') : steps);
  WriteText(path, file.substr(0, file.size() - cut));
}

// A game for a session, as far as the session can tell: two machines whose state after a step is a hash of their
// state before, of the step's inputs (read from the input ring, as the seat routines read them) and of the generator
// word; a page elsewhere in the simulation's memory written now and then; and the frame routine as netframe.py's:
// the hook before the frame's first step, then the further steps the session asked for, the hook before each.
struct FakeRace
{
  FakeRace(TestHost& h, u32 race_from_step, u32 race_to_step, bool leaking)
      : host(h), g(h, Data()), from(race_from_step), to(race_to_step), leak(leaking)
  {
    first = (MODULE + net::DATA) & ~(net::PAGE - 1);
    machines = first + 0x400000;
    g.Write32(net::MODULE_POINTER, MODULE);
    g.Write32(MODULE + net::MACHINES, machines);
    g.Write8(MODULE + net::MACHINE_COUNT, 2);
    for (u32 i = 0; i < 2; ++i)
    {
      g.Write32(machines + i * game::MACHINE_SIZE + game::MACHINE_PROGRESS, first + 0x500000 + i * 0x200);
      g.Write32(machines + i * game::MACHINE_SIZE + game::MACHINE_POSITION_X, 0x1000 + i);
    }
    g.Write32(net::DOL_RNG, 0x2026);
  }
  void Step(u32 t)
  {
    const u32 rng = g.Read32(net::DOL_RNG);
    for (u32 i = 0; i < 2; ++i)
    {
      u8 record[6];
      g.CopyFrom(record, layout::INPUTS + ((t & 1023) * 30 + i) * 8, 6);
      u32 h = g.Read32(machines + i * game::MACHINE_SIZE + game::MACHINE_POSITION_X) ^ rng ^ (t * 2654435761u);
      for (const u8 b : record)
        h = (h ^ b) * 0x01000193;
      if (leak)
        h ^= g.Read32(0x80003200);  // a word no rollback puts back
      // as the game's collision work area in the locked cache: a machine takes from it what the step before left
      if (!host.scratch.empty())
        h ^= ScratchWord();
      g.Write32(machines + i * game::MACHINE_SIZE + game::MACHINE_POSITION_X, h);
      g.Write32(g.Read32(machines + i * game::MACHINE_SIZE + game::MACHINE_PROGRESS) + game::PROGRESS_DISTANCE, h ^ 0x55AA55AA);
      // and a word the race's results are read from: the thousandths of the best lap (progress record +0x1CC + 10)
      if (lap_times)
        g.Write16(g.Read32(machines + i * game::MACHINE_SIZE + game::MACHINE_PROGRESS) + game::PROGRESS_BEST_LAP + 10, static_cast<u16>(h % 1000));
      // a page that may never have changed before (a late page of the undo log)
      if (h % 7 == 0)
        g.Write32(first + (h % 0x300) * net::PAGE + 0x40, h);
    }
    // a race that ends by what the machines did: each gets on by one a step, and by one more when the first byte
    // of its input is odd, and has finished (flag 0x10000, as the game's) once it has come `finish_goal` far; seat 0
    // once it has come `seat0_goal` far (1: from the race's first step)
    for (u32 i = 0; finish_goal != 0 && t >= from && i < 2; ++i)
    {
      const u32 machine = machines + i * game::MACHINE_SIZE;
      const u32 far = g.Read32(machine + 0x10) + 1 + (g.Read8(layout::INPUTS + ((t & 1023) * 30 + i) * 8) & 1);
      g.Write32(machine + 0x10, far);
      if (far >= (i == 0 ? seat0_goal : finish_goal))
        g.Write32(machine, g.Read32(machine) | 0x10000);
    }
    // a knock-out by the other seat's input (knock_out_at): seat 0 retired (0x08000000, as the game's) in that step
    // unless the first byte of seat 1's input is the step + 1, which the paced relay's official row holds there when
    // that seat changes every step (test_pace.cpp): only a prediction knocks it out
    if (knock_out_at != 0 && t == knock_out_at &&
        g.Read8(layout::INPUTS + ((t & 1023) * 30 + 1) * 8) != static_cast<u8>(t + 1))
      g.Write32(machines, g.Read32(machines) | 0x08000000);
    g.Write32(net::DOL_RNG, rng * 1664525 + 1013904223);
    g.Write32(0x80003200, g.Read32(0x80003200) + 1);
    // and the step leaves there what the next step's machines take (a word that changes every few steps)
    if (!host.scratch.empty())
    {
      const u32 left = (rng >> 20) / 3;
      std::memcpy(host.scratch.data() + SCRATCH_WORD, &left, 4);
    }
    ++steps_run;
  }
  static constexpr u32 SCRATCH_WORD = 0x1DB0 + 0x47C;
  u32 ScratchWord() const
  {
    u32 word;
    std::memcpy(&word, host.scratch.data() + SCRATCH_WORD, 4);
    return word;
  }
  // One displayed frame; false once the session asked the game to stop.
  bool Frame()
  {
    if (host.time)
      *host.time += frame_time;
    const u32 step = g.Read32(net::STEP);
    const bool running = g.Read32(layout::CONTROL + 120) != 0;
    if (finish_goal != 0)
      PhaseByTheMachines(step, running);
    else
      g.Write16(MODULE + net::BSS + 0x3C2A, step < from ? 0x28 : (late_end ? !Ended() : step < to) ? 0x29 : 0x2A);
    g.Write32(layout::CONTROL + 44, 1);  // at the race-start gate
    g.Write32(layout::CONTROL + 12, 0);
    NetSync();
    if (host.stops != 0)
      return false;
    if (!running)
      return true;  // the pass that took the go
    bool over_before = LooksOver();  // (after the hook: a rollback has put the memory back)
    Step(g.Read32(net::STEP));
    g.Write32(net::STEP, g.Read32(net::STEP) + 1);
    g.Write32(layout::CONTROL + 12, 1);
    // (image_loop: the count is read again after every step, as the image does; its "done" reaches the host)
    for (u32 left = g.Read32(layout::CONTROL + 8); left != 0;)
    {
      // the image that holds the end: a frame runs no further step once a step has made the race look over
      const bool over_now = LooksOver();
      if (holds_end && over_now && !over_before)
      {
        ++cuts;
        g.Write32(layout::CONTROL + 8, 0);
        break;
      }
      over_before = over_now;
      NetSync();
      Step(g.Read32(net::STEP));
      g.Write32(net::STEP, g.Read32(net::STEP) + 1);
      left = image_loop ? g.Read32(layout::CONTROL + 8) - 1 : left - 1;
      g.Write32(layout::CONTROL + 8, image_loop ? left : g.Read32(layout::CONTROL + 8));
    }
    g.Write32(layout::CONTROL + 12, 0);
    WindowWritten(layout::CONTROL + 12, 0);  // (a call only with the scrubber on)
    // the game's way out of the race: a count of displayed frames once the steps are past the race's end, kept in
    // the simulation's memory, and the phase changes at its second frame
    if (late_end && g.Read32(net::STEP) > to)
    {
      g.Write32(first + END_COUNT, g.Read32(first + END_COUNT) + 1);
      if (g.Read32(first + END_COUNT) >= 2)
        g.Write32(first + END_COUNT + 4, 1);
    }
    return true;
  }
  bool Ended() const { return g.Read32(first + END_COUNT + 4) != 0; }
  // A race that ends by what the machines did (finish_goal), as the game ends one: every machine has finished.
  bool LooksOver() const
  {
    return finish_goal != 0 && g.Read16(MODULE + net::BSS + 0x3C2A) == 0x29 && (g.Read32(machines) & 0x10000) != 0 &&
           (g.Read32(machines + 0x620) & 0x10000) != 0;
  }
  // What the game does once a displayed frame before the frame's steps, outside them. While the race is on its
  // race-over test runs; from the first "over" the game plays its finish sequence, a few frames here and seconds in
  // the game, with the test saying "over" in each, and then asks for the results phase, which begins in the frame
  // after. The count of those frames, the request and the phase are in memory a rollback puts back; what the
  // phase's first frame does is counted outside it, in `ends`.
  // With holds_end the test is the image's of standin_race.py: "over" only when no step below the one about to run
  // is a prediction (END_OFFICIAL), told to the host (END_OVER); otherwise it asks (END_ASKED) and says "not over".
  void PhaseByTheMachines(u32 step, bool running)
  {
    if (g.Read32(first + END_COUNT) != 0 && !Ended())
    {
      g.Write32(first + END_COUNT + 4, 1);
      ++ends;
    }
    g.Write16(MODULE + net::BSS + 0x3C2A, step < from ? 0x28 : Ended() ? 0x2A : 0x29);
    if (!LooksOver() || g.Read32(first + END_COUNT) != 0)
      return;
    const u32 official = g.Read32(layout::END_OFFICIAL);
    if (holds_end && running && official != 0 && step > official)
    {
      ++asks;
      g.Write32(layout::END_ASKED, 1);
      return;
    }
    if (holds_end && running && official != 0)
      g.Write32(layout::END_OVER, 1);
    if (over_step == 0)
      over_step = step;
    g.Write32(first + END_COUNT + 8, g.Read32(first + END_COUNT + 8) + 1);
    if (g.Read32(first + END_COUNT + 8) > FINISH_FRAMES)
      g.Write32(first + END_COUNT, 1);
  }
  static constexpr u32 FINISH_FRAMES = 3;  // frames of the finish sequence, between the first "over" and the request
  static constexpr u32 END_COUNT = 0x480000;
  TestHost& host;
  Guest g;
  u32 from, to;
  bool leak;
  bool late_end = false;  // the phase leaves the race two displayed frames after step `to` ran, by words a rollback
                          // puts back (as the game's); false: with step `to`, whatever was rolled back
  bool lap_times = false;  // every step moves a word of each machine that the race's results are read from
  u32 finish_goal = 0, seat0_goal = 1;  // finish_goal not 0: the race ends when both machines have finished (Step)
  u32 knock_out_at = 0;    // not 0: seat 0 is knocked out in this step by seat 1's input (Step)
                           // (`to` is not used), and the phase follows the game's race-over test (PhaseByTheMachines)
  bool holds_end = false;  // with finish_goal: the image whose race-over test waits for official steps
  bool image_loop = false;  // the extra steps as the image's own loop runs them (Frame; the replay scrubber)
  u32 ends = 0, asks = 0, cuts = 0;  // the results phase's first frames; the test's asks; frames cut short
  u32 over_step = 0;                 // the step about to run when the race-over test first said "over"
  std::chrono::nanoseconds frame_time{16'666'667};  // what a displayed frame adds to the host's time, when it has one
  u32 first = 0, machines = 0, steps_run = 0;
};

// A wall clock in milliseconds, for a test whose relay is a thread of its own.
inline long long WallMs()
{
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

// The relay: official frames 0 to `floor` - 1 as the test asks before each displayed frame, and one more for
// every LATE_RELAY_MORE times the game has read its clock beyond LATE_RELAY_WAITING since the frame began: the game
// is waiting for it. `reads` is the host's count of the session's clock reads (TestHost::clock_reads), `frame_reads`
// that count when the frame began. A frame that runs, however deep its rollback, reads the clock two times a step
// and a handful more (under a hundred); a frame that waits reads it in a loop. No wall clock: with one, the relay
// took a frame that had stood 20 ms for a waiting one, which a predicting frame does on a loaded PC, and a test
// that kept the relay from doing so (a time far ahead) stood for ever when its game came to wait.
// Joined (seat 0 of the player seats 0 and 1) and go with every batch. `row` gives a step's official inputs: the
// two seats' records. `sent`, when given, is told how far the frames have been sent: a test that must have them at
// the game before a frame begins waits for that, not for a length of time. (test_relay.cpp, test_race_end.cpp)
constexpr unsigned long long LATE_RELAY_WAITING = 600, LATE_RELAY_MORE = 60;
inline void RunTheLateRelay(Udp& relay, const std::atomic<u32>& floor, const std::atomic<unsigned long long>& reads,
                            const std::atomic<unsigned long long>& frame_reads, const std::atomic<bool>& stop,
                            const std::function<std::array<u8, 12>(u32)>& row, std::atomic<u32>* sent = nullptr)
{
  std::optional<Endpoint> game;
  u32 released = 0;
  unsigned long long last_more = 0;
  const auto put32 = [](std::string& p, u32 v) {
    for (int i = 3; i >= 0; --i)
      p += static_cast<char>(v >> (8 * i));
  };
  while (!stop)
  {
    u8 buffer[512];
    std::size_t n = 0;
    Endpoint from;
    while (relay.Receive(buffer, sizeof(buffer), &n, &from))
      game = from;
    released = std::max(released, floor.load());
    const unsigned long long now = reads.load();
    if (now > frame_reads.load() + LATE_RELAY_WAITING && now > last_more + LATE_RELAY_MORE)
    {
      ++released;
      last_more = now;
    }
    if (game)
    {
      std::string joined = FromRelay('\x04');
      put32(joined, 1);
      joined += '\0';
      put32(joined, 3);
      joined += std::string(13, '\0');   // (no cookie, no flags, no frames closed)
      relay.Send(joined.data(), joined.size(), *game);
      std::string go = FromRelay('\x06');
      put32(go, 1);
      put32(go, 0);
      relay.Send(go.data(), go.size(), *game);
      for (u32 first = released > 12 ? released - 12 : 0; first < released; first += 6)
      {
        const u32 count = std::min<u32>(6, released - first);
        std::string official = FromRelay('\x08');
        put32(official, first);
        official += static_cast<char>(count);
        put32(official, 3);
        for (u32 t = first; t < first + count; ++t)
        {
          const std::array<u8, 12> r = row(t);
          official.append(r.begin(), r.end());
        }
        relay.Send(official.data(), official.size(), *game);
      }
      if (sent != nullptr)
        *sent = released;   // (every frame below this has been sent at least once)
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

// the results block is the file's last bytes: JSON, its length, 'FZRS'
inline std::string ResultsOf(const std::string& path)
{
  std::ifstream file(path, std::ios::binary);
  const std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  if (bytes.size() < 8 || bytes.substr(bytes.size() - 4) != "FZRS")
    return std::string();
  u32 size = 0;
  for (int i = 8; i > 4; --i)
    size = size << 8 | static_cast<u8>(bytes[bytes.size() - i]);
  return size + 8 > bytes.size() ? std::string() : bytes.substr(bytes.size() - 8 - size, size);
}
