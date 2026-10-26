// Tests of a match race against a relay that is the test itself, on the loopback.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "harness.h"

namespace
{
// A match race against a relay that is this test, on the loopback: joined, go, and the official frames as the game
// needs them; `also` adds what a test wants sent with every batch (given the step, and a way to send). Returns the
// displayed frames run (`most`, 90 unless a test stops earlier, when nothing stopped the race).
u32 RaceAgainstARelay(TestHost& host, FakeRace& race, Udp& relay,
                      const std::function<void(u32 step, const std::function<void(const std::string&)>& send)>& also,
                      u32 most = 90)
{
  std::optional<Endpoint> game;
  const auto put32 = [](std::string& p, u32 v) {
    for (int i = 3; i >= 0; --i)
      p += static_cast<char>(v >> (8 * i));
  };
  const std::function<void(const std::string&)> send = [&](const std::string& packet) {
    relay.Send(packet.data(), packet.size(), *game);
  };
  u32 frames = 0;
  for (; frames < most; ++frames)
  {
    u8 buffer[512];
    std::size_t n = 0;
    Endpoint from;
    while (relay.Receive(buffer, sizeof(buffer), &n, &from))
      game = from;
    if (game)
    {
      const u32 step = race.g.Read32(net::STEP);
      also(step, send);
      std::string joined = FromRelay('\x04');   // seat 0 of the player seats 0 and 1
      put32(joined, 1);
      joined += '\0';
      put32(joined, 3);
      joined += std::string(13, '\0');   // (no cookie, no flags, no frames closed)
      send(joined);
      std::string go = FromRelay('\x06');
      put32(go, 1);
      put32(go, 0);
      send(go);
      for (u32 first = step > 2 ? step - 2 : 0; first < step + 8; first += 6)
      {
        std::string official = FromRelay('\x08');
        put32(official, first);
        official += static_cast<char>(6);
        put32(official, 3);
        official += std::string(6 * 12, static_cast<char>(first % 5));
        send(official);
      }
    }
    if (!race.Frame())
      break;
  }
  CHECK(game.has_value() && host.stops == 0);
  return frames;
}
}  // namespace

// With every batch a pong whose time is not one this game ever sent (a relay that is broken, or not ours): the
// security audit's finding L2. Taken as a round trip it was a minus of days, the
// race clock followed it, and as a count of tenths of a millisecond it was a number no word holds. The pong is
// ignored: the race runs through, and the round-trip word stays a round trip.
void TestARelayWhoseClockIsNoClock()
{
  Udp relay;
  CHECK(relay.Open());
  Options options;
  options.debug.relay = "127.0.0.1:" + std::to_string(relay.LocalPort()) + ":1f2e3d4c5b6a7988";
  TestHost host;
  host.time = std::chrono::steady_clock::now();
  Init(host, options);
  FakeRace race(host, 10, 60, false);
  const u32 frames = RaceAgainstARelay(host, race, relay, [](u32, const std::function<void(const std::string&)>& send) {
    // "sent" 0x7FFFFFFFFFFFFFFF nanoseconds: long after now; the relay's clock 500 ms
    send(FromRelay('\x02') + std::string("\0\0\0\0\x7F\xFF\xFF\xFF\xFF\xFF\xFF\xFF\0\0\x01\xF4", 16));
  });
  CHECK(frames == 90);
  CHECK(race.g.Read32(net::STEP) > 60);                      // through the countdown, the race and into the results
  CHECK(host.notices.find("fzgx: rollback on") != std::string::npos);   // the race phase ran as a race does
  CHECK(race.g.Read32(layout::CONTROL + 148) <= 10 * 60000);  // the round trip, in tenths of a millisecond
  CHECK(host.log.empty());
  Shutdown();
  CHECK(ExitCode() == 0);   // (closed in the results: a normal leave, no rejoin)
  CHECK(host.notices.find("the race is kept for a rejoin") == std::string::npos);
  Init(host, Options{});
}

