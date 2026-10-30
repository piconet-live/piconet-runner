// The end of a race is held until it is official: a match race against a relay that
// is the test, on the loopback, whose official frames come late, with the stand-in game that ends a race by what
// its machines did and holds the end as the image does (FakeRace::holds_end).
// SPDX-License-Identifier: GPL-3.0-or-later

#include "harness.h"

namespace
{
constexpr u32 FROM = 10;

// seat 0 is this game's and presses nothing; seat 1 gets on by two in a step whose input's first byte is odd and by
// one otherwise, and the byte changes every five steps: a game ahead of the official frames predicts the steps after
// each change wrong
std::array<u8, 12> OfficialRow(u32 t)
{
  std::array<u8, 12> r{};
  r[6] = static_cast<u8>((t / 5) & 1);
  return r;
}

// how far a seat (seat 1 unless named) has come after step t on the official inputs
u32 Far(u32 t, u32 seat = 1)
{
  u32 sum = 0;
  for (u32 u = FROM; u <= t; ++u)
    sum += 1 + (OfficialRow(u)[seat * 6] & 1);
  return sum;
}

// The game's frames, to eighteen steps past the last finish, against a relay `lateness` steps behind it in the race.
// `withheld_from` (0: none): once the game stands at that step the relay sends the frames below it and no more, until
// the game stands before step `given_at`: then every frame below that step, at once.
void RunTheRace(TestHost& host, FakeRace& race, Udp& relay, u32 finish, u32 lateness, u32 withheld_from, u32 given_at)
{
  std::atomic<u32> floor{0};
  std::atomic<unsigned long long> frame_reads{host.clock_reads->load()};
  std::atomic<bool> stop{false};
  std::atomic<u32> sent{0};
  std::thread relay_thread(
      [&] { RunTheLateRelay(relay, floor, *host.clock_reads, frame_reads, stop, OfficialRow, &sent); });
  // (bounded by frames, not by the wall clock: a loaded PC runs the same frames, later)
  for (u32 frames = 0; race.g.Read32(net::STEP) < finish + 18 && host.stops == 0 && frames < 4000; ++frames)
  {
    const u32 step = race.g.Read32(net::STEP);
    // (the game's frames pass at once: not faster than the relay's thread turns)
    std::this_thread::sleep_for(std::chrono::milliseconds(race.g.Read32(layout::CONTROL + 120) == 0 ? 1 : 2));
    // before the race, and once it is over, the game runs in lockstep and is given the frames up to its step; in
    // the race the relay is `lateness` steps behind it
    const bool over = race.g.Read32(layout::END_OVER) != 0 || race.Ended();
    const u32 in_the_race = step > lateness ? step - lateness : 0;
    const bool withholding = withheld_from != 0 && !over && step >= withheld_from;
    floor = step < FROM ? step + 8 : over ? step + 1 : !withholding ? in_the_race :
            step < given_at ? withheld_from : step;
    // (the frames given at once are all sent before the game's frame begins: waited for by what the relay says it
    // has sent, and two turns of its thread more for them to be at the game, not by a length of time)
    if (withholding && step == given_at)
    {
      for (u32 turns = 0; sent < step && turns < 100000; ++turns)
        std::this_thread::sleep_for(std::chrono::microseconds(200));
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    // (a relay that withholds frames gives none to a frame that runs, however long the PC takes over it: the late
    // relay tells a waiting game by its clock reads, and a game that does come to wait is given what it waits for.
    // Held off by a wall-clock time far ahead, a game that came to wait stood until the test's time limit)
    frame_reads = host.clock_reads->load();
    if (!race.Frame())
      break;
  }
  stop = true;
  relay_thread.join();
}

// The results phase began once, "over" was first said before the step after the official finish, and every step ran
// last with the official inputs. Gives the steps the game ran.
// A game that watched (its racer has finished: spectate.cpp) ran in lockstep from the race's first frame on.
u32 TheRaceEndedOnce(TestHost& host, FakeRace& race, u32 finish, u32 lateness, bool watched)
{
  CHECK(host.stops == 0 && host.log.empty());
  CHECK(watched ? race.g.Read32(layout::CONTROL + 136) == 0 &&
                      host.notices.find("watching in lockstep on official frames") != std::string::npos
                : race.g.Read32(layout::CONTROL + 136) > 0);
  CHECK(race.ends == 1);
  CHECK(race.over_step == finish + 1);
  if (race.ends != 1 || race.over_step != finish + 1)
    std::printf("  finish at step %u, relay %u steps late: the results phase began %u times, \"over\" before step %u "
                "(asked %u times, %u frames cut)\n",
                finish, lateness, race.ends, race.over_step, race.asks, race.cuts);
  // the frames of the finish sequence, in the race phase still, are not "the race phase back"
  CHECK(host.notices.find("fzgx: the race phase is back") == std::string::npos);
  const u32 steps = race.g.Read32(net::STEP);
  CHECK(steps >= finish + 18);
  u32 other_inputs = 0;
  for (u32 t = 0; t < steps; ++t)
  {
    u8 used[12];
    net::Rollback::UsedInputs(race.g, t, {0, 1}, used);
    const std::array<u8, 12> official = OfficialRow(t);
    other_inputs += std::memcmp(used, official.data(), 12) != 0;
  }
  CHECK(other_inputs == 0);
  return steps;
}

// The recording's results were taken once, in the results phase's first frame, before the same step in every game
// (the finish sequence ran in lockstep, a frame a step); and they and the machines are those of a game that was
// only ever given the official inputs. So is every step the recording holds: the inputs it ran with and its
// checksum. (A step is recorded when the session counts it final. A frame the image cut at the step that made the
// race over ended below the steps the session had counted as run, and `checked` is cut to where the game stands
// (frame.cpp, TakeTheEndsQuestion); without that the steps left out were recorded as the predictions had left them.)
// With the host's rule that ends a race so long after the first finish (`at_the_end`), they are taken where the race
// ended for everyone, before the step "over" was first said before: machines may race on behind the results then,
// and the stand-in game's machines do change in every step.
void TheResultsAreTheOfficialRaces(TestHost& host, const std::string& folder, u32 finish, u32 goal, u32 steps,
                                   const u32 mine[2], bool at_the_end, u32 seat0_goal)
{
  const std::string said = "fzgx: race results taken for the replay before step ";
  const std::size_t at = host.notices.find(said);
  CHECK(at != std::string::npos && host.notices.find(said, at + 1) == std::string::npos);
  const u32 taken = at == std::string::npos ? 0 : static_cast<u32>(std::atoi(host.notices.c_str() + at + said.size()));
  CHECK(taken == (at_the_end ? finish + 1 : finish + 2 + FakeRace::FINISH_FRAMES));
  const std::string recorded = ResultsOf(folder + "/race.fzrp");
  // the recording's steps: after the lineup ('FZRL', its length, 124 bytes), 16 bytes a step (the two seats' records,
  // the step's checksum), up to the results (the JSON, its length, 'FZRS')
  constexpr std::size_t LINEUP = 8 + 124, A_STEP = 12 + 4;
  std::ifstream file(folder + "/race.fzrp", std::ios::binary);
  const std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  const std::size_t steps_end =
      bytes.size() >= LINEUP + recorded.size() + 8 ? bytes.size() - recorded.size() - 8 : LINEUP;
  const u32 recorded_steps = static_cast<u32>((steps_end - LINEUP) / A_STEP);
  CHECK(bytes.compare(0, 4, "FZRL") == 0 && (steps_end - LINEUP) % A_STEP == 0);
  CHECK(recorded_steps > taken && recorded_steps <= steps);
  u32 other_steps = 0, first_other = 0;
  TestHost plain_host;
  FakeRace plain(plain_host, FROM, 0, false);
  plain.finish_goal = goal;
  plain.seat0_goal = seat0_goal;
  plain.lap_times = true;
  std::string official_results;
  u32 plain_over = 0;
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
    if (t < recorded_steps)
    {
      const u32 sum = net::StateChecksum(plain.g, MODULE);
      const std::array<u8, 4> checksum{static_cast<u8>(sum >> 24), static_cast<u8>(sum >> 16),
                                       static_cast<u8>(sum >> 8), static_cast<u8>(sum)};
      const char* step_bytes = bytes.data() + LINEUP + t * A_STEP;
      const bool other = std::memcmp(step_bytes, official.data(), 12) != 0 ||
                         std::memcmp(step_bytes + 12, checksum.data(), 4) != 0;
      if (other && other_steps++ == 0)
        first_other = t;
    }
    const bool finished = (plain.g.Read32(plain.machines) & plain.g.Read32(plain.machines + 0x620) & 0x10000) != 0;
    if (finished && plain_over == 0)
      plain_over = t + 1;
  }
  CHECK(plain_over == finish + 1);
  CHECK(mine[0] == plain.g.Read32(plain.machines + 0x7C) && mine[1] == plain.g.Read32(plain.machines + 0x620 + 0x7C));
  CHECK(!recorded.empty() && recorded == official_results);
  CHECK(other_steps == 0);
  if (other_steps != 0)
    std::printf("  finish at step %u: %u of the recording's %u steps are not the official race's (inputs or checksum), "
                "the first step %u\n",
                finish, other_steps, recorded_steps, first_other);
}
}  // namespace

