// The deep-rollback worker, stage S1 (fzgx/rollback_worker.h): W, its log, the job's start in the frame's hook, the
// context's own network hook, and the comparison with the game thread's steps at the hook of the frame's step.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fzgx/rollback_worker.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <format>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "internal.h"
#include "mode.h"
#include "net_internal.h"

namespace fzgx::rollback_worker
{
namespace
{
using net::Rollback;
using net::Session;

constexpr u32 RAM = layout::RAM;
constexpr u32 CACHE_WINDOW = 0x40000;  // the locked cache's window as the CPU context reaches it (256 KiB)
constexpr u32 CURRENT_THREAD = 0x800000E4;          // OSCurrentThread
constexpr u32 ACTIVE_THREADS = 0x800000DC, ACTIVE_NEXT = 0x2FC;  // the OS's active threads, OSThread.linkActive
constexpr u32 STACK_BASE = 0x304, STACK_END = 0x308;
// The OS's record of its last interrupt (DOL .sbss, the decomp's symbols): its srr0, its type (u16), its time (u64)
constexpr u32 LAST_INTERRUPT_SRR0 = 0x801A67AC, LAST_INTERRUPT = 0x801A67B0, LAST_INTERRUPT_TIME = 0x801A67B8;  // OSThread: the stack's top and its lowest word
constexpr u32 MOST_LINES = 40;                        // difference lines in a run's log

// W: main memory and a locked cache of its own.
class WorkerMemory final : public Host
{
public:
  explicit WorkerMemory(u32 ram_size, u32 scratch_size)
      : ram(ram_size, 0), cache(std::max(scratch_size, CACHE_WINDOW), 0), scratch(scratch_size)
  {
  }
  u8* Ram() override { return ram.data(); }
  u32 RamSize() override { return static_cast<u32>(ram.size()); }
  u8* Scratch() override { return scratch == 0 ? nullptr : cache.data(); }
  u32 ScratchSize() override { return scratch; }
  void RequestStop() override {}
  float EmulationSpeed() override { return 1; }
  float CpuClock() override { return 1; }
  void SetPace(float, float) override {}
  void Log(LogLevel level, const std::string& text) override { TheHost().Log(level, text); }
  bool ReadGameFile(const std::string&, std::vector<u8>*) override { return false; }
  u64 PageHash(const u8* data, std::size_t size) override { return TheHost().PageHash(data, size); }
  std::vector<u8> ram, cache;
  u32 scratch;
};

enum class Job
{
  NONE,
  PREPARED,  // W rewound to R, before the session's restore
  READY,     // the frame runs every step to L: the context runs at the hook's return
  RUNNING,
  RAN,       // W stands before L: compared at the game's hook of L
};

struct Worker
{
  std::string mode;
  u32 depth = 7;
  std::unique_ptr<WorkerMemory> memory;
  ModData window;
  std::unique_ptr<Guest> g;
  Rollback log{false};
  bool primed = false;  // W holds the game's memory but for the pages TakeDirty names
  u32 region = 0;
  Job job = Job::NONE;
  u32 from = 0, to = 0;
  std::vector<u8> outside_before;  // W outside the log at the job's start ("check")
  // the run's sums
  u64 jobs = 0, ran = 0, compared = 0, gave_up = 0, held = 0, uncompared = 0;
  u64 base_differ = 0, memory_differ = 0, entries_differ = 0, rings_differ = 0;
  u64 time_jobs = 0, time_words = 0;
  u64 memory_words = 0, outside_words = 0, outside_jobs = 0, reverted = 0, reverted_changed = 0;
  u64 pages_copied = 0, steps = 0;
  std::map<std::string, u64> why, counts;
  std::vector<u64> outside_at = std::vector<u64>(0x200, 0);  // outside words differing, by 64 KiB
  std::map<std::string, u32> lines;
  // the words a comparison found differing, followed step by step in the jobs after (the log's "first differs")
  std::vector<u32> watch;
  std::vector<std::array<u32, 4>> seen;  // W's values at each hook of the job, from its step R + 1
  u32 told = 0;                           // the watched words already told of in this job (bits)
  u64 irq_time = 0;                       // the game's last interrupt's time at the previous hook
  u32 thread_at_restore = 0;
};
Worker w;

// A part of a job's cost on the game's thread, summed in microseconds for the last line.
struct Timed
{
  const char* what;
  std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
  ~Timed()
  {
    w.counts[what] += static_cast<u64>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count());
  }
};

bool Say(const char* kind)
{
  return w.lines[kind]++ < MOST_LINES;
}

u32 Word(const u8* p)
{
  return static_cast<u32>(p[0]) << 24 | static_cast<u32>(p[1]) << 16 | static_cast<u32>(p[2]) << 8 | p[3];
}

// The thread whose stack holds address a (the OS's active-thread list), for the log: "" if none.
std::string StackOf(const Guest& g, u32 a)
{
  const u32 current = g.Read32(CURRENT_THREAD);
  u32 t = g.Read32(ACTIVE_THREADS);
  for (u32 n = 0; n < 64 && t >= RAM && t < RAM + g.GetHost().RamSize(); ++n, t = g.Read32(t + ACTIVE_NEXT))
    if (a >= g.Read32(t + STACK_END) && a < g.Read32(t + STACK_BASE))
      return std::format("; the stack of thread {:08x}{}", t, t == current ? ", running" : ", not running");
  return "";
}

// The task manager's time words (main.rel fn_1_3BDC and fn_1_407C, read in the decomp's _prolog.c): the ticks at
// each timing's start (.bss 0xD7C, ten words) and every task node's sum of its run's ticks (+28), of the simulation's
// and the drawing's managers. The worker's time base stands still, so these differ by kind; no checksum reads them.
std::vector<u32> TimeWords(const Guest& a, const Guest& b)
{
  constexpr u32 BSS = game::REL_BSS_AT;
  constexpr u32 MANAGERS[] = {BSS + 0x3C00 + 0x21F88, BSS + 0x3C00 + 0x220A4};
  std::vector<u32> words;
  for (u32 k = 0; k < 10; ++k)
    words.push_back(BSS + 0xD7C + k * 4);
  const auto in_ram = [](u32 address) { return address >= RAM && address < net::STATE_END && (address & 3) == 0; };
  for (const Guest* g : {&a, &b})
  {
    for (const u32 at : MANAGERS)
    {
      const u32 manager = g->Read32(at);
      for (u32 list = 0; in_ram(manager) && list < 32; ++list)
      {
        const u32 head = g->Read32(manager + 24 + list * 4);
        u32 node = in_ram(head) ? g->Read32(head + 8) : 0;
        for (u32 n = 0; in_ram(node) && n < 4096; ++n, node = g->Read32(node + 8))
          words.push_back(node + 28);
      }
    }
  }
  std::sort(words.begin(), words.end());
  words.erase(std::unique(words.begin(), words.end()), words.end());
  return words;
}

// The memory outside the log: below it (the executable, the OS, the threads' stacks) and above it (the display
// buffers), less the generator word, which the log puts back.
template <typename Each>
void Outside(u32 region, u32 ram_size, Each&& each)
{
  each(0u, region - RAM);
  each(net::STATE_END - RAM, ram_size - (net::STATE_END - RAM));
}
}  // namespace

