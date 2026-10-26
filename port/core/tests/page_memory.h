// The tests' memory in whole pages of its own (TestHost::ram), which the undo log's guard can write-protect.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <new>

#include <sys/mman.h>

// Memory in whole pages of its own (mmap), as an emulator's mapping of the game's memory is: what the undo log's guard
// can write-protect (TestHost::guardable). Copies are new pages.
template <typename T>
struct PageMemory
{
  using value_type = T;
  PageMemory() = default;
  template <typename U>
  PageMemory(const PageMemory<U>&)
  {
  }
  static std::size_t Bytes(std::size_t n) { return (n * sizeof(T) + 0xFFF) & ~std::size_t{0xFFF}; }
  T* allocate(std::size_t n)
  {
    void* p = mmap(nullptr, Bytes(n), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED)
      throw std::bad_alloc();
    return static_cast<T*>(p);
  }
  void deallocate(T* p, std::size_t n) { munmap(p, Bytes(n)); }
  template <typename U>
  bool operator==(const PageMemory<U>&) const
  {
    return true;
  }
};