// The player closes the game (its window, Escape) while their seat still races: the process ends with
// EXIT_LEFT_RACING, and the launcher keeps the race and offers Rejoin, as after a crash. Closed in the results it is a normal leave: TestARelayWhoseClockIsNoClock.
void TestAGameClosedMidRaceEndsAsLeftRacing()
{
  Udp relay;
  CHECK(relay.Open());
  Options options;
  options.debug.relay = "127.0.0.1:" + std::to_string(relay.LocalPort()) + ":1f2e3d4c5b6a7988";
  TestHost host;
  host.time = std::chrono::steady_clock::now();
  Init(host, options);
  FakeRace race(host, 10, 60, false);
  RaceAgainstARelay(host, race, relay, [](u32, const std::function<void(const std::string&)>&) {}, 30);
  const u32 step = race.g.Read32(net::STEP);
  CHECK(step > 10 && step < 60);   // in the race phase
  CHECK(host.stops == 0 && ExitCode() == 0);
  Shutdown();   // (the window closed: no stop of the game's own)
  CHECK(ExitCode() == EXIT_LEFT_RACING);
  CHECK(host.notices.find("fzgx: the game stopped while its racer raced: the race is kept for a rejoin (exit code 4)") !=
        std::string::npos);
  Init(host, Options{});
  CHECK(ExitCode() == 0);
}

// A game started again into its race (rejoin) whose relay says the race is over (Joined's flag 2): it stops at once, says why, never says it is ready, and asks the process to end with
// EXIT_RACE_OVER (run rj-p3c: no answer came, and the game stood under LOADING until closed). The crashed game's
// replay, the file the launcher names again, stays as it was: this game recorded beside it and removes its own.
void TestAGameStartedIntoARaceThatIsOverStops()
{
  Udp relay;
  CHECK(relay.Open());
  Options options;
  options.debug.relay = "127.0.0.1:" + std::to_string(relay.LocalPort()) + ":1f2e3d4c5b6a7988";
  // (the test host is no boot: no LOADING panel is drawn; the answer is taken in the join, whatever the start)
  options.rejoin = true;
  options.replay_file = Temp() + "/crashed.fzrp";
  WriteText(options.replay_file, "the crashed game's race");
  TestHost host;
  host.time = std::chrono::steady_clock::now();
  Init(host, options);
  CHECK(ExitCode() == 0);
  FakeRace race(host, 10, 60, false);
  std::optional<Endpoint> game;
  int readies = 0;
  for (u32 frame = 0; frame < 60 && host.stops == 0; ++frame)
  {
    u8 buffer[512];
    std::size_t n = 0;
    Endpoint from;
    while (relay.Receive(buffer, sizeof(buffer), &n, &from))
    {
      game = from;
      readies += n >= 4 && buffer[3] == 5;
    }
    if (game)  // seat 0 of the player seats 0 and 1, the race over at frame 0x1234
    {
      const std::string over = FromRelay('\x04') + std::string("\0\0\0\x01\0\0\0\0\x03\0\0\0\0\0\0\0\x07\x02\0\0\x12\x34", 22);
      relay.Send(over.data(), over.size(), *game);
    }
    race.Frame();
  }
  CHECK(host.stops == 1);
  CHECK(readies == 0);
  CHECK(ExitCode() == EXIT_RACE_OVER);
  CHECK(host.notices.find("fzgx: the relay says the race is over (frame 4660): nothing to rejoin; stopping") !=
        std::string::npos);
  CHECK(IsFile(options.replay_file + ".rejoin"));   // (recorded beside it until the answer)
  Shutdown();
  CHECK(ExitCode() == EXIT_RACE_OVER);   // (read by the host after the stop)
  std::string kept;
  CHECK(ReadFileToString(options.replay_file, kept) && kept == "the crashed game's race");
  CHECK(!IsFile(options.replay_file + ".rejoin"));
  CHECK(host.notices.find("not given the race: the crashed game's replay is kept as it was") != std::string::npos);
  Init(host, Options{});
  CHECK(ExitCode() == 0);
}

