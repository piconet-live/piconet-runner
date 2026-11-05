// The deep-rollback worker, stage S1 (research/study-rollback-second-core.md, sections 1 to 3 and 10): a deep
// rollback's steps run again by a second CPU context over a private memory W, with a Rollback log of its own, on the
// game's thread for now (the thread is S2). Every player's race runs "on" from 4 hardware threads, the old sync path
// retired for players (study section 21): no setting, launch key or variable a player's start has turns it off, and
// below 4 threads the rollbacks run sync, as the start's log says (ModeSaid). The launch file's test part
// ("debug.rollbackWorker", tests only) names another mode; with "off" nothing here runs and nothing the game computes
// changes (the sync path, kept as the tests' reference and as the fall-back for a job the worker gives up):
//   - "compare": every rollback at least `depth` steps deep (7 by default) is run on W first: at the frame's network
//     hook, before the restore, W takes the pages of the game's memory either log has seen change since the last job
//     (Rollback::TakeDirty) and the memory outside the log whole, is rewound to the rollback's step R by the game's
//     log (Rollback::Apply) and taken as the worker log's moment there (Adopt); the context runs the image's own
//     frame routine on W from that hook to the network hook of the frame's step L, its own hooks serving each step's
//     words as ExtraStep does. Then the game's thread runs the same steps as it always does, and at the hook of step
//     L the two are compared: the memory a restore puts back (Rollback::Compare), the two logs' entries of steps R to
//     L - 1 (CompareEntries), the checksums and input rows of those steps; and, as a named class, the words outside
//     the log. The first differences go to the log, the sums at the end.
//   - "check": the same, and the game's memory outside the log is then left as an adoption would leave it: the
//     words outside the log the worker's steps changed take back the values they had at the job's start (the
//     predicted timeline's, which the worker does not hand over), but for the running thread's stack. A playback
//     that stays exact shows these words do not reach the checksum.
// In those two nothing is adopted: the game's thread goes on with its own steps. S2 (section 11):
//   - "on": the worker's thread. At a rollback at least `depth` deep, from the countdown's end on, before any machine
//     has finished, on a machine of 4 cores or more, and when the frame would not fit as a sync rollback (its own
//     cost lately plus d steps run again lately plus 1.5 ms over a field; "rule": "always" for tests), the game does
//     not restore: W is prepared as above and the context runs on the thread, taking each step's row from the job
//     (published at every frame: a row that changed sends it back to that step on its own log), and waits at the
//     next frame's step. At the hook of the step A it stands at, after SaveStep(A), with the rows unchanged, the game
//     takes it over (Rollback::TakeOver, the VI callbacks' words and the task manager's time words kept as the game
//     has them); steps from R stay open until then. S4, chaining: a row that changes under a step W has run, or a
//     correction below R, waits for the next job: W parked at A is adopted as it ran (`used` and the replay's ring hold
//     the rows its steps ran on), the session's check then finds the first step whose row differs and the next job
//     starts from it at once, however shallow; no deadline and no restart. A forced rollback (tests) on a step W has run is
//     owed to the chain the same way: the next job starts there. The bounds: a job whose span or correction delay
//     passes "catchUp" (60 steps or frames) stops and one sync rollback from its R catches up; a worker whose step
//     takes "slowStepUs" (6 ms) or more by the median of three windows in a row (240 steps and 1 s each) with two
//     catch-ups in them switches the race to sync rollbacks, and the worker is tried again 10 s later (or at a new
//     race); no job starts whose depth and foreseen frames pass "catchUp", and none is adopted past 512 steps. The
//     race's end checked or a job given up: the job stops and the game's sync rollback runs from its step.
//   - "sync": the same frame counts and measurements with no worker, the baseline for "on".
// The second worker W2 (research/study-rollback-second-core.md section 20): two slots of one kind of worker, each a
// memory, a log, a context and a thread. With one job O open past a row of it that turned out late (c, R < c), a
// second job N starts on the free slot from c, its base the game's log to R and then O's after-images of R to c - 1
// (Rollback::Forward, O's worker running on); O is adopted first, N once O is closed; O's end without an adoption
// stops N. Only where the process may use 6 hardware threads (UsableThreads), or forced by the test key's "workers";
// it backs off first on a slow pace. Its line is "rollback worker S6".
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

#include "fzgx/guest.h"

namespace fzgx
{
class Host;
}

