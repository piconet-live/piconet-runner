// Tests of the replay scrubber's harder moves: steps to run again queued when the front edge finds the race's end,
// keys dropped over the memory budget, the racer watched in the race view through pauses and seeks, and the finish
// banner's words at the play position.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "harness.h"
#include "recorded_race.h"
#include "scrub_host.h"
#include "undo_log.h"

namespace
{
using namespace recorded;
using scrub_test::ScrubHost;

// A replay folder with its bundle, and a race recorded into it.
std::string Folder(std::mt19937& rng, u32 finish_goal, u32 knock_out_at)
{
  const std::string dir = Temp();
  std::string image(0x100, '\0');
  image.replace(0, 4, "FZMD");
  image.replace(0x10, 4, "FZSI");
  WriteText(dir + "/mod.bin", image);
  WriteText(dir + "/manifest.json", "{\"commit\": \"abc1234\"}");
  WriteText(dir + "/codes.txt", "$Boot to race\n48000000 800030C8\n$Replay boot\n04000000 00000001\n");
  std::vector<u8> end_state;
  WriteReplay(dir + "/race.fzrp", {1, 0}, "", "", 0,
              RecordTheRace(rng, Ways{Game::Plain, false, false, false}, end_state, finish_goal, knock_out_at));
  return dir;
}

Options Scrubbed(const std::string& dir, u32 steps, const std::string& script)
{
  Options options;
  options.bundle_dir = dir;
  options.play_file = dir + "/race.fzrp";
  options.replay_scrubber = true;
  options.debug.buffer_steps = steps;
  options.debug.replay_script = script;
  return options;
}

bool Said(const TestHost& host, const std::string& text)
{
  return host.notices.find(text) != std::string::npos;
}

bool Exact(const TestHost& host)
{
  return Said(host, "scrubber: checksums: 0 of ") && Said(host, " front steps differed from the recording; 0 of ") &&
         host.log.empty();
}

// A jump far back, to steps whose log was dropped, queues them to be run again after the frame's front steps. When
// that frame's front step is the one that makes the race over, the image ends the frame there and none of them runs:
// the move is taken again in the next frame, from the step the memory is really at. Tried at every frame around the
// race's end; at least one of them meets it, and every step shown still gives the recording's checksum.
void TestASeekQueuedAtTheEnd()
{
  BEGIN_TEST();
  std::mt19937 rng(15);
  const std::string dir = Folder(rng, 280, 0);
  u32 met = 0, runs = 0;
  for (u32 at = 20; at < 80; ++at)
  {
    ScrubHost host;
    Options options = Scrubbed(dir, 4, std::to_string(at) + ":j:250," + std::to_string(at + 40) + ":j:150");
    options.debug.replay_memory = 1;  // (every packed unit dropped but near the play position: a jump is steps run
    options.debug.replay_near = 64;   // again from a key)
    Init(host, options);
    FakeRace race(host, RACE_FROM, 100000, false);
    race.image_loop = true;
    race.finish_goal = 280;
    race.holds_end = true;
    for (u32 frames = 0; frames < 400 && race.Frame(); ++frames)
    {
    }
    Shutdown();
    ++runs;
    met += Said(host, "were queued at the race's end, and none ran") ? 1 : 0;
    CHECK(Exact(host) && Said(host, "steps of the play position were run again from a key in") && race.cuts == 1);
  }
  CHECK(runs == 60 && met >= 1);
  TestHost last;
  Init(last, Options{});
  std::filesystem::remove_all(dir);
}

// Over the budget the keys go too, farthest from the play position first, but never those near it nor the race's
// first; a jump past keys that went is steps run again from the nearest key below, and gives the recording's race.
void TestKeysOverTheBudget()
{
  BEGIN_TEST();
  std::mt19937 rng(16);
  const std::string dir = Folder(rng, 0, 0);
  ScrubHost host;
  // the play position near step 70 while the front buffers the whole race; then a jump to step 400, far above
  Options options = Scrubbed(dir, 8, "5:p,90:p,100:j:400");
  options.debug.replay_memory = 1;
  options.debug.replay_near = 64;
  Init(host, options);
  FakeRace race(host, RACE_FROM, 100000, false);
  race.image_loop = true;
  for (u32 frames = 0; frames < 300 && race.Frame(); ++frames)
  {
  }
  Shutdown();
  // keys at 128, 256 and 384 above the race's first, 60: those beyond 64 steps of the play position (near 66) go
  CHECK(Said(host, " keys dropped)") && !Said(host, "(0 keys dropped)") && Exact(host));
  CHECK(Said(host, "frames over it with nothing left that may go") && !Said(host, "in 0 moves"));
  TestHost last;
  Init(last, Options{});
  std::filesystem::remove_all(dir);
}

// The keys kept over the budget (undo_pack.cpp, DropFarthestKey): keys are taken at multiples of 128 steps and a race
// starts on any step (here 60), so one key every 512 steps stays whatever the race's first step: 512 and 1,024 are
// never dropped, nor those within `near` of the play position, nor the race's first; every other key goes.
void TestKeysKeptEvery512()
{
  BEGIN_TEST();
  fzgx::net::Tracking s;
  for (u32 at = 128; at <= 1536; at += 128)
  {
    fzgx::net::Key key;
    key.ram.resize(64);
    s.key_bytes += key.ram.size();
    s.keys.emplace(at, std::move(key));
  }
  u32 dropped = 0;
  while (fzgx::net::DropFarthestKey(s, 1500, 64, 60, 512) != 0)
    ++dropped;
  std::vector<u32> left;
  for (const auto& [at, key] : s.keys)
    left.push_back(at);
  CHECK((left == std::vector<u32>{512, 1024, 1536}));
  CHECK(dropped == 9 && s.key_bytes == 3 * 64);
}

// The undo log's spare lists (undo_log.cpp, DropEntry) serve the replay scrubber's packing (undo_pack.cpp): a step's
// entry packed gives its list to the spare ones, a unit unpacked takes them again, comes out as it went in (its steps
// XORed with the one before, one of them already in the map) and gives them back when it is let go. Through all of it
// the log's count of its entries' bytes is what they hold.
void TestPackingTakesTheSpareLists()
{
  BEGIN_TEST();
  namespace net = fzgx::net;
  ScrubHost host;
  net::Tracking s;
  const auto made = [](u32 t) {
    net::Undo e;
    for (u32 b = 0; b < 40; ++b)
    {
      auto& block = e.blocks.emplace_back(b * 64 + (t % 3) * 32, std::array<u8, 32>{});
      for (u32 i = 0; i < 32; ++i)
        block.second[i] = static_cast<u8>((b * 7 + i + t / 4) & 0xFF);
    }
    e.has_rng = t % 5 == 0;
    e.rng = {static_cast<u8>(t), 1, 2, 3};
    return e;
  };
  const auto counted = [&s] {
    u64 sum = 0;
    for (const auto& [t, e] : s.undo)
      sum += net::EntryBytes(e);
    return sum == s.undo_bytes;
  };
  for (u32 t = 0; t < 2 * net::UNIT; ++t)
  {
    net::Undo& e = s.undo[t];
    e = made(t);
    s.undo_bytes += net::EntryBytes(e);
    net::PackStep(host, s, t);
  }
  const std::size_t given = s.spare.size();
  CHECK(s.undo.empty() && s.undo_bytes == 0 && given == 2 * net::UNIT && s.spare_bytes > 0 && s.packs == 2 * net::UNIT);
  // a step of the first unit already in the map, here unlike its packed one: it stays, and the step after it is
  // unpacked against the one made again from the unit, not against the map's
  s.undo[5] = made(1000);
  s.undo_bytes += net::EntryBytes(s.undo[5]);
  CHECK(net::EnsureEntries(host, s, 2 * net::UNIT, 0) && s.undo.size() == 2 * net::UNIT && counted());
  CHECK(s.spare.size() < given);
  bool same = true;
  for (u32 t = 0; t < 2 * net::UNIT; ++t)
  {
    const net::Undo& e = s.undo.at(t);
    const net::Undo want = made(t == 5 ? 1000 : t);
    same = same && e.blocks == want.blocks && e.scratch.empty() && e.has_rng == want.has_rng && e.rng == want.rng;
  }
  CHECK(same);
  // the unit farther from step 63 let go: its entries' lists back to the spare ones
  const std::size_t before = s.spare.size();
  CHECK(net::DropFarthestUnit(s, 2 * net::UNIT - 1, 0) != 0 && s.undo.size() == net::UNIT && counted());
  CHECK(s.spare.size() > before && s.undo.begin()->first == net::UNIT);
}

// The race view's racer watched (playback.cpp, SpectateReplay): 120 simulated steps after the chosen racer is knocked
// out the camera moves to the leader still racing; a pause counts none of them; a seek back before the knock-out
// watches the chosen racer again, and playing through it again waits the 120 steps again.
void TestTheRacerWatchedThroughSeeks()
{
  BEGIN_TEST();
  std::mt19937 rng(17);
  const std::string dir = Folder(rng, 0, 200);  // (seat 0 knocked out in step 200)
  ScrubHost host;
  // the stand-in seat to seat 0 (u or i, by where it starts), paused from frame 160 to 460, a jump back to 150 at 600
  Options options = Scrubbed(dir, 4, "160:p,460:p,600:j:150");
  Init(host, options);
  FakeRace race(host, RACE_FROM, 100000, false);
  race.image_loop = true;
  race.knock_out_at = 200;
  struct Seen
  {
    u32 step, seat;
    bool standing;
  };
  std::vector<Seen> seen;
  bool chose = false;
  for (u32 frames = 0; frames < 900 && race.Frame(); ++frames)
  {
    const u32 seat = race.g.Read32(table::SLOT + 4);
    if (!chose && !host.bars.empty() && host.bars.back().first == 60)
    {
      chose = true;
      host.keys = seat == 0 ? 0u : static_cast<u32>(KeyRacerAhead);  // (two seats: the next is seat 0)
      continue;
    }
    if (chose && !host.bars.empty() && host.bars.back().first == 60)
      seen.push_back({host.bars.back().played - 1, seat, host.bars.back().standing});
  }
  Shutdown();
  CHECK(chose && seen.size() > 500);
  // seat 0 all through the pause (it began after the knock-out, before 120 steps had passed)
  u32 paused = 0, first_switch = 0, back = 0, second_switch = 0;
  for (std::size_t i = 5; i < seen.size(); ++i)
  {
    if (seen[i].standing && seen[i].step > 200 && seen[i].step < 320)
      paused += seen[i].seat == 0 ? 1 : 1000;
    if (first_switch == 0 && seen[i].seat == 1)
      first_switch = seen[i].step;
    if (first_switch != 0 && back == 0 && seen[i].step < 200 && seen[i].seat == 0)
      back = seen[i].step;
    if (back != 0 && second_switch == 0 && seen[i].seat == 1)
      second_switch = seen[i].step;
  }
  CHECK(paused > 250 && paused < 1000);
  CHECK(first_switch >= 320 && first_switch <= 324);
  CHECK(back != 0 && second_switch >= 320 && second_switch <= 324);
  CHECK(Exact(host));
  TestHost last;
  Init(last, Options{});
  std::filesystem::remove_all(dir);
}

// The finish banner's words at the play position (scrub_show.cpp, FinishBanner): before the play step the kind and
// the countdown's end are the play position's own, from the step the front edge saw the racer watched finish (seat 0,
// whose machine this game finishes in the race's first step); they do not move while the front runs ahead, and a seek
// back before the finish takes them back.
void TestTheFinishBanner()
{
  BEGIN_TEST();
  std::mt19937 rng(18);
  const std::string dir = Folder(rng, 280, 0);
  ScrubHost host;
  Options options = Scrubbed(dir, 6, "100:j:60,140:j:60");
  Init(host, options);
  FakeRace race(host, RACE_FROM, 100000, false);
  race.image_loop = true;
  race.finish_goal = 280;
  race.holds_end = true;
  struct Seen
  {
    u32 shown, kind, end;
  };
  std::vector<Seen> seen;
  bool chose = false;
  for (u32 frames = 0; frames < 300 && race.Frame(); ++frames)
  {
    if (host.bars.empty() || host.bars.back().first != 60)
      continue;
    if (!chose)
    {
      chose = true;
      host.keys = race.g.Read32(table::SLOT + 4) == 0 ? 0u : static_cast<u32>(KeyRacerAhead);
      continue;
    }
    if (race.g.Read32(table::SLOT + 4) == 0)
      seen.push_back({host.bars.back().played - 1, race.g.Read32(layout::FINISH_KIND), race.g.Read32(layout::FINISH_END)});
  }
  Shutdown();
  u32 finish = 0xFFFFFFFF, before = 0, after = 0, wrong = 0;
  for (const Seen& x : seen)
    if (x.kind == 1)
      finish = std::min(finish, x.shown);
  for (const Seen& x : seen)
  {
    if (x.shown < finish)
      before += x.kind == 0 ? 1 : 0, wrong += x.kind == 0 ? 0 : 1;
    else
      after += 1, wrong += x.kind == 1 && x.end == finish + 480 ? 0 : 1;
  }
  // the kind is 1 from the finish's boundary on, with the end 480 steps from it, and 0 before it: after both seeks too
  CHECK(chose && finish != 0xFFFFFFFF && before >= 2 && after > 100 && wrong == 0 && Exact(host));
  TestHost last;
  Init(last, Options{});
  std::filesystem::remove_all(dir);
}
}  // namespace

void TestTheScrubbersSeeks()
{
  TestASeekQueuedAtTheEnd();
  TestKeysOverTheBudget();
  TestKeysKeptEvery512();
  TestPackingTakesTheSpareLists();
  TestTheRacerWatchedThroughSeeks();
  TestTheFinishBanner();
}
