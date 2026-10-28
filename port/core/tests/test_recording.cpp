// Tests of a recording: played back with rollbacks forced, and written and read back.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "harness.h"

#include "files.h"
#include "recorded_race.h"

namespace
{
using namespace recorded;

// A playback's forced rollbacks, or how late the recording's inputs are known.
struct Case
{
  u32 every;
  std::vector<u32> depths;
  u32 late = 0;
  std::string undo = "";  // Debug::undo_log, on a host whose memory the guard can protect; empty: the test host's default
};

// What the playback's host was told, and the simulation's memory at the end.
void WhatThePlaybackSaid(TestHost& host, FakeRace& race, const Ways& ways, const Case& c, u32 frames,
                         const std::vector<u8>& end_state)
{
  const auto [game, leak, with_scratch, fails] = ways;
  const bool forced = c.every != 0 || c.late != 0;
  const auto said = [&](const std::string& text) { return host.notices.find(text) != std::string::npos; };
  CHECK(host.stops == 1 && frames == STEPS + 1);   // one new step a displayed frame, forced or not
  CHECK(said("playback: 420 of 420 steps compared"));
  CHECK(said("rollbacks 0 (0 forced)") == !forced);
  CHECK(forced == (race.steps_run > STEPS));
  if (c.late != 0)
  {
    // real rollbacks, none forced, as deep as the inputs were late and one more
    CHECK(said("(0 forced), deepest " + std::to_string(c.late + 1)) && !said("re-simulation audit"));
    CHECK(race.steps_run > STEPS + 200);
  }
  if (forced)
    CHECK(said(game == Game::Scratch ? "and 16 KiB of the locked cache" : "and 0 KiB of the locked cache"));
  if (!fails || !forced)
  {
    CHECK(said(" 0 differed"));
    CHECK(host.log.empty());
    CHECK(std::memcmp(host.ram.data() + (race.first - layout::RAM), end_state.data(), end_state.size()) == 0);
  }
  else
  {
    CHECK(!said(" 0 differed"));
    CHECK(host.log.find("differs from the recording first at step") != std::string::npos);
  }
  if (c.every == 5)
    CHECK(said("deepest 17"));
  // the guard: on once the learning steps are over, every first write caught; with the check, the log of every step
  // the same as every page compared gives
  if ((c.undo == "both" || c.undo == "guard") && forced)
    CHECK(said("undo log: guard on") && said("undo log: guard off") && !said("undo log: no guard"));
  if (c.undo == "scan")
    CHECK(!said("undo log: guard on") && !said("undo log: no guard"));
  // and a watched page quiet for long enough rests (protected again), its next write caught as a first one, exactly
  if (c.undo == "both" && forced)
    CHECK(said("steps compared with every page: the log the same at every step") &&
          said("a watched page rested ") && !said("a watched page rested 0 times"));
  if (c.undo.empty() && forced)
    CHECK(said("undo log: no guard"));
  // the audit: a step reached again by a restore and a re-simulation holds what the unbroken run held; in the
  // leaking game the two words its step writes do not, and both are words the checksum covers
  CHECK(said("re-simulation audit") == (c.every != 0));
  if (c.every != 0)
    CHECK(said(fails ? "1 words of a machine's block and 1 of its progress record came back different at least once, 2 of"
                     : "0 words of a machine's block and 0 of its progress record came back different at least once, 0 of"));
  if (c.every != 0 && fails)
    CHECK(said("block+0x7C (IN THE CHECKSUM)") && said("progress+0xF8 (IN THE CHECKSUM)"));
  CHECK(said("through the race:") == forced);   // said once, when a race that ran with rollback settles
}

void TheTrace(const std::string& dir, TestHost& host, FakeRace& race, Game game)
{
  // the trace: a line for every checksum kept (one after every step run, the first time and again), saying
  // how the step ran
  std::string trace;
  CHECK(ReadFileToString(dir + "/trace.csv", trace));
  CHECK(static_cast<u32>(std::count(trace.begin(), trace.end(), '\n')) == race.steps_run);
  if (game == Game::Scratch)
    CHECK(host.scratch[FakeRace::SCRATCH_WORD] != 0 || host.scratch[FakeRace::SCRATCH_WORD + 1] != 0);
  CHECK(trace.find(",F,") != std::string::npos && trace.find(",M,") != std::string::npos && trace.find(",L,") != std::string::npos);
}

// The undo log's trace (Debug::undo_trace) of a playback with a rollback every frame 6 deep, where steps run again
// first write pages the log has never seen change (FakeRace: a late page now and then). With the guard, each such
// write is caught in the step that makes it: its late line names that step, no line says the step is not known and
// no restore is stale, and the guard's lines count what it caught. Scanning, the same pages are found only at the
// next comparison of every page, which cannot tell the steps between apart: the
// trace says so.
bool TracesTheUndoLog(const Case& c)
{
  return c.every == 1 && c.depths == std::vector<u32>{6};
}

void TheUndoTrace(const std::string& dir, const Case& c)
{
  std::string trace;
  CHECK(ReadFileToString(dir + "/undo.txt", trace));
  const auto has = [&](const std::string& text) { return trace.find(text) != std::string::npos; };
  CHECK(has("\nlate,") || trace.starts_with("late,"));
  if (c.undo == "guard")
  {
    CHECK(!has("WHICH STEP IS NOT KNOWN") && !has("stale,"));
    CHECK(has("guard,on at the log's step ") && has(" pages caught (") && has("guard,off: "));
  }
  else
  {
    CHECK(c.undo == "scan" && has("WHICH STEP IS NOT KNOWN") && !has("guard,"));
  }
}

void PlayItBack(const std::string& dir, const Ways& ways, const Case& c, const std::vector<u8>& end_state)
{
  const auto [game, leak, with_scratch, fails] = ways;
  TestHost host;
  host.scratch.assign(with_scratch ? 0x4000 : 0, 0);
  host.guardable = !c.undo.empty();
  Options options;
  options.debug.undo_log = c.undo;
  options.debug.keep_scratch = game == Game::ScratchKeptOut;
  options.bundle_dir = dir;
  options.play_file = dir + "/race.fzrp";
  options.debug.force_every = c.every;
  options.debug.force_depths = c.depths;
  options.debug.late_steps = c.late;
  options.debug.trace_file = c.late == 5 ? dir + "/trace.csv" : "";
  // a dump asked for (at a step this race never reaches): the image is told to keep its test logs; no other game's is
  options.debug.dump_step = c.late == 5 ? 0x7FFFFFF0 : 0;
  options.debug.dump_prefix = c.late == 5 ? dir + "/dump" : "";
  const bool undo_trace = TracesTheUndoLog(c) && game == Game::Plain;
  options.debug.undo_trace = undo_trace ? dir + "/undo.txt" : "";
  Init(host, options);
  CHECK(Active() && host.log.empty());
  FakeRace race(host, RACE_FROM, RACE_TO, leak);
  u32 frames = 0;
  while (frames < 2000 && race.Frame())
    ++frames;
  WhatThePlaybackSaid(host, race, ways, c, frames, end_state);
  CHECK(race.g.Read32(layout::IMAGE_LOGS) == (c.late == 5 ? 1u : 0u));
  Shutdown();
  if (c.late == 5)
    TheTrace(dir, host, race, game);
  if (undo_trace)
    TheUndoTrace(dir, c);
}
}  // namespace

