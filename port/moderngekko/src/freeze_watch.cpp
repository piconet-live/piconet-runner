// A game that stands still in a race says so in its log, and every stop of the game says why. A thread of its own
// looks four times a second at what the game's thread publishes as it goes (fzgx/freeze.h); a healthy frame pays
// nothing here. When no game frame came for 2 s, and a few times after, it writes where the game is: the emulated
// CPU's program counter and link register, sampled several times so that a loop shows; the host code the game's
// thread is in, sampled as often, and its call stack and the picture's thread's (threads.h); and the state of the
// command list's hand-over between the two as the emulator shows it. Everything is read; nothing of the emulated
// machine is written, and its threads are only ever stopped for the copy of a stack. That copy is made in a game
// that has begun no frame for 2 s (5 s in the countdown) and is not held by the core's own wait for the relay,
// which gets one line and no sampling; and once in a game whose emulator has not shut down 5 s after a stop was
// asked for, which may be a game that only shuts down slowly. The watch starts at the game's first picture, so a
// boot that fails has started nothing.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "adapter_internal.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "Core/Core.h"
#include "Core/CoreTiming.h"
#include "Core/HW/ProcessorInterface.h"
#include "Core/PowerPC/PowerPC.h"
#include "Core/System.h"
#include "VideoCommon/CommandProcessor.h"
#include "VideoCommon/Fifo.h"

#include "fzgx/freeze.h"
#include "threads.h"

