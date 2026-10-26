// The undo log's guard (page_guard.h): the pages' bits, the handler, and the system's page protection.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "page_guard.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cstdint>

#ifdef _WIN32
#include <windows.h>
#else
#include <csignal>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace fzgx::net::guard
{
namespace
{
constexpr u32 WORDS = MOST_PAGES / 64;
// What the handler reads. Fixed arrays, never freed: a fault on another thread may read them at any moment.
std::atomic<std::uintptr_t> s_base{0};  // the memory of the last guard (0: none since the process began)
std::atomic<bool> s_active{false};      // a guard is on (between Begin and End)
std::atomic<std::size_t> s_bytes{0};
std::atomic<u64> s_seen[WORDS];     // a bit a page: protected by this guard at some time since Begin
std::atomic<u64> s_guarded[WORDS];  // a bit a page: protected by this guard, and not yet opened
std::atomic<u64> s_written[WORDS];  // a bit a page: its first write was caught since the last TakeWritten
std::atomic<u64> s_faults{0};
// faults on other threads than Begin's: their count, and the first few pages
constexpr u32 OTHER_PAGES = 16;
std::atomic<u64> s_home{0};
std::atomic<u64> s_other_faults{0};
std::atomic<u32> s_other_pages[OTHER_PAGES];

u64 ThisThread()
{
#ifdef _WIN32
  return GetCurrentThreadId();
#else
  return static_cast<u64>(syscall(SYS_gettid));  // a system call: safe in a signal handler
#endif
}

bool Writable(std::uintptr_t page_address, bool writable)
{
#ifdef _WIN32
  DWORD old = 0;
  return VirtualProtect(reinterpret_cast<void*>(page_address), GUARD_PAGE, writable ? PAGE_READWRITE : PAGE_READONLY,
                        &old) != 0;
#else
  return mprotect(reinterpret_cast<void*>(page_address), GUARD_PAGE, writable ? PROT_READ | PROT_WRITE : PROT_READ) ==
         0;
#endif
}

// The handler's question: is this a write to a page the guard protected? If so the page is noted, made writable, and
// the write may go on. A page the guard protected is the guard's until the next Begin: a fault on it that is handled
// late (another thread's fault on the same page opened it meanwhile, or End did) only makes it writable again, which
// it is. Only atomics and the protection call: safe on any thread, and in a signal handler.
bool Caught(std::uintptr_t address)
{
  const std::uintptr_t base = s_base.load(std::memory_order_acquire);
  if (base == 0 || address < base || address - base >= s_bytes.load(std::memory_order_relaxed))
    return false;
  const u32 page = static_cast<u32>((address - base) / GUARD_PAGE);
  const u64 bit = u64{1} << (page % 64);
  if ((s_seen[page / 64].load(std::memory_order_acquire) & bit) == 0)
    return false;
  if (s_active.load(std::memory_order_acquire))
    s_written[page / 64].fetch_or(bit, std::memory_order_acq_rel);
  s_faults.fetch_add(1, std::memory_order_relaxed);
  if (ThisThread() != s_home.load(std::memory_order_relaxed))
  {
    const u64 k = s_other_faults.fetch_add(1, std::memory_order_relaxed);
    if (k < OTHER_PAGES)
      s_other_pages[k].store(page, std::memory_order_relaxed);
  }
  if (!Writable(base + std::uintptr_t{page} * GUARD_PAGE, true))
    return false;  // the system refused (the memory is gone): the fault goes on to the next handler, a crash as before
  s_guarded[page / 64].fetch_and(~bit, std::memory_order_acq_rel);
  return true;
}

#ifdef _WIN32
PVOID s_handler = nullptr;

LONG NTAPI Handler(PEXCEPTION_POINTERS pointers)
{
  const EXCEPTION_RECORD* record = pointers->ExceptionRecord;
  if (record->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || record->NumberParameters < 2 ||
      record->ExceptionInformation[0] != 1)  // 1: a write
    return EXCEPTION_CONTINUE_SEARCH;
  return Caught(static_cast<std::uintptr_t>(record->ExceptionInformation[1])) ? EXCEPTION_CONTINUE_EXECUTION
                                                                                 : EXCEPTION_CONTINUE_SEARCH;
}

// Vectored handlers are asked in turn until one handles the fault; every other handler here (the emulator's) passes
// on a fault it does not know, so the order does not matter. Installed once, first in line.
bool Install(std::string* why)
{
  if (s_handler != nullptr)
    return true;
  s_handler = AddVectoredExceptionHandler(1, Handler);
  if (s_handler == nullptr)
    *why = "the system refused the fault handler";
  return s_handler != nullptr;
}
#else
struct sigaction s_before{};  // the action that was in place when ours was installed: the next in the chain
bool s_installed = false;

void Handler(int sig, siginfo_t* info, void* context)
{
  if (info != nullptr && info->si_code == SEGV_ACCERR && Caught(reinterpret_cast<std::uintptr_t>(info->si_addr)))
    return;
  if (info != nullptr && info->si_code <= 0)
  {
    // Sent, not a fault: the emulator's handler, asked first, put its own predecessor (this one) back and raised the
    // signal again to hand a fault on. The faulting instruction runs again on return and faults again, now with
    // this handler in place, which then sees its address.
    return;
  }
  // a fault that is not ours: the action before ours takes it
  if ((s_before.sa_flags & SA_SIGINFO) != 0 && s_before.sa_sigaction != nullptr)
  {
    s_before.sa_sigaction(sig, info, context);
    return;
  }
  if (s_before.sa_handler != SIG_DFL && s_before.sa_handler != SIG_IGN && s_before.sa_handler != nullptr)
  {
    s_before.sa_handler(sig);
    return;
  }
  // the default: put it in place, and the instruction faults again on return under it (the process ends as it would
  // have without the guard)
  struct sigaction fallback{};
  fallback.sa_handler = SIG_DFL;
  sigemptyset(&fallback.sa_mask);
  sigaction(sig, &fallback, nullptr);
}

// Installed at every Begin unless it is the action in place already: an emulator that installed its own after this
// one (at a boot) is then the next in the chain, and is asked about every fault that is not ours.
bool Install(std::string* why)
{
  struct sigaction now{};
  if (sigaction(SIGSEGV, nullptr, &now) != 0)
  {
    *why = "the signal action could not be read";
    return false;
  }
  if (s_installed && (now.sa_flags & SA_SIGINFO) != 0 && now.sa_sigaction == Handler)
    return true;
  struct sigaction mine{};
  mine.sa_sigaction = Handler;
  mine.sa_flags = SA_SIGINFO;
  sigemptyset(&mine.sa_mask);
  struct sigaction before{};
  if (sigaction(SIGSEGV, &mine, &before) != 0)
  {
    *why = "the system refused the signal handler";
    return false;
  }
  s_before = before;
  s_installed = true;
  return true;
}
#endif
}  // namespace

bool Begin(u8* base, std::size_t bytes, std::string* why)
{
  End();
  const auto address = reinterpret_cast<std::uintptr_t>(base);
#ifndef _WIN32
  if (sysconf(_SC_PAGESIZE) != static_cast<long>(GUARD_PAGE))
  {
    *why = "the system's pages are not 4 KiB";
    return false;
  }
#endif
  if (address == 0 || address % GUARD_PAGE != 0 || bytes % GUARD_PAGE != 0 || bytes / GUARD_PAGE > MOST_PAGES)
  {
    *why = "the memory is not whole pages, or too large";
    return false;
  }
  if (!Install(why))
    return false;
  s_base.store(0, std::memory_order_release);  // (the last guard's pages are all open: End)
  for (u32 w = 0; w < WORDS; ++w)
  {
    s_seen[w].store(0, std::memory_order_relaxed);
    s_guarded[w].store(0, std::memory_order_relaxed);
    s_written[w].store(0, std::memory_order_relaxed);
  }
  s_faults.store(0, std::memory_order_relaxed);
  s_other_faults.store(0, std::memory_order_relaxed);
  s_home.store(ThisThread(), std::memory_order_relaxed);
  s_bytes.store(bytes, std::memory_order_relaxed);
  s_base.store(address, std::memory_order_release);
  s_active.store(true, std::memory_order_release);
  return true;
}

bool Protect(u32 page)
{
  const std::uintptr_t base = s_base.load(std::memory_order_acquire);
  if (!s_active.load(std::memory_order_acquire) || page >= s_bytes.load(std::memory_order_relaxed) / GUARD_PAGE)
    return false;
  const u64 bit = u64{1} << (page % 64);
  // marked first: a write the moment the protection lands is caught, never refused
  s_seen[page / 64].fetch_or(bit, std::memory_order_acq_rel);
  s_guarded[page / 64].fetch_or(bit, std::memory_order_acq_rel);
  if (Writable(base + std::uintptr_t{page} * GUARD_PAGE, false))
    return true;
  s_guarded[page / 64].fetch_and(~bit, std::memory_order_acq_rel);
  return false;
}

void Open(u32 page)
{
  const std::uintptr_t base = s_base.load(std::memory_order_acquire);
  if (!s_active.load(std::memory_order_acquire) || page >= s_bytes.load(std::memory_order_relaxed) / GUARD_PAGE)
    return;
  const u64 bit = u64{1} << (page % 64);
  if ((s_guarded[page / 64].load(std::memory_order_acquire) & bit) == 0)
    return;
  Writable(base + std::uintptr_t{page} * GUARD_PAGE, true);
  s_guarded[page / 64].fetch_and(~bit, std::memory_order_acq_rel);
}

void TakeWritten(std::vector<u32>* pages)
{
  pages->clear();
  const u32 words = static_cast<u32>((s_bytes.load(std::memory_order_relaxed) / GUARD_PAGE + 63) / 64);
  for (u32 w = 0; w < words; ++w)
  {
    if (s_written[w].load(std::memory_order_relaxed) == 0)
      continue;
    for (u64 bits = s_written[w].exchange(0, std::memory_order_acq_rel); bits != 0; bits &= bits - 1)
    {
      int b = 0;
      while (((bits >> b) & 1) == 0)
        ++b;
      pages->push_back(w * 64 + static_cast<u32>(b));
    }
  }
}

void End()
{
  if (!s_active.load(std::memory_order_acquire))
    return;
  const u32 words = static_cast<u32>((s_bytes.load(std::memory_order_relaxed) / GUARD_PAGE + 63) / 64);
  for (u32 w = 0; w < words; ++w)
  {
    for (u64 bits = s_guarded[w].load(std::memory_order_acquire); bits != 0; bits &= bits - 1)
    {
      int b = 0;
      while (((bits >> b) & 1) == 0)
        ++b;
      Open(w * 64 + static_cast<u32>(b));
    }
  }
  // the memory and the pages' bits stay until the next Begin, for a fault that is handled late (Caught)
  s_active.store(false, std::memory_order_release);
}

void Abandon()
{
  s_active.store(false, std::memory_order_release);
  s_base.store(0, std::memory_order_release);  // a fault there now is another allocation's: never caught
  for (u32 w = 0; w < WORDS; ++w)
  {
    s_seen[w].store(0, std::memory_order_relaxed);
    s_guarded[w].store(0, std::memory_order_relaxed);
  }
}

bool Active()
{
  return s_active.load(std::memory_order_acquire);
}

u64 Faults()
{
  return s_faults.load(std::memory_order_relaxed);
}

u32 Guarded()
{
  u32 pages = 0;
  const u32 words = static_cast<u32>((s_bytes.load(std::memory_order_relaxed) / GUARD_PAGE + 63) / 64);
  for (u32 w = 0; Active() && w < words; ++w)
    pages += static_cast<u32>(std::popcount(s_guarded[w].load(std::memory_order_relaxed)));
  return pages;
}

u64 OtherThreadFaults()
{
  return s_other_faults.load(std::memory_order_relaxed);
}

u32 OtherThreadPages(u32* pages, u32 most)
{
  const u32 n = static_cast<u32>(std::min<u64>({OtherThreadFaults(), OTHER_PAGES, most}));
  for (u32 i = 0; i < n; ++i)
    pages[i] = s_other_pages[i].load(std::memory_order_relaxed);
  return n;
}
}  // namespace fzgx::net::guard
