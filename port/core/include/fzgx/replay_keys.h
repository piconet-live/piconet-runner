// The replay scrubber's keys (fzgx/host.h, ReplayKey) from a key of the game's window: a Windows virtual-key code and
// whether the message is a repeat of a key held down. Plain table, so that the runner and the core's tests share it.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>

#include "fzgx/host.h"

namespace fzgx
{
// What a key-down message is to the scrubber: `ours` when the key is one of its keys (the window then takes it, repeat
// or not), and the ReplayKey bit it presses (0: a repeat of a key that acts once a press).
struct ReplayKeyPress
{
  bool ours = false;
  u32 bit = 0;
};

inline ReplayKeyPress ReplayKeyOf(std::uintptr_t key, bool repeat)
{
  struct Key
  {
    std::uintptr_t key;  // Windows' virtual-key code
    u32 bit;
    bool repeats;        // repeats while held, at the system's key-repeat rate
  };
  constexpr Key KEYS[] = {
      {0x20, KeyPause, false},      {0x25, KeyBack, true},        {0x27, KeyAhead, true},  // Space, Left, Right
      {0x24, KeyFirst, false},      {0x23, KeyLast, false},                                // Home, End
      {0xBE, KeyFrameAhead, true},  {0xBC, KeyFrameBack, true},                            // . and ,
      {0xDB, KeyRacerBack, false},  {0xDD, KeyRacerAhead, false},                          // [ and ]
  };
  for (const Key& k : KEYS)
  {
    if (k.key == key)
      return {true, !repeat || k.repeats ? k.bit : 0u};
  }
  return {};
}
}  // namespace fzgx