// A game started again into a race that still runs: once the relay gives it the race (its Joined), its own
// recording, from the race's first step, takes the crashed game's replay's place and goes on there.
void TestAGameStartedAgainTakesTheReplaysPlace()
{
  Udp relay;
  CHECK(relay.Open());
  Options options;
  options.debug.relay = "127.0.0.1:" + std::to_string(relay.LocalPort()) + ":1f2e3d4c5b6a7988";
  options.rejoin = true;
  options.replay_file = Temp() + "/crashed.fzrp";
  WriteText(options.replay_file, "the crashed game's race");
  TestHost host;
  host.time = std::chrono::steady_clock::now();
  Init(host, options);
  FakeRace race(host, 10, 60, false);
  RaceAgainstARelay(host, race, relay, [](u32, const std::function<void(const std::string&)>&) {});
  Shutdown();
  std::string now;
  CHECK(ReadFileToString(options.replay_file, now) && now.size() > 64 && now.find("crashed") == std::string::npos);
  CHECK(!IsFile(options.replay_file + ".rejoin"));
  CHECK(host.notices.find("given the race: this game's replay replaces the crashed game's") != std::string::npos);
  Init(host, Options{});
}

// A relay that is not ours (a LAN server is anyone's; findings L6 and M2): before the race's own "joined" it names
// this game a seat no race has, and player seats 30 and 31; and with every batch it sends official frames a million
// steps ahead, 255 a packet. Neither is taken: the game joins as the seat of the race, keeps none of those frames,
// says so once, and races through.
void TestARelayThatNamesSeatsAndFramesOfNoRace()
{
  Udp relay;
  CHECK(relay.Open());
  Options options;
  options.debug.relay = "127.0.0.1:" + std::to_string(relay.LocalPort()) + ":1f2e3d4c5b6a7988";
  TestHost host;
  host.time = std::chrono::steady_clock::now();
  Init(host, options);
  FakeRace race(host, 10, 60, false);
  const u32 frames = RaceAgainstARelay(host, race, relay, [](u32, const std::function<void(const std::string&)>& send) {
    send(FromRelay('\x04') + std::string("\0\0\0\x01\xC8\0\0\0\x03", 9) + std::string(13, '\0'));      // seat 200
    send(FromRelay('\x04') + std::string("\0\0\0\x01\0\xC0\0\0\x01", 9) + std::string(13, '\0'));      // player seats 0, 30, 31
    send(FromRelay('\x04') + std::string("\0\0\0\x01\0\0\0\0\0", 9) + std::string(13, '\0'));          // no player seats
    for (const u32 first : {1000000u, 0x7FFFFF00u})
      send(FromRelay('\x08') + std::string{static_cast<char>(first >> 24), static_cast<char>(first >> 16),
                                                      static_cast<char>(first >> 8), static_cast<char>(first), '\xFF'} +
           std::string("\0\0\0\x03", 4) + std::string(255 * 12, '\x05'));
  });
  CHECK(frames == 90 && race.g.Read32(net::STEP) > 60);
  CHECK(host.notices.find("joined the race as seat 0 of 2 player seats") != std::string::npos);
  CHECK(host.notices.find("seat 200") == std::string::npos);
  CHECK(host.log.find("the relay names a seat no race has (seat 200") != std::string::npos);
  // said once, whatever came after; nothing else went wrong
  CHECK(std::count(host.log.begin(), host.log.end(), '\n') == 1);
  Shutdown();

  // and with a "joined" that is a race's, the frames of no race are the first thing refused
  TestHost second;
  Udp other;
  CHECK(other.Open());
  options.debug.relay = "127.0.0.1:" + std::to_string(other.LocalPort()) + ":1f2e3d4c5b6a7988";
  second.time = std::chrono::steady_clock::now();
  Init(second, options);
  FakeRace again(second, 10, 60, false);
  bool sent = false;
  RaceAgainstARelay(second, again, other, [&](u32 step, const std::function<void(const std::string&)>& send) {
    if (step < 20)
      return;
    sent = true;
    send(FromRelay('\x08') + std::string("\0\x0F\x42\x40\xFF", 5) + std::string("\0\0\0\x03", 4) + std::string(255 * 12, '\x05'));
  });
  CHECK(sent && again.g.Read32(net::STEP) > 60);
  CHECK(second.log.find("the relay sent official frame 1000000, far past this game's step") != std::string::npos);
  CHECK(std::count(second.log.begin(), second.log.end(), '\n') == 1);
  Shutdown();
  Init(host, Options{});
}

