// The deep-rollback worker's CPU context (fzgx/rollback_worker.h; research/study-rollback-second-core.md, section 1
// and S1): a second CPUState over the worker's memory W, run on the game's thread right after the network hook of a
// frame with a deep rollback, from the game's registers there, through the image's own frame routine up to the
// network hook of the frame's step L. Its hooks replicate what a step reaches besides memory and registers (section
// 1's table): the data window from W's own, the locked cache in W's own window (the module reaches it directly), the
// locked cache's block copy on W, the statistics table's translated reads served from W, the system call (the
// game's vector is a bare rfi: SRR0, SRR1 and the next instruction, section 9.5); it defers every interrupt (none is
// delivered: no clock runs) and starts with MSR.FP set. Anything else gives the job up: hardware, an instruction left
// to the interpreter, code the CPU core has not checked, a host call but the network hook and the machines' task.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "adapter_internal.h"
#include "lanes_internal.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <chrono>
#include <format>
#include <string>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <xmmintrin.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <intrin.h>
#endif

#include "Core/Core.h"
#include "Core/PowerPC/MMU.h"
#include "Core/PowerPC/PowerPC.h"
#include "Core/PowerPC/StaticRecomp/StaticRecompCore.h"
#include "Core/System.h"

#include "fzgx/rollback_worker.h"

