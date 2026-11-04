// Tests of the Rollback library's operations for the deep-rollback worker (research/study-rollback-second-core.md,
// S1): TakeOver, the per-log and per-memory Moments, the dirty pages and the two comparisons.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fzgx/rollback_worker.h"
#include "harness.h"
#include "undo_log.h"

namespace
{
// A step of a fake race: words at places chosen by the step and the timeline (0 the predicted one, 1 the corrected
// one), so that the two timelines change partly the same blocks and partly others; the locked cache and the
// generator word too.
void Poke(Guest& g, TestHost& host, u32 first, u32 size, u32 step, u32 timeline)
{
  std::mt19937 at(step * 7919u + 13u), value(step * 104729u + timeline * 1299709u + 1u);
  std::mt19937 own(step * 31u + timeline * 977u + 5u);
  for (int k = 0; k < 30; ++k)
  {
    const u32 page = k < 20 ? (at() % 48) : (at() % (size / net::PAGE));
    g.Write32(first + page * net::PAGE + (at() % (net::PAGE / 4)) * 4, value());
  }
  for (int k = 0; k < 6; ++k)  // blocks only this timeline changes in this step
    g.Write32(first + (own() % (size / 4)) * 4, value());
  const u32 c = (at() % (0x4000 / 4)) * 4;
  if (!host.scratch.empty() && (c < net::SCRATCH_KEPT || c >= net::SCRATCH_KEPT_END))
  {
    const u32 v = value();
    std::memcpy(host.scratch.data() + c, &v, 4);
  }
  g.Write32(net::DOL_RNG, 0x5000 + step * 2 + timeline);
}

// The tracked memory, the locked cache and the generator word of a memory, whole.
struct Copy
{
  std::vector<u8> ram, scratch;
  u32 rng = 0;
};
Copy Take(Guest& g, TestHost& host, u32 first, u32 size)
{
  return {std::vector<u8>(g.Ram(first, size), g.Ram(first, size) + size), host.scratch, g.Read32(net::DOL_RNG)};
}
}  // namespace