bool On()
{
  return !TheOptions().debug.worker_mode.empty() && TheOptions().debug.worker_mode != "off";
}

ModData& Window()
{
  return w.window;
}

bool Begin(Memory* memory)
{
  if (w.job != Job::READY)
    return false;
  w.window.Restore(Data().Snapshot());  // the session's words of this hook: the extra steps, R's row, the step word
  w.job = Job::RUNNING;
  memory->ram = w.memory->ram.data();
  memory->ram_size = w.memory->RamSize();
  memory->cache = w.memory->cache.data();
  memory->cache_size = static_cast<u32>(w.memory->cache.size());
  return true;
}

bool Hook()
{
  if (w.job != Job::RUNNING)
  {
    GiveUp("the network hook outside a job");
    return false;
  }
  Guest& gw = *w.g;
  const u32 t = gw.Read32(net::STEP);
  if (t > 0)  // the checksum of the step just run, as the game's hook keeps it (ChecksumPrevious)
    Rollback::KeepChecksum(gw, t - 1, Rollback::Checksum(gw));
  if (t <= w.from || t > w.to)
  {
    GiveUp(std::format("the steps went another way (step {} in a job from {} to {})", t, w.from, w.to));
    return false;
  }
  if (!w.watch.empty() && t - w.from < w.seen.size())
    for (std::size_t i = 0; i < w.watch.size(); ++i)
      w.seen[t - w.from][i] = gw.Read32(w.watch[i]);
  w.log.SaveStep(gw, t, false);
  if (t == w.to)
  {
    w.job = Job::RAN;
    ++w.ran;
    return false;
  }
  net::WriteRow(gw, *net::s_session, t, net::RowFor(*net::s_session, t));
  ++w.steps;
  return true;
}

