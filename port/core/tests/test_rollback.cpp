// Tests of the rollback: the undo log, the locked cache in it, the interface a session uses, the checksum.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "harness.h"
#include "undo_log.h"

void TestUndoLog()
{
  TestHost host;
  ModData data;
  Guest g(host, data);
  std::mt19937 rng(20261005);
  FakeGame(host, g, rng);
  const u32 first = (MODULE + net::DATA) & ~(net::PAGE - 1);
  // the mod lists one whole page and one part of a page as never rolled back
  const u32 keep_page = first + 40 * net::PAGE, keep_part = first + 90 * net::PAGE + 0x100;
  g.Write32(net::UNTRACKED, 2);
  g.Write32(net::UNTRACKED + 4, keep_page);
  g.Write32(net::UNTRACKED + 8, keep_page + net::PAGE);
  g.Write32(net::UNTRACKED + 12, keep_part);
  g.Write32(net::UNTRACKED + 16, keep_part + 0x40);
  for (u32 a = layout::INPUTS; a < layout::INPUTS + 0x400; a += 4)
    g.Write32(a, rng());

  net::Rollback t;
  CHECK(!t.Active() && t.TrackedBytes() == 0);
  t.Start(g);
  CHECK(t.Active() && t.TrackedBytes() == net::STATE_END - first);

  // 200 steps (past the 120 learning steps, so late pages are met too); each writes a few words across the tracked
  // memory, the generator word, the two kept ranges, and memory outside the tracked range
  std::vector<std::vector<u8>> before;  // the tracked memory before each step
  std::vector<u32> rng_before;
  const u32 size = net::STATE_END - first;
  const auto poke = [&](u32 step) {
    for (int k = 0; k < 40; ++k)
    {
      // a few hot pages every step, and now and then a page never touched before
      const u32 page = k < 30 ? (rng() % 64) : (rng() % (size / net::PAGE));
      g.Write32(first + page * net::PAGE + (rng() % (net::PAGE / 4)) * 4, rng());
    }
    g.Write32(net::DOL_RNG, 0x1000 + step);
    g.Write32(keep_page + 8, 0xAA000000 + step);
    g.Write32(keep_part + 4, 0xBB000000 + step);
    g.Write32(0x80003100, 0xCC000000 + step);    // the DOL: outside the undo log
    g.Write32(net::STATE_END + 16, 0xDD000000 + step);  // the display buffers: outside too
  };
  for (u32 step = 0; step < 200; ++step)
  {
    t.SaveStep(g, step, step % 3 == 0);
    before.emplace_back(g.Ram(first, size), g.Ram(first, size) + size);
    rng_before.push_back(g.Read32(net::DOL_RNG));
    poke(step);
  }
  t.SaveStep(g, 200, true);
  g.Write32(net::STEP, 150);  // the one word of the window a restore writes: put aside here, pinned further down
  const std::vector<u8> window = data.Snapshot();
  CHECK(t.TakeWork().blocks > 0);

  // back to before step 150
  t.RestoreTo(g, 200, 150);
  std::vector<u8> expect = before[150];
  // the kept ranges stay as they are now, through the restore
  const u32 page_off = keep_page - first, part_off = keep_part - first;
  std::memcpy(expect.data() + page_off, g.Ram(keep_page, net::PAGE), net::PAGE);
  std::memcpy(expect.data() + part_off, g.Ram(keep_part, 0x40), 0x40);
  CHECK(std::memcmp(g.Ram(first, size), expect.data(), size) == 0);
  CHECK(g.Read32(net::DOL_RNG) == rng_before[150]);
  CHECK(g.Read32(keep_page + 8) == 0xAA000000 + 199 && g.Read32(keep_part + 4) == 0xBB000000 + 199);
  CHECK(g.Read32(0x80003100) == 0xCC000000 + 199 && g.Read32(net::STATE_END + 16) == 0xDD000000 + 199);
  // the mod's data window is no part of the undo log: a rollback leaves every byte of it alone
  CHECK(data.Snapshot() == window);

  // re-simulating from there and rolling back again, deeper, is as exact
  for (u32 step = 150; step < 180; ++step)
  {
    t.SaveStep(g, step, false);
    poke(step);
  }
  t.SaveStep(g, 180, true);
  // steps below 150 are final: their entries go, and a restore to 150 is as exact
  t.Final(150);
  t.RestoreTo(g, 180, 150);
  std::memcpy(expect.data() + page_off, g.Ram(keep_page, net::PAGE), net::PAGE);
  std::memcpy(expect.data() + part_off, g.Ram(keep_part, 0x40), 0x40);
  CHECK(std::memcmp(g.Ram(first, size), expect.data(), size) == 0);
  CHECK(data.Snapshot() == window);
  const net::Work work = t.TakeWork();
  CHECK(work.restore_us > 0 && work.track_us > 0 && work.watched_pages > 0);
  CHECK(t.TakeWork().track_us == 0);
  t.Stop();
  CHECK(!t.Active() && t.TrackedBytes() == 0);
  CHECK(host.log.empty());
}