// The worker's job, as S1 to S3 run it: the game's memory at the frame's step L with its log; the worker's memory a
// copy of it, rewound to R by the game's log (Apply), taken as its log's moment there (Adopt), and run again from R
// on the corrected timeline while the game goes on predicted to A; then TakeOver at A. The game's memory before A
// must then be the worker's on everything a restore puts back, the kept parts and untracked pages as the game left
// them, and the history from R on the worker's: a restore to any step from R on gives the corrected moment, a
// restore below R the old one.
void TestTheWorkersTakeOver()
{
  TestHost host, other;
  ModData data, other_data;
  Guest g(host, data), gw(other, other_data);
  std::mt19937 rng(20261104);
  FakeGame(host, g, rng);
  host.scratch.assign(0x4000, 0);
  for (u8& b : host.scratch)
    b = static_cast<u8>(rng());
  const u32 first = (MODULE + net::DATA) & ~(net::PAGE - 1), size = net::STATE_END - first;
  const u32 keep_page = first + 40 * net::PAGE, keep_part = first + 90 * net::PAGE + 0x100;
  g.Write32(net::UNTRACKED, 2);
  g.Write32(net::UNTRACKED + 4, keep_page);
  g.Write32(net::UNTRACKED + 8, keep_page + net::PAGE);
  g.Write32(net::UNTRACKED + 12, keep_part);
  g.Write32(net::UNTRACKED + 16, keep_part + 0x40);

  net::Rollback live;
  live.Start(g);
  CHECK(live.Region() == first && live.TakeDirty().size() == size / net::PAGE);  // all of it after a Start
  constexpr u32 R = 150, L = 160, A = 163;
  std::vector<Copy> predicted;
  for (u32 step = 0; step < L; ++step)
  {
    live.SaveStep(g, step, step % 3 == 0);
    predicted.push_back(Take(g, host, first, size));
    Poke(g, host, first, size, step, 0);
    const u8 row[6] = {static_cast<u8>(step), 1, 2, 3, 4, 5};
    net::Rollback::UseInputs(g, step, {0}, row);
    net::Rollback::KeepChecksum(g, step, 0xC0000000 + step);
  }
  live.SaveStep(g, L, true);
  // the pages the steps changed are dirty; an untouched page is not, an untracked one always is
  const std::vector<u32> dirty = live.TakeDirty();
  CHECK(!dirty.empty() && dirty.size() < size / net::PAGE);
  CHECK(std::find(dirty.begin(), dirty.end(), keep_page - first) != dirty.end());
  CHECK(std::find(dirty.begin(), dirty.end(), (keep_part - first) & ~(net::PAGE - 1)) != dirty.end());
  CHECK(live.TakeDirty().size() == 2);  // (only the untracked page and the kept part's are left)

  // the job's start: the worker's memory is the game's, rewound to R by the game's log
  other.ram = host.ram;
  other.scratch = host.scratch;
  other_data.Restore(data.Snapshot());
  const u32 live_moments = live.Moments(), game_moments = net::Rollback::Moments(host);
  const u32 worker_memory = net::Rollback::Moments(other);
  live.Apply(gw, L, R);
  CHECK(live.Moments() == live_moments && net::Rollback::Moments(host) == game_moments);  // another memory's moment
  CHECK(net::Rollback::Moments(other) == worker_memory + 1);
  {
    const Copy at = Take(gw, other, first, size);
    CHECK(at.scratch == predicted[R].scratch && at.rng == predicted[R].rng);
    u32 differ = 0;
    for (u32 b = 0; b < size; ++b)
      differ += at.ram[b] != predicted[R].ram[b] && !(first + b >= keep_page && first + b < keep_page + net::PAGE) &&
                !(first + b >= keep_part && first + b < keep_part + 0x40);
    CHECK(differ == 0);
  }
  net::Rollback worker(false);
  worker.Start(gw);
  worker.Adopt(gw, live, R);
  CHECK(worker.Moments() == 2 && net::Rollback::Moments(other) == worker_memory + 3);
  CHECK(live.Moments() == live_moments && net::Rollback::Moments(host) == game_moments);

  // the worker runs R to A - 1 on the corrected timeline; the game goes on predicted from L to A - 1, and writes
  // its untracked page, its kept part and memory outside the log
  std::vector<Copy> corrected;
  for (u32 step = R; step < A; ++step)
  {
    worker.SaveStep(gw, step, false);
    corrected.push_back(Take(gw, other, first, size));
    Poke(gw, other, first, size, step, 1);
    gw.Write32(first + 0x10, 0xDD000000 + step);  // a word the adoption keeps as the game has it (S2: a time word)
    const u8 row[6] = {static_cast<u8>(step), 9, 9, 9, 9, 9};
    net::Rollback::UseInputs(gw, step, {0}, row);
    net::Rollback::KeepChecksum(gw, step, 0xD0000000 + step);
  }
  worker.SaveStep(gw, A, false);
  const Copy worker_at_a = Take(gw, other, first, size);
  for (u32 step = L; step < A; ++step)
  {
    if (step != L)  // (step L was saved at the hook)
      live.SaveStep(g, step, false);
    Poke(g, host, first, size, step, 0);
    g.Write32(keep_page + 8, 0xAA000000 + step);
    g.Write32(keep_part + 4, 0xBB000000 + step);
    g.Write32(0x80003100, 0xCC000000 + step);
    g.Write32(first + 0x10, 0xEE000000 + step);
  }
  live.SaveStep(g, A, true);
  CHECK(live.Compare(g, gw, nullptr, 0) > 0);
  std::vector<u32> words;
  CHECK(live.Compare(g, gw, &words, 3) > 3 && words.size() == 3 && words[0] >= first);

  // the adoption, one word kept as the game has it
  const std::vector<u32> keep = {first + 0x10};
  live.TakeOver(g, worker, gw, R, A, &keep);
  std::vector<u32> kept_word;
  CHECK(live.Compare(g, gw, &kept_word, 4) == 1 && kept_word.size() == 1 && kept_word[0] == first + 0x10);
  CHECK(g.Read32(first + 0x10) == 0xEE000000 + A - 1 && gw.Read32(first + 0x10) == 0xDD000000 + A - 1);
  CHECK(live.Moments() == live_moments && net::Rollback::Moments(host) == game_moments);
  CHECK(g.Read32(keep_page + 8) == 0xAA000000 + A - 1 && g.Read32(keep_part + 4) == 0xBB000000 + A - 1);
  CHECK(g.Read32(0x80003100) == 0xCC000000 + A - 1);
  CHECK(host.scratch == worker_at_a.scratch && g.Read32(net::DOL_RNG) == worker_at_a.rng);
  for (u32 step = R; step < A; ++step)
  {
    u8 row[6];
    net::Rollback::UsedInputs(g, step, {0}, row);
    CHECK(row[1] == 9 && net::Rollback::KeptChecksum(g, step) == 0xD0000000 + step);
  }
  CHECK(net::Rollback::KeptChecksum(g, R - 1) == 0xC0000000 + R - 1);  // below R: the game's own
  // the copied pages are dirty
  const std::vector<u32> taken = live.TakeDirty();
  CHECK(taken.size() > 2);

  // a step on from A, then a restore to a step the worker ran: its moment there, kept parts as they are now
  Poke(g, host, first, size, A, 1);
  live.SaveStep(g, A + 1, true);
  const auto same = [&](const Copy& want) {
    const Copy now = Take(g, host, first, size);
    u32 differ = 0;
    for (u32 b = 0; b < size; ++b)
      differ += now.ram[b] != want.ram[b] && !(first + b >= keep_page && first + b < keep_page + net::PAGE) &&
                !(first + b >= keep_part && first + b < keep_part + 0x40);
    return differ == 0 && now.scratch == want.scratch && now.rng == want.rng;
  };
  live.RestoreTo(g, A + 1, R + 4);
  CHECK(same(corrected[4]));
  CHECK(g.Read32(keep_page + 8) == 0xAA000000 + A - 1);
  // and below R: the old moment, the entries there untouched
  live.RestoreTo(g, R + 4, R - 3);
  CHECK(same(predicted[R - 3]));
  live.Stop();
  worker.Stop();
  CHECK(host.log.empty() && other.log.empty());
}