void ModePoint()
{
  if (w.job == Job::RUNNING)
    mode::Step(*w.g);
}

void GiveUp(const std::string& why)
{
  if (w.job != Job::NONE)
    ++w.gave_up;
  ++w.why[why];
  if (Say("gave up"))
    Notice("fzgx: rollback worker: the job from {} to {} gave up at step {}: {}", w.from, w.to,
           w.g ? w.g->Read32(net::STEP) : 0, why);
  w.job = Job::NONE;
  w.primed = false;  // W stands part way through a step that no log saw end
}

void Count(const char* what, u64 n)
{
  w.counts[what] += n;
}

void Stop()
{
  if (!On() || w.jobs == 0)
    return;
  std::string why, counts, outside;
  for (const auto& [text, n] : w.why)
    why += std::format(" [{}: {}]", text, n);
  for (const auto& [text, n] : w.counts)
    counts += std::format(" {} {}", text, n);
  for (u32 k = 0; k < w.outside_at.size(); ++k)
  {
    if (w.outside_at[k] != 0)
      outside += std::format(" {:08x}:{}", RAM + k * 0x10000, w.outside_at[k]);
  }
  Notice("fzgx: rollback worker: {} mode, depth {} on: {} jobs, {} ran, {} compared, {} gave up, {} held, {} not "
         "compared; differing jobs: base {}, memory {} ({} words; the task manager's time words apart: {} jobs, {} words), moments {}, checksums or rows {}; outside the log {} "
         "jobs ({} words; adoption's model: {} put back, {} of them changed); {} steps run, {} pages copied;{}{}",
         w.mode, w.depth, w.jobs, w.ran, w.compared, w.gave_up, w.held, w.uncompared, w.base_differ, w.memory_differ,
         w.memory_words, w.time_jobs, w.time_words, w.entries_differ, w.rings_differ, w.outside_jobs, w.outside_words, w.reverted,
         w.reverted_changed, w.steps, w.pages_copied, counts, why);
  if (!outside.empty())
    Notice("fzgx: rollback worker: words outside the log differing, by 64 KiB:{}", outside);
}
}  // namespace fzgx::rollback_worker

