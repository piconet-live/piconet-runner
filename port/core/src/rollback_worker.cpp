// The deep-rollback worker, stage S1 (fzgx/rollback_worker.h): W, its log, the job's start in the frame's hook, the
// context's own network hook, and the comparison with the game thread's steps at the hook of the frame's step.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fzgx/rollback_worker.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>
#include <cstring>
#include <format>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "files.h"
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
// S2, the worker's thread
constexpr u32 NONE = 0xFFFFFFFF;
constexpr u32 EXTRA_STEPS = layout::CONTROL + 8;    // the frame routine's further steps (netframe.py): kept high on W
constexpr u32 SOUNDS_FROM = layout::CONTROL + 492;  // a step below it was run before: muted (mute.py)
constexpr u32 W_EXTRA = 1000;                       // W never reaches a frame's last step (the drawing's)
// The countdown's 0 is step 421 in every race (count 420 at the race start, race-map-flow.md 4.1): a job starts only
// after it. Before it, steps 364 to 405 write the drawing's pipe (GXSetProjectionv, S1), and the module's pipe is one
// pointer for every thread (DolRecomp's dolrecomp_gather_pipe): a store there on W would land in the game's pipe.
constexpr u32 COUNTDOWN_END = 422;
constexpr u32 MOST_SPAN = 30;     // A - R at most (per-machine rollback keeps 32 runs, lanes_partial.h KEEP)
constexpr u32 LEAST_CORES = 4;    // below it, no thread (study section 7, decision 5)
constexpr double FIELD_MS = 1000.0 * 1001.0 / 60000.0;
constexpr double MARGIN_MS = 1.5;      // run-ahead's MARGIN_MS: room kept before the field
constexpr double START_MS = 0.5;       // c of the study's section 4: a job's start and the thread's wake
constexpr double FIRST_CONST_MS = 5.4; // a frame's own cost before one is measured (study section 4)
constexpr double FIRST_STEP_MS = 2.0;  // a step run again before one is measured
// The VI retrace callbacks' words (S1, difference 1): written by interrupts the worker never takes
constexpr u32 VI_COUNT = game::REL_BSS_AT + 0x6F640, PAD_READY = game::REL_DATA_AT + 0x2B64;

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

// S2: a job on the worker's thread. The game's thread owns everything but what the mutex guards while the worker runs.
struct Async
{
  std::mutex m;
  std::condition_variable cv;
  // the game's thread, read by the worker under the mutex
  u32 from = 0, target = 0, rewind = NONE;
  bool stop = false;
  std::vector<net::Row> rows;  // the rows of steps from `from` on, as the session has them now
  std::vector<int> seats;
  // the worker, read by the game's thread under the mutex
  bool running = false;  // the worker's thread is in the job (from Begin until it has left it)
  bool parked = false;   // it waits at the hook of `at`
  u32 at = 0;
  bool ended = false;    // it gave up
  bool mid_step = false; // ... part way through a step
  std::string why;
  double r_ms = 0;       // the worker's step lately
  u64 steps = 0, rewinds = 0, waits = 0;
  std::chrono::steady_clock::time_point left{};  // the hook last returned (the worker's)
  // the game's thread's own
  bool open = false, begun = false;
  u32 started_at = 0, depth = 0, frames = 0, j_pred = 0;
  std::vector<u32> time_words;  // the task nodes' time words of W at R (nodes that leave the lists in the job)
};
Async s_a;
thread_local bool t_worker = false;  // this is the worker's thread

