// Tests of the Rollback library's operations for the deep-rollback worker (research/study-rollback-second-core.md,
// S1): TakeOver, the per-log and per-memory Moments, the dirty pages and the two comparisons.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fzgx/rollback_worker.h"
#include "harness.h"
#include "rollback_worker_guard.h"
#include "rollback_worker_kept.h"
#include "rollback_worker_pace.h"
#include "rollback_worker_pair.h"
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

// The second worker's base (study section 20.1): a job O (W, from R) has run past c on the corrected rows when the
// row of c turns out late. N's memory is the game's, rewound to R by the game's log (Apply), then W's after-images
// of steps R to c - 1 written over it (Forward, W running on): W's moment at c. N runs from c on the rows of now;
// the game goes on predicted, adopts O at A1 (TakeOver) and N at A2. The game's memory is then one job's from R on
// the corrected rows (O's to c, N's from c), a restore to any step from R gives its moment there, below R the old
// one; the rows and checksums are O's then N's. After-images go with a restore, Final, KeepAfter(false) and Stop.
void TestTheSecondWorkersBase()
{
  BEGIN_TEST();
  TestHost host, w_host, n_host, r_host;
  ModData data, w_data, n_data, r_data;
  Guest g(host, data), gw(w_host, w_data), gn(n_host, n_data), gr(r_host, r_data);
  std::mt19937 rng(20261106);
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
  // two moments the same on what a restore puts back (the untracked page and the kept part apart)
  const auto same = [&](const Copy& a, const Copy& b) {
    u32 differ = 0;
    for (u32 k = 0; k < size; ++k)
      differ += a.ram[k] != b.ram[k] && !(first + k >= keep_page && first + k < keep_page + net::PAGE) &&
                !(first + k >= keep_part && first + k < keep_part + 0x40);
    return differ == 0 && a.scratch == b.scratch && a.rng == b.rng;
  };
  const auto row_of = [](u32 step, u8 fill) {
    return std::array<u8, 6>{static_cast<u8>(step), fill, fill, fill, fill, fill};
  };

  net::Rollback live;
  live.Start(g);
  constexpr u32 R = 150, C = 155, L = 160, A1 = 163, A2 = 167;
  std::vector<Copy> predicted;
  for (u32 step = 0; step < L; ++step)
  {
    live.SaveStep(g, step, step % 3 == 0);
    predicted.push_back(Take(g, host, first, size));
    Poke(g, host, first, size, step, 0);
    net::Rollback::UseInputs(g, step, {0}, row_of(step, 1).data());
    net::Rollback::KeepChecksum(g, step, 0xC0000000 + step);
  }
  live.SaveStep(g, L, true);

  // O's start: W the game's memory rewound to R, its log's moment there, keeping after-images
  w_host.ram = host.ram;
  w_host.scratch = host.scratch;
  w_data.Restore(data.Snapshot());
  live.Apply(gw, L, R);
  // the reference: one job from R on the corrected rows (O's below c, N's from c)
  r_host.ram = w_host.ram;
  r_host.scratch = w_host.scratch;
  r_data.Restore(w_data.Snapshot());
  std::vector<Copy> corrected;  // the reference's moment before each step from R
  for (u32 step = R; step < A2; ++step)
  {
    corrected.push_back(Take(gr, r_host, first, size));
    Poke(gr, r_host, first, size, step, step < C ? 1 : 2);
  }
  corrected.push_back(Take(gr, r_host, first, size));
  net::Rollback worker(false);
  worker.Start(gw);
  worker.KeepAfter(true);
  worker.Adopt(gw, live, R);
  CHECK(!worker.Forward(gn, R, R + 1, nullptr));  // (none kept yet)
  // W runs from R past c on O's rows (its first hook is R + 1's, as the worker's)
  constexpr u32 W_AT = C + 3;
  std::vector<Copy> w_at;
  for (u32 step = R; step < W_AT; ++step)
  {
    if (step != R)
      worker.SaveStep(gw, step, false);
    w_at.push_back(Take(gw, w_host, first, size));
    Poke(gw, w_host, first, size, step, 1);
    net::Rollback::UseInputs(gw, step, {0}, row_of(step, 9).data());
    net::Rollback::KeepChecksum(gw, step, 0xD0000000 + step);
  }
  worker.SaveStep(gw, W_AT, false);
  CHECK(same(w_at[C - R], corrected[C - R]));
  CHECK(!worker.Forward(gn, R, W_AT + 1, nullptr));  // (step W_AT not run yet)

  // N's base: the game's memory rewound to R by the game's log, then W's after-images of R to c - 1
  n_host.ram = host.ram;
  n_host.scratch = host.scratch;
  n_data.Restore(data.Snapshot());
  live.Apply(gn, L, R);
  const auto before_forward = n_host.ram;
  std::vector<u32> pages = {123};
  CHECK(worker.Forward(gn, R, C, &pages));
  CHECK(!pages.empty() && std::is_sorted(pages.begin(), pages.end()) &&
        std::adjacent_find(pages.begin(), pages.end()) == pages.end());
  {
    // every byte Forward changed is on a page it names; and the base is W's moment at c
    u32 unnamed = 0;
    for (u32 k = 0; k < size; ++k)
    {
      const std::size_t at = first - fzgx::layout::RAM + k;
      unnamed += n_host.ram[at] != before_forward[at] &&
                 !std::binary_search(pages.begin(), pages.end(), k & ~(net::PAGE - 1));
    }
    CHECK(unnamed == 0);
  }
  CHECK(same(Take(gn, n_host, first, size), w_at[C - R]));
  CHECK(gn.Read32(keep_part + 4) == g.Read32(keep_part + 4));  // (the kept part as N's memory had it)
  net::Rollback second(false);
  second.Start(gn);
  second.KeepAfter(true);
  second.Adopt(gn, live, C, &pages);
  // N runs c to A2 - 1 on the rows of now; W runs on to A1 on O's; the game predicted to A1
  for (u32 step = C; step < A2; ++step)
  {
    if (step != C)
      second.SaveStep(gn, step, false);
    Poke(gn, n_host, first, size, step, 2);
    net::Rollback::UseInputs(gn, step, {0}, row_of(step, 7).data());
    net::Rollback::KeepChecksum(gn, step, 0xE0000000 + step);
  }
  second.SaveStep(gn, A2, false);
  CHECK(same(Take(gn, n_host, first, size), corrected[A2 - R]));
  for (u32 step = W_AT; step < A1; ++step)
  {
    if (step != W_AT)
      worker.SaveStep(gw, step, false);
    Poke(gw, w_host, first, size, step, 1);
    net::Rollback::UseInputs(gw, step, {0}, row_of(step, 9).data());
    net::Rollback::KeepChecksum(gw, step, 0xD0000000 + step);
  }
  worker.SaveStep(gw, A1, false);
  const auto game_steps = [&](u32 from, u32 to) {
    for (u32 step = from; step < to; ++step)
    {
      if (step != from)
        live.SaveStep(g, step, false);
      Poke(g, host, first, size, step, 0);
      g.Write32(keep_page + 8, 0xAA000000 + step);
      g.Write32(keep_part + 4, 0xBB000000 + step);
      g.Write32(0x80003100, 0xCC000000 + step);
    }
    live.SaveStep(g, to, true);
  };
  game_steps(L, A1);
  // O adopted at A1 (its after-images go with its entries), the game predicted on to A2, N adopted there
  live.TakeOver(g, worker, gw, R, A1);
  CHECK(!worker.Forward(gr, R, C, nullptr));
  game_steps(A1, A2);
  live.TakeOver(g, second, gn, C, A2);
  CHECK(same(Take(g, host, first, size), corrected[A2 - R]));
  CHECK(g.Read32(keep_page + 8) == 0xAA000000 + A2 - 1 && g.Read32(keep_part + 4) == 0xBB000000 + A2 - 1);
  CHECK(g.Read32(0x80003100) == 0xCC000000 + A2 - 1);
  for (u32 step = R; step < A2; ++step)
  {
    u8 row[6];
    net::Rollback::UsedInputs(g, step, {0}, row);
    CHECK(row[1] == (step < C ? 9 : 7));
    CHECK(net::Rollback::KeptChecksum(g, step) == (step < C ? 0xD0000000 : 0xE0000000) + step);
  }
  CHECK(net::Rollback::KeptChecksum(g, R - 1) == 0xC0000000 + R - 1);
  // a restore to any step from R: the corrected moment; below R, the predicted one
  for (u32 step = A2; step-- > R;)
  {
    live.RestoreTo(g, step + 1, step);
    CHECK(same(Take(g, host, first, size), corrected[step - R]));
  }
  live.RestoreTo(g, R, R - 3);
  CHECK(same(Take(g, host, first, size), predicted[R - 3]));
  live.Stop();
  worker.Stop();
  second.Stop();

  // the after-images go with a restore, with Final, with KeepAfter(false) and with Stop; the game's log keeps none
  net::Rollback third(false);
  third.Start(gr);
  third.KeepAfter(true);
  for (u32 step = 0; step < 10; ++step)
  {
    third.SaveStep(gr, step, false);
    Poke(gr, r_host, first, size, step, 3);
  }
  third.SaveStep(gr, 10, false);
  CHECK(third.Forward(gn, 0, 10, nullptr));
  third.RestoreTo(gr, 10, 6);
  CHECK(third.Forward(gn, 0, 6, nullptr) && !third.Forward(gn, 0, 7, nullptr) && !third.Forward(gn, 6, 7, nullptr));
  for (u32 step = 6; step < 10; ++step)
  {
    if (step != 6)
      third.SaveStep(gr, step, false);
    Poke(gr, r_host, first, size, step, 4);
  }
  third.SaveStep(gr, 10, false);
  CHECK(third.Forward(gn, 0, 10, nullptr));
  third.Final(4);
  CHECK(!third.Forward(gn, 3, 10, nullptr) && third.Forward(gn, 4, 10, nullptr));
  third.KeepAfter(false);
  CHECK(!third.Forward(gn, 4, 10, nullptr));
  third.KeepAfter(true);
  third.SaveStep(gr, 11, false);
  CHECK(third.Forward(gn, 10, 11, nullptr));
  third.Stop();
  CHECK(!third.Forward(gn, 10, 11, nullptr));
  net::Rollback plain;
  plain.Start(g);
  plain.SaveStep(g, 0, false);
  g.Write32(first + 8, 1);
  plain.SaveStep(g, 1, false);
  CHECK(!plain.Forward(gn, 0, 1, nullptr));
  plain.Stop();
  CHECK(host.log.empty() && w_host.log.empty() && n_host.log.empty() && r_host.log.empty());
}

