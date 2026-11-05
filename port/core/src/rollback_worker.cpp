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
#include <cstdlib>
#include <cstring>
#include <format>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "files.h"
#include "internal.h"
#include "mode.h"
#include "net_internal.h"
#include "rollback_worker_guard.h"
#include "rollback_worker_kept.h"
#include "rollback_worker_pace.h"
#include "rollback_worker_pair.h"

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
// The countdown's 0 is step 421 in every race (count 420 at the race start, race-map-flow.md 4.1). Before it, steps
// 364 to 405 write the drawing's pipe (GXSetProjectionv, S1). With a module whose pipe is one pointer for every thread
// (DolRecomp before its per-thread pipe) a store there on W would land in the game's pipe: then a job starts only
// after the countdown's end (and before any machine has finished). With the pipe per thread W's bytes are dropped.
constexpr u32 COUNTDOWN_END = 422;
// A - R at most for an adoption: TakeOver hands over the input and checksum rings' last 1024 steps, so far below that
// on every path. A job's start is barred deeper than "catchUp" (60), and an open job past it is caught up sync
// (S4; was 30 for the start, lanes_partial.h KEEP, but a run not kept only means that step re-runs whole).
constexpr u32 MOST_ADOPT = 512;
static_assert(MOST_ADOPT < 1024 && MOST_ADOPT > 256, "far below the rings, above the largest \"catchUp\"");
constexpr u32 LEAST_CORES = 4;    // below it, no thread (study section 7, decision 5)
constexpr double FIELD_MS = 1000.0 * 1001.0 / 60000.0;
constexpr double MARGIN_MS = 1.5;      // run-ahead's MARGIN_MS: room kept before the field
constexpr double START_MS = 0.5;       // c of the study's section 4: a job's start and the thread's wake
constexpr double FIRST_CONST_MS = 5.4; // a frame's own cost before one is measured (study section 4)
constexpr double FIRST_STEP_MS = 2.0;  // a step run again before one is measured
// S3: the code outside the log, which no step writes (the DOL's text, main.rel's text, constructors and read-only
// data up to its data, where the log's region starts; whole pages): taken into W with the whole memory, then only
// compared at every CODE_CHECK-th job, the pages that differ (none expected: the native module cannot follow changed
// code) copied and counted in the log's line.
constexpr u32 DOL_TEXT_FROM = 0x80004000, DOL_TEXT_TO = 0x8008F000;  // (DOL text 0x80003100 to 0x8008FF00)
constexpr u32 REL_CODE_FROM = (game::REL_TEXT_AT + 0xFFF) & ~0xFFFu, REL_CODE_TO = game::REL_DATA_AT & ~0xFFFu;
constexpr u32 CODE_CHECK = 64;
// The words an adoption keeps (the VI callbacks', the loads' callbacks', the time words, the frame routine's): one
// table, rollback_worker_kept.cpp (study section 18).
// The frame routine's own words (S3, async-check): what its `filters` (frame_replay.py) writes back from the copies the
// frame's entry saved, right after the first step's network hook and again before the frame's last step: the
// simulation manager's ([.bss 0xDAC]) list mask (+0x10) and task filter (+0x14), and .data 0x3599C. Extra steps run
// with the filter at 1 and 0x3599C at 0 (`replay_filter`), and W runs every step as an extra step and never reaches
// the frame's last, so W holds those values where the game holds the restored ones (the fingerprint: 0x3599C 1 here,
// 0 on W; the filter 0 here, 1 on W). They are a frame's state, not a step's: the adoption runs at the first step's
// hook, and `call1` runs `filters` before any step, so the game's value at a hook is the sync rollback's.
constexpr u32 FRAME_MANAGER = game::REL_BSS_AT + 0xDAC;  // (its +0x10 and +0x14: rollback_worker_kept.cpp)
constexpr u32 FRAME_PRESENT = game::REL_DATA_AT + 0x3599C;

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
  void Log(LogLevel level, const std::string& text) override { TheHostAnyThread().Log(level, text); }
  bool ReadGameFile(const std::string&, std::vector<u8>*) override { return false; }
  u64 PageHash(const u8* data, std::size_t size) override { return TheHostAnyThread().PageHash(data, size); }
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
  u32 game_starts = 0;  // the game's log's Starts when W's log last started (a new count: a new race's log)
  Job job = Job::NONE;
  u32 from = 0, to = 0;
  u32 rings_from = 0;  // the checksums and rows compared from here (S4, "async-check": the chain's first R)
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
  // the second worker (study section 20.5): the game's dirty pages taken by the other slot's starts since this slot's
  // last (TakeDirty clears the game log's bits once; each slot keeps its own pending set), and the pages a Forward
  // wrote into this memory (they differ from the game's until taken again)
  std::vector<u32> pending;
  bool keeps_after = false;  // its log keeps after-images (Rollback::KeepAfter at the job's start)
};
// Two slots, one kind of worker (study section 20.2): slot 0 is W (S1's jobs on the game's thread too), slot 1 W2.
// The run's sums (the jobs' reasons, the counts, the last line) are slot 0's.
constexpr int SLOTS = 2;
Worker s_ws[SLOTS];
Worker& w = s_ws[0];

// S2: a job on the worker's thread. The game's thread owns everything but what the mutex guards while the worker runs.
struct Async
{
  std::mutex m;
  std::condition_variable cv;
  // the game's thread, read by the worker under the mutex
  u32 from = 0, target = 0, rewind = NONE;
  bool stop = false;
  std::vector<net::Row> rows;  // the rows of steps from `from` on, as the session has them now
  std::vector<net::Row> ran;   // the rows W's steps from `from` on took (written into W's ring): what an adoption
                               // hands the session and the replay, so W's steps are checked by the rows they ran on
  std::vector<int> seats;
  // the worker, read by the game's thread under the mutex
  bool running = false;  // the worker's thread is in the job (from Begin until it has left it)
  bool parked = false;   // it waits at the hook of `at`
  u32 at = 0;
  bool ended = false;    // it gave up
  bool mid_step = false; // ... part way through a step
  std::string why;
  double r_ms = 0;       // the worker's step lately
  double ran_sum_ms = 0;  // every step's time summed, and their count (the S2 line's mean)
  u64 ran_n = 0;
  std::vector<float> window;  // the step times since the last rate measurement (the slow worker's switch, WatchRate)
  u64 steps = 0, rewinds = 0, waits = 0;
  std::chrono::steady_clock::time_point left{};  // the hook last returned (the worker's)
  // the game's thread's own
  bool open = false, begun = false;
  u32 started_at = 0, depth = 0, frames = 0, j_pred = 0;
  std::vector<u32> time_words;  // the task nodes' time words of W at R (nodes that leave the lists in the job)
  u32 moves = 0;  // S3: the times this job (and the jobs it started again from) was sent back
  u64 since = 0;          // S4: the frame of the oldest correction this job runs (its delay at the adoption)
  bool below = false;     // S4: a correction below R was left to the chain (the job's base is not the session's)
  u32 perturb_at = NONE;  // tests ("perturb"): W's row of this step altered, as a misprediction would be
  // the second worker (study section 20): as O, the first of its rows that changed under a step it had run (N's c;
  // NONE: none); as N, the open job's rows from its R to N's c (N's ring holds them: Begin writes them)
  u32 late = NONE;
  u32 base_from = 0;
  std::vector<net::Row> base_rows;
  std::vector<u32> base_sums;  // ... and O's checksums of those steps
  bool newer = false;   // this job is N, derived from the other slot's open job (until that one is closed)
  bool second = false;  // this job was started as N (the S6 line's counts)
  bool second_tried = false;  // a job N was started from this one (one at most: its late row is then the chain's)
};
Async s_as[SLOTS];
// Close: no job begins on either worker's thread (under s_as[0].m)
bool s_closed = false;
bool s_pipe_per_thread = false;  // the module's pipe is per thread (SetPipePerThread)
thread_local int t_slot = -1;    // this is a worker's thread: its slot
// The game's thread: the slot of the open job when one is open, of O when two are (the other's is N)
int s_older = 0;
unsigned (*s_usable_threads)() = nullptr;  // the adapter's UsableThreads (SetUsableThreads)

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
  u64 jobs = 0, adopted = 0, gave_up = 0, cancelled = 0, rewinds_asked = 0, sync_fit = 0,
      barred_countdown = 0, barred_other = 0, barred_finish = 0, waited_idle = 0;
  u64 j_sum = 0, j_pred_sum = 0, j_high = 0, d_sum = 0;
  std::map<u32, u64> j_seen;
  double start_us = 0, adopt_us = 0, begin_us = 0, idle_us = 0;
  u64 adopt_words = 0;
  // S3: the restarts ("restarts")
  u32 restart_moves = 0;  // a job just stopped for a correction below its R: its moves + 1, for the job after it
  std::map<u32, u64> moves_adopted, moves_fell, fell_depth;
  u64 fell = 0, fell_depth_sum = 0;
  u64 unwatched = 0;  // jobs given up at their park for a page neither log watched
  // S4, chaining: a row that changes under a step W has run no longer sends W back; the job is adopted as it ran
  // (`used` and the replay's ring hold the rows its steps ran on), and the session's own check then finds the step
  // and the next job starts from it. A chain: from the frame a correction first missed a job until an adoption whose
  // rows are the session's (or a frame with no job open after one).
  u64 frame_no = 0;
  bool chain = false;
  u64 chain_since = 0;
  u32 chain_from = 0;  // the chain's first R ("async-check": the sync rollback it is compared with runs from here)
  u64 slow = 0;  // jobs started with the worker's step at a frame or slower (the chain cannot catch up)
  u64 provisional = 0, clean = 0, chains = 0, chain_frames = 0, chain_high = 0, chained_jobs = 0, deferred = 0;
  u32 behind_high = 0;  // steps from a job's R to its adoption, at most
  std::map<u64, u64> chain_seen;
  // a correction's delay: from the frame a row first missed a job (or a job started for it) to the adoption of the
  // job that runs it (frames; the next job's own frames included)
  u64 late_since = ~u64{0};  // the first frame a late row was seen since the last job's start
  std::map<u64, u64> delay_seen;
  u64 delay_sum = 0, delay_n = 0, delay_high = 0;
  // S4: a step the chain owes a rerun from that the session's check cannot find (a forced rollback, tests, on a step W
  // has run or below R; no row differs): the next job starts there right after the adoption, or the sync rollback of
  // a job that ends without one goes back there. Steps from it stay open.
  u32 owed = NONE;
  u64 owed_n = 0, forced_ahead = 0;
  // S4, the bounds: catch-ups (a job past "catchUp" stopped, its rollback sync) and the slow worker's switch
  u64 catch_ups = 0, catch_up_depth_high = 0, barred_slow = 0, barred_deep = 0;
  u64 barred_late = 0;  // starts barred only by their foreseen frames (d within the bound): catch-ups foreseen
  pace::Pace pace;      // the slow worker's switch and the trial of W's estimate (WatchRate); pace.switched: the
                        // race's rollbacks run sync (W too slow) until the retry
  // "async-check"
  u64 checked = 0, check_memory = 0, check_words = 0, check_moments = 0, check_rings = 0, check_kept = 0;
  u32 frame_manager = 0;  // [.bss 0xDAC] at the last comparison (the frame's words' manager)
  bool check_pending = false;   // "async-check": the adoption of [check_from, check_to) to compare at check_to's hook
  u32 check_from = 0, check_to = 0;
  u32 check_job_from = 0;       // the job's own R (W's log's moments start there; the chain's earlier ones are adopted)
  int check_slot = 0;           // the slot whose memory is compared (S1's jobs: 0)
  // S6, the second worker (study section 20): its starts, its adoptions (in the hook of O's too), its jobs stopped
  // with O, closed with O running (their row back to the chain), ended alone; O's adoptions clean as far as N
  // carried them; the starts barred, by reason; the workers and usable threads at the last start weighed
  u64 second_starts = 0, second_adopted = 0, second_same_hook = 0, second_stopped = 0, second_back = 0,
      second_fell = 0, older_through = 0, second_forward_failed = 0;
  std::map<std::string, u64> second_barred;
  u32 workers_seen = 0;
  unsigned threads_seen = 0;
  double second_start_us = 0;
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