// A write into the game's memory by another thread than the game's. The emulator's picture thread clears
// the destination of a frame-buffer copy when it comes to the copy, milliseconds after the game asked for it. The
// game asked while its memory stood at the present, where the destination is a buffer; the clear arrives after a
// restore has put the memory back to a step before the buffer was allocated, where the same bytes hold the heap's
// cell headers. The stand-in below clears as the adapter's hook lets the emulator: only where no rollback puts
// memory back (fzgx::UnderRollback). Without that answer the last comparison fails: the memory the steps are run
// again from is not the memory they first ran from.
namespace
{
// what the log puts back, and what it leaves: the listed ranges, memory below it (the boost's other copy goes to
// 0x80095EC0, in the executable's memory) and the display buffers above it
void WhatTheLogPutsBack(u32 first, u32 heap, u32 portraits, u32 portraits_end, u32 words)
{
  CHECK(fzgx::UnderRollback(heap, 0x280) && fzgx::UnderRollback(first, 1) && fzgx::UnderRollback(net::STATE_END - 1, 1));
  CHECK(!fzgx::UnderRollback(portraits + 0x320, 0x2000) && !fzgx::UnderRollback(portraits, portraits_end - portraits));
  CHECK(fzgx::UnderRollback(portraits - 1, 2) && fzgx::UnderRollback(portraits_end - 1, 2));
  CHECK(!fzgx::UnderRollback(words, 12) && fzgx::UnderRollback(words, 13) && fzgx::UnderRollback(words - 4, 8));
  CHECK(!fzgx::UnderRollback(layout::RAM + 0x95EC0, 0x23000) && !fzgx::UnderRollback(net::STATE_END, 0x100000));
  // the executable's generator word is put back too (Restore): the one word below the log
  CHECK(fzgx::UnderRollback(net::DOL_RNG, 4) && fzgx::UnderRollback(net::DOL_RNG + 3, 8) &&
        !fzgx::UnderRollback(net::DOL_RNG + 4, 0x100) && !fzgx::UnderRollback(net::DOL_RNG - 0x100, 0x100));
  CHECK(fzgx::UnderRollback(first - 0x100, 0x101) && !fzgx::UnderRollback(first - 0x100, 0x100));
  CHECK(fzgx::UnderRollback(layout::RAM, layout::RETAIL_RAM_SIZE) && !fzgx::UnderRollback(heap, 0));
  CHECK(!fzgx::UnderRollback(0, 0x280) && !fzgx::UnderRollback(0xFFFFFF00, 0x200));  // (no sum wraps into the log)
}
}  // namespace