// Packets from the relay's address and port without this seat's key (the client audit's M1): whoever can send from that address (on the path, or forging it) could otherwise stop the game with a
// "desync", hand its seat to the AI or feed it other official frames. Each such packet is dropped: the race runs
// through on the relay's own frames, says so once, and the key is the one the relay works out (Online.Protocol
// RelayKeys: SHA-1 of "fzgx relay key" and the token, its first 8 bytes). And official frames with the key but of
// other player seats, as many (0 and 2 for 0 and 1): the session read a packet's rows by the joined seats, so these
// were taken; a packet's mask must now be the joined one, said once too.
void TestAPacketWithoutTheSeatsKeyIsNotTheRelays()
{
  CHECK(net::RelayKey(TEST_TOKEN) == 0x455c92ed08b334a3ull);
  CHECK(net::RelayKey(1) == 0x4720595509aad07full);
  Udp relay;
  CHECK(relay.Open());
  Options options;
  options.debug.relay = "127.0.0.1:" + std::to_string(relay.LocalPort()) + ":1f2e3d4c5b6a7988";
  TestHost host;
  host.time = std::chrono::steady_clock::now();
  Init(host, options);
  FakeRace race(host, 10, 60, false);
  const u32 frames = RaceAgainstARelay(host, race, relay, [](u32 step, const std::function<void(const std::string&)>& send) {
    const u64 other = 1;  // another seat's token: its key is not this seat's
    send(FromRelay('\x0A', other) + std::string("\0\0\0\x05", 4));                                    // desync
    send(FromRelay('\x0B', other) + std::string(1, '\0'));                                              // seat left
    send(FromRelay('\x0C', other) + std::string("\0\0\0\0\x09", 5));                                   // seat back
    send(FromRelay('\x04', other) + std::string("\0\0\0\x01\x01\0\0\0\x03", 9) + std::string(13, '\0'));  // seat 1
    send(std::string("FZ\x03\x0A\0\0\0\x05", 8));                                                    // no key at all
    std::string official = FromRelay('\x08', other);  // six frames from this step, other records than the relay's
    for (int i = 3; i >= 0; --i)
      official += static_cast<char>(step >> (8 * i));
    official += '\x06';
    official += std::string("\0\0\0\x03", 4) + std::string(6 * 12, '\x7F');
    send(official);
    official.replace(4, 8, FromRelay('\x08').substr(4));  // the seat's key, of seats 0 and 2
    official.replace(17, 4, std::string("\0\0\0\x05", 4));
    send(official);
  });
  CHECK(frames == 90 && race.g.Read32(net::STEP) > 60);
  CHECK(host.notices.find("joined the race as seat 0 of 2 player seats") != std::string::npos);
  CHECK(host.log.find("differs from the majority") == std::string::npos);
  CHECK(host.notices.find("seat 0 left") == std::string::npos);
  CHECK(host.log.find("a packet from the relay's address without this seat's key") != std::string::npos);
  CHECK(host.log.find("official frames of player seats 00000005, this race's are 00000003") != std::string::npos);
  CHECK(std::count(host.log.begin(), host.log.end(), '\n') == 2);  // each said once
  Shutdown();
  Init(host, Options{});
}

// A "joined" whose seat is not one of the player seats it names (found by the relay fuzzer): taken, the game had
// no row of its own and waited for frames it could not run. It is refused as a seat of no race is.
void TestAJoinedWhoseSeatIsNoPlayerSeatIsRefused()
{
  Udp relay;
  CHECK(relay.Open());
  Options options;
  options.debug.relay = "127.0.0.1:" + std::to_string(relay.LocalPort()) + ":1f2e3d4c5b6a7988";
  TestHost host;
  host.time = std::chrono::steady_clock::now();
  Init(host, options);
  FakeRace race(host, 10, 60, false);
  const u32 frames = RaceAgainstARelay(host, race, relay, [](u32, const std::function<void(const std::string&)>& send) {
    send(FromRelay('\x04') + std::string("\0\0\0\x01\x05\0\0\0\x03", 9) + std::string(13, '\0'));  // seat 5 of 0, 1
  });
  CHECK(frames == 90 && race.g.Read32(net::STEP) > 60);
  CHECK(host.notices.find("joined the race as seat 0 of 2 player seats") != std::string::npos);
  CHECK(host.log.find("the relay names a seat no race has (seat 5, player seats 00000003)") != std::string::npos);
  Shutdown();
  Init(host, Options{});
}