// The game's race-over test runs once a displayed frame, outside the steps, and the results phase it leads to
// cannot be simulated again: said on a predicted step, a rollback took the phase's first frame back and the race
// ended a second time (TestTheRacePhaseBackAfterTheRaceSettled shows what the session does then). With the image
// that holds the end (standin_race.py `over`, frame_entry.py `extra_cut`), in a race that ends by what the machines
// did and whose last finisher's inputs are predicted wrong around its finish:
// - the results phase begins once, and the frames of the game's finish sequence after "over" (still the race phase)
//   run in lockstep and are not reported as the race phase come back;
// - the test first says "over" before the same step as in a game that was only ever given the official inputs,
//   whether this game's predictions made the finish come early (the first "over" was a prediction's: the session
//   checked it and the race went on) or late (the game had run past the finish when it was corrected), and however
//   far behind the relay's official frames are: two games of one race end it before the same step;
// - every step ran last with the official inputs, and the recording's results and every step it holds are those
//   of the official race;
// - the "over" of a race before this one in the same process does not stand: each race here begins with the word
//   set, and runs as a race (it rolls back, its test asks).
// (On the session of before, which never says how far the official steps go, this image ends the race as it always
// did, on predicted steps, and the phase begins twice in each of the first four races.)
// The local seat has finished from the race's first step, so the game watches the race behind live (spectate.cpp),
// in lockstep: each race is run so, and once with the local seat finishing in the step the other does
// (FakeRace::seat0_goal), so the game races, with the rollbacks these races are about, to the end.
void TestTheRaceEndsOnceOnOfficialInputs()
{
  u32 asks = 0, cuts = 0;
  struct Race
  {
    u32 finish, lateness, withheld_from, given_at;
    u32 finish_limit = 0;  // the host's rule in the race block, in steps (0: a race without it)
  };
  // The last finish at step 56, the second of five fast steps (a game that predicted the slow steps on finds the
  // finish late), and at step 61, the second of five slow ones (predicted fast, the finish comes a step early).
  // And the finish at 56 against a relay that sends nothing from step 55 on: the game runs on predicting slow
  // steps, its machines look finished after step 58, and the test asks before step 59, where the relay's frames to
  // 58 come at once. The check's frame runs again from step 55 and is cut after step 56, which ends the race: two
  // steps below those the session had counted as run, which it must not record as they were left.
  // And two of them with the host's rule: the recording's results are then the standing where "over" was said.
  for (const Race& a : {Race{56, 3, 0, 0}, Race{56, 6, 0, 0}, Race{61, 3, 0, 0}, Race{61, 6, 0, 0}, Race{56, 3, 55, 59},
                        Race{61, 6, 0, 0, 5400}, Race{56, 3, 55, 59, 5400}})
  for (const bool racing : {true, false})
  {
    const u32 finish = a.finish, lateness = a.lateness;
    Udp relay;
    CHECK(relay.Open());
    Options options;
    options.debug.relay = "127.0.0.1:" + std::to_string(relay.LocalPort()) + ":1f2e3d4c5b6a7988";
    const std::string folder = Temp();
    options.replay_file = folder + "/race.fzrp";
    TestHost host;
    host.time = std::chrono::steady_clock::now();
    Init(host, options);
    FakeRace race(host, FROM, 0, false);
    race.finish_goal = Far(finish);
    race.seat0_goal = racing ? Far(finish, 0) : 1;
    race.holds_end = true;
    race.lap_times = true;
    // (as a race before this one in the same process left the word: the session clears it when it starts, or no
    // step of this race would be predicted)
    race.g.Write32(layout::END_OVER, 1);
    race.g.Write32(layout::RACE_FINISH_LIMIT_AT, a.finish_limit);
    RunTheRace(host, race, relay, finish, lateness, a.withheld_from, a.given_at);
    const u32 steps = TheRaceEndedOnce(host, race, finish, lateness, !racing);
    asks += race.asks;
    cuts += race.cuts;
    CHECK(!racing || a.withheld_from == 0 || (race.asks > 0 && race.cuts > 0));  // (the fifth race's case was met)
    const u32 mine[2] = {race.g.Read32(race.machines + 0x7C), race.g.Read32(race.machines + 0x620 + 0x7C)};
    Shutdown();
    TheResultsAreTheOfficialRaces(host, folder, finish, race.finish_goal, steps, mine, a.finish_limit != 0, race.seat0_goal);
    Init(host, Options{});
  }
  // the cases were met: a race-over test that had to ask, and a frame cut at the step that made the race over
  CHECK(asks > 0 && cuts > 0);
}