// (TimeWords: rollback_worker_kept.cpp, the table's time rows)

// The memory outside the log: below it (the executable, the OS, the threads' stacks) and above it (the display
// buffers), less the generator word, which the log puts back.
template <typename Each>
void Outside(u32 region, u32 ram_size, Each&& each)
{
  each(0u, region - RAM);
  each(net::STATE_END - RAM, ram_size - (net::STATE_END - RAM));
}
}  // namespace

std::string ModeFor(const std::string& key, const char* env, unsigned cores)
{
  if (!key.empty())
    return key;
  if (env != nullptr && (std::string_view(env) == "off" || std::string_view(env) == "on"))
    return env;
  return cores >= LEAST_CORES ? "on" : "off";
}

const std::string& Mode()
{
  static const char* const env = std::getenv("FZGX_ROLLBACK_WORKER");
  static const unsigned cores = std::thread::hardware_concurrency();
  thread_local std::string key, mode;  // (the options' mode, and what it gave)
  if (mode.empty() || key != TheOptions().debug.worker_mode)
  {
    key = TheOptions().debug.worker_mode;
    mode = ModeFor(key, env, cores);
  }
  return mode;
}

bool On()
{
  return Mode() != "off";
}

// S2's thread: "on"; "sync" measures the same frames with no worker (the baseline).
bool Threaded()
{
  return Mode() == "on" || Mode() == "async-check";
}

bool Measuring()
{
  return Threaded() || Mode() == "sync";
}

// The workers this game runs now (study section 20.2): the launch file's "workers", else FZGX_ROLLBACK_WORKERS, else
// 2 from 6 usable threads (the adapter's UsableThreads: the hardware's count and the process's affinity, read again at
// each start, so a pinning after the launch turns the second off).
u32 Workers()
{
  static const char* const env = std::getenv("FZGX_ROLLBACK_WORKERS");
  const unsigned threads = s_usable_threads != nullptr ? s_usable_threads() : std::thread::hardware_concurrency();
  s_r.threads_seen = threads;
  return s_r.workers_seen = pair::WorkersFor(TheOptions().debug.worker_workers, env, threads);
}

// S3's "async-check": every adoption is compared word for word with the sync rollback from its R instead.
static bool AsyncCheck()
{
  return Mode() == "async-check";
}

void SetPipePerThread(bool per_thread)
{
  s_pipe_per_thread = per_thread;
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
  return s_ws[t_slot >= 0 ? t_slot : 0].window;
}

int Slot()
{
  return t_slot;
}

void SetUsableThreads(unsigned (*threads)())
{
  s_usable_threads = threads;
}