// A recorded race played back with rollbacks forced gives the recorded race: every step's checksum, and the
// simulation's memory at the end. And the check can fail: a game whose steps depend on a word that no rollback puts
// back differs from its recording as soon as a step is run again.
// The locked cache: a game whose steps take from it what the step before left there
// plays back as recorded, through forced rollbacks and through real ones, because the undo log covers it; with the
// cache left out of the log (Options::keep_scratch: how it was) the first step run again after a rollback reads
// what the newest step left, and the playback differs from its recording.
void TestAPlaybackWithForcedRollbacks()
{
  BEGIN_TEST();
  const std::string dir = Temp();
  std::string image(0x100, '\0');
  image.replace(0, 4, "FZMD");
  image.replace(0x10, 4, "FZSI");
  WriteText(dir + "/mod.bin", image);
  WriteText(dir + "/manifest.json", "{\"commit\": \"abc1234\"}");
  WriteText(dir + "/codes.txt", "$Boot to race\n48000000 800030C8\n$Replay boot\n04000000 00000001\n");
  std::mt19937 rng(4);

  for (const Game game : {Game::Plain, Game::Leaking, Game::Scratch, Game::ScratchKeptOut})
  {
    const bool leak = game == Game::Leaking;
    const bool with_scratch = game == Game::Scratch || game == Game::ScratchKeptOut;
    const bool fails = leak || game == Game::ScratchKeptOut;  // under rollbacks
    const Ways ways{game, leak, with_scratch, fails};
    std::vector<u8> end_state;
    const std::string steps = RecordTheRace(rng, ways, end_state);
    WriteReplay(dir + "/race.fzrp", {1, 0}, "", "", 0, steps);

    // the last two: no rollback forced; the recording's inputs known 2 and 5 steps late, so the race runs on
    // predictions that are wrong at nearly every step (the inputs change that often) and rolls back as a live race
    // and the guard: checked at every step against every page compared, and alone
    for (const Case& c : {Case{0, {}}, Case{1, {1}}, Case{2, {1, 3, 7, 12}}, Case{5, {17}}, Case{0, {}, 2}, Case{0, {}, 5},
                          Case{2, {1, 3, 7, 12}, 0, "both"}, Case{0, {}, 2, "both"}, Case{1, {1}, 0, "guard"},
                          Case{0, {}, 5, "scan"}, Case{1, {6}, 0, "guard"}, Case{1, {6}, 0, "scan"}})
      PlayItBack(dir, ways, c, end_state);
  }
  TestHost last;
  Init(last, Options{});
  std::filesystem::remove_all(dir);
}