// The host's rule in a match start or a replay's header: "finishLimitSeconds", the seconds after the first finish at
// which the race ends for everyone. The game is given steps (the race block's word, which the image's raceend.py
// reads); a set-up without the rule, as every recording made before it, gives 0 and the race ends as it always did.
// Kept in its range as the CPUs' level is: ten minutes at most, none below zero, whole seconds, a number.
void TestTheFinishLimitInTheRaceBlock()
{
  const std::string dir = Temp();
  std::string image(0x100, '\0');
  image.replace(0, 4, "FZMD");
  image.replace(0x10, 4, "FZSI");
  WriteText(dir + "/mod.bin", image);
  WriteText(dir + "/manifest.json", "{\"commit\": \"abc1234\"}");
  WriteText(dir + "/codes.txt", "$Boot to race\n48000000 800030C8\n$Replay boot\n04000000 00000001\n");
  TestHost host;
  Options options;
  options.bundle_dir = dir;
  options.play_file = dir + "/limit.fzrp";
  for (const auto& [rules, steps] :
       {std::pair{"", 0u}, std::pair{"\"rules\": {\"cpuLevel\": 3}, ", 0u},
        std::pair{"\"rules\": {\"finishLimitSeconds\": 90}, ", 5400u},
        std::pair{"\"rules\": {\"finishLimitSeconds\": 100000}, ", 36000u},
        std::pair{"\"rules\": {\"finishLimitSeconds\": -5}, ", 0u},
        std::pair{"\"rules\": {\"finishLimitSeconds\": 12.9}, ", 720u},
        std::pair{"\"rules\": {\"finishLimitSeconds\": \"90\"}, ", 0u},
        std::pair{"\"rules\": {\"finishLimitSeconds\": 0}, ", 0u}})
  {
    WriteReplay(options.play_file, {1, 0}, rules);
    Init(host, options);
    CHECK(Active() && host.log.empty());
    FrameTick();
    CHECK(Data().Read(layout::RACE_FINISH_LIMIT_AT, 4) == steps);
    CHECK(Data().Read(layout::RACE_FLOW_AT, 4) == 0);   // a recording made before the race's flow
    Shutdown();
  }
  // the race's flow (files.h RACE_FLOW): a recording of it plays with it; a flow this core does not know is refused
  for (const auto& [flow, played] : {std::pair{"1", true}, std::pair{"0", true}, std::pair{"2", false}})
  {
    WriteReplay(options.play_file, {1, 0}, "", std::string(", \"flow\": ") + flow);
    host.log.clear();
    Init(host, options);
    CHECK(Active() == played);
    if (played)
    {
      FrameTick();
      CHECK(Data().Read(layout::RACE_FLOW_AT, 4) == static_cast<u32>(flow[0] - '0'));
    }
    else
      CHECK(host.log.find("player seats, flow or rubber band is not a number in its range") != std::string::npos);
    Shutdown();
  }
  // the rubber band (files.h RACE_RUBBER_BAND_ON): a recording without the key raced with it (before 0.4.0); one
  // of 0.4.0 says false and is version 7 (test_grid.cpp plays it), so a version 2 file that says false is refused;
  // a key that is not true or false is refused
  for (const auto& [band, word] : {std::pair{"", 1}, std::pair{", \"rubberBand\": true", 1},
                                   std::pair{", \"rubberBand\": false", -2}, std::pair{", \"rubberBand\": 0", -1}})
  {
    WriteReplay(options.play_file, {1, 0}, "", std::string(", \"flow\": 1") + band);
    host.log.clear();
    Init(host, options);
    CHECK(Active() == (word >= 0));
    if (word >= 0)
    {
      FrameTick();
      CHECK(Data().Read(layout::RACE_RUBBER_BAND_ON_AT, 4) == static_cast<u32>(word));
    }
    else
      CHECK(host.log.find(word == -1 ? "flow or rubber band is not a number in its range" : "or no rubber band, which only version 7 does") != std::string::npos);
    Shutdown();
  }
  // the simulation's own generator (files.h RACE_SIM_GENERATOR): a recording without the key drew from the
  // executable's word (before it) and plays so; a key that is not true or false is refused
  for (const auto& [sim, word] : {std::pair{"", 0}, std::pair{", \"simGenerator\": true", 1},
                                  std::pair{", \"simGenerator\": false", 0}, std::pair{", \"simGenerator\": 1", -1}})
  {
    WriteReplay(options.play_file, {1, 0}, "", std::string(", \"flow\": 1") + sim);
    host.log.clear();
    Init(host, options);
    CHECK(Active() == (word >= 0));
    if (word >= 0)
    {
      FrameTick();
      CHECK(Data().Read(layout::RACE_SIM_GENERATOR_AT, 4) == static_cast<u32>(word));
    }
    else
      CHECK(host.log.find("or its generator is not true or false") != std::string::npos);
    Shutdown();
  }
  Init(host, Options{});
  std::filesystem::remove_all(dir);
}