// The rule's measurements and the frame counts (game's thread)
struct Rule
{
  double r_sync = FIRST_STEP_MS;  // a step run again on the game's thread lately
  double c_high = FIRST_CONST_MS; // a frame's own cost lately (its recent high)
  bool c_seen = false;
  std::chrono::steady_clock::time_point hook{};  // the last hook of this frame (frame or extra)
  double steps_ms = 0;  // the steps run again in this frame so far
  u32 steps_n = 0;
  u32 late_before = 0;
  int kind = -1;  // the frame's kind, counted at the next frame's start
  bool fall_back = false;  // this frame runs a given-up job's rollback sync: no job starts in it
  // by kind: frames, late (the late-frame skip's test), drawn, busy sum and high
  static constexpr int KINDS = 6;
  u64 frames[KINDS] = {}, late[KINDS] = {}, drawn[KINDS] = {};
  double busy_sum[KINDS] = {}, busy_high[KINDS] = {};
  // the jobs
  u64 jobs = 0, adopted = 0, gave_up = 0, cancelled = 0, too_long = 0, rewinds_asked = 0, sync_fit = 0,
      barred_countdown = 0, barred_other = 0, barred_finish = 0, waited_idle = 0;
  u64 j_sum = 0, j_pred_sum = 0, j_high = 0, d_sum = 0;
  std::map<u32, u64> j_seen;
  double start_us = 0, adopt_us = 0, begin_us = 0, idle_us = 0;
  u64 adopt_words = 0;
};
Rule s_r;
enum Kind
{
  K_PLAIN,      // no rollback
  K_SHALLOW,    // a sync rollback below the worker's depth
  K_DEEP_SYNC,  // a sync rollback at the worker's depth or deeper (a job given up or not started included)
  K_START,      // a job started
  K_OPEN,       // a job open (taken corrections included)
  K_ADOPT,      // an adoption
};
const char* const KIND_NAMES[Rule::KINDS] = {"plain", "shallow", "deep sync", "job start", "job open", "adoption"};
void (*s_forget)(u32 step) = nullptr;

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

// S2's thread: "on"; "sync" measures the same frames with no worker (the baseline).
bool Threaded()
{
  return TheOptions().debug.worker_mode == "on";
}

bool Measuring()
{
  return TheOptions().debug.worker_mode == "on" || TheOptions().debug.worker_mode == "sync";
}

void SetForget(void (*forget)(u32 step))
{
  s_forget = forget;
}

const std::string& WatchSpec()
{
  return TheOptions().debug.worker_watch;
}

ModData& Window()
{
  return w.window;
}

bool Begin(Memory* memory)
{
  if (s_a.open && !s_a.begun)
  {
    // the job's start (S2): W's window from the game's at this hook, the step word and the row of R, the frame's
    // further steps kept high
    const auto start = std::chrono::steady_clock::now();
    Guest& gw = *w.g;
    w.window.Restore(Data().Snapshot());
    gw.Write32(net::STEP, s_a.from);
    if (!s_a.rows.empty())
      Rollback::UseInputs(gw, s_a.from, s_a.seats, s_a.rows[0].data());
    gw.Write32(EXTRA_STEPS, W_EXTRA);
    {
      const std::lock_guard lock(s_a.m);
      s_a.running = true;
      s_a.at = s_a.from;
      s_a.left = std::chrono::steady_clock::now();
    }
    s_a.begun = true;
    memory->ram = w.memory->ram.data();
    memory->ram_size = w.memory->RamSize();
    memory->cache = w.memory->cache.data();
    memory->cache_size = static_cast<u32>(w.memory->cache.size());
    memory->threaded = true;
    s_r.begin_us += std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
    return true;
  }
  if (w.job != Job::READY)
    return false;
  memory->threaded = false;
  w.window.Restore(Data().Snapshot());  // the session's words of this hook: the extra steps, R's row, the step word
  w.job = Job::RUNNING;
  memory->ram = w.memory->ram.data();
  memory->ram_size = w.memory->RamSize();
  memory->cache = w.memory->cache.data();
  memory->cache_size = static_cast<u32>(w.memory->cache.size());
  return true;
}