// A worker's start in two halves (study section 22): what Apply and Adopt read of the game's log taken at the hook
// (Rollback::Take), and the work on the worker's memory done from it later, while the game's log goes on (more steps
// saved, a page it watches first meanwhile). The worker's memory and its log's moment must be what Apply and Adopt
// at the hook make them: the same memory, the same pages watched, the same dirty pages, and the same moments after
// steps run on both from R (a restore back to R and to a step between).
void TestTheWorkersBaseTaken()
{
  BEGIN_TEST();
  TestHost host, a_host, b_host;
  ModData data, a_data, b_data;
  Guest g(host, data), ga(a_host, a_data), gb(b_host, b_data);
  std::mt19937 rng(20261105);
  FakeGame(host, g, rng);
  host.scratch.assign(0x4000, 0);
  for (u8& b : host.scratch)
    b = static_cast<u8>(rng());
  const u32 first = (MODULE + net::DATA) & ~(net::PAGE - 1), size = net::STATE_END - first;
  const u32 keep_part = first + 90 * net::PAGE + 0x100;
  g.Write32(net::UNTRACKED, 1);
  g.Write32(net::UNTRACKED + 4, keep_part);
  g.Write32(net::UNTRACKED + 8, keep_part + 0x40);
  net::Rollback live;
  live.Start(g);
  constexpr u32 R = 150, L = 160;
  for (u32 step = 0; step < L; ++step)
  {
    live.SaveStep(g, step, step % 3 == 0);
    Poke(g, host, first, size, step, 0);
  }
  live.SaveStep(g, L, true);
  // both workers' memories the game's at L, their logs started there (a slot's log runs on from its last job)
  a_host.ram = b_host.ram = host.ram;
  a_host.scratch = b_host.scratch = host.scratch;
  a_data.Restore(data.Snapshot());
  b_data.Restore(data.Snapshot());
  net::Rollback wa(false), wb(false);
  wa.Start(ga);
  wb.Start(gb);
  // A: at the hook, as before; B: taken at the hook, done after the game's log went on
  live.Apply(ga, L, R);
  wa.Adopt(ga, live, R);
  const auto taken = live.Take(L, R);
  const u32 game_moments = net::Rollback::Moments(host), b_moments = net::Rollback::Moments(b_host);
  const u32 fresh_page = first + (size / net::PAGE - 3) * net::PAGE;  // a page the game's log first watches after
  CHECK(live.WatchedPages() == wa.WatchedPages());
  for (u32 step = L; step < L + 4; ++step)
  {
    if (step != L)
      live.SaveStep(g, step, false);
    Poke(g, host, first, size, step, 0);
    g.Write32(fresh_page + 4 * step, 0xF0000000 + step);
  }
  live.SaveStep(g, L + 4, true);
  CHECK(live.WatchedPages() > wa.WatchedPages());  // (the game's log watches more now than at the Take)
  net::Rollback::Apply(g, *taken);  // (onto the log's own memory: refused, nothing written)
  CHECK(net::Rollback::Moments(host) == game_moments);
  net::Rollback::Apply(gb, *taken);
  CHECK(net::Rollback::Moments(b_host) == b_moments + 1);
  wb.Adopt(gb, *taken, R);
  CHECK(a_host.ram == b_host.ram && a_host.scratch == b_host.scratch);
  CHECK(wa.WatchedPages() == wb.WatchedPages() && wa.Moments() == wb.Moments());
  CHECK(wa.TakeDirty() == wb.TakeDirty());
  // the same steps on both from R: the same entries (a restore gives the same moments) and the same dirty pages
  for (u32 step = R; step < R + 8; ++step)
  {
    wa.SaveStep(ga, step, step % 3 == 0);
    wb.SaveStep(gb, step, step % 3 == 0);
    Poke(ga, a_host, first, size, step, 1);
    Poke(gb, b_host, first, size, step, 1);
  }
  wa.SaveStep(ga, R + 8, true);
  wb.SaveStep(gb, R + 8, true);
  CHECK(wa.TakeDirty() == wb.TakeDirty());
  wa.RestoreTo(ga, R + 8, R + 3);
  wb.RestoreTo(gb, R + 8, R + 3);
  CHECK(a_host.ram == b_host.ram && a_host.scratch == b_host.scratch);
  wa.RestoreTo(ga, R + 3, R);
  wb.RestoreTo(gb, R + 3, R);
  CHECK(a_host.ram == b_host.ram && a_host.scratch == b_host.scratch);
  live.Stop();
  wa.Stop();
  wb.Stop();
}