namespace fzgx_port
{
namespace
{
namespace rw = fzgx::rollback_worker;

constexpr u32 RFI = 0x4C000064;
constexpr u32 SYSTEM_CALL_VECTOR = 0x00000C00;
constexpr u32 MOST_TURNS = 50000000;  // dispatches in one job: past it, the job is taken for a loop and given up

struct Worker
{
  CPUState cpu{};
  rw::Memory memory;
  bool looked = false;
  int (*dispatch)(CPUState*, u32) = nullptr;
  void (*loaded)(CPUState*) = nullptr;
  void (*set_pipe)(u8**, u8*, void (*)(CPUState*)) = nullptr;  // the module's inline write-gather pipe
  alignas(32) u8 pipe[256] = {};
  u8* pipe_at = nullptr;
  bool pipe_per_thread = false;  // the module's pipe is per thread (S3): the worker's thread drops its pipe's bytes
  u64 pipe_dropped = 0;          // bytes the worker's steps wrote to the drawing's pipe, dropped
  std::string gave_up;
  bool running = false;
  u32 dmau = 0, dmal = 0;
  std::unordered_map<u32, u32> translated;  // a page's translation (the statistics table), from the game's MMU
  // S2: the pages translated at a job's start on the game's thread (the statistics table's, and those a job met)
  std::unordered_set<u32> pages;
  u32 missed = 0;  // a page a job on the thread met untranslated (its next job takes it)
  bool threaded = false;
  u64 jobs = 0;
  const u8* game_ram = nullptr;
  u64 dispatches = 0, system_calls = 0, block_copies = 0, translated_reads = 0, tasks = 0;
};
Worker s_w;

void GiveUp(const std::string& why)
{
  if (s_w.gave_up.empty())
    s_w.gave_up = why;
  s_w.cpu.exception = s_w.cpu.exception != 0 ? s_w.cpu.exception : lanes::GAVE_UP;
  s_w.cpu.cycle_budget = 0;
}

void Look()
{
  if (std::exchange(s_w.looked, true))
    return;
#ifdef _WIN32
  const HMODULE module = GetModuleHandleW(std::filesystem::path(lanes::s_work.module).filename().c_str());
  using GetModule = const StaticRecompModuleDesc* (*)();
  const auto get = module ? reinterpret_cast<GetModule>(GetProcAddress(module, STATICRECOMP_GET_MODULE_SYMBOL)) : nullptr;
  if (const StaticRecompModuleDesc* desc = get ? get() : nullptr)
  {
    s_w.dispatch = desc->dispatch;
    s_w.loaded = desc->on_state_loaded;
  }
  if (module)
  {
    s_w.set_pipe = reinterpret_cast<void (*)(u8**, u8*, void (*)(CPUState*))>(
        GetProcAddress(module, "staticrecomp_set_gather_pipe"));
    using PerThread = int (*)();
    const auto per_thread = reinterpret_cast<PerThread>(GetProcAddress(module, "staticrecomp_gather_pipe_per_thread"));
    s_w.pipe_per_thread = s_w.set_pipe != nullptr && per_thread != nullptr && per_thread() != 0;
    // U6 (study section 17): a module that counts what it runs or trains a profile says so; no job on it
    using Instrumented = const char* (*)();
    if (const auto instrumented = reinterpret_cast<Instrumented>(GetProcAddress(module, "staticrecomp_instrumented")))
      rw::SetInstrumented(instrumented());
  }
#endif
  rw::SetPipePerThread(s_w.pipe_per_thread);
  // U1: the CPU core's code tables' generation, which a job's adoption checks
  rw::SetCodeGeneration([]() -> fzgx::u64 { return g_static_recomp_core ? g_static_recomp_core->CodeGeneration() : 0; });
  // U7: no job while the mod loader intercepts (W would run an intercepted function natively)
  rw::SetInterception([] { return InterceptionActive(); });
}

// Main memory through the game's address translation (the statistics table at 0x23800000, 0x01554000 physical):
// the page's translation asked of the game's MMU once, the bytes served from W. False: no such page.
u8* Translated(u32 ea, u32 size)
{
  const u32 page = ea & ~0xFFFu;
  auto it = s_w.translated.find(page);
  if (it == s_w.translated.end() && s_w.threaded)
  {
    // (the game's MMU is the game's thread's: the page is taken at the next job's start)
    s_w.missed = page;
    return nullptr;
  }
  if (it == s_w.translated.end())
  {
    const std::optional<u32> physical = Core::System::GetInstance().GetMMU().GetTranslatedAddress(page);
    it = s_w.translated.emplace(page, physical ? *physical : 0xFFFFFFFFu).first;
  }
  const u32 at = it->second == 0xFFFFFFFFu ? 0xFFFFFFFFu : (it->second & 0x3FFFFFFFu) + (ea & 0xFFFu);
  if (at >= s_w.memory.ram_size || s_w.memory.ram_size - at < size || (ea & 0xFFFu) + size > 0x1000u)
    return nullptr;
  ++s_w.translated_reads;
  return s_w.memory.ram + at;
}

u64 HookRead(CPUState*, u32 ea, u8 size)
{
  if (fzgx::ModData::Contains(ea, size))
    return rw::Window().Read(ea, size);
  if (const u8* p = Translated(ea, size))
  {
    u64 value = 0;
    for (u8 i = 0; i < size; ++i)
      value = value << 8 | p[i];
    return value;
  }
  GiveUp(std::format("a read at {:08X} (hardware, or memory it has no copy of)", ea));
  return 0;
}

void HookWrite(CPUState*, u32 ea, u64 value, u8 size)
{
  // On the thread W keeps +492 past every step it runs, so the frame routine itself writes it only where the race's
  // end cuts a frame's steps (frame_entry.py `extra_cut`): what follows is the frame's drawing, whose pipe is the
  // game's. The job gives up at once.
  if (s_w.threaded && ea == table::CONTROL + 492)
    GiveUp("the race's end cut W's steps");
  if (fzgx::ModData::Contains(ea, size))
    return rw::Window().Write(ea, value, size);
  if (u8* p = Translated(ea, size))
  {
    for (u8 i = size; i > 0; --i, value >>= 8)
      p[i - 1] = static_cast<u8>(value);
    return;
  }
  GiveUp(std::format("a write at {:08X} (hardware, or memory it has no copy of)", ea));
}

u32 HookRead32(CPUState*, u32 ea, u8)
{
  GiveUp(std::format("an external-control read at {:08X}", ea));
  return 0;
}

void HookWrite32(CPUState*, u32 ea, u32, u8)
{
  GiveUp(std::format("an external-control write at {:08X}", ea));
}

void* HookPointer(CPUState*, u32 ea, u32 size)
{
  const u32 offset = ea - game::LOCKED_CACHE;
  if (size != 0 && offset < lanes::SCRATCH && lanes::SCRATCH - offset >= size)
    return s_w.memory.cache + offset;
  return nullptr;
}

u32 HookSprRead(CPUState*, u16 spr, u32)
{
  if (spr == lanes::DMAU || spr == lanes::DMAL)
    return spr == lanes::DMAU ? s_w.dmau : s_w.dmal;
  GiveUp(std::format("a read of special register {}", spr));
  return 0;
}

// The locked cache's block copy on W (as lanes_hooks.cpp's BlockCopy, MMU::DMA_LCToMemory and DMA_MemoryToLC).
void BlockCopy(CPUState* cpu)
{
  u32 blocks = (s_w.dmau & 0x1Fu) << 2 | (s_w.dmal >> 2 & 3u);
  blocks = blocks == 0 ? 128 : blocks;
  const u32 size = blocks * 32, memory = (s_w.dmau & ~0x1Fu) & 0x3FFFFFFFu, cache = (s_w.dmal & ~0x1Fu) & 0x3FFFFu;
  if (memory >= cpu->ram_size || cpu->ram_size - memory < size || cache + size > lanes::SCRATCH)
    return GiveUp("a block copy of the locked cache that is not between its first 16 KiB and main memory");
  ++s_w.block_copies;
  if ((s_w.dmal & 0x10u) != 0)
    std::memcpy(s_w.memory.cache + cache, cpu->ram + memory, size);
  else
    std::memcpy(cpu->ram + memory, s_w.memory.cache + cache, size);
}

void HookSprWrite(CPUState* cpu, u16 spr, u32 value, u32)
{
  if (spr != lanes::DMAU && spr != lanes::DMAL)
    return GiveUp(std::format("a write of special register {}", spr));
  (spr == lanes::DMAU ? s_w.dmau : s_w.dmal) = value;
  if (spr == lanes::DMAL && (value & 2u) != 0)
    BlockCopy(cpu);
  s_w.dmal &= ~2u;
}

// dcbf, dcbst, dcbi change nothing a step reads; icbi (none in S0's census) would have to reach the CPU core.
void HookCache(CPUState*, u8 operation, u32 ea, u32)
{
  if (operation == PPC_CACHE_ICBI)
    GiveUp(std::format("an icbi at {:08X}", ea));
}

void HookFallback(CPUState*, u32 raw, u32 cia)
{
  GiveUp(std::format("an instruction left to the interpreter ({:08X} at {:08X})", raw, cia));
}

// A store to the write-gather pipe (the drawing's FIFO), which a module built with DOLRECOMP_GATHER_PIPE=inline makes
// itself, into the pipe the worker hands it for the job: hardware, the job gives up.
void PipeFlush(CPUState* cpu)
{
  GiveUp(std::format("a write to the drawing's pipe (in the code from {:08X}, lr {:08X})", cpu->pc, cpu->lr));
  s_w.pipe_at = s_w.pipe;
}

// S3, on the worker's thread with a module whose pipe is per thread: the bytes a step run again writes to the drawing's
// pipe are dropped. The game's own run of the step sent them to the drawing at its first run; a sync rollback's re-run
// sends them again (a projection set twice, research/study-rollback-second-core.md section 10's give-ups), which
// nothing of the race reads back: the pipe is hardware, outside the memory a step's checksum covers.
void PipeDrop(CPUState*)
{
  s_w.pipe_dropped += static_cast<u64>(s_w.pipe_at - s_w.pipe);
  s_w.pipe_at = s_w.pipe;
}

bool HookHostCall(CPUState*, u32)
{
  return false;
}

// Test only: a write watch on one word (the launch file's debug.rollbackWorker "watch": "address[:first-last]", with the access-trace
// module, DolRecomp's DOLRECOMP_MEM_TRACE): every native store to it in those steps, the game's and W's, goes to the
// log with its value, the step, whether it is a step run again, the OS's running thread and the store's place in the
// module (the host return address's offset in the module's image: the guest function is found from it offline).
struct WriteWatch
{
  bool looked = false;
  u32 word = 0, first = 0, last = 0xFFFFFFFF;
  u32 step_game = 0, sim_to = 0;  // the game's step about to run and its +492 at its last network hook
  uintptr_t base = 0;
  std::atomic<u32> lines = 0;
};
WriteWatch s_watch;
// The store a thread reported last, told at its next access (study section 17, U3: each thread's own, the game's and
// W's, never one for both)
struct WatchPending
{
  bool pending = false, on_w = false;
  std::string text;
};
thread_local WatchPending t_watch;

u32 Big(const u8* p)
{
  return static_cast<u32>(p[0]) << 24 | static_cast<u32>(p[1]) << 16 | static_cast<u32>(p[2]) << 8 | p[3];
}

void WatchAccess(unsigned address, unsigned width_and_write)
{
  WriteWatch& x = s_watch;
  WatchPending& p = t_watch;
  if (p.pending)
  {
    const u8* ram = p.on_w ? s_w.memory.ram : s_w.game_ram;
    if (ram != nullptr && x.lines.fetch_add(1) < 200)
      s_core_host.Log(fzgx::LogLevel::Notice, std::format("fzgx: write watch: {}; the word is now {:08x}", p.text, Big(ram + (x.word & 0x01FFFFFFu))));
    p.pending = false;
  }
  if ((width_and_write & 0x100u) == 0)
    return;
  const u32 width = width_and_write & 0xFFu;
  if (address + width <= x.word || address >= x.word + 4)
    return;
  const bool on_w = s_w.running || rw::OnW();  // (S1's job on the game's thread, or W's thread: U3)
  const u32 step = on_w ? static_cast<u32>(rw::Window().Read(table::CONTROL + 124, 4)) : x.step_game;
  if (step < x.first || step > x.last)
    return;
  const u8* ram = on_w ? s_w.memory.ram : s_w.game_ram;
  if (ram == nullptr)
    return;
  uintptr_t back = 0;
#ifdef _MSC_VER
  back = reinterpret_cast<uintptr_t>(_ReturnAddress());
#endif
  p.text = std::format("{} at step {} ({}), a {}-byte store to {:08x}, thread {:08x}, from the module's +{:x}",
                       on_w ? "W" : "the game", step, on_w ? "W's run" : (step < x.sim_to ? "run again" : "first run"),
                       width, address, Big(ram + 0xE4), back - x.base);
  p.pending = true;
  p.on_w = on_w;
}

void LookWatch(CPUState* cpu)
{
  s_watch.step_game = static_cast<u32>(fzgx::Data().Read(table::CONTROL + 124, 4));
  s_watch.sim_to = static_cast<u32>(fzgx::Data().Read(table::CONTROL + 492, 4));
  s_w.game_ram = cpu->ram;
  if (std::exchange(s_watch.looked, true))
    return;
#ifdef _WIN32
  const char* spec = rw::WatchSpec().c_str();
  if (*spec == 0)
    return;
  unsigned word = 0, first = 0, last = 0xFFFFFFFFu;
  if (std::sscanf(spec, "%x:%u-%u", &word, &first, &last) < 1)
    return;
  s_watch.word = word & ~3u;
  s_watch.first = first;
  s_watch.last = last;
  const HMODULE module = GetModuleHandleW(std::filesystem::path(lanes::s_work.module).filename().c_str());
  using SetTrace = void (*)(void (*)(unsigned, unsigned));
  const auto set = module ? reinterpret_cast<SetTrace>(GetProcAddress(module, "fz_set_trace")) : nullptr;
  s_watch.base = reinterpret_cast<uintptr_t>(module);
  if (set)
    set(&WatchAccess);
  s_core_host.Log(fzgx::LogLevel::Notice, std::format("fzgx: write watch: {:08x}, steps {} to {}: {}", s_watch.word, first, last,
         set ? "the module's access trace listened to" : "no access trace in this module"));
#endif
}

// The context's state: the game's at the hook, but its memory, hooks, MSR.FP set, no clock.
void Dress(const CPUState& game_cpu)
{
  CPUState& c = s_w.cpu;
  c = game_cpu;
  c.pc = game_cpu.lr;  // where the network hook's host call returns to
  c.ram = s_w.memory.ram;
  c.ram_size = s_w.memory.ram_size;
  c.exram = s_w.memory.cache;
  c.exram_size = lanes::WINDOW;
  const void* no_record = nullptr;  // (the module's own record of the locked cache stays off: forks/dolrecomp/4-lanes)
  std::memcpy(s_w.memory.cache + lanes::WINDOW - 8, &no_record, sizeof(no_record));
  c.external_read = &HookRead;
  c.external_write = &HookWrite;
  c.external_read32 = &HookRead32;
  c.external_write32 = &HookWrite32;
  c.external_pointer = &HookPointer;
  c.instruction_fallback = &HookFallback;
  c.host_call = &HookHostCall;
  c.spr_read = &HookSprRead;
  c.spr_write = &HookSprWrite;
  c.cache_control = &HookCache;
  c.external_user_data = &s_w;
  c.cycle_budget = s64{1} << 60;
  c.downcount = 0;
  c.exception = 0;
  c.msr |= PPC_MSR_FP;
  s_w.dmau = Core::System::GetInstance().GetPPCState().spr[lanes::DMAU];
  s_w.dmal = Core::System::GetInstance().GetPPCState().spr[lanes::DMAL];
  s_w.translated.clear();
  s_w.gave_up.clear();
}

u32 VectorWord(const CPUState& c)
{
  const u8* p = c.ram + SYSTEM_CALL_VECTOR;
  return static_cast<u32>(p[0]) << 24 | static_cast<u32>(p[1]) << 16 | static_cast<u32>(p[2]) << 8 | p[3];
}

// One exception the generated code raised: the system call replicated (the vector's first word an rfi), any other
// gives up.
bool Exception(CPUState& c)
{
  if (c.exception == lanes::GAVE_UP)
    return false;
  if ((c.exception & PPC_EXC_SYSTEM_CALL) != 0 && c.pc == SYSTEM_CALL_VECTOR &&
      VectorWord(c) == RFI)
  {
    // sc then rfi: back at the instruction after sc with the MSR sc saved
    c.pc = c.srr0;
    c.msr = (c.msr & ~PPC_MSR_RFI_MASK) | (c.srr1 & PPC_MSR_RFI_MASK);
    c.exception = 0;
    c.program_exception = 0;
    ++s_w.system_calls;
    return true;
  }
  GiveUp(std::format("an exception ({:X}) at {:08X}, from {:08X}", c.exception, c.pc, c.srr0));
  return false;
}

// One dispatch. A fault inside it (a stray pointer: the context's state is not what the code expects) gives the job
// up with where it was, rather than ending the game (S1's own net while the context is new; on Windows).
int Dispatch(CPUState& c, u32 pc, u64* fault, u64* where)
{
#ifdef _MSC_VER
  __try
  {
    return s_w.dispatch(&c, pc);
  }
  __except (*fault = GetExceptionInformation()->ExceptionRecord->ExceptionCode,
            *where = GetExceptionInformation()->ExceptionRecord->NumberParameters > 1 ?
                         GetExceptionInformation()->ExceptionRecord->ExceptionInformation[1] : 0,
            EXCEPTION_EXECUTE_HANDLER)
  {
    return -1;
  }
#else
  (void)fault;
  (void)where;
  return s_w.dispatch(&c, pc);
#endif
}

// The job: dispatch on W until the context reaches the network hook of the frame's step L.
void Run()
{
  CPUState& c = s_w.cpu;
  for (u32 turns = 0;; ++turns)
  {
    if (!s_w.gave_up.empty())
      return rw::GiveUp(s_w.gave_up);
    if (turns > MOST_TURNS)
      return rw::GiveUp("a job that does not reach its step");
    const u32 pc = c.pc;
    if (pc == table::HOST_NET)
    {
      if (!rw::Hook())
        return;
      c.pc = c.lr;
      continue;
    }
    if (pc == table::HOST_MODE)
    {
      rw::ModePoint();
      c.pc = c.lr;
      continue;
    }
    if (pc >= HOST_CALLS && pc < HOST_CALLS_END + 4)
      return rw::GiveUp(std::format("a host call at {:08X}", pc));
    const bool task = lanes::IsTask(pc);  // (per-machine rollback on: the machines' task runs whole on W)
    if (!task && !(g_static_recomp_core && g_static_recomp_core->FastDispatchableAt(pc)))
      return rw::GiveUp(std::format("code the CPU core has not checked at {:08X}", pc));
    s_w.tasks += task;
    c.downcount = 0;
    u64 fault = 0, where = 0;
    const int ran = Dispatch(c, pc, &fault, &where);
    if (ran < 0)
      return rw::GiveUp(std::format("a fault {:X} at host address {:X} in the code from {:08X} (lr {:08X}; W at {:X}, the game's memory at {:X})",
                                    fault, where, pc, c.lr, reinterpret_cast<uintptr_t>(s_w.memory.ram),
                                    reinterpret_cast<uintptr_t>(s_w.game_ram)));
    if (ran == 0)
      return rw::GiveUp(std::format("no native code at {:08X}", pc));
    ++s_w.dispatches;
    if (c.exception != 0 && !Exception(c))
      return rw::GiveUp(s_w.gave_up);
  }
}
}  // namespace

// S2: the worker's thread. It waits for a job, runs it (Run, its hooks waiting for the game's news inside), and tells
// the core it has left it.
struct WorkerThread
{
  std::thread thread;
  std::mutex m;
  std::condition_variable cv;
  bool go = false, quit = false;
  unsigned mxcsr = 0;  // the game's thread's floating-point control, copied at each job's start
  u64 us_running = 0;
};
WorkerThread s_t;

constexpr u32 STATISTICS = 0x23800000;  // the statistics table's pages (study section 1 item 5), taken at every start
constexpr u32 STATISTICS_PAGES = 16;

void ThreadMain()
{
  rw::ThreadStarts();
  // study section 17's general guard: the CPU core's own hooks (the game's hardware) are never W's
  StaticRecompCore::SetForeignThread([](const char* hook) { rw::OffLimits(hook); });
  std::unique_lock lock(s_t.m);
  for (;;)
  {
    s_t.cv.wait(lock, [] { return s_t.go || s_t.quit; });
    if (s_t.quit)
      return;
    s_t.go = false;
    lock.unlock();
    _mm_setcsr(s_t.mxcsr);  // (rounding and flush-to-zero: the game's, study section 1 item 17)
    if (s_w.loaded)
      s_w.loaded(&s_w.cpu);
    // this thread's own pipe (the module's is per thread): what the steps write there is dropped
    s_w.pipe_at = s_w.pipe;
    if (s_w.pipe_per_thread)
      s_w.set_pipe(&s_w.pipe_at, s_w.pipe, &PipeDrop);
    const auto start = std::chrono::steady_clock::now();
    Run();
    PipeDrop(&s_w.cpu);
    s_t.us_running += static_cast<u64>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count());
    rw::Left();
    lock.lock();
  }
}

