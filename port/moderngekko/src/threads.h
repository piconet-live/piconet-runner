// Where a thread of the runner is, read from another thread: for the freeze report (freeze_watch.cpp). The game's
// thread and the picture's thread make themselves known once; the watching thread may then stop one for a moment,
// copy its registers and the top of its stack, let it go on, and work out the call stack from the copy. Nothing of
// the emulated machine is read or written here. Windows only; elsewhere nothing is known and no sample is taken.
// When it samples (freeze_watch.cpp): in a game that has begun no frame for 2 s or longer in a race (5 s in the
// countdown) and is not held by the core's own wait for the relay; and once, 5 s after a stop was asked for, in a
// game whose emulator has not shut down by then, which may be one that only shuts down slowly.
// Known limit: working out the stack asks the system for each function's unwinding record and each address's
// file, which take the loader's lock; and the lines are written with the C library, which takes the heap's. A
// thread that stands still holding one of them makes the watcher stand with it, and nothing more is written.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace fzgx_port::threads
{
enum class Which { Game, Picture, Count };

// On the thread itself, as often as it likes: the first call keeps a handle and the stack's bounds; every later
// one is one load of a flag.
void KnowThisThread(Which which);
bool Known(Which which);

// The addresses of host code a thread is in: `count` of them, the innermost first (the first is where it runs or
// waits; the others are where its callers go on).
struct Stack
{
  std::array<std::uint64_t, 24> at{};
  int count = 0;
};
// The thread is stopped for the copy only. With `whole` false only the innermost address is taken. False: the
// thread is not known, or the system refused.
bool Sample(Which which, bool whole, Stack* out);

// An address as "program-or-library+offset" (the offset as the file's map lists it, less its base), by the file's
// name alone; "?+ADDRESS" for code no file holds (the emulator's own compiler's).
std::string NameOf(std::uint64_t address);
// The stack's addresses so named, innermost first, separated by " < ".
std::string Named(const Stack& stack);
// Forgets both threads (the game has stopped).
void Forget();
}  // namespace fzgx_port::threads