// A start's Take in a held frame (study section 22.5): the hold restores the game's log by one step (RestoreTo(L,
// L - 1)), which erases the entry of L - 1 that the Take holds. The base is made before that restore (BasesHere) and
// equals the one made at the hook; a Take whose entry was erased, or written again by the step run again, no longer
// stands (Begin then refuses its base); a restore above the Take's steps (the step run ahead) leaves it standing.
void TestTheWorkersBaseHeld()
{
  BEGIN_TEST();
  TestHost host, a_host, b_host;
  ModData data, a_data, b_data;
  Guest g(host, data), ga(a_host, a_data), gb(b_host, b_data);
  std::mt19937 rng(20261106);
  FakeGame(host, g, rng);
  const u32 first = (MODULE + net::DATA) & ~(net::PAGE - 1), size = net::STATE_END - first;
  net::Rollback live;
  live.Start(g);
  constexpr u32 R = 150, L = 160;
  for (u32 step = 0; step < L; ++step)
  {
    live.SaveStep(g, step, step % 3 == 0);
    Poke(g, host, first, size, step, 0);
  }
  live.SaveStep(g, L, true);
  a_host.ram = b_host.ram = host.ram;
  a_data.Restore(data.Snapshot());
  b_data.Restore(data.Snapshot());
  net::Rollback wa(false), wb(false);
  wa.Start(ga);
  wb.Start(gb);
  live.Apply(ga, L, R);  // A: at the hook, as before section 22
  wa.Adopt(ga, live, R);
  const auto taken = live.Take(L, R);
  CHECK(taken->ids.size() == L - R && taken->ids.front().first == L - 1);
  CHECK(net::Rollback::Stands(*taken));
  // B: the held frame's order: the base made, then the restore
  net::Rollback::Apply(gb, *taken);
  wb.Adopt(gb, *taken, R);
  live.RestoreTo(g, L, L - 1);
  CHECK(!net::Rollback::Stands(*taken));  // (the entry of L - 1 is gone: a base made after this would read it)
  CHECK(a_host.ram == b_host.ram);
  CHECK(wa.WatchedPages() == wb.WatchedPages() && wa.Moments() == wb.Moments() && wa.TakeDirty() == wb.TakeDirty());
  // the step run again: its entry written anew (perhaps in the same node), and the Take still does not stand
  Poke(g, host, first, size, L - 1, 0);
  live.SaveStep(g, L, true);
  CHECK(!net::Rollback::Stands(*taken));
  // a Take at the next hook, then the step run ahead and put back (above the Take's steps): it stands
  const auto next = live.Take(L, R);
  CHECK(net::Rollback::Stands(*next));
  Poke(g, host, first, size, L, 0);
  live.SaveStep(g, L + 1, true);
  live.RestoreTo(g, L + 1, L);
  CHECK(net::Rollback::Stands(*next));
  live.Stop();
  wa.Stop();
  wb.Stop();
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

// The worker's mode (every player's race since the old path's retirement, study section 21): the launch file's test
// key when it names one, else on from 4 hardware threads and sync ("off") below; no variable turns it off. The start's
// line says which and why.
void TestTheWorkersDefault()
{
  using fzgx::rollback_worker::ModeFor;
  using fzgx::rollback_worker::ModeSaid;
  CHECK(ModeFor("", 16) == "on");
  CHECK(ModeFor("", 4) == "on");
  CHECK(ModeFor("", 3) == "off");
  CHECK(ModeFor("", 0) == "off");  // (hardware_concurrency unknown)
  CHECK(ModeFor("off", 16) == "off" && ModeFor("sync", 16) == "sync");  // (the tests' reference)
  CHECK(ModeFor("on", 2) == "on");
  CHECK(ModeFor("async-check", 16) == "async-check");
  CHECK(ModeSaid("", 16).starts_with("fzgx: rollback worker: on (16 hardware threads)"));
  CHECK(ModeSaid("", 2) == "fzgx: rollback worker: off: this PC has 2 hardware threads and the worker needs 4; this "
                           "game's rollbacks run sync on the game's thread");
  CHECK(ModeSaid("sync", 16).find("\"sync\" mode, the launch file's test key") != std::string::npos);
}

// The words an adoption keeps come from one table (rollback_worker_kept.cpp), held here to the list of the study's
// section 18 (research/study-rollback-second-core.md, 18.2 and 18.4): every row it says to keep, by its guest address
// as the section gives it (main.rel at its pinned place), with its writer and its class; none of the words it says
// must not be kept (a race step writes them); each inside the log's region; the class (a) row behind the jobs' bar.
void TestTheKeptWordsTable()
{
  BEGIN_TEST();
  namespace rw = fzgx::rollback_worker;
  using rw::Where;
  using rw::Writer;
  struct Want
  {
    fzgx::u32 at, size;
    Writer writer;
    char sweep;
  };
  // section 18.2's rows kept (VI_WORDS before it), and 18.4's "to add"
  const Want want[] = {
      {0x803E3CE0, 4, Writer::INTERRUPT, 'b'},     // .bss 0x6F640 VI count
      {0x8032A7C4, 4, Writer::INTERRUPT, 'c'},     // .data 0x2B64 PAD_READY
      {0x8037540C, 4, Writer::INTERRUPT, 'c'},     // .bss 0xD6C the VI callback's retrace count
      {0x80375020, 0x18, Writer::INTERRUPT, 'b'},  // .bss 0x980 to 0x997 (VI post, stall)
      {0x803EF3E8, 0xC, Writer::INTERRUPT, 'c'},   // .bss 0x7AD48 to 0x7AD53 (reset check)
      {0x803ACB68, 8, Writer::CALLBACK, 'c'},      // .bss 0x384C8, 0x384CC (ARQ load flags)
      {0x803B2384, 0x40, Writer::CALLBACK, 'c'},   // .bss 0x3DCE4 to 0x3DD23 (the ring's busy words)
      {0x803C00F8, 8, Writer::CALLBACK, 'c'},      // .bss 0x4BA58, 0x4BA5C
      {0x803F339C, 4, Writer::CALLBACK, 'c'},      // .bss 0x7ECFC
      {0x803F9B68, 4, Writer::CALLBACK, 'a'},      // .bss 0x854C8 the countdown movie's state (a bar keeps it off W)
      {0x8037541C, 40, Writer::TIME_BASE, 't'},    // .bss 0xD7C, ten timings' starts (18.3)
      {0x8035D5FC, 4, Writer::FRAME, 'f'},         // .data 0x3599C (`filters`)
  };
  const auto table = rw::KeptTable();
  for (const Want& x : want)
  {
    const auto row = std::find_if(table.begin(), table.end(), [&](const rw::KeptRow& r) {
      return r.where == Where::FIXED && r.at == x.at;
    });
    CHECK(row != table.end());
    if (row == table.end())
      continue;
    CHECK(row->size == x.size && row->writer == x.writer && row->sweep == x.sweep);
  }
  std::size_t fixed = 0, pointer = 0, list = 0;
  for (const rw::KeptRow& r : table)
  {
    fixed += r.where == Where::FIXED;
    pointer += r.where == Where::POINTER;
    list += r.where == Where::LIST;
    CHECK(r.size % 4 == 0 && r.by != nullptr && *r.by != 0);
    if (r.where == Where::FIXED)
      CHECK(r.at % 4 == 0 && r.at >= 0x80327000 && r.at + r.size <= fzgx::net::STATE_END);
    // a class (a) row is kept only behind the bar (18.4 item 4): no job starts while its reader runs (steps to 421)
    if (r.sweep == 'a')
      CHECK(rw::KEPT_BAR >= 422 && fzgx::TheOptions().debug.worker_from >= rw::KEPT_BAR);
  }
  CHECK(fixed == std::size(want) && pointer == 2 && list == 1);
  // the frame routine's two words of the simulation manager, [.bss 0xDAC] + 0x10 and + 0x14 (18.4 item 1 (ii)), and
  // the task nodes' + 0x1C (item 1 (iii))
  CHECK(std::count_if(table.begin(), table.end(), [](const rw::KeptRow& r) {
          return r.where == Where::POINTER && r.at == 0x8037544C && (r.size == 0x10 || r.size == 0x14);
        }) == 2);
  CHECK(std::any_of(table.begin(), table.end(), [](const rw::KeptRow& r) {
    return r.where == Where::LIST && r.size == 0x1C && r.writer == Writer::TIME_BASE;
  }));
  // 18.4 item 2: words a race step writes are never kept (.bss 0xDA8, 0xDAC, .data 0x2CDC to 0x2CE8, .data 0x2C70,
  // .bss 0x772C8)
  for (const fzgx::u32 never : {0x80375448u, 0x8037544Cu, 0x8032A93Cu, 0x8032A940u, 0x8032A944u, 0x8032A948u,
                                0x8032A8D0u, 0x803EB968u})
    for (const rw::KeptRow& r : table)
      CHECK(r.where != Where::FIXED || never < r.at || never >= r.at + r.size);
  // resolved over a memory with no managers: the fixed rows' words only, each once, sorted
  TestHost host;
  fzgx::ModData data;
  Guest g(host, data);
  const std::vector<fzgx::u32> words = rw::KeptTableWords(g, g);
  std::size_t fixed_words = 0;
  for (const rw::KeptRow& r : table)
    fixed_words += r.where == Where::FIXED ? r.size / 4 : 0;
  CHECK(words.size() == fixed_words && std::is_sorted(words.begin(), words.end()));
  CHECK(std::binary_search(words.begin(), words.end(), 0x803B2384u + 0x3C) &&
        std::binary_search(words.begin(), words.end(), 0x803EF3E8u + 8));
  // a simulation manager in memory: its +0x10 and +0x14 join; the time words alone are the time rows'
  g.Write32(0x8037544C, 0x80400000);
  const std::vector<fzgx::u32> more = rw::KeptTableWords(g, g);
  CHECK(more.size() == fixed_words + 2 && std::binary_search(more.begin(), more.end(), 0x80400014u));
  CHECK(rw::TimeWords(g, g).size() == 10);
}

// The thread audit's guards (study section 17): "on W" is the worker's thread's alone; a job's start is barred by a
// module whose pipe is one for every thread (U2), an instrumented module (U6) and an active interception (U7).
void TestTheWorkersGuards()
{
  BEGIN_TEST();
  namespace rw = fzgx::rollback_worker;
  CHECK(!rw::OnW());
  rw::OffLimits("a test's entry");  // (not on W: nothing)
  bool on_w = false;
  std::thread([&] {
    rw::ThreadStarts(0);
    on_w = rw::OnW();
  }).join();
  CHECK(on_w && !rw::OnW());
  namespace guard = rw::guard;
  CHECK(guard::StartBar(true) == nullptr);
  CHECK(guard::StartBar(false) != nullptr);
  rw::SetInstrumented("a profile-training module");
  CHECK(guard::StartBar(true) != nullptr);
  rw::SetInstrumented(nullptr);
  static bool active = true;
  rw::SetInterception([] { return active; });
  CHECK(guard::StartBar(true) != nullptr);
  active = false;
  CHECK(guard::StartBar(true) == nullptr);
  rw::SetInterception(nullptr);
  // U1: the generation taken at a job's start; a move by its adoption is seen
  static fzgx::u64 generation = 7;
  rw::SetCodeGeneration([] { return generation; });
  guard::TakeGeneration();
  CHECK(!guard::Moved());
  ++generation;
  CHECK(guard::Moved());
  rw::SetCodeGeneration(nullptr);
  CHECK(!guard::Moved());
}

// The slow worker's switch and the trial of W's estimate (study section 19): one spike that sets W's estimate near a
// frame bars every start, W never runs and its estimate never moves. The barred windows strike on the estimate, the
// race is switched (and logged), and the retry gives W a fresh estimate: a good PC's worker runs again and stays on.
// A PC whose worker really is slow is switched again; with "slowStepUs" 0 the estimate is reseeded after RETRY_S.
void TestTheWorkersPace()
{
  BEGIN_TEST();
  namespace pace = fzgx::rollback_worker::pace;
  using pace::Verdict;
  std::vector<float> window;
  pace::Now now;
  now.limit_us = 6000;
  now.starts = 1;
  // a frame of a good PC: W runs 4 steps of 3 ms (a frame a step and some)
  const auto run = [&](float step_ms) {
    for (int k = 0; k < 4; ++k)
      window.push_back(step_ms);
  };
  pace::Pace p;
  CHECK(p.Watch(now, window) == Verdict::NONE);  // (the first window starts)
  now.estimate_ms = 3;
  double t = 0;
  for (; t < 5; t += 1.0 / 60)
  {
    now.s = t;
    run(3);
    CHECK(p.Watch(now, window) == Verdict::NONE);
  }
  CHECK(p.switches == 0 && p.barred_windows == 0 && p.rate_high == 3);
  // the spike: the estimate at 16.5 ms, every start barred late from now on, W runs no step; before the fix no
  // window ever ended and nothing was logged
  now.estimate_ms = 16.5;
  Verdict seen = Verdict::NONE;
  double switched_at = -1;
  for (; t < 9 && seen == Verdict::NONE; t += 1.0 / 60)
  {
    now.s = t;
    ++now.barred_late;
    ++now.behind;
    seen = p.Watch(now, window);
    if (seen == Verdict::SWITCH)
      switched_at = t;
  }
  CHECK(seen == Verdict::SWITCH && p.switched && p.switches == 1 && p.step_n == 0 && p.step_ms == 16.5);
  // (the first barred window ends within a window of the spike, the switch STRIKES - 1 windows after it)
  CHECK(switched_at >= 5 + (pace::STRIKES - 1) * pace::WINDOW_S && switched_at < 5 + pace::STRIKES * pace::WINDOW_S + 0.1);
  CHECK(p.barred_windows == pace::STRIKES);
  // switched: every start barred while switched (not late ones), nothing until RETRY_S
  for (; t < switched_at + pace::RETRY_S - 0.05; t += 1.0 / 60)
  {
    now.s = t;
    CHECK(p.Watch(now, window) == Verdict::NONE);
  }
  for (; t < switched_at + pace::RETRY_S + 0.05 && seen != Verdict::RETRY; t += 1.0 / 60)
  {
    now.s = t;
    seen = p.Watch(now, window);
  }
  CHECK(seen == Verdict::RETRY && !p.switched && p.retries == 1 && !p.new_race);
  // the retry: the caller gives W a fresh estimate (the game's own rerun step); a good PC's worker runs at 3 ms again
  // and stays on for the rest of the race
  now.estimate_ms = 3;
  const double retried_at = t;
  for (; t < retried_at + 30; t += 1.0 / 60)
  {
    now.s = t;
    run(3);
    CHECK(p.Watch(now, window) == Verdict::NONE);
  }
  CHECK(p.switches == 1 && !p.switched && window.size() < 4 * 60 + 8);
  // a worker that really is slow (8 ms a step, its starts still barred late now and then): switched again, logged
  for (seen = Verdict::NONE; t < retried_at + 40 && seen == Verdict::NONE; t += 1.0 / 60)
  {
    now.s = t;
    run(8);
    ++now.behind;
    seen = p.Watch(now, window);
  }
  CHECK(seen == Verdict::SWITCH && p.switches == 2 && p.step_n >= pace::WINDOW && p.step_ms == 8);
  // a new race ends the switch at once
  now.starts = 2;
  t += 1.0 / 60;
  now.s = t;
  CHECK(p.Watch(now, window) == Verdict::RETRY && p.new_race && p.retries == 2);
  // a brief load on a good PC (a window's median slow, no catch-ups) does not switch
  for (fzgx::u32 k = 0; k < 2 * pace::STRIKES; ++k)
  {
    t += pace::WINDOW_S + 0.01;
    now.s = t;
    for (fzgx::u32 n = 0; n < pace::WINDOW; ++n)
      window.push_back(9);
    CHECK(p.Watch(now, window) == Verdict::NONE);
  }
  CHECK(p.switches == 2);
  // "slowStepUs" 0 (no switch): a worker barred by its estimate gets a fresh one after RETRY_S, not before
  pace::Pace q;
  window.clear();
  now = pace::Now{};
  CHECK(q.Watch(now, window) == Verdict::NONE);
  now.estimate_ms = 16.5;
  for (t = 0, seen = Verdict::NONE; t < 20 && seen == Verdict::NONE; t += 1.0 / 60)
  {
    now.s = t;
    ++now.barred_late;
    seen = q.Watch(now, window);
  }
  CHECK(seen == Verdict::RESEED && q.reseeds == 1 && q.switches == 0 && t >= pace::RETRY_S && t < pace::RETRY_S + 1.1);
  // and its windows are emptied as they end (no switch kept them growing before)
  for (fzgx::u32 n = 0; n < 2 * pace::WINDOW; ++n)
    window.push_back(3);
  t += pace::WINDOW_S + 0.01;
  now.s = t;
  CHECK(q.Watch(now, window) == Verdict::NONE && window.empty());
}

// The second worker's estimate (study section 21): it moves only while slot 1 runs, so one slow step of N (about
// 110 ms, the PC busy for a moment) left it above a frame's room, every later N start foreseen at FORESEEN_NEVER
// frames and barred, and W's own good windows never reseeded it: W2 off for the session. A full window in which slot 1
// ran no step now reseeds it from that window's median (WatchRate), and N's starts are allowed again.
void TestTheSecondWorkersEstimate()
{
  BEGIN_TEST();
  namespace pace = fzgx::rollback_worker::pace;
  using pace::Verdict;
  constexpr fzgx::u32 CATCH_UP = 60, DEPTH = 6;  // ("catchUp" by default; a race of humans' depth)
  const auto allowed = [&](double r_ms) { return DEPTH + pace::Foreseen(DEPTH, r_ms) <= CATCH_UP; };
  std::vector<float> window;
  pace::Now now;
  now.limit_us = 6000;
  now.starts = 1;
  now.estimate_ms = 3;
  pace::Pace p;
  CHECK(p.Watch(now, window) == Verdict::NONE);
  // slot 1 runs a few good steps, then one slow one
  double r1 = 0;
  for (int k = 0; k < 20; ++k)
    r1 = pace::Smoothed(r1, 3);
  CHECK(allowed(r1));
  r1 = pace::Smoothed(r1, 113);
  CHECK(r1 > FIELD_MS - 0.5 && pace::Foreseen(DEPTH, r1) == pace::FORESEEN_NEVER && !allowed(r1));
  now.second_steps = 21;  // (its steps, taken into the window that ends below)
  // W runs 4 steps of 3 ms a frame, slot 1 none (its starts barred): the first window still holds slot 1's steps
  double t = 0;
  int idle_windows = 0;
  bool ended_with_second = false;
  for (int frame = 0; frame < 4 * 60 && idle_windows == 0; ++frame)
  {
    t += 1.0 / 60;
    now.s = t;
    for (int k = 0; k < 4; ++k)
      window.push_back(3);
    const std::size_t before = window.size();
    CHECK(p.Watch(now, window) == Verdict::NONE);  // (no RESEED nor RETRY: W's windows are good)
    if (window.empty() && before >= pace::WINDOW)
    {
      if (p.second_idle)
      {
        ++idle_windows;
        if (r1 > p.step_ms)
          r1 = p.step_ms;  // (WatchRate's reseed)
      }
      else
        ended_with_second = true;
    }
  }
  CHECK(ended_with_second && idle_windows == 1 && r1 == 3 && allowed(r1));
  CHECK(p.switches == 0 && p.reseeds == 0 && p.retries == 0);
  // a window in which slot 1 ran a step is not idle: its estimate stays its own
  t += pace::WINDOW_S + 0.01;
  now.s = t;
  ++now.second_steps;
  window.assign(pace::WINDOW, 3.0f);
  CHECK(p.Watch(now, window) == Verdict::NONE && window.empty() && !p.second_idle);
  // and the flag holds for the Watch that ended the window only
  t += 1.0 / 60;
  now.s = t;
  window.assign(pace::WINDOW, 3.0f);
  CHECK(p.Watch(now, window) == Verdict::NONE && !p.second_idle);
  // the run-retire spike (c03, "always"): W's own starts barred short of a full window too (a chain's next job taken
  // by slot 1 while W's thread leaves its last, foreseen at FORESEEN_NEVER), so windows end barred, on W's estimate;
  // slot 1 idle in one is reseeded all the same, from W's estimate
  pace::Pace q;
  window.clear();
  now = pace::Now{};
  now.limit_us = 6000;
  now.starts = 1;
  now.estimate_ms = 2.5;  // (W's: the open job's slot is 0)
  CHECK(q.Watch(now, window) == Verdict::NONE);
  r1 = pace::Smoothed(2.5, 152.5);
  CHECK(!allowed(r1));
  bool idle = false;
  for (t = 0; t < 3 && !idle; t += 1.0 / 60)
  {
    now.s = t;
    window.push_back(2.5f);  // (a step a frame: never a full window)
    ++now.barred_late;
    ++now.behind;
    CHECK(q.Watch(now, window) == Verdict::NONE);
    idle = q.second_idle;
  }
  CHECK(idle && q.step_n == 0 && q.step_ms == 2.5 && q.switches == 0);
  r1 = std::min(r1, now.estimate_ms);  // (WatchRate: a barred window's reseed takes W's estimate)
  CHECK(allowed(r1));
}

// The two workers' rules (study section 20.2 to 20.4, rollback_worker_pair.cpp): how many workers a PC runs; when N
// starts from O's log; the order of the adoptions (O first, N only once O is closed); the cascade (O's end without an
// adoption stops N; N's end with O open sends its row back to the chain, with O adopted it runs sync alone); O clean as
// far as N carries it; a step owed returned only when no job is left open.
void TestTheWorkersPair()
{
  BEGIN_TEST();
  namespace pair = fzgx::rollback_worker::pair;
  using pair::Fate;
  using pair::NONE;
  // the workers: the test key's, else 2 from 6 usable threads (an affinity of 4: 1)
  CHECK(pair::WorkersFor(0, 16) == 2 && pair::WorkersFor(0, 6) == 2);
  CHECK(pair::WorkersFor(0, 5) == 1 && pair::WorkersFor(0, 4) == 1 && pair::WorkersFor(0, 0) == 1);
  CHECK(pair::WorkersFor(1, 16) == 1 && pair::WorkersFor(2, 2) == 2);
  CHECK(pair::WorkersFor(3, 2) == 1 && pair::WorkersFor(3, 16) == 2);  // (another value is not taken)

  // N's start: one job O open and begun, its late row c with R < c, O's worker past c, the slot idle, the pace good
  pair::SecondView v;
  v.workers = 2;
  v.older_open = true;
  v.slot_idle = true;
  v.after_images = true;
  v.from = 500;
  v.late = 505;
  v.at = 509;
  v.taken = 510;
  v.window_ms = 2.5;
  v.slow_us = 6000;
  CHECK(pair::SecondBar(v) == nullptr);
  const auto barred = [&](auto change) {
    pair::SecondView x = v;
    change(x);
    return pair::SecondBar(x) != nullptr;
  };
  CHECK(barred([](pair::SecondView& x) { x.workers = 1; }));            // fewer than 6 usable threads
  CHECK(barred([](pair::SecondView& x) { x.older_open = false; }));     // no job, or it ended
  CHECK(barred([](pair::SecondView& x) { x.newer_open = true; }));      // never three timelines at once
  CHECK(barred([](pair::SecondView& x) { x.late = NONE; }));            // no late row
  CHECK(barred([](pair::SecondView& x) { x.late = 500; }));             // c must be past R
  CHECK(barred([](pair::SecondView& x) { x.late = 510; }));             // O's worker has not saved step c
  CHECK(barred([](pair::SecondView& x) { x.taken = 505; }));
  CHECK(!barred([](pair::SecondView& x) { x.late = 509; }));            // (c at the step O's worker stands at)
  CHECK(barred([](pair::SecondView& x) { x.older_below = true; }));     // O's own base is stale
  CHECK(barred([](pair::SecondView& x) { x.after_images = false; }));
  CHECK(barred([](pair::SecondView& x) { x.slot_idle = false; }));      // never a wait for a running worker
  CHECK(barred([](pair::SecondView& x) { x.window_ms = 6.0; }));        // the pace: W2 backs off first
  CHECK(!barred([](pair::SecondView& x) { x.window_ms = 5.9; }));
  CHECK(!barred([](pair::SecondView& x) { x.window_ms = 9; x.slow_us = 0; }));  // ("slowStepUs" 0: no pace bar)
  CHECK(barred([](pair::SecondView& x) { x.window_barred = true; }));   // W's estimate barred its starts
  CHECK(barred([](pair::SecondView& x) { x.window_behind = 1; }));      // a catch-up in the last window

  // the order and the cascade
  const pair::JobView none{}, runs{true, false, false}, ends{true, true, false}, ready{true, false, true};
  const auto order = [](pair::JobView o, pair::JobView n, Fate fo, Fate fn) {
    const pair::Plan p = pair::Order(o, n);
    return p.older == fo && p.newer == fn;
  };
  CHECK(order(none, none, Fate::NONE, Fate::NONE));
  CHECK(order(runs, none, Fate::RUNS, Fate::NONE));
  CHECK(order(ready, none, Fate::ADOPT, Fate::NONE));
  CHECK(order(ends, none, Fate::FALL, Fate::NONE));
  // O first: N parked first waits; N adopted in O's hook only after O's
  CHECK(order(runs, ready, Fate::RUNS, Fate::RUNS));
  CHECK(order(ready, ready, Fate::ADOPT, Fate::ADOPT));
  CHECK(order(ready, runs, Fate::ADOPT, Fate::RUNS));
  // O's end without an adoption stops N (its base is O's), however N stands
  CHECK(order(ends, runs, Fate::FALL, Fate::STOP));
  CHECK(order(ends, ready, Fate::FALL, Fate::STOP));
  CHECK(order(ends, ends, Fate::FALL, Fate::STOP));
  // N's end: with O open its row goes back to the chain; with O adopted it is the single job's sync from its R
  CHECK(order(runs, ends, Fate::RUNS, Fate::BACK));
  CHECK(order(ready, ends, Fate::ADOPT, Fate::FALL));

  // O clean as far as N carries it: its stale rows all at or above c, nothing below its R
  CHECK(pair::CleanThroughNewer(NONE, 505, false));
  CHECK(pair::CleanThroughNewer(505, 505, false) && pair::CleanThroughNewer(507, 505, false));
  CHECK(!pair::CleanThroughNewer(504, 505, false));
  CHECK(!pair::CleanThroughNewer(507, 505, true));

  // owed: returned only when no job is left open after the frame's adoptions
  CHECK(pair::OwedNow(503, false) == 503);
  CHECK(pair::OwedNow(503, true) == NONE);
  CHECK(pair::OwedNow(NONE, false) == NONE);

  // the pace's catch-ups in the last window that ended (W2's bar): one in it, then none in the next
  namespace pace = fzgx::rollback_worker::pace;
  pace::Pace p;
  std::vector<float> window;
  pace::Now now;
  now.limit_us = 6000;
  CHECK(p.Watch(now, window) == pace::Verdict::NONE);
  now.s = pace::WINDOW_S + 0.01;
  now.behind = 1;
  window.assign(pace::WINDOW, 3.0f);
  CHECK(p.Watch(now, window) == pace::Verdict::NONE && p.last_behind == 1 && p.step_ms == 3);
  now.s += pace::WINDOW_S + 0.01;
  window.assign(pace::WINDOW, 3.0f);
  CHECK(p.Watch(now, window) == pace::Verdict::NONE && p.last_behind == 0);
}