// A job's start for the thread (the game's thread, at the hook): the context dressed from the game's registers, the
// translated pages asked of the game's MMU now, the floating-point control copied; then the thread is woken.
void StartThreaded(CPUState* cpu)
{
  s_w.game_ram = cpu->ram;
  s_w.threaded = true;
  Dress(*cpu);
  if (s_w.missed != 0)
    s_w.pages.insert(std::exchange(s_w.missed, 0u));
  if (s_w.pages.empty())
    for (u32 k = 0; k < STATISTICS_PAGES; ++k)
      s_w.pages.insert(STATISTICS + k * 0x1000);
  auto& mmu = Core::System::GetInstance().GetMMU();
  for (const u32 page : s_w.pages)
  {
    const std::optional<u32> physical = mmu.GetTranslatedAddress(page);
    s_w.translated.emplace(page, physical ? *physical : 0xFFFFFFFFu);
  }
  {
    const std::lock_guard lock(s_t.m);
    s_t.mxcsr = _mm_getcsr();
    s_t.go = true;
  }
  if (!s_t.thread.joinable())
    s_t.thread = std::thread(&ThreadMain);
  s_t.cv.notify_all();
}

// After the game's network hook (window.cpp): a job prepared there runs now on W, and the game goes on as ever.
void RunRollbackWorker(CPUState* cpu)
{
  LookWatch(cpu);
  static const bool forget = (rw::SetForget([](fzgx::u32 step) { lanes::PartialForget(step); }), true);
  (void)forget;
  Look();  // (at the first hook: the module's pipe per thread or not, before the first job is weighed)
  if (!rw::Begin(&s_w.memory))
    return;
  ++s_w.jobs;
  if (s_w.dispatch == nullptr)
  {
    rw::GiveUp("the native module's dispatch was not found");
    if (s_w.memory.threaded)
      rw::Left();
    return;
  }
  if (s_w.memory.threaded)
    return StartThreaded(cpu);
  s_w.threaded = false;
  s_w.game_ram = cpu->ram;
  Dress(*cpu);
  if (s_w.loaded)
    s_w.loaded(&s_w.cpu);
  s_w.pipe_at = s_w.pipe;
  if (s_w.set_pipe)
    s_w.set_pipe(&s_w.pipe_at, s_w.pipe, &PipeFlush);
  const auto start = std::chrono::steady_clock::now();
  s_w.running = true;
  Run();
  s_w.running = false;
  rw::Count("us running", static_cast<u64>(std::chrono::duration_cast<std::chrono::microseconds>(
                                               std::chrono::steady_clock::now() - start).count()));
  if (s_w.set_pipe && g_static_recomp_core)
    g_static_recomp_core->RestoreGatherPipe();
  if (s_w.pipe_at != s_w.pipe)
    rw::GiveUp(std::format("{} bytes written to the drawing's pipe", s_w.pipe_at - s_w.pipe));
  if (s_w.loaded)
    s_w.loaded(cpu);  // (the host's floating-point control back to the game's)
}

