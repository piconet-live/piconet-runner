// Tests of the replay scrubber (scrub.cpp, scrub_move.cpp, undo_pack.cpp, undo_moments.cpp): the buffer ahead of
// the picture, the moves of the play position (seeks), the sound gate, and the launch file's key.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fzgx/replay_keys.h"
#include "fzgx/sound_gate.h"
#include "harness.h"
#include "recorded_race.h"
#include "scrub_host.h"

namespace
{
using namespace recorded;
using scrub_test::ScrubHost;

// One kind of playback.
struct Kind
{
  Game game;
  u32 finish_goal;     // not 0: the race ends by its machines (the image cuts the front's frame there)
  u32 steps;           // front steps a frame
  const char* script;
  bool small = false;  // a budget the replay cannot keep: every packed unit is dropped, and steps are run again
};

// Paused 30 frames; a jump far forwards; a jump back; to the race's first step; a scrub dragged forwards 7 steps a
// frame, then back 5; a press beyond the end; to the end, where it pauses; play there: again from the first step.
const Kind KINDS[] = {
    {Game::Plain, 0, 4, "20:p,50:p,60:j:300,110:j:100,120:h,135:j:250,170:s:80,171:f:7,200:k:5,230:r,240:s:9999,"
                        "250:r,260:e,300:p", true},
    // one front step a frame: a jump and a scrub beyond the front edge
    {Game::Scratch, 0, 1, "30:j:200,35:j:9999,80:s:300,81:k:9,100:f:20,130:r"},
    {Game::Plain, 280, 3, "20:p,120:p,130:j:150,150:s:100,151:f:11,170:k:3,200:r,210:e", true},
    {Game::Leaking, 0, 2, "40:j:150,60:s:100,61:f:3,90:r"},
};

// What one kind's playback said of itself, and what its bar showed.
void WhatTheScrubberSaid(const ScrubHost& host, const FakeRace& race, const Kind& kind, u32 highest)
{
  const bool leak = kind.game == Game::Leaking;
  const auto said = [&](const std::string& text) { return host.notices.find(text) != std::string::npos; };
  CHECK(said("scrubber: the race begins at step 60") && said("scrubber: steps buffered a second"));
  CHECK(said("scrubber: scrub 1: from step") && !said("scrubber: 0 scrubs"));
  if (!kind.small)
    CHECK(said(" 0 moves from a key") == (kind.steps == 1));
  CHECK(said("paused on its last picture") && !said("the race is over before step"));
  CHECK(said("units of 32 steps") && said(", 0 dropped;") == !kind.small);
  CHECK(host.wrong_prefix == 0 && (host.packed_with_prefix_bytes > 0) == host.packs);  // (the prefix chain held)
  CHECK(said(" steps of the play position were run again from a key in 0 moves") == !kind.small);
  if (kind.finish_goal != 0)
    CHECK(race.cuts == 1 && said("which is the replay's end") && said(", 1 frames cut at the race's end"));
  else
    CHECK(said("the race did not settle in it; the replay ends before step 419") && highest == 419);
  if (kind.steps == 4)
    CHECK(said("play at the end: again from the race's first step, 60"));
  if (kind.steps == 1)
    CHECK(said("beyond the front edge at"));
  if (!leak)
  {
    CHECK(said("scrubber: checksums: 0 of ") && said(" front steps differed from the recording; 0 of "));
    CHECK(host.log.empty());
    return;
  }
  CHECK(!said("scrubber: checksums: 0 of ") && !said("differed from the recording; 0 of "));
  CHECK(host.log.find("at the front edge differs from the recording") != std::string::npos);
  CHECK(host.log.find("at the play position differs from the recording") != std::string::npos);
}

// The bar of every frame of the race: its steps, and the sound gate's word (ScrubBar::heard).
void WhatTheBarShowed(const ScrubHost& host, const Kind& kind)
{
  u32 race_bars = 0, heard = 0, standing_heard = 0, before_heard = 0, jumped_to_100 = 0, laps = 0;
  for (const ScrubBar& bar : host.bars)
  {
    if (bar.first != 60)
    {
      before_heard += bar.heard ? 1 : 0;  // (before the race: an empty bar, and no sound)
      continue;
    }
    ++race_bars;
    heard += bar.heard ? 1 : 0;
    standing_heard += bar.standing && bar.heard ? 1 : 0;
    jumped_to_100 += bar.played == 101 ? 1 : 0;
    CHECK(bar.played <= bar.last && bar.buffered >= bar.first && bar.played >= bar.first);
    laps = std::max(laps, bar.laps);
  }
  CHECK(race_bars > 300 && before_heard == 0 && standing_heard == 0);
  // the pauses, the scrubs and the end are silent; playing forward is heard
  CHECK(heard > 50 && heard + 30 < race_bars);
  if (kind.steps == 4)
    CHECK(jumped_to_100 >= 1);  // (the jump to step 100: the picture of step 100, then on)
  CHECK(laps == 0);  // (the fake race has no machines' progress records: no lap marks)
}
}  // namespace