// The worker's thread at W's network hook (S2): the step just run's checksum, its undo entry, then the job's news:
// stop, go back to a step whose row changed, run the next step with its row, or wait at the job's target.
bool HookThreaded()
{
  Async& a = s_a;
  Guest& gw = *w.g;
  u32 t = gw.Read32(net::STEP);
  const auto now = std::chrono::steady_clock::now();
  const double ran_ms = std::chrono::duration<double, std::milli>(now - a.left).count();
  if (t > 0)
    Rollback::KeepChecksum(gw, t - 1, Rollback::Checksum(gw));
  std::unique_lock lock(a.m);
  if (t <= a.from)
  {
    a.why = std::format("the steps went another way (step {} in a job from {})", t, a.from);
    a.ended = true;
    a.cv.notify_all();
    return false;
  }
  lock.unlock();
  w.log.SaveStep(gw, t, false);
  lock.lock();
  if (t > a.from + 1 || a.steps != 0)
    a.r_ms = a.r_ms == 0 ? ran_ms : a.r_ms + (ran_ms - a.r_ms) / 8;
  a.at = t;
  for (;;)
  {
    if (a.stop)
      return false;
    if (a.rewind != NONE)
    {
      const u32 c = std::exchange(a.rewind, NONE);
      if (c < t && c >= a.from)
      {
        ++a.rewinds;
        lock.unlock();
        w.log.RestoreTo(gw, t, c);
        lock.lock();
        t = c;
        a.at = t;
        continue;
      }
    }
    if (t < a.target && t - a.from < a.rows.size())
    {
      const net::Row row = a.rows[t - a.from];
      ++a.steps;
      lock.unlock();
      Rollback::UseInputs(gw, t, a.seats, row.data());
      gw.Write32(EXTRA_STEPS, W_EXTRA);
      gw.Write32(SOUNDS_FROM, std::max(gw.Read32(SOUNDS_FROM), t + 1));  // every step W runs, the game has run
      a.left = std::chrono::steady_clock::now();
      return true;
    }
    a.parked = true;
    a.cv.notify_all();
    ++a.waits;
    a.cv.wait(lock);
    a.parked = false;
  }
}

void Left()
{
  const std::lock_guard lock(s_a.m);
  s_a.running = false;
  s_a.cv.notify_all();
}

void ThreadStarts()
{
  t_worker = true;
}

bool Hook()
{
  if (t_worker)
    return HookThreaded();
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
  if (t_worker)
  {
    // (the worker's thread: no log here; the game's thread tells it at its next frame)
    const std::lock_guard lock(s_a.m);
    if (!s_a.ended)
      s_a.why = why;
    s_a.ended = true;
    s_a.mid_step = true;
    s_a.cv.notify_all();
    return;
  }
  if (w.job != Job::NONE)
    ++w.gave_up;
  ++w.why[why];
  if (Say("gave up"))
    Notice("fzgx: rollback worker: the job from {} to {} gave up at step {}: {}", w.from, w.to,
           w.g ? w.g->Read32(net::STEP) : 0, why);
  w.job = Job::NONE;
  w.primed = false;  // W stands part way through a step that no log saw end
}

void Quiesce()
{
  net::WorkerQuiesce();
}

void Count(const char* what, u64 n)
{
  w.counts[what] += n;
}