// The logs' own counts, the memories' counts, and the comparison of entries.
void TestRollbackMomentsPerLog()
{
  TestHost a, b;
  ModData da, db;
  Guest ga(a, da), gb(b, db);
  std::mt19937 rng(20261104);
  FakeGame(a, ga, rng);
  b.ram = a.ram;
  const u32 first = (MODULE + net::DATA) & ~(net::PAGE - 1), size = net::STATE_END - first;
  net::Rollback one, two(false);
  const u32 ma = net::Rollback::Moments(a), mb = net::Rollback::Moments(b);
  one.Start(ga);
  two.Start(gb);
  CHECK(one.Moments() == 1 && two.Moments() == 1);
  CHECK(net::Rollback::Moments(a) == ma + 1 && net::Rollback::Moments(b) == mb + 1);
  // the same steps over the two memories: the same entries; then one step differs in one block
  for (u32 step = 0; step < 130; ++step)
  {
    one.SaveStep(ga, step, step % 2 == 0);
    two.SaveStep(gb, step, step % 2 == 0);
    Poke(ga, a, first, size, step, 0);
    Poke(gb, b, first, size, step, 0);
    if (step == 125)
      gb.Write32(first + 8, gb.Read32(first + 8) ^ 1);
  }
  one.SaveStep(ga, 130, true);
  two.SaveStep(gb, 130, true);
  std::vector<u32> words;
  CHECK(one.Compare(ga, gb, &words, 4) == 1 && words.size() == 1 && words[0] == first + 8);
  // the presents differ in that word: the moments before every step since 125 too; left out, before 125 only
  CHECK(one.CompareMoments(ga, two, gb, 120, 130) == 130);
  const std::vector<u32> ignore{first + 8};
  CHECK(one.Compare(ga, gb, nullptr, 0, &ignore) == 0);
  CHECK(one.CompareMoments(ga, two, gb, 120, 130, &ignore) == 0xFFFFFFFF);
  CHECK(one.CompareMoments(ga, two, gb, 126, 130) == 0xFFFFFFFF);  // (the word is no block of those steps)
  // an operation of a log on its own memory moves its count and its memory's; on the other memory, only that one's
  one.Apply(gb, 130, 128);
  CHECK(one.Moments() == 1 && net::Rollback::Moments(a) == ma + 1 && net::Rollback::Moments(b) == mb + 2);
  one.Discard(ga);
  CHECK(one.Moments() == 2 && net::Rollback::Moments(a) == ma + 2 && two.Moments() == 1);
  // a restore is no other moment
  one.RestoreTo(ga, 130, 126);
  CHECK(one.Moments() == 2 && net::Rollback::Moments(a) == ma + 2);
  one.Stop();
  two.Stop();
  CHECK(one.Moments() == 3 && two.Moments() == 2 && net::Rollback::Moments(b) == mb + 3);
}