void TestAnotherThreadsWriteUnderRollback()
{
  TestHost host;
  ModData data;
  Guest g(host, data);
  std::mt19937 rng(20261013);
  FakeGame(host, g, rng);
  const u32 first = (MODULE + net::DATA) & ~(net::PAGE - 1);
  const u32 size = net::STATE_END - first;
  // as in a race: one range of whole pages never rolled back (the face board's heap, where its portraits are
  // copied to) with a part of a page before it, and a few words elsewhere
  const u32 portraits = first + 300 * net::PAGE - 0x440, portraits_end = first + 364 * net::PAGE - 0x440;
  const u32 words = first + 90 * net::PAGE + 0x100;
  g.Write32(net::UNTRACKED, 2);
  g.Write32(net::UNTRACKED + 4, portraits);
  g.Write32(net::UNTRACKED + 8, portraits_end);
  g.Write32(net::UNTRACKED + 12, words);
  g.Write32(net::UNTRACKED + 16, words + 12);
  const u32 heap = first + 0x1135000;  // cells of the main heap, as the boost's buffer lies among them
  int cleared = 0;
  const auto picture_thread_clears = [&](u32 address, u32 bytes) {
    if (fzgx::UnderRollback(address, bytes))
      return;
    std::memset(g.Ram(address, bytes), 0, bytes);
    ++cleared;
  };

  // no undo log: every write is the other thread's own business (offline play, the menus)
  CHECK(!fzgx::UnderRollback(heap, 0x280) && !fzgx::UnderRollback(layout::RAM, layout::RETAIL_RAM_SIZE));
  net::Rollback t;
  t.Start(g);
  WhatTheLogPutsBack(first, heap, portraits, portraits_end, words);

  // Ten steps. Until step 6 three small cells stand at the place (their headers: previous, next, size); step 6
  // frees them and allocates the boost's buffer over them, and from then on the picture is copied into it.
  std::vector<std::vector<u8>> before;
  const auto step = [&](u32 n) {
    g.Write32(first + 0x200, n);  // the simulation's own state
    for (u32 cell = 0; n < 6 && cell < 3; ++cell)
    {
      g.Write32(heap + 0x20 + cell * 0xC0, cell == 0 ? 0 : heap + 0x20 + (cell - 1) * 0xC0);
      g.Write32(heap + 0x24 + cell * 0xC0, cell == 2 ? 0 : heap + 0x20 + (cell + 1) * 0xC0);
      g.Write32(heap + 0x28 + cell * 0xC0, 0xC0);
      g.Write32(heap + 0x40 + cell * 0xC0, 0x1000 * n + cell);  // what the cell holds
    }
    if (n == 6)
    {
      g.Write32(heap, 0);
      g.Write32(heap + 4, 0);
      g.Write32(heap + 8, 0x2A0);  // one cell: the buffer of 0x280 bytes behind its header
    }
  };
  for (u32 n = 0; n < 10; ++n)
  {
    t.SaveStep(g, n, true);
    before.emplace_back(g.Ram(first, size), g.Ram(first, size) + size);
    step(n);
  }
  // the next frame: the game asks for the copy into its buffer, then the rollback goes back over the allocation
  t.SaveStep(g, 10, true);
  t.RestoreTo(g, 10, 3);
  CHECK(std::memcmp(g.Ram(first, size), before[3].data(), size) == 0);
  // steps 3 and 4 are run again; the picture thread comes to the copy now
  step(3);
  t.SaveStep(g, 4, false);
  step(4);
  picture_thread_clears(heap + 0x20, 0x280);
  CHECK(cleared == 0);
  t.SaveStep(g, 5, false);
  CHECK(std::memcmp(g.Ram(first, size), before[5].data(), size) == 0);  // the three cells stand as step 4 left them
  CHECK(g.Read32(heap + 0x28) == 0xC0 && g.Read32(heap + 0x24) == heap + 0x20 + 0xC0);

  // a portrait's copy lands in memory the log leaves alone: cleared as ever, and a restore keeps it so
  std::memset(g.Ram(portraits + 0x320, 0x2000), 0x55, 0x2000);
  picture_thread_clears(portraits + 0x320, 0x2000);
  CHECK(cleared == 1 && g.Read32(portraits + 0x320) == 0 && g.Read32(portraits + 0x231C) == 0);
  for (u32 n = 5; n < 11; ++n)
  {
    step(n);
    t.SaveStep(g, n + 1, false);
  }
  t.RestoreTo(g, 11, 6);
  CHECK(g.Read32(portraits + 0x320) == 0 && g.Read32(heap + 0x28) == 0xC0);

  // a second log takes over what is answered; the first one's end then changes nothing, the second's ends it
  {
    net::Rollback other;
    other.Start(g);
    t.Stop();
    CHECK(fzgx::UnderRollback(heap, 0x280));
  }
  CHECK(!fzgx::UnderRollback(heap, 0x280) && !fzgx::UnderRollback(first, size));
  picture_thread_clears(heap + 0x20, 0x280);
  CHECK(cleared == 2);
  CHECK(host.log.empty());
}