namespace fzgx_port
{
namespace
{
using Clock = std::chrono::steady_clock;
using fzgx::freeze::Said;

std::atomic<int> s_stops{0};            // stops asked for in this start
std::atomic<long long> s_stop_at{0};    // the first one's time (the steady clock's ticks)
std::atomic<bool> s_started{false};     // the watch and the emulator's lines are on (from the first picture)
// The thread is the last of these to be made and so the first to go when the program ends: it is asked to stop
// and joined while the mutex and the condition it waits on still exist (a start that never reaches EndFreezeWatch).
std::mutex s_wake_mutex;
std::condition_variable_any s_wake;
std::jthread s_watch;

void Say(const std::string& text)
{
  s_core_host.Log(fzgx::LogLevel::Warning, text);
}

// What an address of the emulated CPU is, where the facts table knows: one of the game's waits, the online part.
const char* GuestPlace(u32 pc)
{
  const auto around = [pc](u32 at) { return pc >= at - 0x40 && pc < at + 0x40; };
  if (around(game::IDLE_GPU_SYNC))
    return " (the game's wait for the graphics side: \"draw done\")";
  if (around(game::IDLE_DISC_READ))
    return " (the executable's wait for a disc read)";
  if (around(game::IDLE_LOAD_QUEUE))
    return " (the game's wait for its load queue)";
  if (around(game::IDLE_OS))
    return " (the operating system's idle loop)";
  if (pc >= HOST_CALLS && pc < HOST_CALLS_END)
    return " (a host call)";
  if (pc >= table::WINDOW && pc < table::WINDOW_END)
    return " (the online part's own code)";
  return "";
}

// Values that were sampled several times, as "A x5, B x3" in the order they were first seen.
template <typename T, typename Name>
std::string Counted(const std::vector<T>& samples, Name name)
{
  std::vector<std::pair<T, int>> seen;
  for (const T& sample : samples)
  {
    bool found = false;
    for (auto& [value, count] : seen)
      if (value == sample)
        ++count, found = true;
    if (!found)
      seen.emplace_back(sample, 1);
  }
  std::string text;
  for (const auto& [value, count] : seen)
    text += (text.empty() ? "" : ", ") + name(value) + " x" + std::to_string(count);
  return text;
}

// The emulated CPU and the game's thread, SAMPLES times EVERY apart: a loop shows as several addresses, a wait as one.
constexpr int SAMPLES = 8;
constexpr auto EVERY = std::chrono::milliseconds(10);

void SayWhereTheCpuIs()
{
  Core::System& system = Core::System::GetInstance();
  const PowerPC::PowerPCState& cpu = system.GetPPCState();
  std::vector<std::pair<u32, u32>> guest;
  std::vector<std::uint64_t> host;
  const u64 ticks_before = system.GetCoreTiming().GetTicks();
  const fzgx::u64 hooks_before = s_state.steps, window_before = s_state.window_reads + s_state.window_writes;
  for (int i = 0; i < SAMPLES; ++i)
  {
    if (i != 0)
      std::this_thread::sleep_for(EVERY);
    guest.emplace_back(cpu.pc, LR(cpu));
    if (threads::Stack one; threads::Sample(threads::Which::Game, false, &one))
      host.push_back(one.at[0]);
  }
  const u64 ticks = system.GetCoreTiming().GetTicks() - ticks_before;
  const bool one_place = std::all_of(guest.begin(), guest.end(), [&guest](const auto& at) { return at == guest.front(); });
  Say("fzgx: freeze: emulated CPU, " + std::to_string(SAMPLES) +
      " samples 10 ms apart (as last stored: native code keeps them only at its exits): " +
      Counted(guest, [](const std::pair<u32, u32>& at) {
        char text[96];
        std::snprintf(text, sizeof(text), "pc %08X lr %08X%s", at.first, at.second, GuestPlace(at.first));
        return std::string(text);
      }));
  char text[320];
  std::snprintf(text, sizeof(text),
                "fzgx: freeze: in those %d ms: emulated time advanced %llu ticks, %llu network hook calls, %llu data window "
                "accesses; msr %08X (interrupts %s), pending exceptions %08X, interrupt cause %08X mask %08X (0400: draw "
                "done, 0200: token, 0800: command list, 0100: video)",
                (SAMPLES - 1) * 10, static_cast<unsigned long long>(ticks),
                static_cast<unsigned long long>(s_state.steps - hooks_before),
                static_cast<unsigned long long>(s_state.window_reads + s_state.window_writes - window_before), cpu.msr.Hex,
                cpu.msr.EE ? "on" : "off", cpu.Exceptions, system.GetProcessorInterface().GetCause(),
                system.GetProcessorInterface().GetMask());
  Say(text);
  // one place, interrupts off, and the emulated clock runs: the CPU spins where nothing can interrupt it, which is
  // how the game's system halts after a panic (its own words, when it printed any, are "fzgx: emulator:" lines)
  if (ticks != 0 && !cpu.msr.EE && one_place)
    Say("fzgx: freeze: one place with interrupts off while emulated time runs: a halt loop, as the game's system makes "
        "after a panic; the game's own report lines above (\"fzgx: emulator: N[OSREPORT]\"), if any, say which");
  if (!host.empty())
    Say("fzgx: freeze: the game's thread, host code, " + std::to_string(host.size()) + " samples: " +
        Counted(host, [](std::uint64_t at) { return threads::NameOf(at); }));
}

void SayTheStacks()
{
  const std::pair<threads::Which, const char*> both[] = {{threads::Which::Game, "the game's thread"},
                                                         {threads::Which::Picture, "the picture's thread"}};
  for (const auto& [which, name] : both)
  {
    threads::Stack stack;
    if (threads::Sample(which, true, &stack))
      Say(std::string("fzgx: freeze: ") + name + ", call stack (innermost first): " + threads::Named(stack));
    else
      Say(std::string("fzgx: freeze: ") + name + ": no call stack (" +
          (threads::Known(which) ? "the system refused" : "the thread is not known") + ")");
  }
}

// The command list between the CPU's thread and the picture's, as the emulator's command processor holds it.
void SayTheHandOver()
{
  Core::System& system = Core::System::GetInstance();
  const CommandProcessor::SCPFifoStruct& fifo = system.GetCommandProcessor().GetFifo();
  const auto state = Core::GetState(system);
  char text[480];
  std::snprintf(text, sizeof(text),
                "fzgx: freeze: command list: write %08X read %08X (safe %08X) distance %u, base %08X end %08X, breakpoint "
                "%08X; read enable %u, link %u, breakpoint enable %u, at breakpoint %s; high watermark %u, low %u; a command "
                "processor interrupt %s; picture on its own thread %s, deterministic %s, sync %s; emulator %s",
                fifo.CPWritePointer.load(), fifo.CPReadPointer.load(), fifo.SafeCPReadPointer.load(),
                fifo.CPReadWriteDistance.load(), fifo.CPBase.load(), fifo.CPEnd.load(), fifo.CPBreakpoint.load(),
                fifo.bFF_GPReadEnable.load(), fifo.bFF_GPLinkEnable.load(), fifo.bFF_BPEnable.load(),
                Fifo::AtBreakpoint(system) ? "yes" : "no", fifo.bFF_HiWatermark.load(), fifo.bFF_LoWatermark.load(),
                system.GetCommandProcessor().IsInterruptWaiting() ? "waits" : "does not wait",
                system.IsDualCoreMode() ? "yes" : "no", system.GetFifo().UseDeterministicGPUThread() ? "yes" : "no",
                system.GetFifo().UseSyncGPU() ? "yes" : "no",
                state == Core::State::Running ? "running" : state == Core::State::Paused ? "paused" :
                state == Core::State::Stopping ? "stopping" : state == Core::State::Starting ? "starting" : "not started");
  Say(text);
}

void SayWhereTheGameIs()
{
  SayWhereTheCpuIs();
  SayTheStacks();
  SayTheHandOver();
}

constexpr const char* UNASKED = "the emulator stops with no stop asked for by the window, the keyboard or the game (a "
                                "test's stop command, or the emulator's own)";

// A stop was asked for and the emulator has not shut down this long after: said once, with where its threads are.
constexpr auto SHUTDOWN_SECONDS = std::chrono::seconds(5);

void WatchLoop(std::stop_token stop)
{
  fzgx::freeze::Watch watch;
  fzgx::freeze::Pulse& pulse = fzgx::freeze::ThePulse();
  bool late_said = false;
  while (!stop.stop_requested())
  {
    {
      std::unique_lock<std::mutex> lock(s_wake_mutex);
      s_wake.wait_for(lock, stop, std::chrono::milliseconds(250), [] { return false; });
    }
    if (stop.stop_requested())
      return;
    const auto now = Clock::now();
    // the emulator stops and nothing that passes NoteStop asked for it: said here, while it shuts down (a shutdown
    // runs no frame, and is no freeze)
    if (!pulse.stopping && Core::GetState(Core::System::GetInstance()) == Core::State::Stopping)
      NoteStop(UNASKED, fzgx::board::StopReason::Unasked);
    fzgx::board::Look();  // (the launcher's status board: this thread lives, whatever the game's does)
    const Said said = watch.Look(pulse, now);
    if (said.what == Said::What::Stands)
    {
      FlushEmulatorLines();  // (what the game or the emulator said last stands before the report)
      Say(fzgx::freeze::StandsLine(said, pulse));
      // the core's own wait for the relay is known for what it is: one line, and no thread is stopped for it
      if (!said.wait)
      {
        fzgx::board::Stands(static_cast<fzgx::u32>(said.seconds * 1000), said.report);
        SayWhereTheGameIs();
      }
    }
    else if (said.what == Said::What::MovesAgain || said.what == Said::What::Left)
    {
      if (!said.wait)
        fzgx::board::Moves(static_cast<fzgx::u32>(said.seconds * 1000));
      Say(fzgx::freeze::MovesLine(said));
    }
    if (const long long asked = s_stop_at.load(); asked != 0 && !late_said &&
        now.time_since_epoch() - Clock::duration(asked) > SHUTDOWN_SECONDS)
    {
      late_said = true;
      Say("fzgx: stop: the emulator has not shut down 5 s after the stop was asked for; where its threads are:");
      SayWhereTheGameIs();
    }
  }
}
}  // namespace

// At the game's first picture, on the CPU's thread, and from then on one flag read a picture: the watch's thread
// and the emulator's lines. Not before the boot: a boot that fails runs no picture and so starts neither, and
// nothing is left on when the runner then ends without AfterStop.
void StartWatches()
{
  if (s_started.load(std::memory_order_relaxed)) [[likely]]
    return;
  if (s_started.exchange(true))
    return;
  TakeEmulatorLines();
  s_watch = std::jthread(WatchLoop);
}

// Every way the game is asked to stop comes through here, before the shutdown: one line that names it. The first
// also ends the freeze watch and keeps whether the game stood (the summary line's "stuck").
void NoteStop(const std::string& why, fzgx::board::StopReason reason)
{
  fzgx::board::Stopped(reason);  // (the first ask's reason stays on the launcher's status board)
  fzgx::freeze::Pulse& pulse = fzgx::freeze::ThePulse();
  const bool stood = pulse.standing;
  const fzgx::u32 stood_ms = pulse.standing_ms;  // the freeze of this moment, not the race's longest
  const bool first = fzgx::freeze::Stop(pulse);
  const int n = ++s_stops;
  if (first)
    s_stop_at = Clock::now().time_since_epoch().count();
  if (n > 4)
    return;  // (a held key repeats)
  FlushEmulatorLines();
  s_core_host.Log(fzgx::LogLevel::Notice, fzgx::freeze::StopLine(why, first, stood, stood_ms));
}

#ifdef _WIN32
namespace
{
WindowMessage s_next_message = nullptr;
// What asked the window to close, from that until the close message it leads to; none: the message came from
// outside the window.
const char* s_close_by = nullptr;
fzgx::board::StopReason s_close_reason = fzgx::board::StopReason::OutsideClose;  // and the board's word for it

bool WatchedMessage(void* window, unsigned message, std::uintptr_t wparam, std::intptr_t lparam, std::intptr_t* answer)
{
  // the system's close command: Alt+F4, the window's menu, the taskbar's "Close window"
  if (message == WM_SYSCOMMAND && (wparam & 0xFFF0) == SC_CLOSE)
  {
    const bool keys = (GetKeyState(VK_MENU) & 0x8000) != 0 && (GetKeyState(VK_F4) & 0x8000) != 0;
    s_close_by = keys ? "Alt+F4" : "the system's close command (the window's menu or the taskbar)";
    s_close_reason = keys ? fzgx::board::StopReason::AltF4 : fzgx::board::StopReason::SystemClose;
  }
  if (message == WM_CLOSE)
  {
    // (the emulator's own handling of the message stops the game: said before it does)
    NoteStop(std::string("the game's window was asked to close: ") +
                 (s_close_by ? s_close_by : "a close message from outside the window (another program, the launcher)"),
             s_close_by ? s_close_reason : fzgx::board::StopReason::OutsideClose);
    s_close_by = nullptr;
  }
  return s_next_message(window, message, wparam, lparam, answer);
}
}  // namespace

WindowMessage WatchStops(WindowMessage next)
{
  s_next_message = next;
  return &WatchedMessage;
}

void NoteStripClose()
{
  s_close_by = "the strip's close button";
  s_close_reason = fzgx::board::StopReason::StripClose;
}
#endif

void EndFreezeWatch()
{
  if (!s_started.exchange(false))
    return;  // (no picture was ever shown: nothing was started, and there is no stop to say)
  const bool unasked = s_stops == 0;  // (the watch did not see it stop: said now)
  if (unasked)
    NoteStop(UNASKED, fzgx::board::StopReason::Unasked);
  if (s_watch.joinable())
  {
    s_watch.request_stop();
    s_wake.notify_all();
    s_watch.join();
  }
  if (const long long asked = s_stop_at.load(); asked != 0 && !unasked)
  {
    char text[120];
    std::snprintf(text, sizeof(text), "fzgx: stop: the emulator has shut down, %.1f s after the stop was asked for",
                  std::chrono::duration<double>(Clock::now().time_since_epoch() - Clock::duration(asked)).count());
    s_core_host.Log(fzgx::LogLevel::Notice, text);
  }
  threads::Forget();
}
}  // namespace fzgx_port