// The emulator's stop: Core::Stop tells it (on the thread that stops the emulator) before it stops the CPU, and
// Core::Shutdown then unloads the native module (StaticRecompCore::Shutdown) before AfterStop runs. A job's steps run
// in that module on the worker's thread, so the thread leaves its job here, and no job begins after.
static Common::EventHook s_stop_hook;

void WatchStopForRollbackWorker()
{
  s_stop_hook = Core::AddOnStateChangedCallback([](Core::State state) {
    if (state == Core::State::Stopping)
      rw::Close();
  });
}

void StopRollbackWorker()
{
  s_stop_hook = {};
  rw::Quiesce();  // (an open job stopped: the thread waits for the next)
  {
    const std::lock_guard lock(s_t.m);
    s_t.quit = true;
  }
  s_t.cv.notify_all();
  if (s_t.thread.joinable())
    s_t.thread.join();
  s_t.quit = false;
  if (s_t.us_running != 0)
    rw::Count("us running on the thread", std::exchange(s_t.us_running, 0));
  rw::Count("dispatches", std::exchange(s_w.dispatches, 0));
  rw::Count("system calls", std::exchange(s_w.system_calls, 0));
  rw::Count("block copies", std::exchange(s_w.block_copies, 0));
  rw::Count("translated", std::exchange(s_w.translated_reads, 0));
  rw::Count("tasks", std::exchange(s_w.tasks, 0));
  rw::Count("pipe bytes dropped", std::exchange(s_w.pipe_dropped, 0));
  rw::Stop();
}
}  // namespace fzgx_port