// The locked cache in the undo log: a restore puts it back with main memory, all but the game's two
// records of what it last told the display hardware (net::SCRATCH_KEPT on), which stay as the newest picture left
// them: the hardware is not put back, and a record put back alone made the game skip a setting it had not made.
void TestTheLockedCacheInTheUndoLog()
{
  TestHost host;
  ModData data;
  Guest g(host, data);
  std::mt19937 rng(20261006);
  FakeGame(host, g, rng);
  host.scratch.assign(0x4000, 0);
  for (u8& b : host.scratch)
    b = static_cast<u8>(rng());
  // the bounds are the game's own lay-out (its set-up routine at 0x80070158): the frame-buffer record at 0x1C0, the
  // graphics wrapper's record of 0xCA0 bytes from 0x1E0; the collision work area (0x1DB0) is no part of it
  CHECK(net::SCRATCH_KEPT == 0x1C0 && net::SCRATCH_KEPT_END == 0x1E0 + 0xCA0);
  const u32 kept = net::SCRATCH_KEPT, kept_end = net::SCRATCH_KEPT_END, size = 0x4000;
  const u32 first = (MODULE + net::DATA) & ~(net::PAGE - 1);

  net::Rollback t;
  t.Start(g);
  CHECK(t.TrackedBytes() == net::STATE_END - first + size);
  std::vector<std::vector<u8>> before;  // the locked cache before each step
  const auto put = [&](u32 offset, u32 value) { std::memcpy(host.scratch.data() + offset, &value, 4); };
  const auto get = [&](u32 offset) {
    u32 value;
    std::memcpy(&value, host.scratch.data() + offset, 4);
    return value;
  };
  // a step and the picture after it write the maths area, both records at their first and last words (the copy
  // sizes among them: the wrapper's record +0xB08), the words on either side of them, the collision work area and
  // words all over the cache
  const auto poke = [&](u32 step) {
    put(0x40, 0x11000000 + step);
    put(kept - 4, 0x22000000 + step);
    put(kept, 0x33000000 + step);
    put(0x1D8, 0x44000000 + step);
    put(0x1E0, 0x55000000 + step);
    put(0x1E0 + 0xB08, 0x66000000 + step);
    put(kept_end - 4, 0x77000000 + step);
    put(kept_end, 0x88000000 + step);
    put(0x1DB0 + 0x47C, 0x99000000 + step);
    for (int k = 0; k < 24; ++k)
      put((rng() % (size / 4)) * 4, rng());
    g.Write32(first + (rng() % 64) * net::PAGE + 0x80, rng());
  };
  for (u32 step = 0; step < 200; ++step)
  {
    t.SaveStep(g, step, step % 3 == 0);
    before.push_back(host.scratch);
    poke(step);
  }
  t.SaveStep(g, 200, true);
  const std::vector<u8> newest = host.scratch;

  // back to before step 150: the cache is as it was then, the two records as they are now
  const auto as_restored = [&](u32 step, const std::vector<u8>& now) {
    std::vector<u8> expect = before[step];
    std::memcpy(expect.data() + kept, now.data() + kept, kept_end - kept);
    return expect;
  };
  t.RestoreTo(g, 200, 150);
  CHECK(host.scratch == as_restored(150, newest));
  CHECK(get(0x40) == 0x11000000 + 149 && get(kept - 4) == 0x22000000 + 149);
  CHECK(get(kept) == 0x33000000 + 199 && get(0x1D8) == 0x44000000 + 199);
  CHECK(get(0x1E0) == 0x55000000 + 199 && get(0x1E0 + 0xB08) == 0x66000000 + 199);
  CHECK(get(kept_end - 4) == 0x77000000 + 199);
  CHECK(get(kept_end) == 0x88000000 + 149 && get(0x1DB0 + 0x47C) == 0x99000000 + 149);

  // the steps run again and the pictures drawn meanwhile write on (as the session does it: step 150 runs from the
  // restored memory, a save before each later one); a second, deeper restore is as exact, and what the records took
  // since the first one stays
  for (u32 step = 150; step < 180; ++step)
  {
    if (step > 150)
      t.SaveStep(g, step, step % 3 == 0);
    before[step] = host.scratch;
    poke(step + 1000);
  }
  t.SaveStep(g, 180, true);
  const std::vector<u8> later = host.scratch;
  t.RestoreTo(g, 180, 140);
  CHECK(host.scratch == as_restored(140, later));
  CHECK(get(0x1E0 + 0xB08) == 0x66000000 + 1179 && get(0x1DB0 + 0x47C) == 0x99000000 + 139);
  t.Stop();
}