namespace
{
// TestTheRacePhaseBackAfterTheRaceSettled's race, its relay, and its steps.
constexpr u32 FROM = 10, TO = 60, LATE = 3;

// seat 0 is this game's and presses nothing, as its pad; seat 1's input changes every ninth step, with the race's
// last step (TO) and three steps after it
std::array<u8, 12> OfficialRow(u32 t)
{
  std::array<u8, 12> r{};
  for (int k = 6; k < 12; ++k)
    r[k] = t < TO ? static_cast<u8>(1 + t / 9) : t < TO + 3 ? 100 : 101;
  return r;
}

using TestClock = std::chrono::steady_clock;
long long NowMs()
{
  return std::chrono::duration_cast<std::chrono::milliseconds>(TestClock::now().time_since_epoch()).count();
}

// The game's frames, to twelve steps past the race's end. `left`, `back`: the phase left the race, and was in it again.
void RunTheRace(TestHost& host, FakeRace& race, std::atomic<u32>& floor, std::atomic<unsigned long long>& frame_reads,
                bool& left, bool& back)
{
  const long long started = NowMs();
  while (race.g.Read32(net::STEP) < TO + 12 && host.stops == 0 && NowMs() - started < 20000)
  {
    const u32 step = race.g.Read32(net::STEP);
    // (until the relay has said go the game's frames pass at once: not faster than the relay's thread turns)
    if (race.g.Read32(layout::CONTROL + 120) == 0)
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    if (race.Ended())
      left = true;
    else if (left)
      back = true;
    // before the race and after it the game runs in lockstep and is given the frames up to its step; in the race
    // phase the relay is three steps behind it
    floor = step < FROM ? step + 8 : race.Ended() ? step + 1 : step > LATE ? step - LATE : 0;
    frame_reads = host.clock_reads->load();   // (the relay tells a waiting game by its clock reads since here)
    if (!race.Frame())
      break;
  }
}

void TheCaseWasMet(TestHost& host, FakeRace& race, bool left, bool back)
{
  // the case was met: rollbacks in the race, the phase out of the race and back in it, and the session said so
  CHECK(host.stops == 0 && host.log.empty());
  CHECK(race.g.Read32(layout::CONTROL + 136) > 0);
  CHECK(left && back);
  CHECK(host.notices.find("fzgx: the race phase is back at step 63") != std::string::npos);
}

// Gives the steps the game ran.
u32 EveryStepRanWithTheOfficialInputs(FakeRace& race)
{
  // every step ran with the official inputs, last of all
  const u32 steps = race.g.Read32(net::STEP);
  CHECK(steps >= TO + 12 && race.g.Read32(layout::CONTROL + 232) >= TO + 8);
  u32 other_inputs = 0, first_other = 0;
  for (u32 t = 0; t < steps; ++t)
  {
    u8 used[12];
    net::Rollback::UsedInputs(race.g, t, {0, 1}, used);
    const std::array<u8, 12> official = OfficialRow(t);
    if (std::memcmp(used, official.data(), 12) != 0 && other_inputs++ == 0)
      first_other = t;
  }
  CHECK(other_inputs == 0);
  if (other_inputs != 0)
    std::printf("  %u steps ran last with other inputs than the official ones, the first step %u\n", other_inputs,
                first_other);
  return steps;
}

// Gives the step before which they were taken.
u32 TheResultsWereTakenOnce(TestHost& host, u32 steps)
{
  // the results were taken once, after the phase came back (step 63) and left again, and say before which step
  const std::string said = "fzgx: race results taken for the replay before step ";
  const std::size_t at = host.notices.find(said);
  CHECK(at != std::string::npos && host.notices.find(said, at + 1) == std::string::npos);
  const u32 taken = at == std::string::npos ? 0 : static_cast<u32>(std::atoi(host.notices.c_str() + at + said.size()));
  CHECK(taken > 63 && taken <= steps);
  return taken;
}

void AGameGivenOnlyTheOfficialInputs(const std::string& folder, u32 steps, u32 taken, const u32 mine[2],
                                     const std::string& recorded)
{
  // and the machines are where a game that was only ever given the official inputs has them
  TestHost plain_host;
  FakeRace plain(plain_host, FROM, TO, false);
  plain.lap_times = true;
  std::string official_results;
  for (u32 t = 0; t < steps; ++t)
  {
    if (t == taken)
    {
      net::Recorder before_this_step;
      CHECK(before_this_step.Open(folder + "/official.fzrp"));
      before_this_step.TakeResults(plain.g);
      before_this_step.Close();
      official_results = ResultsOf(folder + "/official.fzrp");
    }
    const std::array<u8, 12> official = OfficialRow(t);
    net::Rollback::UseInputs(plain.g, t, {0, 1}, official.data());
    plain.Step(t);
  }
  CHECK(mine[0] == plain.g.Read32(plain.machines + 0x7C) && mine[1] == plain.g.Read32(plain.machines + 0x620 + 0x7C));
  CHECK(!recorded.empty() && recorded == official_results);
  if (recorded != official_results)
    std::printf("  the recording's results, taken before step %u:\n    %s\n  the official inputs' results there:\n    %s\n",
                taken, recorded.c_str(), official_results.c_str());
}
}  // namespace