void Stop()
{
  net::WorkerStop();
  if (!On() || Measuring() || w.jobs == 0)
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

void PrepareMemory(Guest& g, Session& s, u32 step, u32 run_from);

// Before the session's restore from `step` to `run_from` (CheckPredictions): a job when the rollback is deep enough.
void WorkerPrepare(Guest& g, Session& s, u32 step, u32 run_from)
{
  if (w.job != Job::NONE)
  {
    ++w.uncompared;  // (a frame that ended before its step L: the race's end cut it)
    w.job = Job::NONE;
  }
  if (!On() || Measuring() || s.practice || s.scrub.forked || step - run_from < w.depth || !s.rollback.Active())
    return;
  const Timed timed{"us preparing"};
  PrepareMemory(g, s, step, run_from);
  if (w.mode == "check")
  {
    const u32 region = s.rollback.Region(), ram_size = w.memory->RamSize();
    w.outside_before.resize(ram_size);
    Outside(region, ram_size, [&](u32 at, u32 size) { std::memcpy(w.outside_before.data() + at, w.memory->ram.data() + at, size); });
  }
  w.from = run_from;
  w.to = step;
  w.job = Job::PREPARED;
}

// W as the memory before R (S1 and S2): the pages either log saw change since the last job, the memory outside the
// log and the locked cache whole, the window; rewound by the game's log and taken as the worker log's moment at R.
void PrepareMemory(Guest& g, Session& s, u32 step, u32 run_from)
{
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
  if (w.jobs <= 2)
    Notice("fzgx: rollback worker: job {} from {} to {} prepared ({} and {} pages taken)", w.jobs, run_from, step,
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
void PublishRows(Session& s, u32 upto);

void WorkerOpen(Session& s, u32 run_from, u32 extra, bool held)
{
  if (s_a.open)
  {
    // S2: the rows of every step to the next frame's hook (run_from + extra + 1), as the session has them now
    PublishRows(s, run_from + extra + 1);
    return;
  }
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
  if (Measuring())
  {
    // the rule's step run again on the game's thread: from the hook before to this one
    const auto now = std::chrono::steady_clock::now();
    if (s_r.hook != std::chrono::steady_clock::time_point{})
    {
      const double ms = std::chrono::duration<double, std::milli>(now - s_r.hook).count();
      s_r.steps_ms += ms;
      ++s_r.steps_n;
      s_r.r_sync += (std::min(ms, FIELD_MS) - s_r.r_sync) / 16;
    }
    s_r.hook = now;
  }
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

// ---- S2: the worker's thread (game's thread side) ----

// The rows of steps R to `upto` - 1 as the session has them now into the job; the first that changed, or one the
// worker has run past `upto`, becomes the step it goes back to.
void PublishRows(Session& s, u32 upto)
{
  Async& a = s_a;
  if (upto <= a.from)
    upto = a.from + 1;
  std::vector<net::Row> rows;
  rows.reserve(upto - a.from);
  for (u32 t = a.from; t < upto; ++t)
    rows.push_back(net::RowFor(s, t));
  const std::lock_guard lock(a.m);
  u32 changed = NONE;
  for (u32 k = 0; k < rows.size() && k < a.rows.size(); ++k)
  {
    if (rows[k] != a.rows[k])
    {
      changed = a.from + k;
      break;
    }
  }
  if (a.at > upto)
    changed = std::min(changed, upto);
  if (changed != NONE && changed < a.at)
  {
    a.rewind = std::min(a.rewind, changed);
    ++s_r.rewinds_asked;
  }
  a.rows = std::move(rows);
  a.target = upto;
  a.cv.notify_all();
}

// The game's thread waits for the worker to have left its job (it stops at its next hook).
void WaitIdle()
{
  Async& a = s_a;
  std::unique_lock lock(a.m);
  if (!a.running)
    return;
  const auto start = std::chrono::steady_clock::now();
  a.stop = true;
  a.cv.notify_all();
  a.cv.wait(lock, [&] { return !a.running; });
  ++s_r.waited_idle;
  s_r.idle_us += std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
}

// The job ends without an adoption: stopped (`why` for the log) or given up.
void CloseJob(const char* why)
{
  Async& a = s_a;
  WaitIdle();
  if (a.ended)
  {
    ++s_r.gave_up;
    ++w.why[a.why];
    if (Say("gave up"))
      Notice("fzgx: rollback worker: the job from {} (started at {}) gave up at step {}: {}", a.from, a.started_at,
             a.at, a.why);
    if (a.mid_step)
      w.primed = false;  // W stands part way through a step that no log saw end
  }
  else if (why != nullptr)
  {
    ++s_r.cancelled;
    ++w.why[why];
  }
  a.open = a.begun = false;
}

// The words an adoption keeps as the game has them: what W cannot have (the VI callbacks' words, which interrupts
// write; the task manager's time words, which the time base writes: the nodes in the lists of either memory now and
// those of W at R, which may have left them since, as the countdown's Starter does at step 421).
std::vector<u32> KeptWords(const Guest& g, const Guest& gw)
{
  std::vector<u32> words = TimeWords(g, gw);
  words.insert(words.end(), s_a.time_words.begin(), s_a.time_words.end());
  words.push_back(VI_COUNT);
  words.push_back(PAD_READY);
  std::sort(words.begin(), words.end());
  words.erase(std::unique(words.begin(), words.end()), words.end());
  return words;
}

// The frame that just ended into its kind's counts (lateness by the late-frame skip's own test).
void CountFrame()
{
  Rule& r = s_r;
  const u32 late = PresentLateFrames();
  const double busy = PresentLastBusyMs();
  if (r.kind >= 0)
  {
    ++r.frames[r.kind];
    r.late[r.kind] += late != r.late_before;
    if (busy >= 0)
    {
      ++r.drawn[r.kind];
      r.busy_sum[r.kind] += busy;
      r.busy_high[r.kind] = std::max(r.busy_high[r.kind], busy);
      // the frame's own cost: its time less its steps run again
      const double own = std::max(0.0, busy - r.steps_ms);
      if (r.kind == K_PLAIN || r.kind == K_OPEN)
      {
        r.c_high = !r.c_seen ? own : std::max(own, r.c_high + (own - r.c_high) / 32);
        r.c_seen = true;
      }
    }
  }
  r.late_before = late;
  r.kind = K_PLAIN;
  r.steps_ms = 0;
  r.steps_n = 0;
}

// At CheckPredictions' start (after the frame's SaveStep(A)): the frame's count, the open job's adoption when the
// worker stands at A with the rows of now, else its news. The step a sync rollback must go back to when the job
// ended without an adoption (given up, too long, or the race's end checked); none else.
std::optional<u32> WorkerFrame(Guest& g, Session& s, u32 step, bool settle, bool check)
{
  if (!Measuring())
    return std::nullopt;
  CountFrame();
  s_r.hook = std::chrono::steady_clock::now();
  s_r.fall_back = false;
  Async& a = s_a;
  if (!a.open)
    return std::nullopt;
  s_r.kind = K_OPEN;
  ++a.frames;
  const u32 from = a.from;
  if (!a.begun || settle || check)
  {
    CloseJob(!a.begun ? "a job not begun" : "the race's end checked");
    s_r.kind = K_DEEP_SYNC;
    s_r.fall_back = true;
    return from;
  }
  bool adopt = false;
  {
    // the rows of the steps to A as the session has them now
    std::vector<net::Row> rows;
    for (u32 t = a.from; t < step; ++t)
      rows.push_back(net::RowFor(s, t));
    std::unique_lock lock(a.m);
    bool same = rows.size() <= a.rows.size();
    for (std::size_t k = 0; same && k < rows.size(); ++k)
      same = rows[k] == a.rows[k];
    adopt = !a.ended && a.parked && a.at == step && a.rewind == NONE && same;
  }
  if (adopt)
  {
    const auto start = std::chrono::steady_clock::now();
    Guest& gw = *w.g;
    const std::vector<u32> keep = KeptWords(g, gw);
    {
      const std::lock_guard lock(a.m);  // (the worker waits at A's hook)
      s.rollback.TakeOver(g, w.log, gw, from, step, &keep);
      for (u32 t = from; t < step; ++t)
      {
        s.used[t] = a.rows[t - from];
        if (s_forget)
          s_forget(t);
      }
      a.stop = true;
      a.cv.notify_all();
    }
    a.open = a.begun = false;
    ++s_r.adopted;
    s_r.j_sum += a.frames;
    s_r.j_pred_sum += a.j_pred;
    s_r.j_high = std::max<u64>(s_r.j_high, a.frames);
    ++s_r.j_seen[a.frames];
    s_r.kind = K_ADOPT;
    s_r.adopt_us += std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
    if (s_r.adopted <= 3)
      Notice("fzgx: rollback worker: job from {} (started at {}, depth {}) adopted at {}, {} frames after its start "
             "({} foreseen)", from, a.started_at, a.depth, step, a.frames, a.j_pred);
    return std::nullopt;
  }
  bool ended;
  {
    const std::lock_guard lock(a.m);
    ended = a.ended;
  }
  if (ended || a.frames > a.j_pred + 4 || step - from > MOST_SPAN)
  {
    if (!ended)
      ++s_r.too_long;
    CloseJob(ended ? nullptr : "too long");
    s_r.kind = K_DEEP_SYNC;
    s_r.fall_back = true;
    return from;
  }
  PublishRows(s, std::max(step, std::min(a.target, step + 1)));
  return std::nullopt;
}

// While a job is open, the predictions are compared below its R only: the job's own steps take theirs from the
// session at every frame (PublishRows).
u32 WorkerSearchTo(u32 arrived)
{
  return s_a.open ? std::min(arrived, s_a.from) : arrived;
}

// A correction at `wrong` while a job is open: the worker takes it (true: it goes back there itself), or, below the
// job's R, the job is stopped and the correction is the session's (false).
bool WorkerTakes(Session& s, u32 step, u32 wrong)
{
  Async& a = s_a;
  if (!a.open)
    return false;
  if (wrong >= a.from)
  {
    const std::lock_guard lock(a.m);
    if (wrong < a.at)
      a.rewind = std::min(a.rewind, wrong);
    a.cv.notify_all();
    ++s_r.rewinds_asked;
    return true;
  }
  CloseJob("a correction below the job's start");
  (void)s;
  (void)step;
  return false;
}

// The rule at a rollback from `step` back to `wrong` with no job open: true, a job starts on the worker's thread (no
// restore: the game goes on with its predicted steps); false, the session's sync rollback as ever.
bool WorkerStart(Guest& g, Session& s, u32 step, u32 wrong)
{
  if (!Measuring())
    return false;
  const u32 d = step - wrong, least = std::max<u32>(TheOptions().debug.worker_depth, 1);
  s_r.kind = d >= least ? K_DEEP_SYNC : K_SHALLOW;
  if (!Threaded() || s_r.fall_back || s.practice || s.scrub.forked || !s.rollback.Active() || d < least)
    return false;
  static const unsigned cores = std::thread::hardware_concurrency();
  const boot::Match* m = boot::Get();
  if (cores < LEAST_CORES || (m && m->game_mode))
  {
    ++s_r.barred_other;
    return false;
  }
  if (wrong < COUNTDOWN_END)
  {
    ++s_r.barred_countdown;
    return false;
  }
  // Not once a machine has finished: the race's end can then fall in the job, and W's frame routine would cut its
  // steps there and go on to the frame's drawing (frame_entry.py `extra_cut`), whose pipe is the game's.
  {
    const u32 module = g.Read32(net::MODULE_POINTER);
    const u32 count = g.Read8(module + net::MACHINE_COUNT), machines = g.Read32(module + net::MACHINES);
    for (u32 i = 0; machines != 0 && i < count && i < 64; ++i)
    {
      if ((g.Read32(machines + i * game::MACHINE_SIZE + game::MACHINE_FLAGS) & game::MACHINE_FLAG_FINISHED) != 0)
      {
        ++s_r.barred_finish;
        return false;
      }
    }
  }
  if (d + 2 > MOST_SPAN)
  {
    ++s_r.barred_other;
    return false;
  }
  const double r = s_a.r_ms > 0 ? s_a.r_ms : s_r.r_sync;
  if (TheOptions().debug.worker_rule != "always" && s_r.c_high + d * s_r.r_sync + MARGIN_MS <= FIELD_MS)
  {
    ++s_r.sync_fit;
    return false;
  }
  if (r >= FIELD_MS - MARGIN_MS)
  {
    ++s_r.barred_other;
    return false;
  }
  const auto start = std::chrono::steady_clock::now();
  WaitIdle();
  PrepareMemory(g, s, step, wrong);
  Async& a = s_a;
  {
    const std::lock_guard lock(a.m);
    a.from = wrong;
    a.target = wrong;
    a.rewind = NONE;
    a.stop = a.ended = a.mid_step = a.parked = false;
    a.why.clear();
    a.rows.clear();
    a.seats = s.seats;
    a.at = wrong;
  }
  a.time_words = TimeWords(*w.g, *w.g);
  a.open = true;
  a.begun = false;
  a.started_at = step;
  a.depth = d;
  a.frames = 0;
  a.j_pred = static_cast<u32>(std::ceil((d * r + START_MS) / (FIELD_MS - r)));
  ++s_r.jobs;
  s_r.d_sum += d;
  s_r.kind = K_START;
  s_r.start_us += std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
  if (s_r.jobs <= 2)
    Notice("fzgx: rollback worker: job {} on the worker's thread from {} at {} (depth {}; r {:.2f} ms, j {} foreseen)",
           s_r.jobs, wrong, step, d, r, a.j_pred);
  return true;
}

// Steps below `known` become final; not the open job's (its entries and checksums are the worker's to give).
u32 WorkerKnown(u32 known)
{
  return s_a.open ? std::min(known, s_a.from) : known;
}

// The game stops: the job stopped (the adapter joins the thread after) and the S2 line.
void WorkerQuiesce()
{
  if (s_a.open)
    CloseJob("the game stopped");
  WaitIdle();
}

void WorkerStop()
{
  WorkerQuiesce();
  if (!Measuring())
    return;
  CountFrame();
  const Rule& r = s_r;
  std::string kinds;
  for (int k = 0; k < Rule::KINDS; ++k)
  {
    if (r.frames[k] == 0)
      continue;
    kinds += std::format(" [{}: {} frames, {} late, drawn {} at {:.2f} ms on average, {:.2f} at most]", KIND_NAMES[k],
                         r.frames[k], r.late[k], r.drawn[k], r.drawn[k] ? r.busy_sum[k] / r.drawn[k] : 0.0,
                         r.busy_high[k]);
  }
  std::string js;
  for (const auto& [j, n] : r.j_seen)
    js += std::format(" {}:{}", j, n);
  Notice("fzgx: rollback worker S2: {} mode, depth {}, {} cores; jobs {} (depth {:.1f} on average), adopted {}, gave "
         "up {}, stopped {} ({} too long), corrections taken {}; barred: countdown {}, a finish {}, other {}; sync as it fits {}; j "
         "seen{} (average {:.2f}, foreseen {:.2f}, at most {}); worker {} steps, {} went back, a step {:.2f} ms (the "
         "game's {:.2f}), a frame's own {:.2f} ms; game's thread: starts {:.0f} us, begins {:.0f} us, adoptions {:.0f} "
         "us, waits for the worker {} ({:.0f} us)",
         TheOptions().debug.worker_mode, std::max<u32>(TheOptions().debug.worker_depth, 1),
         std::thread::hardware_concurrency(), r.jobs, r.jobs ? double(r.d_sum) / r.jobs : 0.0, r.adopted, r.gave_up,
         r.cancelled, r.too_long, r.rewinds_asked, r.barred_countdown, r.barred_finish, r.barred_other, r.sync_fit, js,
         r.adopted ? double(r.j_sum) / r.adopted : 0.0, r.adopted ? double(r.j_pred_sum) / r.adopted : 0.0, r.j_high,
         s_a.steps, s_a.rewinds, s_a.r_ms, r.r_sync, r.c_high, r.start_us, r.begin_us, r.adopt_us, r.waited_idle,
         r.idle_us);
  Notice("fzgx: rollback worker S2: frames by kind:{}", kinds);
  std::string counts;
  for (const auto& [text, n] : w.counts)
    counts += std::format(" {} {}", text, n);
  Notice("fzgx: rollback worker S2: W {} jobs prepared, {} pages copied;{}", w.jobs, w.pages_copied, counts);
  std::string why;
  for (const auto& [text, n] : w.why)
    why += std::format(" [{}: {}]", text, n);
  if (!why.empty())
    Notice("fzgx: rollback worker S2: jobs ended without an adoption:{}", why);
}
}  // namespace fzgx::net