// What a rollback's user says through the interface and nowhere else: the inputs a step runs with, how many steps
// the frame runs again, the checksum kept for a step.
void TestTheRollbackInterface()
{
  TestHost host;
  ModData data;
  Guest g(host, data);
  const std::vector<int> seats = {3, 17};
  const u8 records[12] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
  net::Rollback::UseInputs(g, 1025, seats, records);  // the ring keeps 1024 steps: step 1025 is entry 1
  u8 got[6];
  g.CopyFrom(got, layout::INPUTS + (1 * 30 + 3) * 8, 6);
  CHECK(std::memcmp(got, records, 6) == 0);
  g.CopyFrom(got, layout::INPUTS + (1 * 30 + 17) * 8, 6);
  CHECK(std::memcmp(got, records + 6, 6) == 0);
  CHECK(g.Read32(layout::INPUTS + (1 * 30 + 4) * 8) == 0);  // no other seat's
  net::Rollback::Resimulate(g, 7);
  CHECK(g.Read32(layout::CONTROL + 8) == 7 && !net::Rollback::Resimulating(g));
  g.Write32(layout::CONTROL + 12, 1);  // the frame routine sets it while its further steps run
  CHECK(net::Rollback::Resimulating(g));
  g.Write32(net::STEP, 99);
  CHECK(net::Rollback::Step(g) == 99);
  net::Rollback::KeepChecksum(g, 2049, 0xC0FFEE00);
  CHECK(net::Rollback::KeptChecksum(g, 2049) == 0xC0FFEE00 && g.Read32(layout::CHECKSUMS + 4) == 0xC0FFEE00);
  // what the checksum covers: a machine's position, not its drawn matrix (+0x14C) nor a pointer (+0x49C)
  CHECK(net::Rollback::InChecksum(false, 0x7C) && !net::Rollback::InChecksum(false, 0x14C) &&
        !net::Rollback::InChecksum(false, 0x49C) && net::Rollback::InChecksum(true, 0xF8) &&
        !net::Rollback::InChecksum(false, 0x620) && !net::Rollback::InChecksum(true, 0x1FC));
}

void TestChecksum()
{
  TestHost host;
  ModData data;
  Guest g(host, data);
  std::mt19937 rng(7);
  FakeGame(host, g, rng);
  const u32 machines = 0x80C00000, records = 0x80D00000;
  g.Write32(MODULE + net::MACHINES, machines);
  g.Write8(MODULE + net::MACHINE_COUNT, 30);
  for (u32 i = 0; i < 30; ++i)
    g.Write32(machines + i * 0x620 + 0x49C, records + i * 0x200);
  const u32 sum = net::StateChecksum(g, MODULE);
  CHECK(sum == net::StateChecksum(g, MODULE));
  // the value is FNV-1a over the words InChecksum names, taken one by one in the order of the blocks
  u32 h = 2166136261u;
  for (u32 i = 0; i < 30; ++i)
  {
    for (u32 off = 0; off < 0x620; off += 4)
      h = net::Rollback::InChecksum(false, off) ? (h ^ g.Read32(machines + i * 0x620 + off)) * 16777619u : h;
    for (u32 off = 0; off < 0x1FC; off += 4)
      h = net::Rollback::InChecksum(true, off) ? (h ^ g.Read32(records + i * 0x200 + off)) * 16777619u : h;
  }
  CHECK(h == sum);
  // a physics word of the last machine counts; its progress-record pointer (a pointer) does not
  g.Write32(machines + 29 * 0x620 + 0x7C, g.Read32(machines + 29 * 0x620 + 0x7C) ^ 1);
  CHECK(net::StateChecksum(g, MODULE) != sum);
  g.Write32(machines + 29 * 0x620 + 0x7C, g.Read32(machines + 29 * 0x620 + 0x7C) ^ 1);
  CHECK(net::StateChecksum(g, MODULE) == sum);
  g.Write32(records + 5 * 0x200 + 0x118, 3);
  CHECK(net::StateChecksum(g, MODULE) != sum);
}