// The end of a race against a relay whose official frames come three steps late, so the race runs on predictions.
// When the phase leaves the race the session checks the predicted steps in one last
// rollback frame; a prediction of the race's last steps was wrong, so that rollback goes back before the race's end
// and takes the phase word with it: the game is in the race phase again for a frame and ends the race a second
// time. No step of that frame may be a prediction: nothing checks one any more. Every step the game ran must have
// run with the official inputs, and its machines must be where a game given only those inputs has them. (Run as a
// race's frame, step 63 here kept seat 1's predicted input, 100 for the official 101, and the state differed from
// there on: two games of one race then disagree, each sure of its own.)
// The results the recording carries are the race's as the official inputs give it: read after that last rollback,
// when the game is out of the race phase for good, and equal to what a game given only the official inputs holds
// before the same step. (Read in the settling frame before its rollback, they were the predicted steps' results.)
void TestTheRacePhaseBackAfterTheRaceSettled()
{
  Udp relay;
  CHECK(relay.Open());
  Options options;
  options.debug.relay = "127.0.0.1:" + std::to_string(relay.LocalPort()) + ":1f2e3d4c5b6a7988";
  const std::string folder = Temp();
  options.replay_file = folder + "/race.fzrp";
  TestHost host;
  host.time = std::chrono::steady_clock::now();
  Init(host, options);
  FakeRace race(host, FROM, TO, false);
  race.late_end = true;
  race.lap_times = true;

  std::atomic<u32> floor{0};
  std::atomic<unsigned long long> frame_reads{host.clock_reads->load()};
  std::atomic<bool> stop{false};
  std::thread relay_thread([&] { RunTheLateRelay(relay, floor, *host.clock_reads, frame_reads, stop, OfficialRow); });
  bool left = false, back = false;
  RunTheRace(host, race, floor, frame_reads, left, back);
  stop = true;
  relay_thread.join();

  TheCaseWasMet(host, race, left, back);
  const u32 steps = EveryStepRanWithTheOfficialInputs(race);
  const u32 mine[2] = {race.g.Read32(race.machines + 0x7C), race.g.Read32(race.machines + 0x620 + 0x7C)};
  Shutdown();
  const u32 taken = TheResultsWereTakenOnce(host, steps);
  const std::string recorded = ResultsOf(options.replay_file);
  CHECK(recorded.find("\"bestLapMs\":") != std::string::npos);
  AGameGivenOnlyTheOfficialInputs(folder, steps, taken, mine, recorded);
  Init(host, Options{});
}