namespace fzgx::rollback_worker
{
// The worker's mode: the launch file's "debug.rollbackWorker" when it names one (tests only: "off" and "sync" run the
// sync path, the reference); else "on" with at least 4 hardware threads and "off" below (every player's race since
// the old path's retirement, study section 21; the FZGX_ROLLBACK_WORKER variable that could turn it off is gone).
// `key` the launch file's mode ("" when it names none), `cores` std::thread::hardware_concurrency().
std::string ModeFor(const std::string& key, unsigned cores);
// The start's log line on the mode (the runner's ReportSettings): what ModeFor gives and why; below the thread minimum
// it says the PC's count and that rollbacks run sync.
std::string ModeSaid(const std::string& key, unsigned cores);
// This game's: ModeFor of its options and its machine.
const std::string& Mode();
// The switch is on (compare, check, sync or on).
bool On();
// S2: "on", jobs on the worker's thread (and the machine has the cores; the thread is the adapter's).
bool Threaded();
// The adapter's per-machine rollback forgets a step's kept run (at an adoption, for every step the worker ran).
void SetForget(void (*forget)(u32 step));
// A worker's thread (slot 0 W, slot 1 the second worker W2, research/study-rollback-second-core.md section 20): it
// starts (once), and it has left a job (after Begin's job ended, given up or stopped).
void ThreadStarts(int slot);
void Left(int slot);
// The calling thread's worker slot (-1: not a worker's thread).
int Slot();
// The calling thread's worker's own memory as a host that does nothing (its Ram W's memory, its calls no-ops; nullptr:
// not a worker's thread): what TheHost gives on W after OffLimits.
Host* OwnHost();
// The second worker (section 20.2) runs only where the process may use at least 6 hardware threads: the adapter's
// count of them (the hardware's, and on Windows the process's affinity mask), read again at each start. Unset: the
// hardware's count.
void SetUsableThreads(unsigned (*threads)());
// The game stops: the open jobs are stopped and the workers' threads are idle (before the adapter joins them).
void Quiesce();
// The emulator is about to stop (any thread; the adapter's Core::Stop hook, before the CPU stops and the native module
// W's steps run in is unloaded): the worker's thread leaves its job and no job begins on it after. Stop lifts it.
void Close();
// Tests: the launch file's write watch ("address[:first-last]"; empty: none).
const std::string& WatchSpec();

// The CPU context's memory for a job about to run.
struct Memory
{
  u8* ram = nullptr;
  u32 ram_size = 0;
  u8* cache = nullptr;  // the locked cache's window
  u32 cache_size = 0;
  bool threaded = false;  // S2: the job runs on the worker's thread (else S1's, on the game's, at once)
};
// After the game's network hook has returned (the adapter's host call, once per slot): a job prepared in this hook
// for `slot` is to run now. False: none. The context starts from the game's registers at this hook. S1's jobs (on
// the game's thread) are slot 0's.
bool Begin(int slot, Memory* memory);
// The calling thread's worker's data window (slot 0's on the game's thread): the context's slow accesses to the window
// are served from it.
ModData& Window();
// The context reached the network hook: the step about to run's words are written into W (or not, at the job's last
// hook). True: go on; false: the job's steps are run (the context stops here). On the worker's thread (S2): it may
// wait there for the game's news (the next rows, a step to go back to, the adoption).
bool Hook();
// The context reached the mode point (the image's HOST_MODE): the game mode's rules run on W (mode.cpp keeps every
// state between two steps in the guest's block; a race without a mode returns at once).
void ModePoint();
// The context met what it does not handle: the job gives up (and the game's thread runs the rollback as ever). On a
// worker's thread, its slot's job; on the game's thread, `slot`'s threaded job when it names one, else S1's.
void GiveUp(const std::string& why, int slot = -1);
// The adapter found the native module's write-gather pipe per thread (DolRecomp's staticrecomp_gather_pipe_per_thread):
// the worker's thread has a pipe of its own, whose bytes are dropped, so a step that writes the drawing's pipe no
// longer bars a job. Without it (an older module) a store there on the worker's thread would land in the game's pipe:
// no job starts before the countdown's end or once a machine has finished, as in S2.
void SetPipePerThread(bool per_thread);
// The thread audit (research/study-rollback-second-core.md section 17): W reaches the host only through an allowlist.
// The calling thread is the worker's (ThreadStarts ran on it).
bool OnW();
// A host entry outside W's allowlist was reached on W's thread (the adapter's host calls, the window's side effects,
// the machines' task call, the CPU core's hardware hooks, the core's host): with the launch file naming the worker's
// mode (a test, a gate) the game stops loudly, with the entry's name; in a player's build the job gives up, the
// context is stopped (SetContextStop's function: its exception and cycle budget, as the adapter's own give-up), and
// the caller does nothing and returns a neutral value (a task call false, a host call handled, TheHost OwnHost's).
void OffLimits(const char* entry);
// The adapter's give-up of the calling thread's context (its exception and cycle budget set), for OffLimits.
void SetContextStop(void (*stop)(const std::string& why));
// U1: the CPU core's code tables' generation (StaticRecompCore::CodeGeneration), taken at a job's start; a job whose
// generation moved by its adoption gives up (a chunk native code could run was retired, invalidated or cleared while
// W ran; a chunk checked meanwhile does not move it: W could not run it before, and runs the module's code after).
void SetCodeGeneration(u64 (*generation)());
// U6: the native module is instrumented (a profile-training build, call counters, the access trace): `what` names
// it, and no job starts (W's counts would race the game's). nullptr: not.
void SetInstrumented(const char* what);
// U7: the mod loader's interception is active (W would run an intercepted function natively): no job starts.
void SetInterception(bool (*active)());
// A count for the log's last line (the adapter's: system calls replicated, block copies, dispatches).
void Count(const char* what, u64 n);
// The game stops: the last line.
void Stop();
}  // namespace fzgx::rollback_worker