// Condition 1 of the native form: the mod's data window is this process's buffer and no
// part of the rolled-back state. After a rollback as the session makes it, the buffer is byte for byte what it was
// before, apart from the one word the rollback writes explicitly: the step about to run.
void TestARollbackLeavesTheWindow()
{
  TestHost host;
  ModData data;
  Guest g(host, data);
  std::mt19937 rng(20261006);
  FakeGame(host, g, rng);
  // the window as a race leaves it: every byte in use (image, control block, checksum ring, names, race, inputs)
  for (u32 a = layout::WINDOW; a < layout::WINDOW_END; a += 4)
    g.Write32(a, rng());
  g.Write32(net::UNTRACKED, 0);
  net::Rollback t;
  t.Start(g);
  const u32 first = (MODULE + net::DATA) & ~(net::PAGE - 1);
  const u32 size = net::STATE_END - first;
  std::vector<u8> before40;
  for (u32 step = 0; step < 60; ++step)
  {
    t.SaveStep(g, step, true);
    if (step == 40)
      before40.assign(g.Ram(first, size), g.Ram(first, size) + size);
    for (int k = 0; k < 40; ++k)
      g.Write32(first + (rng() % 64) * net::PAGE + (rng() % (net::PAGE / 4)) * 4, rng());
    // what the game's routines write into the window while a step runs: the step word, a checksum, an input row
    g.Write32(net::STEP, step + 1);
    g.Write32(layout::CHECKSUMS + 4 * (step & 1023), rng());
    g.Write32(layout::INPUTS + (step & 1023) * 30 * 8, rng());
  }
  t.SaveStep(g, 60, true);
  std::vector<u8> window = data.Snapshot();
  CHECK(g.Read32(net::STEP) == 60);

  t.RestoreTo(g, 60, 40);
  CHECK(std::memcmp(g.Ram(first, size), before40.data(), size) == 0);   // the simulation's memory is back
  const std::vector<u8> after = data.Snapshot();
  CHECK(after.size() == window.size());
  std::size_t differing = 0, outside_the_step_word = 0;
  for (std::size_t i = 0; i < after.size(); ++i)
  {
    if (after[i] == window[i])
      continue;
    ++differing;
    const u32 address = layout::WINDOW + static_cast<u32>(i);
    outside_the_step_word += address < net::STEP || address >= net::STEP + 4;
  }
  CHECK(outside_the_step_word == 0);
  CHECK(differing >= 1 && differing <= 4 && g.Read32(net::STEP) == 40);
  // with the step word put aside, nothing at all
  std::vector<u8> same = after;
  std::memcpy(same.data() + (net::STEP - layout::WINDOW), window.data() + (net::STEP - layout::WINDOW), 4);
  CHECK(same == window);
  CHECK(host.log.empty());
}

// The undo log's spare lists (DropEntry): what dropped entries leave for the next ones is bounded by bytes, never by a
// count of blocks alone: no list of over 2 MiB of room is kept, and at most 16 MiB in all.
void TestTheSpareListsAreBounded()
{
  net::Tracking s;
  const std::size_t block = sizeof(net::Blocks::value_type);
  net::Undo big;
  big.blocks.reserve((3u << 20) / block);  // 3 MiB of room: let go
  net::DropEntry(s, big);
  CHECK(s.spare.empty() && s.spare_bytes == 0);
  for (int i = 0; i < 40; ++i)
  {
    net::Undo entry;
    entry.blocks.reserve((1u << 20) / block);  // 1 MiB each
    entry.blocks.resize(10);
    net::DropEntry(s, entry);
  }
  std::size_t held = 0;
  for (const net::Blocks& list : s.spare)
    held += list.capacity() * block;
  CHECK(held == s.spare_bytes && s.spare_bytes <= (16u << 20) && s.spare.size() >= 15 && s.spare.size() <= 16);
  CHECK(s.spare.back().empty());  // emptied, its room kept
}