namespace
{
// A playback watched with the scrubber (as the race, as every playback): its race simulated ahead of its
// picture in the same game, the two moments exchanged in every frame. Every step at the front edge and every step
// run for a picture gives the recording's checksum: through a pause, jumps forwards and back, a jump beyond the
// front edge, and scrubs (the bar held: the pointer dragged forwards and backwards, slowly and fast, and beyond
// what is buffered), whose memory comes from the front's keys. A game whose steps read a word that is not
// exchanged differs at both. The replay ends where its race settles, or at the recording's last step: the play
// position never runs that step, pauses on the picture before it, and the game is not stopped; play there starts
// again from the race's first step. The front's log is packed by units, with a host's packer and without; with a
// budget smaller than the replay needs its packed units are dropped, and a step whose log is gone is run again from
// the key below before it is shown. The bar's sound gate is open only while the replay plays forward.
void TestAPlaybackScrubbed()
{
  BEGIN_TEST();
  const std::string dir = Temp();
  std::string image(0x100, '\0');
  image.replace(0, 4, "FZMD");
  image.replace(0x10, 4, "FZSI");
  WriteText(dir + "/mod.bin", image);
  WriteText(dir + "/manifest.json", "{\"commit\": \"abc1234\"}");
  WriteText(dir + "/codes.txt", "$Boot to race\n48000000 800030C8\n$Replay boot\n04000000 00000001\n");
  std::mt19937 rng(12);
  for (const Kind& kind : KINDS)
  {
    const bool leak = kind.game == Game::Leaking;
    const Ways ways{kind.game, leak, kind.game == Game::Scratch, leak};
    std::vector<u8> end_state;
    WriteReplay(dir + "/race.fzrp", {1, 0}, "", "", 0, RecordTheRace(rng, ways, end_state, kind.finish_goal));
    ScrubHost host;
    host.packs = kind.game != Game::Scratch;  // (one kind keeps its units flat)
    host.scratch.assign(ways.with_scratch ? 0x4000 : 0, 0);
    // the undo log's guard, checked at every step against every page
    // compared: the process has one, and the front's log, at its first step after the learning ones before the play
    // log's, takes it; the moments' exchanges, keys and adoptions write memory it protects
    host.guardable = true;
    Options options;
    options.debug.undo_log = "both";
    options.bundle_dir = dir;
    options.play_file = dir + "/race.fzrp";
    options.replay_scrubber = true;
    options.debug.replay_memory = kind.small ? 64 : 4096;
    options.debug.replay_near = kind.small ? 64 : 1024;  // (small: only the steps near the play position stay)
    options.debug.buffer_steps = kind.steps;
    options.debug.replay_script = kind.script;
    Init(host, options);
    CHECK(Active() && host.log.empty());
    FakeRace race(host, RACE_FROM, 100000, leak);  // (the phase never leaves the race by the step alone)
    race.image_loop = true;
    race.finish_goal = kind.finish_goal;
    race.holds_end = kind.finish_goal != 0;
    u32 frames = 0, highest = 0;
    for (; frames < 1200 && race.Frame(); ++frames)
      highest = std::max(highest, race.g.Read32(net::STEP));
    // the game is not stopped at the replay's end, and nothing of the game's own race end happened
    CHECK(host.stops == 0 && frames == 1200 && race.ends == 0);
    Shutdown();
    WhatTheScrubberSaid(host, race, kind, highest);
    WhatTheBarShowed(host, kind);
    const auto said = [&](const std::string& text) { return host.notices.find(text) != std::string::npos; };
    CHECK(said("undo log: guard on") && said("undo log: the guard is another log's"));
    CHECK(said("steps compared with every page: the log the same at every step") && !said("log different at") &&
          said("a watched page rested 0 times"));
  }
  TestHost last;
  Init(last, Options{});
  std::filesystem::remove_all(dir);
}

// A check's playback with the scrubber (Debug::replay_end_quits): at the replay's end its summary says every step
// compared, front edge and picture alike, and the game is stopped.
void TestAScrubbedPlaybackChecked()
{
  BEGIN_TEST();
  const std::string dir = Temp();
  std::string image(0x100, '\0');
  image.replace(0, 4, "FZMD");
  image.replace(0x10, 4, "FZSI");
  WriteText(dir + "/mod.bin", image);
  WriteText(dir + "/manifest.json", "{\"commit\": \"abc1234\"}");
  WriteText(dir + "/codes.txt", "$Boot to race\n48000000 800030C8\n$Replay boot\n04000000 00000001\n");
  std::mt19937 rng(13);
  const Ways ways{Game::Plain, false, false, false};
  std::vector<u8> end_state;
  WriteReplay(dir + "/race.fzrp", {1, 0}, "", "", 0, RecordTheRace(rng, ways, end_state));
  ScrubHost host;
  Options options;
  options.bundle_dir = dir;
  options.play_file = dir + "/race.fzrp";
  options.replay_scrubber = true;
  options.debug.buffer_steps = 3;
  options.debug.replay_end_quits = true;
  // (paused at 80 and so to the end: a check stops there too)
  options.debug.replay_script = "30:j:250,40:j:90,50:s:200,51:k:4,70:r,80:p,90:e";
  Init(host, options);
  FakeRace race(host, RACE_FROM, 100000, false);
  race.image_loop = true;
  for (u32 frames = 0; frames < 1200 && host.stops == 0 && race.Frame(); ++frames)
  {
  }
  Shutdown();
  const auto said = [&](const std::string& text) { return host.notices.find(text) != std::string::npos; };
  CHECK(host.stops == 1 && said("the check's playback stops here"));
  CHECK(said("playback: 420 of 420 steps compared with the recording, 0 differed") && host.log.empty());
  TestHost last;
  Init(last, Options{});
  std::filesystem::remove_all(dir);
}

// Frame steps (. and ,): a playing replay pauses at the first, each step shows the next step's picture or the one
// before, by one, silently; back below where the play position's own log begins (it begins again at a jump) the
// memory comes from the front's log or a key, and every step shown still gives the recording's checksum. In the race
// view [ and ] change the racer watched, wrapping round.
void TestFrameSteps()
{
  BEGIN_TEST();
  const std::string dir = Temp();
  std::string image(0x100, '\0');
  image.replace(0, 4, "FZMD");
  image.replace(0x10, 4, "FZSI");
  WriteText(dir + "/mod.bin", image);
  WriteText(dir + "/manifest.json", "{\"commit\": \"abc1234\"}");
  WriteText(dir + "/codes.txt", "$Boot to race\n48000000 800030C8\n$Replay boot\n04000000 00000001\n");
  std::mt19937 rng(14);
  const Ways ways{Game::Plain, false, false, false};
  std::vector<u8> end_state;
  WriteReplay(dir + "/race.fzrp", {1, 0}, "", "", 0, RecordTheRace(rng, ways, end_state));
  ScrubHost host;
  Options options;
  options.bundle_dir = dir;
  options.play_file = dir + "/race.fzrp";
  options.replay_scrubber = true;
  options.debug.buffer_steps = 8;
  // playing at race frame 60: a jump to step 300; on, then 15 steps back, one a frame (the last ones below the
  // jump, where the play position's own log begins again: from the front's log); three on; then standing; the
  // racer after and twice the one before
  std::string script = "60:j:300";
  for (u32 f = 70; f < 85; ++f)
    script += "," + std::to_string(f) + ":v";
  script += ",90:n,91:n,92:n,120:i,121:u,122:u";
  options.debug.replay_script = script;
  Init(host, options);
  FakeRace race(host, RACE_FROM, 100000, false);
  race.image_loop = true;
  u32 seat_before = 0xFFFFFFFF;
  for (u32 frames = 0; frames < 300 && race.Frame(); ++frames)
    if (seat_before == 0xFFFFFFFF && !host.bars.empty() && host.bars.back().first == 60)
      seat_before = race.g.Read32(table::SLOT + 4);
  const u32 seat_after = race.g.Read32(table::SLOT + 4);
  Shutdown();
  const auto said = [&](const std::string& text) { return host.notices.find(text) != std::string::npos; };
  std::vector<u32> shown;  // the picture's step, frame by frame, from the jump's picture on
  for (const ScrubBar& bar : host.bars)
    if (bar.first == 60 && (!shown.empty() || bar.played == 301))
      shown.push_back(bar.played - 1);
  CHECK(shown.size() > 100 && shown[0] == 300);
  // on to the script's first step back, fifteen back one a frame, standing, three on one a frame, standing
  const auto run = [&shown](std::size_t from, std::size_t count, int by) {
    for (std::size_t k = from + 1; k <= from + count; ++k)
      if (static_cast<int>(shown[k]) - static_cast<int>(shown[k - 1]) != by)
        return false;
    return true;
  };
  std::size_t back = 0;
  while (back + 1 < shown.size() && shown[back + 1] == shown[back] + 1)
    ++back;
  CHECK(back == 9 && run(back, 15, -1) && shown[back + 15] == 294);
  std::size_t on = back + 15;
  while (on + 1 < shown.size() && shown[on + 1] == shown[on])
    ++on;
  CHECK(on > back + 15 && run(on, 3, 1) && run(on + 3, shown.size() - on - 4, 0));
  CHECK(said("frame steps: 3 on, 15 back (") && said("3 changes of the racer watched"));
  CHECK(seat_before < 2 && seat_after == (seat_before + 1) % 2);
  CHECK(said("scrubber: checksums: 0 of ") && said(" front steps differed from the recording; 0 of ") && host.log.empty());
  // every frame from the first frame step on is silent
  u32 heard_after = 0;
  for (std::size_t i = host.bars.size() - 150; i < host.bars.size(); ++i)
    heard_after += host.bars[i].heard ? 1 : 0;
  CHECK(heard_after == 0);
  TestHost last;
  Init(last, Options{});
  std::filesystem::remove_all(dir);
}

// The sound gate's level: it fades over 100 ms each way, from wherever it is, and the volume it gives is never above
// the player's own (0 under --mute).
void TestTheSoundGate()
{
  BEGIN_TEST();
  SoundGate gate;
  CHECK(gate.Now() == 0.0 && gate.Level(false, 1000) == 0.0 && !gate.Fading(false));  // (shut from the start)
  CHECK(gate.Fading(true) && std::abs(gate.Level(true, 1050) - 0.5) < 1e-9 && gate.Fading(true));
  CHECK(gate.Level(true, 1100) == 1.0 && !gate.Fading(true));
  CHECK(gate.Level(true, 5000) == 1.0);            // (heard while it stays open)
  CHECK(std::abs(gate.Level(false, 5025) - 0.75) < 1e-9);  // (the fade counts from the last tick: 25 ms)
  CHECK(std::abs(gate.Level(true, 5035) - 0.85) < 1e-9);   // (turned back on its way: up from where it was)
  CHECK(gate.Level(false, 9000) == 0.0 && !gate.Fading(false) && gate.Now() == 0.0);
  CHECK(gate.Level(false, 8000) == 0.0);           // (a clock that went back moves nothing)
  // the host's ticks: the window's timer (33 ms) and the gate's own while it fades (10 ms) take it from shut to open in
  // about 100 ms, every level on the way at most the player's volume
  SoundGate ticked;
  int last = 0, steps = 0;
  bool over = false;
  for (double ms = 0; ticked.Fading(true) || ms == 0; ms += 10)
  {
    const int volume = SoundGate::Volume(80, ticked.Level(true, ms));
    over |= volume > 80 || volume < last;
    last = volume;
    ++steps;
  }
  CHECK(!over && last == 80 && steps >= 10 && steps <= 12);
  CHECK(SoundGate::Volume(80, 1.0) == 80 && SoundGate::Volume(80, 0.5) == 40 && SoundGate::Volume(80, 0.0) == 0);
  CHECK(SoundGate::Volume(0, 1.0) == 0 && SoundGate::Volume(150, 1.0) == 100 && SoundGate::Volume(-5, 1.0) == 0);
}

// The keys of the game's window (fzgx/replay_keys.h): which are the scrubber's, and which of them repeat while held.
void TestTheReplayKeys()
{
  BEGIN_TEST();
  const auto press = [](std::uintptr_t key, bool repeat) { return ReplayKeyOf(key, repeat); };
  CHECK(press(0x20, false).ours && press(0x20, false).bit == KeyPause && press(0x20, true).ours && press(0x20, true).bit == 0);
  CHECK(press(0x25, true).bit == KeyBack && press(0x27, true).bit == KeyAhead);        // Left and Right repeat
  CHECK(press(0xBE, true).bit == KeyFrameAhead && press(0xBC, true).bit == KeyFrameBack);  // . and , repeat
  CHECK(press(0x24, false).bit == KeyFirst && press(0x24, true).bit == 0 && press(0x23, false).bit == KeyLast);
  CHECK(press(0xDB, false).bit == KeyRacerBack && press(0xDD, false).bit == KeyRacerAhead && press(0xDD, true).bit == 0);
  CHECK(!press(0x41, false).ours && !press(0x7A, false).ours && !press(0x1B, false).ours);  // A, F11, Escape: not ours
}

// The launch file: a playback has the scrubber unless "replay.scrubber" says false; a test of the rollback path has
// it off, and refuses it named; its tests' keys need it.
void TestTheScrubberInALaunchFile()
{
  BEGIN_TEST();
  Options o;
  const auto read = [&o](const char* text) { return ParseLaunchFile(text, &o); };
  const auto refused = [&](const char* text, const char* why) {
    const std::optional<std::string> problem = read(text);
    return problem && problem->find(why) != std::string::npos;
  };
  CHECK(!read(R"({"about": "t", "bundle": "b", "replay": {"file": "f"}})") && o.replay_scrubber);
  CHECK(!read(R"({"about": "t", "bundle": "b", "replay": {"file": "f", "scrubber": false}})") && !o.replay_scrubber);
  CHECK(!read(R"({"about": "t"})") && !o.replay_scrubber);
  CHECK(!read(R"({"about": "t", "bundle": "b", "replay": {"file": "f"},
                  "debug": {"forceRollback": {"every": 2, "depths": [3]}}})") && !o.replay_scrubber);
  CHECK(!read(R"({"about": "t", "bundle": "b", "replay": {"file": "f"}, "debug": {"lateSteps": 2}})") &&
        !o.replay_scrubber);
  CHECK(refused(R"({"about": "t", "bundle": "b", "replay": {"file": "f", "scrubber": true}, "debug": {"lateSteps": 2}})",
                "the scrubber runs the race its own way"));
  CHECK(refused(R"({"about": "t", "bundle": "b", "replay": {"file": "f", "scrubber": false},
                    "debug": {"replayScript": "10:p"}})", "which is off in this start"));
  CHECK(refused(R"({"about": "t", "bundle": "b", "replay": {"file": "f"}, "debug": {"replayScript": "10:x:3"}})",
                "is not FRAME:WHAT[:N]"));
  CHECK(!read(R"({"about": "t", "bundle": "b", "replay": {"file": "f"},
                  "debug": {"bufferShare": 30, "replayMemory": 200, "replayEndQuits": true,
                            "replayScript": "10:p,20:j:500,30:s:9,31:f:2,40:r"}})") &&
        o.replay_scrubber && o.debug.buffer_share == 30 && o.debug.replay_memory == 200 && o.debug.replay_end_quits);
}
}  // namespace

void TestTheScrubbersBudget();  // (test_scrub_budget.cpp)

// The replay scrubber's tests, in one call (main.cpp).
void TestTheReplayScrubber()
{
  TestAPlaybackScrubbed();
  TestAScrubbedPlaybackChecked();
  TestFrameSteps();
  TestTheSoundGate();
  TestTheReplayKeys();
  TestTheScrubberInALaunchFile();
  TestTheScrubbersBudget();
}
