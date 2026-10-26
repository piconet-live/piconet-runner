// Tests of the undo log's guard (page_guard.h): pages write-protected, the first write to each caught on any thread,
// and the handler chained with another one for the same faults, installed before it or after it, as the emulator's
// own is (Dolphin's MemTools: it handles faults of its own memory, and hands any other on by putting the action
// before it back in place and raising the signal again).
// SPDX-License-Identifier: GPL-3.0-or-later

#include "harness.h"

#include <csignal>

#include "page_guard.h"

namespace
{
constexpr std::size_t PAGES = 32, P = net::guard::GUARD_PAGE;

u8* Pages(std::size_t count)
{
  void* p = mmap(nullptr, count * P, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  return p == MAP_FAILED ? nullptr : static_cast<u8*>(p);
}

std::vector<u32> Written()
{
  std::vector<u32> pages;
  net::guard::TakeWritten(&pages);
  return pages;
}

// The emulator's handler, as Dolphin's MemTools does it: its own memory's faults handled (here: made writable and
// counted), any other handed on by putting the action before it back and raising the signal again.
struct Emulator
{
  static inline u8* memory = nullptr;
  static inline std::atomic<int> handled{0};
  static inline struct sigaction before{};
  static void Handler(int sig, siginfo_t* info, void*)
  {
    const auto at = reinterpret_cast<std::uintptr_t>(info->si_addr);
    const auto from = reinterpret_cast<std::uintptr_t>(memory);
    if ((info->si_code == SEGV_ACCERR || info->si_code == SEGV_MAPERR) && at >= from && at < from + 4 * P)
    {
      mprotect(memory, 4 * P, PROT_READ | PROT_WRITE);
      ++handled;
      return;
    }
    sigaction(sig, &before, nullptr);
    raise(sig);
  }
  static void Install()
  {
    if (memory == nullptr)
      memory = Pages(4);
    struct sigaction mine{};
    mine.sa_sigaction = Handler;
    mine.sa_flags = SA_SIGINFO;
    sigemptyset(&mine.sa_mask);
    sigaction(SIGSEGV, &mine, &before);
  }
  // its memory made read-only again, and a write to it that its handler must take
  static void Fault()
  {
    mprotect(memory, 4 * P, PROT_READ);
    const int was = handled;
    memory[P + 7] = 0x5A;
    CHECK(handled == was + 1 && memory[P + 7] == 0x5A);
  }
};
}  // namespace

void TestThePageGuard()
{
  u8* memory = Pages(PAGES);
  std::string why;
  CHECK(!net::guard::Begin(memory + 8, PAGES * P, &why) && !why.empty());  // not page-aligned
  CHECK(net::guard::Begin(memory, PAGES * P, &why) && net::guard::Active());
  for (u32 page = 4; page < 20; ++page)
    CHECK(net::guard::Protect(page));
  // the first write to a protected page is caught and goes through; later writes, and writes elsewhere, do not fault
  memory[5 * P + 8] = 1;
  CHECK(memory[5 * P + 8] == 1 && Written() == std::vector<u32>{5});
  memory[5 * P + 9] = 2;
  memory[2 * P] = 3;
  memory[25 * P] = 4;
  CHECK(Written().empty() && net::guard::Faults() == 1 && net::guard::OtherThreadFaults() == 0);  // this thread's
  // reading a protected page does not fault
  u32 sum = 0;
  for (std::size_t i = 6 * P; i < 7 * P; ++i)
    sum += memory[i];
  CHECK(sum == 0 && Written().empty());
  // other threads' writes, four at once on the same pages, many times each
  std::vector<std::thread> threads;
  for (int k = 0; k < 4; ++k)
    threads.emplace_back([memory, k] {
      for (int n = 0; n < 1000; ++n)
        for (u32 page = 10; page < 14; ++page)
          memory[page * P + static_cast<u32>(k) * 64 + static_cast<u32>(n % 64)] = static_cast<u8>(n);
    });
  for (std::thread& thread : threads)
    thread.join();
  CHECK((Written() == std::vector<u32>{10, 11, 12, 13}));
  CHECK(net::guard::Faults() >= 5 && net::guard::Faults() <= 5 + 3 * 4);  // a page's other threads may fault once each
  // those were other threads' than Begin's: counted, their pages named
  u32 pages[8] = {};
  const u32 named = net::guard::OtherThreadPages(pages, 8);
  CHECK(net::guard::OtherThreadFaults() == net::guard::Faults() - 1 && named == std::min<u64>(8, net::guard::Faults() - 1));
  for (u32 i = 0; i < named; ++i)
    CHECK(pages[i] >= 10 && pages[i] < 14);
  CHECK(memory[13 * P + 3 * 64 + 999 % 64] == static_cast<u8>(999));
  // a page opened by the log (it compares it at every step from then on) is not caught
  net::guard::Open(15);
  memory[15 * P] = 9;
  CHECK(Written().empty());
  // a memcpy across three pages, two of them protected: both caught
  std::vector<u8> bytes(2 * P + 100, 0xEE);
  std::memcpy(memory + 16 * P + 50, bytes.data(), bytes.size());
  CHECK((Written() == std::vector<u32>{16, 17, 18}) && memory[18 * P + 149] == 0xEE);
  // ended: every page writable, nothing caught
  net::guard::End();
  CHECK(!net::guard::Active());
  memory[19 * P] = 7;
  memory[4 * P] = 8;
  CHECK(memory[19 * P] == 7 && memory[4 * P] == 8 && Written().empty());
  CHECK(!net::guard::Protect(4));  // no guard: nothing protected
  memory[4 * P + 1] = 1;
  munmap(memory, PAGES * P);
}

// Whichever of the two handlers was installed last, each gets its own faults: the emulator's memory is the
// emulator's, the guard's pages the guard's.
void TestThePageGuardBesideTheEmulatorsHandler()
{
  struct sigaction original{};
  sigaction(SIGSEGV, nullptr, &original);
  u8* memory = Pages(PAGES);
  std::string why;

  // the emulator's first (it installs its handler at a boot; the guard begins at a race): the guard's on top
  Emulator::Install();
  CHECK(net::guard::Begin(memory, PAGES * P, &why));
  CHECK(net::guard::Protect(3) && net::guard::Protect(4));
  memory[3 * P] = 1;
  CHECK(Written() == std::vector<u32>{3});
  Emulator::Fault();  // handed on by the guard's handler to the emulator's
  memory[4 * P] = 1;
  CHECK(Written() == std::vector<u32>{4});

  // the emulator's after the guard's (a second boot in the same process): the guard's page is still caught (the
  // emulator's handler hands it back by putting the guard's in place and raising again)...
  Emulator::Install();
  CHECK(net::guard::Protect(5));
  memory[5 * P] = 1;
  CHECK(Written() == std::vector<u32>{5} && memory[5 * P] == 1);
  // ... and the next Begin puts the guard's in front again, the emulator's behind it
  Emulator::Install();
  CHECK(net::guard::Begin(memory, PAGES * P, &why));
  CHECK(net::guard::Protect(6));
  Emulator::Fault();
  memory[6 * P] = 1;
  CHECK(Written() == std::vector<u32>{6});
  Emulator::Fault();
  net::guard::End();
  munmap(memory, PAGES * P);
  sigaction(SIGSEGV, &original, nullptr);
}

namespace
{
// The protection /proc/self/maps gives the page at `at` ("r--p", "rw-p"; empty: not mapped).
std::string Protection(const u8* at)
{
  std::ifstream maps("/proc/self/maps");
  const auto address = reinterpret_cast<std::uintptr_t>(at);
  for (std::string line; std::getline(maps, line);)
  {
    std::uintptr_t lo = 0, hi = 0;
    char perms[5] = {};
    if (std::sscanf(line.c_str(), "%zx-%zx %4s", &lo, &hi, perms) == 3 && address >= lo && address < hi)
      return perms;
  }
  return {};
}

// The guard's memory let go with pages still protected, and its addresses then another allocation's, read-only (as
// code would be): what the guard does there afterwards.
u8* GoneAndTakenAgain(u8* memory)
{
  munmap(memory, PAGES * P);
  void* again = mmap(memory, PAGES * P, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
  return again == MAP_FAILED ? nullptr : static_cast<u8*>(again);
}
}  // namespace

// The game closed mid-race: the runner's emulator lets the game's memory go before the core's Shutdown, which ends
// the undo log and its guard. Told first (fzgx::MemoryGone), the guard touches no protection there; untold, its end
// makes writable whatever stands at those addresses by then (the control).
void TestThePageGuardWhenItsMemoryIsGone()
{
  std::string why;
  u8* memory = Pages(PAGES);
  CHECK(net::guard::Begin(memory, PAGES * P, &why) && net::guard::Protect(3) && net::guard::Protect(4));
  u8* other = GoneAndTakenAgain(memory);
  CHECK(other == memory && Protection(other + 3 * P) == "r--p");
  net::guard::End();
  CHECK(Protection(other + 3 * P) == "rw-p" && Protection(other + 4 * P) == "rw-p");  // the control: touched
  munmap(other, PAGES * P);

  memory = Pages(PAGES);
  CHECK(net::guard::Begin(memory, PAGES * P, &why) && net::guard::Protect(3) && net::guard::Protect(4));
  other = GoneAndTakenAgain(memory);
  fzgx::MemoryGone();
  CHECK(!net::guard::Active() && !net::guard::Protect(5));
  net::guard::End();  // (as the undo log's stop does after it)
  net::guard::Open(3);
  CHECK(Protection(other + 3 * P) == "r--p" && Protection(other + 4 * P) == "r--p" &&
        Protection(other + 5 * P) == "r--p");
  munmap(other, PAGES * P);
}