// A log whose memory took a new race's bytes behind its back (W's, copied from the game's at a job's start) has a
// stale shadow of the pages it does not watch: its learning steps, which compare every page, log the old race's bytes
// as those pages' content before the step, and a restore writes them back. Started again (as W's log is whenever
// the game's starts: Starts), it logs the present only. And a page first seen changing in any step, learning ones
// too, is a new watched page (WatchedPages: what gives a job up).
void TestTheWorkersLogStartsWithTheRace()
{
  TestHost host, game_host;
  ModData data, game_data;
  Guest g(host, data), game_g(game_host, game_data);
  std::mt19937 rng(20261105);
  FakeGame(host, g, rng);
  game_host.ram = host.ram;
  const u32 first = (MODULE + net::DATA) & ~(net::PAGE - 1);
  const u32 page = first + 70 * net::PAGE;  // a page the first race never writes
  net::Rollback game, w(false);
  game.Start(game_g);
  w.Start(g);
  CHECK(game.Starts() == 1 && w.Starts() == 1 && w.WatchedPages() == 0);
  for (u32 step = 0; step < 130; ++step)  // race 1: past both logs' learning steps
  {
    w.SaveStep(g, step, false);
    g.Write32(first + 8, step);
  }
  w.SaveStep(g, 130, false);
  CHECK(w.WatchedPages() == 1);
  const u32 race1 = g.Read32(page);
  // race 2: the game's log starts again; the memory takes the new race's bytes (W: copied from the game's)
  game.Stop();
  game.Start(game_g);
  CHECK(game.Starts() == 2);
  g.Write32(page, race1 ^ 0x5A5A5A5A);
  const u32 race2 = g.Read32(page);
  // not started again: a full comparison finds the page (late), and a restore to before it writes race 1's bytes
  {
    w.SaveStep(g, 200, true);
    g.Write32(first + 8, 1);
    w.SaveStep(g, 201, true);
    CHECK(w.WatchedPages() == 2 && w.LatePages() == 1);
    w.RestoreTo(g, 201, 199);  // (the change found at 200's comparison is step 199's)
    CHECK(g.Read32(page) == race1);  // the hazard
    g.Write32(page, race2);
  }
  // started again with the game's: the present is the shadow, so nothing of race 1 comes back
  w.Stop();
  w.Start(g);
  CHECK(w.Starts() == 2 && w.WatchedPages() == 0);
  const u32 watched = w.WatchedPages(), late = w.LatePages();
  w.SaveStep(g, 300, false);  // (a learning step: every page compared)
  CHECK(w.WatchedPages() == watched);
  const u32 before = g.Read32(page + 4);
  g.Write32(page + 4, before ^ 7);  // a page first written in a learning step: watched from here on (a job gives up)
  w.SaveStep(g, 301, false);
  CHECK(w.WatchedPages() == watched + 1 && w.LatePages() == late);  // (not a late page: no other count sees it)
  w.RestoreTo(g, 301, 300);
  CHECK(g.Read32(page) == race2 && g.Read32(page + 4) == before);
  game.Stop();
  w.Stop();
  CHECK(host.log.empty() && game_host.log.empty());
}

// The worker's mode (S3's default): the launch file's when it names one, else FZGX_ROLLBACK_WORKER's "off" or "on",
// else on from 4 hardware threads.
void TestTheWorkersDefault()
{
  using fzgx::rollback_worker::ModeFor;
  CHECK(ModeFor("", nullptr, 16) == "on");
  CHECK(ModeFor("", nullptr, 4) == "on");
  CHECK(ModeFor("", nullptr, 3) == "off");
  CHECK(ModeFor("", nullptr, 0) == "off");  // (hardware_concurrency unknown)
  CHECK(ModeFor("", "off", 16) == "off");
  CHECK(ModeFor("", "on", 2) == "on");
  CHECK(ModeFor("", "yes", 16) == "on" && ModeFor("", "", 2) == "off");  // (another value is not taken)
  CHECK(ModeFor("off", "on", 16) == "off");
  CHECK(ModeFor("on", "off", 2) == "on");
  CHECK(ModeFor("async-check", nullptr, 16) == "async-check");
}