// A recording is made from what the rollback interface holds and nothing else: the inputs each step ran with and
// the checksum kept for it. Read back, the file gives them again.
void TestTheReplayFilesSamples();

void TestARecordingRoundTrip()
{
  const std::string dir = Temp();
  TestHost host;
  Init(host, Options{});
  FakeRace race(host, 0, 100, false);
  const std::vector<int> seats = {0, 1};
  // the file as it stands when a recording starts: the header and the frame preamble (boot::StartReplay's part)
  const std::string header = "{\"match\": {}}";
  std::string file = "FZRP" + std::string("\0\2", 2) + std::string("\0\0\0", 3) + static_cast<char>(header.size()) + header +
                     "FZRF" + std::string("\0\0\0\3", 4) + std::string("\0\0\0\1", 4);
  WriteText(dir + "/r.fzrp", file);
  net::Recorder recorder;
  CHECK(!recorder.IsOpen());
  recorder.Record(race.g, 5, seats);  // not open: nothing
  CHECK(recorder.Open(dir + "/r.fzrp") && recorder.IsOpen() && recorder.Recorded() == 0);
  recorder.WriteLineup(race.g);
  std::mt19937 rng(9);
  std::vector<std::vector<u8>> rows;
  std::vector<u32> sums;
  for (u32 t = 0; t < 50; ++t)
  {
    std::vector<u8> row(12);
    for (u8& b : row)
      b = static_cast<u8>(rng());
    net::Rollback::UseInputs(race.g, t, seats, row.data());
    race.Step(t);
    net::Rollback::KeepChecksum(race.g, t, net::Rollback::Checksum(race.g));
    rows.push_back(row);
    sums.push_back(net::Rollback::Checksum(race.g));
    if (t == 19)
      recorder.Record(race.g, 12, seats);  // steps become final some steps late, several at a time
  }
  recorder.Record(race.g, 50, seats);
  recorder.Record(race.g, 50, seats);  // nothing new: nothing written
  CHECK(recorder.Recorded() == 50 && !recorder.HasResults());
  recorder.TakeResults(race.g);
  CHECK(recorder.HasResults());
  recorder.Close();
  CHECK(!recorder.IsOpen());
  net::Recording back;
  CHECK(net::ReadRecording(dir + "/r.fzrp", &back));
  CHECK(back.seats == seats && back.seat == 1 && back.index == 1);
  CHECK(back.rows == rows && back.checksums == sums);
  std::string all;
  CHECK(ReadFileToString(dir + "/r.fzrp", all) && all.size() > 4 && all.substr(all.size() - 4) == "FZRS");
  CHECK(all.find("\"results\":[{\"seat\":0,") != std::string::npos);
  WriteText(dir + "/bad.fzrp", "FZRP");
  CHECK(!net::ReadRecording(dir + "/bad.fzrp", &back) && host.log.find("not a version 2, 3, 4, 5, 6, 7 or 8 replay") != std::string::npos);
  // At the recording's end it is made compact (version 8): read back, the same rows, every checksum compares (the
  // whole word every 60th step and at the last, the folded byte between), the results the same.
  CHECK(net::CompactReplay(dir + "/r.fzrp"));
  std::string compact;
  CHECK(ReadFileToString(dir + "/r.fzrp", compact) && compact.substr(0, 8) == std::string("FZRP\0\x08\0\x02", 8));
  CHECK(!std::filesystem::exists(dir + "/r.fzrp.compact"));
  net::Recording small;
  CHECK(net::ReadRecording(dir + "/r.fzrp", &small) && small.compact && !back.compact);
  CHECK(small.seats == seats && small.seat == 1 && small.rows == rows && small.checksums.size() == sums.size());
  CHECK(small.results == back.results && !small.results.empty());
  for (size_t t = 0; t < sums.size(); ++t)
  {
    CHECK(net::RecordedChecksumMatches(small.checksums, true, t, sums[t]));
    const bool full = t % 60 == 0 || t + 1 == sums.size();
    // (a change the folded byte cannot see is seen at the whole steps alone)
    CHECK(net::RecordedChecksumMatches(small.checksums, true, t, sums[t] ^ 0x01010101u) == !full);
    CHECK(!net::RecordedChecksumMatches(small.checksums, true, t, sums[t] ^ 1));
    CHECK(full ? small.checksums[t] == sums[t] : small.checksums[t] < 256);
  }
  CHECK(!net::RecordedChecksumMatches(back.checksums, false, 1, sums[1] ^ 0x01010101u));  // whole: every bit
  CHECK(!net::CompactReplay(dir + "/r.fzrp") && host.notices.find("stays as it was recorded") != std::string::npos);
  WriteText(dir + "/cut.fzrp", compact.substr(0, compact.size() / 2));
  CHECK(!net::ReadRecording(dir + "/cut.fzrp", &small) && host.log.find("is a compact replay (version 8) that does not read") != std::string::npos);
  CHECK(net::SameResults(back.results, back.results) && !net::SameResults(back.results, "{\"results\":[]}"));
  Shutdown();
  std::filesystem::remove_all(dir);
  // the shared samples of every version, and the runner's --replay-info (test_replay_vectors.cpp)
  TestTheReplayFilesSamples();
}