namespace fzgx::net
{
using namespace fzgx::rollback_worker;

// Before the session's restore from `step` to `run_from` (CheckPredictions): a job when the rollback is deep enough.
void WorkerPrepare(Guest& g, Session& s, u32 step, u32 run_from)
{
  if (w.job != Job::NONE)
  {
    ++w.uncompared;  // (a frame that ended before its step L: the race's end cut it)
    w.job = Job::NONE;
  }
  if (!On() || s.practice || s.scrub.forked || step - run_from < w.depth || !s.rollback.Active())
    return;
  const Timed timed{"us preparing"};
  if (w.mode.empty())
  {
    w.mode = TheOptions().debug.worker_mode;
    w.depth = std::max<u32>(TheOptions().debug.worker_depth, 1);
  }
  Host& host = g.GetHost();
  if (!w.memory)
  {
    w.memory = std::make_unique<WorkerMemory>(host.RamSize(), host.ScratchSize());
    w.g = std::make_unique<Guest>(*w.memory, w.window);
  }
  ++w.jobs;
  const u32 region = s.rollback.Region(), ram_size = w.memory->RamSize();
  u8* live = host.Ram();
  u8* mine = w.memory->ram.data();
  // W: the pages either log may have seen change since the last job (all of it the first time, or when the race's
  // log started again), the memory outside the log and the locked cache whole, the window
  const std::vector<u32> dirty = s.rollback.TakeDirty();
  const std::vector<u32> worker_dirty = w.log.Active() ? w.log.TakeDirty() : std::vector<u32>();
  if (!w.primed || region != w.region)
  {
    std::memcpy(mine, live, ram_size);
    w.pages_copied += ram_size / PAGE;
  }
  else
  {
    for (const std::vector<u32>* pages : {&dirty, &worker_dirty})
    {
      for (const u32 off : *pages)
        std::memcpy(mine + (region - RAM) + off, live + (region - RAM) + off, PAGE);
      w.pages_copied += pages->size();
    }
    Outside(region, ram_size, [&](u32 at, u32 size) { std::memcpy(mine + at, live + at, size); });
  }
  if (host.Scratch() != nullptr)
    std::memcpy(w.memory->cache.data(), host.Scratch(), host.ScratchSize());
  w.window.Restore(Data().Snapshot());
  w.primed = true;
  if (w.log.Active() && w.log.Region() != region)
    w.log.Stop();
  w.region = region;
  // rewound by the game's log, and the worker's log's moment before R
  s.rollback.Apply(*w.g, step, run_from);
  if (!w.log.Active())
    w.log.Start(*w.g);
  w.log.Adopt(*w.g, s.rollback, run_from);
  if (w.mode == "check")
  {
    w.outside_before.resize(ram_size);
    Outside(region, ram_size, [&](u32 at, u32 size) { std::memcpy(w.outside_before.data() + at, mine + at, size); });
  }
  w.from = run_from;
  w.to = step;
  w.job = Job::PREPARED;
  if (w.jobs <= 2)
    Notice("fzgx: rollback worker: job {} from {} to {} prepared ({} and {} pages taken)", w.jobs, w.from, w.to,
           dirty.size(), worker_dirty.size());
}

// After the restore: W must be the restored memory on all a restore puts back (the dirty pages and Apply are right).
void WorkerAfterRestore(Guest& g, Session& s)
{
  if (w.job != Job::PREPARED)
    return;
  const Timed timed{"us checking the base"};
  w.seen.assign(w.to - w.from + 1, {});
  w.told = 0;
  w.irq_time = static_cast<u64>(g.Read32(LAST_INTERRUPT_TIME)) << 32 | g.Read32(LAST_INTERRUPT_TIME + 4);
  w.thread_at_restore = g.Read32(CURRENT_THREAD);
  std::vector<u32> words;
  if (const u32 n = s.rollback.Compare(g, *w.g, &words, 4); n != 0)
  {
    ++w.base_differ;
    if (Say("base"))
      Notice("fzgx: rollback worker: the job from {} to {}: W differs from the restored memory in {} words, first {:08x}",
             w.from, w.to, n, words[0]);
  }
}

// The frame's steps are known (RunSteps): the job runs only when the frame runs every step to L (not held, not cut).
void WorkerOpen(u32 extra, bool held)
{
  if (w.job != Job::PREPARED)
    return;
  if (held || extra < w.to - w.from)
  {
    ++w.held;
    w.job = Job::NONE;
    return;
  }
  w.job = Job::READY;
}

// The game's hook before step t, after its SaveStep (ExtraStep): at the job's step L, the comparison.
// The watched words at the game's hook of step t of a job W has run: the first step after which one differs, and
// whether the game took an interrupt in that step (the OS's record of its last one) or ran another thread.
void Follow(const Guest& g, u32 t)
{
  const u64 irq = static_cast<u64>(g.Read32(LAST_INTERRUPT_TIME)) << 32 | g.Read32(LAST_INTERRUPT_TIME + 4);
  for (std::size_t i = 0; i < w.watch.size(); ++i)
  {
    const u32 here = g.Read32(w.watch[i]), there = w.seen[t - w.from][i];
    if (here == there || (w.told & (1u << i)) || !Say("follow"))
      continue;
    w.told |= 1u << i;
    Notice("fzgx: rollback worker: the job from {} to {}: {:08x} first differs at the hook of step {} ({:08x} here, "
           "{:08x} on W); {}; the running thread {:08x} (at the restore {:08x})",
           w.from, w.to, w.watch[i], t, here, there,
           irq != w.irq_time ? std::format("the game took an interrupt in that step (the last: type {}, srr0 {:08x})",
                                           g.Read32(LAST_INTERRUPT) >> 16, g.Read32(LAST_INTERRUPT_SRR0))
                             : std::string("no interrupt in that step"),
           g.Read32(CURRENT_THREAD), w.thread_at_restore);
  }
  w.irq_time = irq;
}

void WorkerCompare(Guest& g, Session& s, u32 t)
{
  if (w.job == Job::RAN && t > w.from && t <= w.to && !w.watch.empty() && t - w.from < w.seen.size())
    Follow(g, t);
  if (w.job != Job::RAN || t != w.to)
    return;
  const Timed timed{"us comparing"};
  w.job = Job::NONE;
  ++w.compared;
  Guest& gw = *w.g;
  std::vector<u32> words;
  const std::vector<u32> time = TimeWords(g, gw);
  u32 time_differ = 0;
  for (const u32 a : time)
    time_differ += g.Read32(a) != gw.Read32(a);
  w.time_jobs += time_differ != 0;
  w.time_words += time_differ;
  if (const u32 n = s.rollback.Compare(g, gw, &words, 8, &time); n != 0)
  {
    ++w.memory_differ;
    w.memory_words += n;
    for (const u32 a : words)
      if (w.watch.size() < 4 && std::find(w.watch.begin(), w.watch.end(), a) == w.watch.end())
        w.watch.push_back(a);
    if (Say("memory"))
    {
      std::string list;
      for (const u32 a : words)
        list += std::format(" {:08x} ({:08x} here, {:08x} on W{})", a, g.Read32(a), gw.Read32(a), StackOf(g, a));
      Notice("fzgx: rollback worker: the job from {} to {}: memory differs in {} words:{}", w.from, w.to, n, list);
    }
  }
  if (const u32 k = s.rollback.CompareMoments(g, w.log, gw, w.from, w.to, &time); k != 0xFFFFFFFF)
  {
    ++w.entries_differ;
    if (Say("moments"))
      Notice("fzgx: rollback worker: the job from {} to {}: the logs' moments differ before step {} (the latest)", w.from,
             w.to, k);
  }
  for (u32 k = w.from; k < w.to; ++k)
  {
    u8 a[30 * 8], b[30 * 8];
    g.CopyFrom(a, layout::INPUTS + (k & 1023) * sizeof(a), sizeof(a));
    gw.CopyFrom(b, layout::INPUTS + (k & 1023) * sizeof(b), sizeof(b));
    if (Rollback::KeptChecksum(g, k) != Rollback::KeptChecksum(gw, k) || std::memcmp(a, b, sizeof(a)) != 0)
    {
      ++w.rings_differ;
      if (Say("rings"))
        Notice("fzgx: rollback worker: the job from {} to {}: step {}'s checksum ({:08x} here, {:08x} on W) or rows "
               "differ", w.from, w.to, k, Rollback::KeptChecksum(g, k), Rollback::KeptChecksum(gw, k));
      break;
    }
  }
  // outside the log: a named class (interrupts, the time base, the stacks), counted by 64 KiB
  u8* live = g.GetHost().Ram();
  const u8* mine = w.memory->ram.data();
  const u32 ram_size = w.memory->RamSize();
  u32 outside = 0;
  Outside(w.region, ram_size, [&](u32 at, u32 size) {
    for (u32 run = at; run < at + size; run += 64)
    {
      if (std::memcmp(live + run, mine + run, std::min<u32>(64, at + size - run)) == 0)
        continue;
      for (u32 a = run; a < std::min<u32>(run + 64, at + size); a += 4)
      {
        if (Word(live + a) != Word(mine + a) && a != net::DOL_RNG - RAM)
        {
          ++outside;
          ++w.outside_at[(a >> 16) & 0x1FF];
        }
      }
    }
  });
  w.outside_words += outside;
  w.outside_jobs += outside != 0;
  if (w.mode != "check")
    return;
  // the adoption's model: the words the worker's steps changed outside the log keep the job's start values (the
  // predicted timeline's), but for the running thread's stack
  const u32 thread = Word(mine + (CURRENT_THREAD - RAM));
  u32 stack_low = 0, stack_high = 0;
  if (thread >= RAM && thread - RAM + STACK_END + 4 <= ram_size)
  {
    stack_high = Word(mine + (thread - RAM) + STACK_BASE);
    stack_low = Word(mine + (thread - RAM) + STACK_END);
  }
  Outside(w.region, ram_size, [&](u32 at, u32 size) {
    for (u32 a = at; a < at + size; a += 4)
    {
      if ((a & 63) == 0 && std::memcmp(mine + a, w.outside_before.data() + a, std::min<u32>(64, at + size - a)) == 0)
      {
        a += 60;
        continue;
      }
      if (Word(mine + a) == Word(w.outside_before.data() + a) || a == net::DOL_RNG - RAM ||
          (RAM + a >= stack_low && RAM + a < stack_high))
        continue;
      ++w.reverted;
      w.reverted_changed += Word(live + a) != Word(w.outside_before.data() + a);
      std::memcpy(live + a, w.outside_before.data() + a, 4);
    }
  });
}
}  // namespace fzgx::net
