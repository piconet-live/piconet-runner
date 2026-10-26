// Where a thread of the runner is (threads.h).
// SPDX-License-Identifier: GPL-3.0-or-later

#include "threads.h"

#include <atomic>
#include <cstdio>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fzgx_port::threads
{
#ifdef _WIN32
namespace
{
struct Thread
{
  std::atomic<bool> known{false};
  std::atomic<HANDLE> handle{nullptr};
  std::uint64_t stack_top = 0;  // the stack's highest address (it grows down from here)
};
Thread s_threads[static_cast<int>(Which::Count)];

// The copy of a stopped thread's stack: its top 64 KiB, and room after it that a read past the copy lands in.
constexpr std::size_t COPY = 64 * 1024, SLACK = 4096;
alignas(16) unsigned char s_copy[COPY + SLACK];

// The thread stopped, its registers and the top of its stack copied, the thread let go. How many bytes of stack
// were copied; -1: refused. Nothing here allocates or takes a lock: the stopped thread may hold any. The copy reads
// another thread's stack by an address that thread gave when it made itself known: a read that faults (a stack
// that is gone) ends the copy and nothing else (the system's exception handling; no C++ object lives here).
long long Copy(Thread& thread, bool whole, CONTEXT* context)
{
  const HANDLE handle = thread.handle.load();
  if (SuspendThread(handle) == static_cast<DWORD>(-1))
    return -1;
  context->ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
  long long copied = -1;
  if (GetThreadContext(handle, context))
  {
    copied = 0;
    if (whole && context->Rsp < thread.stack_top && thread.stack_top - context->Rsp < (std::uint64_t{1} << 30))
    {
      const std::uint64_t size = thread.stack_top - context->Rsp;
      copied = static_cast<long long>(size < COPY ? size : COPY);
      __try
      {
        std::memcpy(s_copy, reinterpret_cast<const void*>(context->Rsp), static_cast<std::size_t>(copied));
      }
      __except (EXCEPTION_EXECUTE_HANDLER)
      {
        copied = 0;
      }
    }
  }
  ResumeThread(handle);
  return copied;
}

// The call stack from the registers and the stack's copy: the system's own unwinding, reading the copy where the
// thread's stack was. Code with no unwinding record (the emulator's own compiler's) is taken as a function that has
// pushed nothing. A stray read ends the walk (the system's exception handling: this function holds no C++ object).
int Walk(CONTEXT context, long long copied, std::uint64_t* out, int most)
{
  int count = 0;
  const std::uint64_t low = context.Rsp, high = context.Rsp + static_cast<std::uint64_t>(copied);
  const std::uint64_t moved = reinterpret_cast<std::uint64_t>(s_copy) - low;
  const std::uint64_t copy_low = low + moved, copy_high = high + moved;
  DWORD64* const pointers[] = {&context.Rsp, &context.Rbp, &context.Rbx, &context.Rsi, &context.Rdi,
                               &context.R12, &context.R13, &context.R14, &context.R15};
  for (DWORD64* p : pointers)
    if (*p >= low && *p < high)
      *p += moved;
  __try
  {
    while (count < most && context.Rip != 0)
    {
      out[count++] = context.Rip;
      if (context.Rsp < copy_low || context.Rsp + 8 > copy_high)
        break;
      DWORD64 base = 0;
      if (const PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(context.Rip, &base, nullptr))
      {
        void* data = nullptr;
        DWORD64 frame = 0;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, context.Rip, function, &context, &data, &frame, nullptr);
      }
      else
      {
        std::memcpy(&context.Rip, reinterpret_cast<const void*>(context.Rsp), 8);
        context.Rsp += 8;
      }
    }
  }
  __except (EXCEPTION_EXECUTE_HANDLER)
  {
  }
  return count;
}
}  // namespace

void KnowThisThread(Which which)
{
  Thread& thread = s_threads[static_cast<int>(which)];
  if (thread.known.load(std::memory_order_relaxed)) [[likely]]
    return;
  HANDLE handle = nullptr;
  if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &handle,
                       THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, 0))
    handle = nullptr;
  thread.stack_top = reinterpret_cast<std::uint64_t>(reinterpret_cast<const NT_TIB*>(NtCurrentTeb())->StackBase);
  thread.handle.store(handle);
  thread.known.store(true);
}

bool Known(Which which)
{
  const Thread& thread = s_threads[static_cast<int>(which)];
  return thread.known.load() && thread.handle.load() != nullptr;
}

bool Sample(Which which, bool whole, Stack* out)
{
  out->count = 0;
  if (!Known(which))
    return false;
  CONTEXT context{};
  const long long copied = Copy(s_threads[static_cast<int>(which)], whole, &context);
  if (copied < 0)
    return false;
  if (copied < 16)
  {
    out->at[0] = context.Rip;
    out->count = 1;
    return true;
  }
  out->count = Walk(context, copied, out->at.data(), static_cast<int>(out->at.size()));
  return out->count != 0;
}

std::string NameOf(std::uint64_t address)
{
  char text[96];
  HMODULE module = nullptr;
  wchar_t path[1024];
  if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         reinterpret_cast<LPCWSTR>(address), &module) &&
      module != nullptr && GetModuleFileNameW(module, path, 1024) != 0)
  {
    // the file's name alone: its folders are the player's
    const wchar_t* name = path;
    for (const wchar_t* p = path; *p; ++p)
      if (*p == L'\\' || *p == L'/')
        name = p + 1;
    char plain[48];
    std::size_t n = 0;
    for (; name[n] && n + 1 < sizeof(plain); ++n)
      plain[n] = name[n] > 32 && name[n] < 127 ? static_cast<char>(name[n]) : '_';
    plain[n] = 0;
    std::snprintf(text, sizeof(text), "%s+%llX", plain,
                  static_cast<unsigned long long>(address - reinterpret_cast<std::uint64_t>(module)));
  }
  else
    std::snprintf(text, sizeof(text), "?+%llX", static_cast<unsigned long long>(address));
  return text;
}

void Forget()
{
  for (Thread& thread : s_threads)
  {
    thread.known.store(false);
    if (const HANDLE handle = thread.handle.exchange(nullptr))
      CloseHandle(handle);
  }
}
#else
void KnowThisThread(Which) {}
bool Known(Which) { return false; }
bool Sample(Which, bool, Stack* out)
{
  out->count = 0;
  return false;
}
std::string NameOf(std::uint64_t address)
{
  char text[32];
  std::snprintf(text, sizeof(text), "?+%llX", static_cast<unsigned long long>(address));
  return text;
}
void Forget() {}
#endif

std::string Named(const Stack& stack)
{
  std::string text;
  for (int i = 0; i < stack.count; ++i)
    text += (i ? " < " : "") + NameOf(stack.at[static_cast<std::size_t>(i)]);
  return text.empty() ? "not known" : text;
}
}  // namespace fzgx_port::threads