// A replay of more than six hours of steps is refused by both readers (security audit 030, G3: a file of one seat
// at the largest size read was 26.7 million steps, 1.8 GB in the session's memory); six hours are played.
void TestAReplayOfMoreThanSixHours()
{
  const std::string dir = Temp();
  std::string image(0x100, '\0');
  image.replace(0, 4, "FZMD");
  image.replace(0x10, 4, "FZSI");
  WriteText(dir + "/mod.bin", image);
  WriteText(dir + "/manifest.json", "{\"commit\": \"abc1234\"}");
  WriteText(dir + "/codes.txt", "$Boot to race\n48000000 800030C8\n$Replay boot\n04000000 00000001\n");
  Options options;
  options.bundle_dir = dir;
  options.play_file = dir + "/r.fzrp";
  for (const size_t steps : {boot::MAX_REPLAY_STEPS, boot::MAX_REPLAY_STEPS + 1})
  {
    WriteReplay(options.play_file, {1, 0}, "", "", 0, std::string(steps * (2 * 6 + 4), '\0'));
    TestHost host;
    net::Recording back;
    const bool read = net::ReadRecording(options.play_file, &back);
    Init(host, options);
    const bool taken = steps <= boot::MAX_REPLAY_STEPS;
    CHECK(read == taken && (!read || back.rows.size() == steps) && Active() == taken);
    CHECK(taken || host.log.find("and a replay is at most 1296000 (six hours): refused") != std::string::npos);
    Shutdown();
  }
  TestHost host;
  Init(host, Options{});
  std::filesystem::remove_all(dir);
}