bool Begin(int slot, Memory* memory)
{
  if (slot < 0 || slot >= SLOTS)
    return false;
  Async& a = s_as[slot];
  Worker& x = s_ws[slot];
  if (a.open && !a.begun)
  {
    // the job's start (S2): W's window from the game's at this hook, the step word and the row of R, the frame's
    // further steps kept high; N's ring holds O's rows from O's R to N's c (study section 20.1)
    const auto start = std::chrono::steady_clock::now();
    Guest& gw = *x.g;
    x.window.Restore(Data().Snapshot());
    gw.Write32(net::STEP, a.from);
    {
      const std::lock_guard lock(a.m);
      for (std::size_t k = 0; k < a.base_rows.size(); ++k)
        Rollback::UseInputs(gw, a.base_from + static_cast<u32>(k), a.seats, a.base_rows[k].data());
      for (std::size_t k = 0; k < a.base_sums.size(); ++k)
        Rollback::KeepChecksum(gw, a.base_from + static_cast<u32>(k), a.base_sums[k]);
      a.ran.clear();
      if (!a.rows.empty())
      {
        a.ran.push_back(a.rows[0]);
        Rollback::UseInputs(gw, a.from, a.seats, a.ran[0].data());
      }
    }
    gw.Write32(EXTRA_STEPS, W_EXTRA);
    {
      // (s_closed is under slot 0's mutex, taken first whenever both are: Close cannot miss a thread that starts)
      std::unique_lock closed(s_as[0].m);
      std::unique_lock lock(a.m, std::defer_lock);
      if (slot != 0)
        lock.lock();
      if (s_closed)
        return false;  // (the emulator stops: the job stays not begun, and the next frame, if any, closes it)
      a.running = true;
      a.at = a.from;
      a.left = std::chrono::steady_clock::now();
    }
    a.begun = true;
    memory->ram = x.memory->ram.data();
    memory->ram_size = x.memory->RamSize();
    memory->cache = x.memory->cache.data();
    memory->cache_size = static_cast<u32>(x.memory->cache.size());
    memory->threaded = true;
    s_r.begin_us += std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
    return true;
  }
  if (slot != 0 || w.job != Job::READY)
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
  Async& a = s_as[t_slot];
  Worker& x = s_ws[t_slot];
  Guest& gw = *x.g;
  u32 t = gw.Read32(net::STEP);
  const Debug& debug = TheOptions().debug;
  if (debug.worker_delay_us != 0 && t >= debug.worker_delay_from && (debug.worker_delay_to == 0 || t < debug.worker_delay_to))
    std::this_thread::sleep_for(std::chrono::microseconds(debug.worker_delay_us));  // (tests, "delayUs": a slow PC's worker)
  if (t > 0)
    Rollback::KeepChecksum(gw, t - 1, Rollback::Checksum(gw));
  std::unique_lock lock(a.m);
  if (t <= a.from)
  {
    a.why = std::format("the steps went another way (step {} in a job from {})", t, a.from);
    a.ended = true;
    a.mid_step = true;  // W stands after a step no log saw end
    a.cv.notify_all();
    return false;
  }
  // W's log has no guard and compares only its watched pages, so a page neither log watched (a fresh heap page first
  // written inside the job) would have no entries: a later restore inside the job's steps would leave it as it is
  // now, and TakeOver would not name it. Before W waits at its target, every page is compared once (the scan a
  // frame's own step gets). A page W's log first watches inside a job (found there, or in its learning steps, which
  // compare every page) gives the job up (the sync rollback runs), and is watched from then on.
  const bool park = t >= a.target;
  lock.unlock();
  const u32 watched = x.log.WatchedPages();
  x.log.SaveStep(gw, t, park);
  const bool unwatched = x.log.WatchedPages() != watched;
  // the step's time: its run, its checksum and its undo entry (SaveStep), from the hook's last return
  double ran_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - a.left).count();
  lock.lock();
  if (unwatched)
  {
    ++s_r.unwatched;
    a.why = "a page neither log watched";
    a.ended = true;
    a.cv.notify_all();
    return false;
  }
  if (t > a.from + 1 || a.steps != 0)
  {
    thread_local bool spiked = false;  // (each worker's thread's own)
    if (debug.worker_spike_us != 0 && !spiked && t >= debug.worker_spike_at)
    {
      spiked = true;  // (tests, "spikeUs": one slow step, the PC busy for a moment, taken into W's measured steps)
      ran_ms += debug.worker_spike_us / 1000.0;
      Notice("fzgx: rollback worker: test spike of {} us at W's step {}", debug.worker_spike_us, t);
    }
    a.r_ms = a.r_ms == 0 ? ran_ms : a.r_ms + (ran_ms - a.r_ms) / 8;
    a.ran_sum_ms += ran_ms;
    ++a.ran_n;
    a.window.push_back(static_cast<float>(ran_ms));
  }
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
        a.ran.resize(std::min<std::size_t>(a.ran.size(), c - a.from));
        lock.unlock();
        x.log.RestoreTo(gw, t, c);
        lock.lock();
        t = c;
        a.at = t;
        continue;
      }
    }
    if (t < a.target && t - a.from < a.rows.size())
    {
      net::Row row = a.rows[t - a.from];
      if (t == a.perturb_at && !row.empty())
      {
        // tests ("perturb"): the first seat's record altered (its stick and buttons), as a wrong prediction; once,
        // so the job is adopted on it and the next job of the chain runs the session's row
        a.perturb_at = NONE;
        for (std::size_t k = 0; k < std::min<std::size_t>(row.size(), net::RECORD); ++k)
          row[k] ^= 0x41;
      }
      a.ran.resize(t - a.from);  // (the rows of the steps below t stand: a send-back to below t was taken above)
      a.ran.push_back(row);
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

void Left(int slot)
{
  Async& a = s_as[slot];
  const std::lock_guard lock(a.m);
  a.running = false;
  a.cv.notify_all();
}

void ThreadStarts(int slot)
{
  t_slot = slot;
}

bool OnW()
{
  return t_slot >= 0;
}

bool Hook()
{
  if (t_slot >= 0)
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
  // (S3: on the worker's thread too; mode.cpp keeps a race's state in the guest's block, and W's run says nothing)
  if (t_slot >= 0)
    mode::Step(*s_ws[t_slot].g, true);
  else if (w.job == Job::RUNNING)
    mode::Step(*w.g, true);
}

void GiveUp(const std::string& why, int slot)
{
  if (t_slot >= 0 || (slot >= 0 && slot < SLOTS && s_as[slot].open && s_as[slot].begun))
  {
    // the worker's thread (no log here; the game's thread tells it at its next frame), or a threaded job given up on
    // the game's thread (the module's dispatch not found): told as the worker's own, so the next frame closes it and
    // runs its rollback sync (W's window and step word are the job's: no log saw them)
    Async& a = s_as[t_slot >= 0 ? t_slot : slot];
    const std::lock_guard lock(a.m);
    if (!a.ended)
      a.why = why;
    a.ended = true;
    a.mid_step = true;
    a.cv.notify_all();
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

void Close()
{
  {
    const std::lock_guard lock(s_as[0].m);
    s_closed = true;
  }
  for (Async& a : s_as)
  {
    std::unique_lock lock(a.m);
    a.stop = true;
    a.cv.notify_all();
    a.cv.wait(lock, [&] { return !a.running; });
  }
}

void Count(const char* what, u64 n)
{
  w.counts[what] += n;
}

void Stop()
{
  net::WorkerStop();
  {
    const std::lock_guard lock(s_as[0].m);
    s_closed = false;
  }
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

void PrepareMemory(Guest& g, Session& s, u32 step, u32 run_from, int slot);

// Before the session's restore from `step` to `run_from` (CheckPredictions): a job when the rollback is deep enough.
void WorkerPrepare(Guest& g, Session& s, u32 step, u32 run_from)
{
  for (Worker& x : s_ws)
  {
    if (x.job != Job::NONE)
    {
      ++w.uncompared;  // (a frame that ended before its step L: the race's end cut it)
      x.job = Job::NONE;
    }
  }
  s_r.check_slot = std::exchange(s_r.check_pending, false) && run_from == s_r.check_from && step == s_r.check_to
                       ? s_r.check_slot
                       : -1;
  if (s_r.check_slot >= 0)
  {
    // "async-check": the sync rollback of an adoption, compared with W at the hook of A (WorkerCompare); S4: from the
    // chain's first R, W's log's moments from its own job's
    Worker& x = s_ws[s_r.check_slot];
    x.from = s_r.check_job_from;
    x.rings_from = run_from;
    x.to = step;
    x.job = Job::RAN;
    return;
  }
  s_r.check_slot = 0;
  if (!On() || Measuring() || s.practice || s.scrub.forked || step - run_from < w.depth || !s.rollback.Active())
    return;
  const Timed timed{"us preparing"};
  PrepareMemory(g, s, step, run_from, 0);
  if (w.mode == "check")
  {
    const u32 region = s.rollback.Region(), ram_size = w.memory->RamSize();
    w.outside_before.resize(ram_size);
    Outside(region, ram_size, [&](u32 at, u32 size) { std::memcpy(w.outside_before.data() + at, w.memory->ram.data() + at, size); });
  }
  w.from = w.rings_from = run_from;
  w.to = step;
  w.job = Job::PREPARED;
}

// A slot's memory as the game's before R (S1 and S2): the pages either log saw change since the slot's last job (the
// game's log's taken by either slot's start, each slot's pending set), the memory outside the log and the locked
// cache whole, the window; rewound by the game's log. Its log started again when the game's was (a new race). The
// pages copied (for the log line).
std::size_t TakeTheGame(Guest& g, Session& s, u32 step, u32 run_from, int slot)
{
  Worker& x = s_ws[slot];
  if (w.mode.empty())
  {
    w.mode = Mode();
    w.depth = std::max<u32>(TheOptions().debug.worker_depth, 1);
  }
  Host& host = g.GetHost();
  if (!x.memory)
  {
    x.memory = std::make_unique<WorkerMemory>(host.RamSize(), host.ScratchSize());
    x.g = std::make_unique<Guest>(*x.memory, x.window);
  }
  ++x.jobs;
  const u32 region = s.rollback.Region(), ram_size = x.memory->RamSize();
  u8* live = host.Ram();
  u8* mine = x.memory->ram.data();
  // W: the pages either log may have seen change since the last job (all of it the first time, or when the race's
  // log started again; each page once), the memory outside the log and the locked cache whole, the window. S3
  // ("start": "changed", the default): a page is compared first and copied only when it differs. The memory outside
  // the log is mostly code and data no step writes: compared, it costs two reads a page where a copy costs a read, a
  // write and the write's own read, and W's lines stay as they are.
  const bool changed = TheOptions().debug.worker_start != "whole";
  u64* copied = &w.pages_copied;
  const auto take = [&](u32 at, u32 size) {
    if (!changed)
    {
      std::memcpy(mine + at, live + at, size);
      w.pages_copied += size / PAGE;
      *copied += size / PAGE;
      return;
    }
    for (u32 end = at + size; at < end; at += PAGE)
    {
      const u32 n = std::min<u32>(PAGE, end - at);
      if (std::memcmp(mine + at, live + at, n) != 0)
      {
        std::memcpy(mine + at, live + at, n);
        ++w.pages_copied;
        ++*copied;
      }
    }
  };
  // outside the log, the code apart: compared only at every CODE_CHECK-th job ("whole": copied as all the rest)
  const bool code_now = !changed || x.jobs % CODE_CHECK == 0;
  const auto outside = [&](u32 at, u32 size) {
    const u32 end = at + size;
    for (const auto& [from, to] : {std::pair{DOL_TEXT_FROM - RAM, DOL_TEXT_TO - RAM}, std::pair{REL_CODE_FROM - RAM, REL_CODE_TO - RAM}})
    {
      if (from >= end || to <= at)
        continue;
      if (from > at)
        take(at, from - at);
      if (code_now)
      {
        copied = &w.counts["pages: code copied (differed)"];
        take(std::max(at, from), std::min(end, to) - std::max(at, from));
        copied = &w.counts["pages: outside copied"];
      }
      at = std::min(end, to);
    }
    if (at < end)
      take(at, end - at);
  };
  std::vector<u32> pages;
  {
    const Timed timed{"us start: dirty pages taken"};
    const std::vector<u32> dirty = s.rollback.TakeDirty();
    const std::vector<u32> worker_dirty = x.log.Active() ? x.log.TakeDirty() : std::vector<u32>();
    // (the game's bits are cleared once: the other slot keeps them for its next start)
    for (Worker& other : s_ws)
    {
      if (&other == &x || !other.primed)
        continue;
      std::vector<u32> both;
      both.reserve(other.pending.size() + dirty.size());
      std::set_union(other.pending.begin(), other.pending.end(), dirty.begin(), dirty.end(), std::back_inserter(both));
      other.pending = std::move(both);
    }
    std::vector<u32> mine_dirty;
    mine_dirty.reserve(dirty.size() + x.pending.size());
    std::set_union(dirty.begin(), dirty.end(), x.pending.begin(), x.pending.end(), std::back_inserter(mine_dirty));
    x.pending.clear();
    pages.reserve(mine_dirty.size() + worker_dirty.size());
    std::set_union(mine_dirty.begin(), mine_dirty.end(), worker_dirty.begin(), worker_dirty.end(),
                   std::back_inserter(pages));
    w.counts["pages named (game's log)"] += dirty.size();
    w.counts["pages named (W's log)"] += worker_dirty.size();
    w.counts["pages named (both, once)"] += pages.size();
  }
  if (!x.primed || region != x.region)
  {
    const Timed timed{"us start: whole memory"};
    std::memcpy(mine, live, ram_size);
    w.pages_copied += ram_size / PAGE;
  }
  else
  {
    {
      const Timed timed{"us start: the log's pages"};
      copied = &w.counts["pages: the log's copied"];
      for (const u32 off : pages)
        take((region - RAM) + off, PAGE);
    }
    {
      const Timed timed{"us start: outside the log, below"};
      copied = &w.counts["pages: outside copied"];
      outside(0, region - RAM);
    }
    const Timed timed{"us start: outside the log, above"};
    copied = &w.counts["pages: above the log copied"];
    take(net::STATE_END - RAM, ram_size - (net::STATE_END - RAM));
  }
  {
    const Timed timed{"us start: cache and window"};
    if (host.Scratch() != nullptr)
      std::memcpy(x.memory->cache.data(), host.Scratch(), host.ScratchSize());
    x.window.Restore(Data().Snapshot());
  }
  x.primed = true;
  // W's log starts again whenever the game's does (each race, and a new region): its shadow of the pages neither log
  // watches would hold the last race's bytes, and its learning steps would log those as the pages' content before a
  // step (a later restore inside a job, or the game's after an adoption, would write them back).
  if (x.log.Active() && (x.log.Region() != region || x.game_starts != s.rollback.Starts()))
    x.log.Stop();
  x.region = region;
  // rewound by the game's log, and the worker's log's moment before R
  {
    const Timed timed{"us start: Apply"};
    s.rollback.Apply(*x.g, step, run_from);
  }
  if (!x.log.Active())
  {
    x.log.Start(*x.g);
    x.game_starts = s.rollback.Starts();
  }
  return pages.size();
}

// W as the memory before R: the game's (TakeTheGame), taken as the slot log's moment at R. Its log keeps
// after-images when two workers run (a second job may start from it: study section 20.1).
void PrepareMemory(Guest& g, Session& s, u32 step, u32 run_from, int slot)
{
  Worker& x = s_ws[slot];
  const std::size_t pages = TakeTheGame(g, s, step, run_from, slot);
  const Timed timed{"us start: Adopt"};
  x.keeps_after = Threaded() && Workers() == 2;
  x.log.KeepAfter(x.keeps_after);
  x.log.Adopt(*x.g, s.rollback, run_from);
  if (x.jobs <= 2)
    Notice("fzgx: rollback worker: slot {} job {} from {} to {} prepared ({} pages taken, both logs)", slot, x.jobs,
           run_from, step, pages);
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
void PublishRows(int k, Session& s, u32 upto);
std::vector<u32> KeptWords(const Guest& g, const Guest& gw, int slot);
void StartSecond(Guest& g, Session& s, u32 step);

void WorkerOpen(Guest& g, Session& s, u32 run_from, u32 extra, bool held)
{
  if (s_as[0].open || s_as[1].open)
  {
    // S2: the rows of every step to the next frame's hook (run_from + extra + 1), as the session has them now (O's
    // first: a row of it that changed below N's c makes N's base stale); then N may start from O's log
    for (const int k : {s_older, 1 - s_older})
    {
      if (s_as[k].open)
        PublishRows(k, s, run_from + extra + 1);
    }
    StartSecond(g, s, run_from);
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
  Worker& x = s_ws[s_r.check_slot];
  const u64 irq = static_cast<u64>(g.Read32(LAST_INTERRUPT_TIME)) << 32 | g.Read32(LAST_INTERRUPT_TIME + 4);
  for (std::size_t i = 0; i < x.watch.size(); ++i)
  {
    const u32 here = g.Read32(x.watch[i]), there = x.seen[t - x.from][i];
    if (here == there || (x.told & (1u << i)) || !Say("follow"))
      continue;
    x.told |= 1u << i;
    Notice("fzgx: rollback worker: the job from {} to {}: {:08x} first differs at the hook of step {} ({:08x} here, "
           "{:08x} on W); {}; the running thread {:08x} (at the restore {:08x})",
           x.from, x.to, x.watch[i], t, here, there,
           irq != x.irq_time ? std::format("the game took an interrupt in that step (the last: type {}, srr0 {:08x})",
                                           g.Read32(LAST_INTERRUPT) >> 16, g.Read32(LAST_INTERRUPT_SRR0))
                             : std::string("no interrupt in that step"),
           g.Read32(CURRENT_THREAD), x.thread_at_restore);
  }
  x.irq_time = irq;
}

void WorkerCompare(Guest& g, Session& s, u32 t)
{
  Worker& x = s_ws[s_r.check_slot];
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
  if (x.job == Job::RAN && t > x.from && t <= x.to && !x.watch.empty() && t - x.from < x.seen.size())
    Follow(g, t);
  if (x.job != Job::RAN || t != x.to)
    return;
  const Timed timed{"us comparing"};
  x.job = Job::NONE;
  ++x.compared;
  Guest& gw = *x.g;
  std::vector<u32> words;
  // (S3's "async-check": the words an adoption keeps as the game has them, KeptWords, are left out as the time
  // words are, and counted)
  const bool threaded = Threaded();
  const std::vector<u32> time = threaded ? KeptWords(g, gw, s_r.check_slot) : TimeWords(g, gw);
  u32 time_differ = 0;
  for (const u32 a : time)
    time_differ += g.Read32(a) != gw.Read32(a);
  x.time_jobs += time_differ != 0;
  x.time_words += time_differ;
  if (threaded)
  {
    ++s_r.checked;
    s_r.check_kept += time_differ;
    s_r.frame_manager = g.Read32(FRAME_MANAGER);
  }
  if (const u32 n = s.rollback.Compare(g, gw, &words, 8, &time); n != 0)
  {
    ++x.memory_differ;
    x.memory_words += n;
    if (threaded)
    {
      ++s_r.check_memory;
      s_r.check_words += n;
    }
    for (const u32 a : words)
      if (!threaded && x.watch.size() < 4 && std::find(x.watch.begin(), x.watch.end(), a) == x.watch.end())
        x.watch.push_back(a);
    if (Say("memory"))
    {
      std::string list;
      for (const u32 a : words)
        list += std::format(" {:08x} ({:08x} here, {:08x} on W{})", a, g.Read32(a), gw.Read32(a), StackOf(g, a));
      Notice("fzgx: rollback worker: the job from {} to {}: memory differs in {} words:{}", x.from, x.to, n, list);
    }
  }
  if (const u32 k = s.rollback.CompareMoments(g, x.log, gw, x.from, x.to, &time); k != 0xFFFFFFFF)
  {
    ++x.entries_differ;
    s_r.check_moments += threaded;
    if (Say("moments"))
    {
      std::vector<std::string> where;
      s.rollback.CompareMoments(g, x.log, gw, x.from, x.to, &time, &where);
      std::string list;
      for (const std::string& text : where)
        list += " " + text;
      Notice("fzgx: rollback worker: the job from {} to {}: the logs' moments differ before step {} (the latest):{}",
             x.from, x.to, k, list);
    }
  }
  for (u32 k = std::min(x.rings_from, x.from); k < x.to; ++k)
  {
    u8 a[30 * 8], b[30 * 8];
    g.CopyFrom(a, layout::INPUTS + (k & 1023) * sizeof(a), sizeof(a));
    gw.CopyFrom(b, layout::INPUTS + (k & 1023) * sizeof(b), sizeof(b));
    if (Rollback::KeptChecksum(g, k) != Rollback::KeptChecksum(gw, k) || std::memcmp(a, b, sizeof(a)) != 0)
    {
      ++x.rings_differ;
      s_r.check_rings += threaded;
      if (Say("rings"))
        Notice("fzgx: rollback worker: the job from {} to {}: step {}'s checksum ({:08x} here, {:08x} on W) or rows "
               "differ", x.from, x.to, k, Rollback::KeptChecksum(g, k), Rollback::KeptChecksum(gw, k));
      break;
    }
  }
  // outside the log: a named class (interrupts, the time base, the stacks), counted by 64 KiB
  u8* live = g.GetHost().Ram();
  const u8* mine = x.memory->ram.data();
  const u32 ram_size = x.memory->RamSize();
  u32 outside = 0;
  Outside(x.region, ram_size, [&](u32 at, u32 size) {
    for (u32 run = at; run < at + size; run += 64)
    {
      if (std::memcmp(live + run, mine + run, std::min<u32>(64, at + size - run)) == 0)
        continue;
      for (u32 a = run; a < std::min<u32>(run + 64, at + size); a += 4)
      {
        if (Word(live + a) != Word(mine + a) && a != net::DOL_RNG - RAM)
        {
          ++outside;
          ++x.outside_at[(a >> 16) & 0x1FF];
        }
      }
    }
  });
  x.outside_words += outside;
  x.outside_jobs += outside != 0;
  if (x.mode != "check")
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
  Outside(x.region, ram_size, [&](u32 at, u32 size) {
    for (u32 a = at; a < at + size; a += 4)
    {
      if ((a & 63) == 0 && std::memcmp(mine + a, x.outside_before.data() + a, std::min<u32>(64, at + size - a)) == 0)
      {
        a += 60;
        continue;
      }
      if (Word(mine + a) == Word(x.outside_before.data() + a) || a == net::DOL_RNG - RAM ||
          (RAM + a >= stack_low && RAM + a < stack_high))
        continue;
      ++x.reverted;
      x.reverted_changed += Word(live + a) != Word(x.outside_before.data() + a);
      std::memcpy(live + a, x.outside_before.data() + a, 4);
    }
  });
}

// ---- S2: the worker's thread (game's thread side) ----

// S4: a correction the open job cannot take (a row changed under a step W has run, or one below its R once the job
// has been sent back as often as "restarts" allows): the job runs on and is adopted as it ran, and the next job
// starts from the step. The chain's first frame is kept for its lag.
void Late(u32 from)
{
  Rule& r = s_r;
  r.late_since = std::min(r.late_since, r.frame_no);
  if (!r.chain)
  {
    r.chain = true;
    r.chain_since = r.frame_no;
    r.chain_from = from;
  }
  r.chain_from = std::min(r.chain_from, from);
}

// A chain ends (an adoption with the session's rows, or a frame with no job open after one): its lag in frames.
void ChainEnds()
{
  Rule& r = s_r;
  r.late_since = ~u64{0};  // (every correction seen late is in the adoption or the sync rollback that ends it)
  if (!r.chain)
    return;
  const u64 lag = r.frame_no - r.chain_since;
  r.chain = false;
  ++r.chains;
  r.chain_frames += lag;
  r.chain_high = std::max(r.chain_high, lag);
  ++r.chain_seen[lag];
}

// The second worker (study section 20.2): a job is open in either slot; the slot of N when two are (else -1).
bool AnyOpen()
{
  return s_as[0].open || s_as[1].open;
}

int Newer()
{
  const int n = 1 - s_older;
  return s_as[n].open ? n : -1;
}

// The rows of steps R to `upto` - 1 as the session has them now into the job in slot k. S4: a row that changed under
// a step W has taken is no longer a step to go back to (the job is adopted as it ran, and the next starts there); one
// the worker has run past `upto` still is. The first such row of a job is its late row (N's c, study section 20.1);
// with N open, a row of O from N's c on is N's to run, and one below it makes N's base stale (N provisional).
void PublishRows(int k, Session& s, u32 upto)
{
  Async& a = s_as[k];
  if (upto <= a.from)
    upto = a.from + 1;
  std::vector<net::Row> rows;
  rows.reserve(upto - a.from);
  for (u32 t = a.from; t < upto; ++t)
    rows.push_back(net::RowFor(s, t));
  const std::lock_guard lock(a.m);
  // against the rows W's steps took, not the rows last published: a row that changes while W runs its step (taken,
  // the step not yet at its end, so `at` is still that step) is one W ran on too
  u32 changed = NONE;
  for (u32 n = 0; n < rows.size() && n < a.ran.size(); ++n)
  {
    if (rows[n] != a.ran[n])
    {
      changed = a.from + n;
      break;
    }
  }
  const u32 taken = a.from + static_cast<u32>(a.ran.size());  // W's steps below this have taken their rows
  if (changed != NONE && changed < taken)
  {
    Async& other = s_as[1 - k];
    if (other.open && other.newer && !a.newer)
    {
      // (the game's thread's own words of N)
      if (changed < other.from)
      {
        other.below = true;
        Late(a.from);
      }
    }
    else
    {
      Late(a.from);
      a.late = std::min(a.late, changed);
    }
  }
  changed = taken > upto ? upto : NONE;
  if (changed != NONE)
  {
    a.rewind = std::min(a.rewind, changed);
    ++s_r.rewinds_asked;
  }
  a.rows = std::move(rows);
  a.target = upto;
  a.cv.notify_all();
}

// The game's thread waits for the worker in slot k to have left its job (it stops at its next hook).
void WaitIdle(int k)
{
  Async& a = s_as[k];
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

// Jobs by the race's time (S5, for tests around an event: a seat handed to the AI, a rejoin): by 300 steps (5 s),
// the jobs adopted and those ended otherwise by reason (a give-up's, or why it was stopped)
struct Span
{
  u64 adopted = 0;
  std::map<std::string, u64> ended;
};
std::map<u32, Span> s_spans;
constexpr u32 SPAN_STEPS = 300;

// The job in slot k ends without an adoption: stopped (`why` for the log) or given up.
void CloseJob(int k, const char* why)
{
  Async& a = s_as[k];
  WaitIdle(k);
  if (a.ended)
  {
    ++s_r.gave_up;
    ++w.why[a.why];
    ++s_spans[a.at / SPAN_STEPS].ended[a.why];
    if (Say("gave up"))
      Notice("fzgx: rollback worker: the job from {} (started at {}{}) gave up at step {}: {}", a.from, a.started_at,
             a.second ? ", the second worker's" : "", a.at, a.why);
    if (a.mid_step)
      s_ws[k].primed = false;  // W stands part way through a step that no log saw end
  }
  else if (why != nullptr)
  {
    ++s_r.cancelled;
    ++w.why[why];
    ++s_spans[a.at / SPAN_STEPS].ended[why];
  }
  a.open = a.begun = false;
  a.newer = false;
  mode::TraceJob(false, k);  // (U4: a job not adopted leaves no line in the mode trace)
}

// S3, S4: a job stopped for a correction below its R and started again from it, while it has been restarted fewer
// times than "restarts" allows (0 by default: never); past that the correction is the chain's (WorkerTakes).
bool MayRestart(u32 moves)
{
  return moves <= TheOptions().debug.worker_restarts;
}

// The step a sync rollback of the open job must go back to: its R, or lower, the step the chain owes a rerun from
// (cleared: the sync rollback runs it), or ("async-check") the chain's first R, which its comparison runs from.
u32 SyncFrom(u32 from)
{
  from = std::min(from, std::exchange(s_r.owed, NONE));
  return s_r.chain && AsyncCheck() ? std::min(from, s_r.chain_from) : from;
}

// S4 (b), at every frame: W's step time by the median of each window (its run, checksum and SaveStep: HookThreaded),
// or, in a window where W was kept from running by its own estimate (a start barred late, too few steps), that
// estimate. pace::STRIKES windows in a row at "slowStepUs" or slower, with at least pace::CATCH_UPS catch-ups in their
// span (a start barred because its foreseen frames would pass "catchUp" counts as one, since it runs sync in the
// catch-up's place), and the race's rollbacks run sync; RETRY_S later, or at a new race (the game's log started
// again), the worker is tried again with a fresh estimate (the game's own rerun step, r_sync, which W's steps then
// move): W's estimate moves only while W runs, so one spike that set it near a frame would otherwise bar every start
// for good (section 19). A median: a few slow steps (a page fault, the PC busy for a moment) are not a slow worker.
// Both workers' steps feed the one window (study section 20.2), the open job's estimate is the one the bar takes.
void WatchRate(Session& s)
{
  Rule& r = s_r;
  if (!Threaded())
    return;
  static const auto epoch = std::chrono::steady_clock::now();
  pace::Now now;
  now.s = std::chrono::duration<double>(std::chrono::steady_clock::now() - epoch).count();
  now.starts = s.rollback.Starts();
  now.behind = r.catch_ups + r.barred_late;
  now.barred_late = r.barred_late;
  now.limit_us = TheOptions().debug.worker_slow_us;
  pace::Verdict verdict;
  double was_ms = 0, second_ms = 0;
  std::vector<float> second;
  {
    const std::lock_guard lock(s_as[1].m);  // (the second worker's steps, into the one window)
    second.swap(s_as[1].window);
    second_ms = s_as[1].r_ms;
  }
  {
    const std::lock_guard lock(s_as[0].m);  // (the worker's thread writes the window and the estimate at each hook)
    std::vector<float>& window = s_as[0].window;
    window.insert(window.end(), second.begin(), second.end());
    const double open_ms = s_older == 1 && second_ms > 0 ? second_ms : s_as[0].r_ms;
    now.estimate_ms = open_ms > 0 ? open_ms : r.r_sync;
    verdict = r.pace.Watch(now, window);
    if (verdict == pace::Verdict::RETRY || verdict == pace::Verdict::RESEED)
      was_ms = std::exchange(s_as[0].r_ms, r.r_sync);
  }
  if (verdict == pace::Verdict::RETRY || verdict == pace::Verdict::RESEED)
  {
    const std::lock_guard lock(s_as[1].m);
    s_as[1].r_ms = s_as[1].r_ms > 0 ? r.r_sync : 0;
  }
  const pace::Pace& p = r.pace;
  switch (verdict)
  {
  case pace::Verdict::NONE:
    break;
  case pace::Verdict::SWITCH:
    Notice("fzgx: rollback worker: switch to sync: the worker's step {:.2f} ms ({}), {} windows in a row at {} us "
           "or slower, {} catch-ups (or starts barred as foreseen ones) in them: the rollbacks run sync for {:.0f} s "
           "(frame {}; jobs so far {}, adopted {})",
           p.step_ms,
           p.step_n != 0 ? std::format("by the median of its last {} steps", p.step_n)
                         : std::string("its estimate, which barred its starts"),
           pace::STRIKES, now.limit_us, now.behind - p.strike_behind.front(), pace::RETRY_S, r.frame_no, r.jobs,
           r.adopted);
    break;
  case pace::Verdict::RETRY:
    Notice("fzgx: rollback worker: {}: the worker's thread again, its step estimate {:.2f} ms afresh from {:.2f} ms "
           "(frame {}; switches {}, retries {})",
           p.new_race ? std::string("a new race") : std::format("{:.0f} s after the switch", pace::RETRY_S), r.r_sync,
           was_ms, r.frame_no, p.switches, p.retries);
    break;
  case pace::Verdict::RESEED:
    Notice("fzgx: rollback worker: the worker's estimate {:.2f} ms barred its starts for {:.0f} s (no switch: "
           "\"slowStepUs\" 0): afresh from {:.2f} ms (frame {}; reseeds {})",
           was_ms, pace::RETRY_S, r.r_sync, r.frame_no, p.reseeds);
    break;
  }
}

// A job that ends without an adoption, its rollback sync from its R in this frame (at `step`): the counts.
void FellBack(u32 step, u32 from, u32 moves)
{
  s_r.late_since = ~u64{0};  // (the sync rollback runs every correction seen late)
  const u32 d = step - from;
  ++s_r.fell;
  s_r.fell_depth_sum += d;
  ++s_r.fell_depth[d];
  ++s_r.moves_fell[moves];
}

// The words an adoption keeps as the game has them: what W cannot have (the VI callbacks' words, which interrupts
// write; the task manager's time words, which the time base writes: the nodes in the lists of either memory now and
// those of W at R, which may have left them since, as the countdown's Starter does at step 421) and what W never
// reaches (the frame routine's own words, FRAME_MANAGER: a frame's last step restores them).
std::vector<u32> KeptWords(const Guest& g, const Guest& gw, int slot)
{
  std::vector<u32> words = KeptTableWords(g, gw);  // (section 18's table: rollback_worker_kept.cpp)
  words.insert(words.end(), s_as[slot].time_words.begin(), s_as[slot].time_words.end());
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

// What a frame finds of the job in slot k at the hook of `step` (after the frame's SaveStep): it ends without an
// adoption (and why), or its worker stands at the present with every step run and it may be adopted (whether on the
// session's rows now, and the first row that differs); else it runs on. `bound`: the catch-up's bound applies (not
// to N while O is open: O's span is the longer).
struct Seen
{
  bool ends = false, adopt = false, clean = false, caught_up = false;
  const char* why = nullptr;  // (CloseJob's: nullptr for a job that gave up, whose own reason is told)
  u32 first_stale = NONE;
  u64 span = 0, delay = 0;
};

Seen Look(int k, Session& s, u32 step, bool settle, bool check, bool bound)
{
  Async& a = s_as[k];
  Seen v;
  if (!a.begun || settle || check)
  {
    v.ends = true;
    v.why = !a.begun ? "a job not begun" : "the race's end checked";
    return v;
  }
  // the rows of the steps to A as the session has them now
  std::vector<net::Row> rows;
  for (u32 t = a.from; t < step; ++t)
    rows.push_back(net::RowFor(s, t));
  bool ended;
  {
    std::unique_lock lock(a.m);
    // S4: W parked at A, every step to A run, is adopted whatever rows it ran on; `clean`, they are the session's now
    // (else the session's check finds the first that differs right after, and the next job starts there)
    // (at most MOST_ADOPT steps: the rings TakeOver hands over hold 1024; the catch-up below keeps a job far shorter)
    v.adopt = !a.ended && a.parked && a.at == step && a.rewind == NONE && a.ran.size() == rows.size() &&
              step - a.from <= MOST_ADOPT;
    for (std::size_t n = 0; v.adopt && n < rows.size() && v.first_stale == NONE; ++n)
    {
      if (rows[n] != a.ran[n])
        v.first_stale = a.from + static_cast<u32>(n);
    }
    v.clean = v.adopt && v.first_stale == NONE && !a.below;
    if (v.adopt && guard::Moved(k))
    {
      // U1 (study section 17): a chunk was verified, retired or cleared while W ran (the code tables W read)
      a.ended = true;
      a.why = "the CPU core's code tables changed during the job";
      v.adopt = v.clean = false;
    }
    ended = a.ended;
  }
  if (v.adopt)
    return v;
  if (ended)
  {
    v.ends = true;
    return v;
  }
  // S4 (a), the safety bound: no deadline while the job stays within "catchUp" steps (span from the step a sync
  // rollback would go back to, or its correction's delay in frames); past it, or with the race switched to sync
  // (WatchRate), the job stops and one sync rollback from there catches up: one hitch, then the chain again
  const u32 limit = TheOptions().debug.worker_catch_up;
  const u32 base = std::min(a.from, s_r.chain && AsyncCheck() ? std::min(s_r.chain_from, s_r.owed) : s_r.owed);
  v.span = step - base;
  v.delay = s_r.frame_no - a.since;
  if (s_r.pace.switched || (bound && (v.span > limit || v.delay > limit)))
  {
    v.ends = true;
    v.why = s_r.pace.switched ? "the worker too slow" : "caught up";
    v.caught_up = !s_r.pace.switched;
  }
  return v;
}

// The job in slot k ends without an adoption: closed, and its rollback runs sync from its R (or lower, SyncFrom) in
// this frame. That step.
u32 FallJob(int k, const Seen& v, u32 step)
{
  Async& a = s_as[k];
  CloseJob(k, v.why);
  const u32 back = SyncFrom(a.from);
  if (v.caught_up)
  {
    ++s_r.catch_ups;
    s_r.catch_up_depth_high = std::max<u64>(s_r.catch_up_depth_high, step - back);
    Notice("fzgx: rollback worker: catch-up at step {} (frame {}): the job from {} {} steps back, its correction "
           "{} frames late (bound {}): sync from {}, {} steps; the worker's step {:.2f} ms",
           step, s_r.frame_no, a.from, v.span, v.delay, TheOptions().debug.worker_catch_up, back, step - back, a.r_ms);
  }
  s_r.second_fell += a.second;
  FellBack(step, back, a.moves);
  s_r.kind = K_DEEP_SYNC;
  s_r.fall_back = true;
  ChainEnds();
  return back;
}

// N closed while O runs on, or stopped with O: no sync rollback of its own (O's covers it, or the chain runs its row).
void CloseNewer(int n, bool with_older)
{
  CloseJob(n, with_older ? "stopped with the older job (its base)" : nullptr);
  ++(with_older ? s_r.second_stopped : s_r.second_back);
}

// "async-check": the job in slot k stopped where it stands (at A), and the sync rollback runs in this frame from the
// chain's first R (or the job's own); at the hook of A the two are compared (WorkerPrepare, WorkerCompare): the
// chain's earlier adoptions, which ran on rows that then changed, are compared through this one. That step.
u32 CheckJob(int k, u32 step)
{
  Async& a = s_as[k];
  {
    const std::lock_guard lock(a.m);
    a.stop = true;
    a.cv.notify_all();
  }
  CloseJob(k, nullptr);
  ++s_r.moves_adopted[a.moves];
  const u32 check_from = SyncFrom(a.from);
  ChainEnds();
  s_r.check_pending = true;
  s_r.check_from = check_from;
  s_r.check_job_from = a.from;
  s_r.check_to = step;
  s_r.check_slot = k;
  s_r.kind = K_DEEP_SYNC;
  s_r.fall_back = true;
  return check_from;
}

// The job in slot k adopted at the hook of `step`: Rollback::TakeOver and the rows its steps ran on. `clean`: on the
// session's rows (the chain ends); `through`: O's, clean as far as N carries it (the chain goes on in N); else
// provisional (the session's check finds its first stale row).
void AdoptJob(int k, Guest& g, Session& s, u32 step, bool clean, bool through)
{
  Async& a = s_as[k];
  Worker& x = s_ws[k];
  const u32 from = a.from;
  const auto start = std::chrono::steady_clock::now();
  Guest& gw = *x.g;
  const std::vector<u32> keep = KeptWords(g, gw, k);
  {
    const std::lock_guard lock(a.m);  // (the worker waits at A's hook)
    s.rollback.TakeOver(g, x.log, gw, from, step, &keep);
    mode::TraceJob(true, k);  // (U4: W's mode trace lines are the adopted runs: written now, in order)
    for (u32 t = from; t < step; ++t)
    {
      s.used[t] = a.ran[t - from];
      if (s_forget)
        s_forget(t);
    }
    a.stop = true;
    a.cv.notify_all();
  }
  a.open = a.begun = false;
  a.newer = false;
  ++s_r.adopted;
  s_r.second_adopted += a.second;
  ++s_spans[step / SPAN_STEPS].adopted;
  s_r.j_sum += a.frames;
  s_r.j_pred_sum += a.j_pred;
  s_r.j_high = std::max<u64>(s_r.j_high, a.frames);
  ++s_r.j_seen[a.frames];
  ++s_r.moves_adopted[a.moves];
  s_r.behind_high = std::max(s_r.behind_high, step - from);
  {
    const u64 delay = s_r.frame_no - a.since;
    ++s_r.delay_seen[delay];
    s_r.delay_sum += delay;
    ++s_r.delay_n;
    s_r.delay_high = std::max(s_r.delay_high, delay);
  }
  s_r.kind = K_ADOPT;
  s_r.adopt_us += std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
  if (clean)
  {
    ++s_r.clean;
    ChainEnds();
  }
  else if (through)
    ++s_r.older_through;  // (no new Late: N runs its stale rows)
  else
  {
    ++s_r.provisional;
    Late(from);
  }
  if (s_r.adopted <= 3)
    Notice("fzgx: rollback worker: slot {} job from {} (started at {}, depth {}) adopted at {}, {} frames after its "
           "start ({} foreseen{})", k, from, a.started_at, a.depth, step, a.frames, a.j_pred,
           clean ? "" : through ? "; its stale rows the second job's" :
                                  "; ran on rows that changed since: the next job starts from the first");
}

// At CheckPredictions' start (after the frame's SaveStep(A)): the frame's count, the open jobs' adoptions when their
// workers stand at A, else their news. The step a sync rollback must go back to when a job ended without an adoption
// (given up, too long, or the race's end checked); none else. Two jobs (study section 20.3 and 20.4): O first, N only
// once O is closed; O's end without an adoption stops N (one sync rollback from O's R); N's end with O open sends its
// late row back to the chain, with O adopted it is the single job's path.
std::optional<u32> WorkerFrame(Guest& g, Session& s, u32 step, bool settle, bool check)
{
  if (!Measuring())
    return std::nullopt;
  CountFrame();
  s_r.hook = std::chrono::steady_clock::now();
  s_r.fall_back = false;
  ++s_r.frame_no;
  WatchRate(s);
  const int o = s_older, n = 1 - o;
  Async& a = s_as[o];
  if (!a.open)
  {
    ChainEnds();  // (the last frame started no job after an adoption: nothing left to correct, or a sync rollback did)
    return std::nullopt;
  }
  s_r.kind = K_OPEN;
  const bool second = s_as[n].open;
  ++a.frames;
  if (second)
    ++s_as[n].frames;
  s_r.restart_moves = 0;
  const Seen vo = Look(o, s, step, settle, check, true);
  const Seen vn = second ? Look(n, s, step, settle, check, false) : Seen{};
  const pair::Plan plan = pair::Order({true, vo.ends, vo.adopt}, {second, vn.ends, vn.adopt});
  if (plan.older == pair::Fate::FALL)
  {
    if (plan.newer == pair::Fate::STOP)
      CloseNewer(n, true);
    return FallJob(o, vo, step);
  }
  if (plan.older == pair::Fate::ADOPT)
  {
    if (vo.clean && AsyncCheck())
    {
      if (second)
        CloseNewer(n, false);  // (O ran on the session's rows after all: N has nothing to carry)
      return CheckJob(o, step);
    }
    const bool through = second && !vo.clean && pair::CleanThroughNewer(vo.first_stale, s_as[n].from, a.below);
    AdoptJob(o, g, s, step, vo.clean, through);
    if (second)
    {
      // N is the only open job now: the chain's next job starts on the free slot from its log
      s_older = n;
      s_as[n].newer = false;
      switch (plan.newer)
      {
      case pair::Fate::FALL:
        return FallJob(n, vn, step);
      case pair::Fate::ADOPT:
        if (vn.clean && AsyncCheck())
          return CheckJob(n, step);
        ++s_r.second_same_hook;
        AdoptJob(n, g, s, step, vn.clean, false);
        break;
      default:
        PublishRows(n, s, std::max(step, std::min(s_as[n].target, step + 1)));
        break;
      }
    }
    if (const u32 owed = pair::OwedNow(s_r.owed, AnyOpen()); owed != NONE)
    {
      // a forced rollback the jobs could not take (tests): the next job of the chain starts there in this frame (no
      // fall-back: the rollback goes to WorkerStart as a correction the session found would)
      Late(owed);
      s_r.owed = NONE;
      return owed;
    }
    return std::nullopt;
  }
  if (plan.newer == pair::Fate::BACK)
    CloseNewer(n, false);  // (its late row is the chain's: O's adoption is provisional, the session's check finds it)
  PublishRows(o, s, std::max(step, std::min(a.target, step + 1)));
  if (s_as[n].open)
    PublishRows(n, s, std::max(step, std::min(s_as[n].target, step + 1)));
  return std::nullopt;
}

// While a job is open, the predictions are compared below the lowest open R only: the jobs' own steps take theirs
// from the session at every frame (PublishRows).
u32 WorkerSearchTo(u32 arrived)
{
  for (const Async& a : s_as)
  {
    if (a.open)
      arrived = std::min(arrived, a.from);
  }
  return arrived;
}

// A correction at `wrong` while a job is open: the worker's (true), or, below the job's R, the job is stopped and the
// correction is the session's (false). S4: a correction at a step W has taken its row for no longer sends W back
// (the job is adopted as it ran and the next starts there); one below R stops the job while it has been sent back
// fewer times than "restarts" allows (0 by default: never), and past that the job runs on too, so a stream of corrections cannot keep
// it from ever landing, and none runs sync. With N open, a correction below O's R is N's too (its base is O's).
bool WorkerTakes(Session& s, u32 step, u32& wrong)
{
  const int o = s_older, n = Newer();
  Async& a = s_as[o];
  if (!a.open)
    return false;
  if (wrong >= a.from)
  {
    // (the session searches only below R while a job is open: a step from R here is a forced rollback's, tests, with
    // no row that differs)
    bool ran;
    {
      const std::lock_guard lock(a.m);
      ran = wrong < a.from + a.ran.size();  // (a step W has taken its row for, its run maybe not ended)
      a.cv.notify_all();
    }
    if (!ran && n >= 0 && wrong >= s_as[n].from)
    {
      const std::lock_guard lock(s_as[n].m);
      ran = wrong < s_as[n].from + s_as[n].ran.size();
    }
    if (ran)
    {
      // owed: the next job of the chain runs it again (the S4 chain's path, as a row that changed under W)
      ++s_r.owed_n;
      s_r.owed = std::min(s_r.owed, wrong);
      Late(a.from);
    }
    else
      ++s_r.forced_ahead;  // (W has not run it yet: it runs it with the session's row)
    return true;
  }
  if (!MayRestart(a.moves + 1))
  {
    // the chain's: the job is adopted as it ran (not clean), and the next starts from here (owed, for a forced one)
    s_r.deferred += !a.below;
    a.below = true;
    if (n >= 0)
      s_as[n].below = true;
    s_r.owed = std::min(s_r.owed, wrong);
    Late(wrong);
    return true;
  }
  if (n >= 0)
    CloseNewer(n, true);
  CloseJob(o, "a correction below the job's start");
  s_r.restart_moves = a.moves + 1;  // (the job that may start from `wrong` now inherits them)
  wrong = std::min(wrong, std::exchange(s_r.owed, NONE));
  (void)s;
  (void)step;
  return false;
}

// What bars a job's start from `wrong` at `step` that every job's start passes, W's and N's (study section 20.2):
// nullptr, or the bar (its count is the caller's). `j_pred` the frames it is foreseen to take; `late` whether only its
// foreseen frames bar it (a catch-up foreseen).
enum class StartBar
{
  NONE,
  OTHER,      // a guard (U2, U6, U7) or too few cores
  SLOW,       // the race switched to sync
  COUNTDOWN,  // before the countdown's end
  FINISH,     // a machine has finished, with the pipe one for every thread
  DEEP,       // its depth and foreseen frames pass "catchUp"
};
StartBar CommonBar(Guest& g, u32 step, u32 wrong, double r, u32* j_pred, const char** guard_bar)
{
  static const unsigned cores = std::thread::hardware_concurrency();
  if (cores < LEAST_CORES)
    return StartBar::OTHER;
  // study section 17: no threaded job on a module whose pipe is one for every thread (U2), an instrumented module
  // (U6), or while the mod loader intercepts (U7)
  if (const char* bar = guard::StartBar(s_pipe_per_thread))
  {
    *guard_bar = bar;
    return StartBar::OTHER;
  }
  if (s_r.pace.switched)
    return StartBar::SLOW;  // S4 (b): this race's worker is too slow for the chain (WatchRate)
  // Not before the countdown's end (TheOptions' "from", 422 = COUNTDOWN_END by default) in any case: with the pipe
  // per thread, jobs from the countdown's steps left the game writing through a null pointer in the drawing after
  // their adoption (S3, research/study-rollback-second-core.md section 12)
  if (wrong < (s_pipe_per_thread ? TheOptions().debug.worker_from : COUNTDOWN_END))
    return StartBar::COUNTDOWN;
  // With a module whose pipe is one for every thread, not once a machine has finished: the race's end can then fall
  // in the job, and W's frame routine would cut its steps there and go on to the frame's drawing (frame_entry.py
  // `extra_cut`), whose pipe is the game's. With the pipe per thread the cut gives the job up (W's write of +492).
  if (!s_pipe_per_thread)
  {
    const u32 module = g.Read32(net::MODULE_POINTER);
    const u32 count = g.Read8(module + net::MACHINE_COUNT), machines = g.Read32(module + net::MACHINES);
    for (u32 i = 0; machines != 0 && i < count && i < 64; ++i)
    {
      if ((g.Read32(machines + i * game::MACHINE_SIZE + game::MACHINE_FLAGS) & game::MACHINE_FLAG_FINISHED) != 0)
        return StartBar::FINISH;
    }
  }
  // j: the frames the job is foreseen to take to its adoption (study section 4), a step a frame
  const u32 d = step - wrong;
  *j_pred = r < FIELD_MS - 0.5 ? static_cast<u32>(std::ceil((d * r + START_MS) / (FIELD_MS - r))) : 99;
  // S4 (a): a job that would pass "catchUp" before its adoption (its span then d + j steps, its delay at least j
  // frames) would only be caught up: the sync rollback now is the shorter one
  if (d + *j_pred > TheOptions().debug.worker_catch_up)
    return StartBar::DEEP;
  return StartBar::NONE;
}

// The rule at a rollback from `step` back to `wrong` with no job open: true, a job starts on the worker's thread (no
// restore: the game goes on with its predicted steps); false, the session's sync rollback as ever.
bool WorkerStart(Guest& g, Session& s, u32 step, u32 wrong)
{
  const u32 moves = std::exchange(s_r.restart_moves, 0u);
  if (!Measuring())
    return false;
  s_r.owed = NONE;  // (no job is open: whatever was owed is this rollback's, from `wrong` or below)
  const u32 d = step - wrong, least = std::max<u32>(TheOptions().debug.worker_depth, 1);
  s_r.kind = d >= least ? K_DEEP_SYNC : K_SHALLOW;
  // S4 (study section 14: a race of humans rolls back about 6 steps every frame): a chain's next job goes to the worker
  // however shallow, so the game's thread runs only its new steps and the job's start; with the rule "fit" any depth
  // goes to the worker when the frame would not fit its sync rollback (`depth` is the least for "always", tests)
  const bool chained = s_r.chain;
  const bool fit_rule = TheOptions().debug.worker_rule != "always";
  if (!Threaded() || s_r.fall_back || s.practice || s.scrub.forked || !s.rollback.Active() ||
      (d < least && !chained && !fit_rule))
    return false;
  // the slot: W's, or the second's while W's thread is still leaving its last job (two workers only)
  int k = 0;
  {
    std::unique_lock lock0(s_as[0].m);
    const bool busy = s_as[0].running;
    lock0.unlock();
    if (busy && Workers() == 2)
    {
      const std::lock_guard lock1(s_as[1].m);
      k = s_as[1].running ? 0 : 1;
    }
  }
  double r;
  {
    const std::lock_guard lock(s_as[k].m);  // (the worker's thread writes it at each hook)
    r = s_as[k].r_ms > 0 ? s_as[k].r_ms : s_r.r_sync;
  }
  u32 j_pred = 0;
  const char* guard_bar = nullptr;
  switch (CommonBar(g, step, wrong, r, &j_pred, &guard_bar))
  {
  case StartBar::NONE:
    break;
  case StartBar::OTHER:
    if (guard_bar != nullptr)
      ++guard::Barred()[guard_bar];
    ++s_r.barred_other;
    return false;
  case StartBar::SLOW:
    ++s_r.barred_slow;
    return false;
  case StartBar::COUNTDOWN:
    ++s_r.barred_countdown;
    return false;
  case StartBar::FINISH:
    ++s_r.barred_finish;
    return false;
  case StartBar::DEEP:
    ++s_r.barred_deep;
    s_r.barred_late += d <= TheOptions().debug.worker_catch_up;  // (the worker's pace bars it: a catch-up foreseen)
    return false;
  }
  if (fit_rule && !chained && s_r.c_high + d * s_r.r_sync + MARGIN_MS <= FIELD_MS)
  {
    ++s_r.sync_fit;
    return false;
  }
  // S4: no bar for a worker slower than a frame (the owner: never a stall): the chain falls behind, and its lag says so
  s_r.slow += r >= FIELD_MS - MARGIN_MS;
  WaitIdle(k);
  const auto start = std::chrono::steady_clock::now();  // (after the wait, which idle_us counts)
  PrepareMemory(g, s, step, wrong, k);
  guard::TakeGeneration(k);  // (U1: the CPU core's code tables as the job starts)
  mode::TraceJob(false, k);  // (U4: W's mode trace lines are this job's from here)
  s_older = k;
  Async& a = s_as[k];
  {
    const std::lock_guard lock(a.m);
    a.from = wrong;
    a.target = wrong;
    a.rewind = NONE;
    a.stop = a.ended = a.mid_step = a.parked = false;
    a.why.clear();
    a.rows.clear();
    a.ran.clear();
    a.base_rows.clear();
    a.base_sums.clear();
    a.seats = s.seats;
    a.at = wrong;
  }
  {
    const Timed timed{"us start: time words"};
    a.time_words = TimeWords(*s_ws[k].g, *s_ws[k].g);
  }
  a.moves = moves;
  a.perturb_at = NONE;
  a.below = false;
  a.late = NONE;
  a.newer = a.second = a.second_tried = false;
  if (const u32 n = TheOptions().debug.worker_perturb; n != 0 && !s_r.chain && (s_r.jobs + 1) % n == 0)
    a.perturb_at = wrong + 1;  // (tests: W's row of the job's second step altered, its first misprediction's model)
  s_r.chained_jobs += s_r.chain;
  a.since = std::min(s_r.late_since, s_r.frame_no);
  s_r.late_since = ~u64{0};
  a.open = true;
  a.begun = false;
  a.started_at = step;
  a.depth = d;
  a.frames = 0;
  a.j_pred = j_pred;
  ++s_r.jobs;
  s_r.d_sum += d;
  s_r.kind = K_START;
  s_r.start_us += std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
  if (s_r.jobs <= 2)
    Notice("fzgx: rollback worker: job {} on the worker's thread (slot {}) from {} at {} (depth {}; r {:.2f} ms, j {} "
           "foreseen)", s_r.jobs, k, wrong, step, d, r, a.j_pred);
  return true;
}

// The second worker's start (study section 20.1 and 20.2), at the frame's hook after the rows are published: with one
// job O open and begun, the other slot idle, a late row c of O with R < c and O's worker past it, two workers on this
// PC, the pace good and every bar a job's start passes passed (with N's own span from c), N starts from c: its memory
// the game's rewound to O's R by the game's log, then O's after-images of R to c - 1 (Forward, W running on), its
// ring O's rows of those steps; it is begun with the window of this hook (Begin) and runs the rows of now. Else the
// row waits for the chain, as with one worker.
void StartSecond(Guest& g, Session& s, u32 step)
{
  const int o = s_older, n = 1 - o;
  Async& a = s_as[o];
  Async& b = s_as[n];
  if (!Threaded() || !a.open || !a.begun || b.open || a.late == NONE || a.second_tried || s_r.fall_back ||
      s.practice || s.scrub.forked || !s.rollback.Active())
    return;
  const auto bar = [](const char* why) { ++s_r.second_barred[why]; };
  pair::SecondView v;
  v.workers = Workers();
  v.newer_open = false;
  {
    const std::lock_guard lock(b.m);
    v.slot_idle = !b.running;
  }
  v.older_below = a.below;
  v.after_images = s_ws[o].keeps_after;
  v.from = a.from;
  v.late = a.late;
  {
    const std::lock_guard lock(a.m);
    v.older_open = !a.ended;
    v.at = a.at;
    v.taken = a.from + static_cast<u32>(a.ran.size());
  }
  v.window_ms = s_r.pace.step_ms;
  v.window_barred = s_r.pace.step_n == 0 && s_r.pace.step_ms > 0;
  v.window_behind = s_r.pace.last_behind;
  v.slow_us = TheOptions().debug.worker_slow_us;
  if (const char* why = pair::SecondBar(v))
  {
    // (a late row that waits for the chain stays one: weighed again at the next hook, but for the bars that hold)
    bar(why);
    if (v.workers < 2 || !v.older_open || v.older_below)
      a.second_tried = true;
    return;
  }
  const u32 c = a.late;
  double r;
  {
    const std::lock_guard lock(b.m);
    r = b.r_ms;
  }
  if (r <= 0)
  {
    const std::lock_guard lock(a.m);
    r = a.r_ms > 0 ? a.r_ms : s_r.r_sync;
  }
  u32 j_pred = 0;
  const char* guard_bar = nullptr;
  switch (CommonBar(g, step, c, r, &j_pred, &guard_bar))
  {
  case StartBar::NONE:
    break;
  case StartBar::OTHER:
    bar(guard_bar != nullptr ? guard_bar : "too few cores");
    return;
  case StartBar::SLOW:
    bar("the race switched to sync");
    return;
  case StartBar::COUNTDOWN:
    bar("before the countdown's end");
    return;
  case StartBar::FINISH:
    bar("a finish");
    return;
  case StartBar::DEEP:
    bar("deeper than the bound (its depth and foreseen frames)");
    return;
  }
  a.second_tried = true;
  const auto start = std::chrono::steady_clock::now();
  Worker& x = s_ws[n];
  // the game's memory rewound to O's R, then O's after-images of R to c - 1 and its rows of those steps (O's worker
  // goes on: its steps below c and their after-images stand, under its log's mutex and O's)
  TakeTheGame(g, s, step, a.from, n);
  std::vector<u32> pages, sums;
  std::vector<net::Row> base;
  bool ok;
  {
    const std::lock_guard lock(a.m);
    ok = !a.ended && a.at >= c && a.ran.size() >= c - a.from;
    if (ok)
    {
      base.assign(a.ran.begin(), a.ran.begin() + (c - a.from));
      // (O's checksums of those steps: its hooks to c have written them, and W writes none of them again)
      for (u32 t = a.from; t < c; ++t)
        sums.push_back(Rollback::KeptChecksum(*s_ws[o].g, t));
      ok = s_ws[o].log.Forward(*x.g, a.from, c, &pages);
    }
  }
  if (!ok)
  {
    ++s_r.second_forward_failed;
    return;  // (N's memory is the game's at R: the pages either log names later take it again)
  }
  {
    // (the pages Forward wrote differ from the game's: the slot's next start takes them again)
    std::vector<u32> both;
    std::set_union(x.pending.begin(), x.pending.end(), pages.begin(), pages.end(), std::back_inserter(both));
    x.pending = std::move(both);
  }
  x.keeps_after = true;  // (a third job may start from N's log once O is adopted)
  x.log.KeepAfter(true);
  x.log.Adopt(*x.g, s.rollback, c, &pages);
  guard::TakeGeneration(n);
  mode::TraceJob(false, n);
  {
    const std::lock_guard lock(b.m);
    b.from = c;
    b.target = c;
    b.rewind = NONE;
    b.stop = b.ended = b.mid_step = b.parked = false;
    b.why.clear();
    b.rows.clear();
    b.ran.clear();
    b.seats = s.seats;
    b.at = c;
    b.base_from = a.from;
    b.base_rows = std::move(base);
    b.base_sums = std::move(sums);
  }
  {
    const Timed timed{"us start: time words"};
    b.time_words = TimeWords(*x.g, *x.g);
  }
  b.moves = 0;
  b.perturb_at = NONE;
  b.below = false;
  b.late = NONE;
  b.newer = b.second = true;
  b.second_tried = false;
  b.since = std::min(s_r.late_since, s_r.frame_no);
  s_r.late_since = ~u64{0};
  b.open = true;
  b.begun = false;
  b.started_at = step;
  b.depth = step - c;
  b.frames = 0;
  b.j_pred = j_pred;
  ++s_r.jobs;
  ++s_r.second_starts;
  s_r.d_sum += step - c;
  s_r.second_start_us += std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
  // the rows of N's steps to the next frame's hook, as O's were published in this hook
  PublishRows(n, s, std::max(a.target, c + 1));
  if (s_r.second_starts <= 3)
    Notice("fzgx: rollback worker S6: the second worker's job {} (slot {}) from {} at {} (depth {}), its base the job "
           "from {}'s state there ({} pages forward; {} usable threads, workers {})",
           s_r.second_starts, n, c, step, step - c, a.from, pages.size(), s_r.threads_seen, s_r.workers_seen);
}

// Steps below `known` become final; not the open jobs' (their entries and checksums are the workers' to give).
u32 WorkerKnown(u32 known)
{
  for (const Async& a : s_as)
  {
    if (a.open)
      known = std::min(known, std::min(a.from, s_r.owed));
  }
  // ("async-check": a chain's steps stay open from its first R, so its sync run can go back there at its end)
  return s_r.chain && AsyncCheck() ? std::min(known, s_r.chain_from) : known;
}

// The game stops: the jobs stopped (the adapter joins the threads after) and the S2 line.
void WorkerQuiesce()
{
  if (const int n = Newer(); n >= 0)
    CloseNewer(n, true);
  for (int k = 0; k < SLOTS; ++k)
  {
    if (s_as[k].open)
      CloseJob(k, "the game stopped");
  }
  s_r.owed = NONE;
  s_r.late_since = ~u64{0};  // (frame_no runs on across races: a stale one would be a delay of the race before)
  for (int k = 0; k < SLOTS; ++k)
    WaitIdle(k);
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
         "up {}, stopped {}, sent back {}; barred: countdown {}, a finish {}, other {}; sync as it fits {}; j "
         "seen{} (average {:.2f}, foreseen {:.2f}, at most {}); worker {} steps, {} went back, a step {:.2f} ms (the "
         "game's {:.2f}), a frame's own {:.2f} ms; game's thread: starts {:.0f} us, begins {:.0f} us, adoptions {:.0f} "
         "us, waits for the worker {} ({:.0f} us)",
         Mode(), std::max<u32>(TheOptions().debug.worker_depth, 1),
         std::thread::hardware_concurrency(), r.jobs, r.jobs ? double(r.d_sum) / r.jobs : 0.0, r.adopted, r.gave_up,
         r.cancelled, r.rewinds_asked, r.barred_countdown, r.barred_finish, r.barred_other, r.sync_fit, js,
         r.adopted ? double(r.j_sum) / r.adopted : 0.0, r.adopted ? double(r.j_pred_sum) / r.adopted : 0.0, r.j_high,
         s_as[0].steps + s_as[1].steps, s_as[0].rewinds + s_as[1].rewinds, s_as[0].r_ms, r.r_sync, r.c_high, r.start_us, r.begin_us, r.adopt_us, r.waited_idle,
         r.idle_us);
  Notice("fzgx: rollback worker S2: frames by kind:{}", kinds);
  const auto list = [](const std::map<u32, u64>& m) {
    std::string text;
    for (const auto& [k, n] : m)
      text += std::format(" {}:{}", k, n);
    return text.empty() ? std::string(" none") : text;
  };
  Notice("fzgx: rollback worker S3: the pipe {}; restarts {} (a job started again for a correction below its R at "
         "most that often, then the correction is the chain's); restarted, adopted jobs:{}; restarted, jobs run sync:{}; "
         "sync from a job's R: {} (depth {:.1f} on average; by depth:{}); given up for a page neither log watched: {}",
         s_pipe_per_thread ? "per thread (W's bytes dropped)" : "one for every thread (countdown and finish barred)",
         TheOptions().debug.worker_restarts, list(r.moves_adopted), list(r.moves_fell), r.fell,
         r.fell ? double(r.fell_depth_sum) / r.fell : 0.0, list(r.fell_depth), r.unwatched);
  std::string lags;
  for (const auto& [lag, n] : r.chain_seen)
    lags += std::format(" {}:{}", lag, n);
  std::string delays;
  for (const auto& [d, n] : r.delay_seen)
    delays += std::format(" {}:{}", d, n);
  Notice("fzgx: rollback worker S4: correction delay (frames from a correction's first frame to its job's adoption): "
         "{:.2f} on average, at most {} (by delay:{})", r.delay_n ? double(r.delay_sum) / r.delay_n : 0.0, r.delay_high,
         delays.empty() ? std::string(" none") : delays);
  Notice("fzgx: rollback worker S4: chaining: adopted {} with the session's rows, {} on rows that changed since "
         "(provisional); jobs chained {}; corrections below a job's R left to the chain {}; chains (to an adoption with the session's rows) {}, "
         "frames {} in all, at most {} (by length:{}); a job's R to its adoption at most {} steps; jobs started with the worker at a frame a step or slower {}",
         r.clean, r.provisional, r.chained_jobs, r.deferred, r.chains, r.chain_frames, r.chain_high,
         lags.empty() ? std::string(" none") : lags, r.behind_high, r.slow);
  Notice("fzgx: rollback worker S4: bounds: catch-ups {} (bound {} steps or frames; the deepest {} steps); starts "
         "barred deeper than the bound (its depth and foreseen frames) {} ({} by the foreseen frames alone); slow-worker switches to sync {} (at {} us a "
         "step, {} windows of {} steps and {:.0f} s in a row, {} catch-ups; the slowest window's step {:.2f} ms; windows "
         "where W's estimate barred it {}), retries {}, estimates afresh without a switch {}, rollbacks barred while switched {}; forced rollbacks on steps W had run, run again by the next "
         "job or the sync {}, ahead of W {}",
         r.catch_ups, TheOptions().debug.worker_catch_up, r.catch_up_depth_high, r.barred_deep, r.barred_late,
         r.pace.switches, TheOptions().debug.worker_slow_us, pace::STRIKES, pace::WINDOW, pace::WINDOW_S,
         pace::CATCH_UPS, r.pace.rate_high, r.pace.barred_windows, r.pace.retries, r.pace.reseeds, r.barred_slow, r.owed_n, r.forced_ahead);
  {
    std::string bars;
    for (const auto& [text, n] : guard::Barred())
      bars += std::format(" [{}: {}]", text, n);
    Notice("fzgx: rollback worker S5: the thread audit's guards: starts barred{}; the kept words' table {} rows "
           "(study sections 17 and 18)", bars.empty() ? std::string(" none") : bars, KeptTable().size());
    std::string spans;
    for (const auto& [span, n] : s_spans)
    {
      spans += std::format(" [{}: adopted {}", span * SPAN_STEPS, n.adopted);
      for (const auto& [text, k] : n.ended)
        spans += std::format("; {} {}", text, k);
      spans += "]";
    }
    Notice("fzgx: rollback worker S5: jobs by {} steps (from the step a job stood at):{}", SPAN_STEPS,
           spans.empty() ? std::string(" none") : spans);
    s_spans.clear();
  }
  {
    std::string bars;
    for (const auto& [text, n] : r.second_barred)
      bars += std::format(" [{}: {}]", text, n);
    Notice("fzgx: rollback worker S6: the second worker: {} usable threads, workers {} at the last start weighed; "
           "second jobs started {} ({:.0f} us on the game's thread), adopted {} ({} in the older job's hook), stopped "
           "with the older job {}, closed with it running (the row back to the chain) {}, run sync alone {}, forward "
           "failed {}; older jobs clean as far as the second carried them {}; starts barred:{}; slot 1 {} jobs "
           "prepared, a step {:.2f} ms",
           r.threads_seen, r.workers_seen, r.second_starts, r.second_start_us, r.second_adopted, r.second_same_hook,
           r.second_stopped, r.second_back, r.second_fell, r.second_forward_failed, r.older_through,
           bars.empty() ? std::string(" none") : bars, s_ws[1].jobs, s_as[1].r_ms);
  }
  if (AsyncCheck())
    Notice("fzgx: rollback worker S3: async-check: {} adoptions compared with their sync rollback: memory differs in {} "
           "({} words), moments in {}, checksums or rows in {}; the kept words (time, VI, the frame's) differing: {}; "
           "the frame's words: the manager {:08x} (+0x10, +0x14), {:08x}",
           r.checked, r.check_memory, r.check_words, r.check_moments, r.check_rings, r.check_kept,
           r.frame_manager, FRAME_PRESENT);
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
