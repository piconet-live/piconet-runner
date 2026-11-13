// A mode's message (`say "TEXT";`, protocol modes.toml; 0.6.5): what the runner's overlay shows over the game's picture.
// The game's thread publishes it once a drawn frame from the mode's block (port/core/src/mode_say.cpp: the block's say
// word and pause word, the package's texts); the thread that presents reads it and draws the panel
// (port/moderngekko/src/mode_say.cpp): in the middle of the picture while the race stands, as a line at its foot while
// it runs, eased in and out. Drawing only: nothing here is in a step, and a rollback's block is simply read again.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <atomic>
#include <mutex>
#include <string>

#include "fzgx/guest.h"

namespace fzgx::mode_say
{
struct Board
{
  std::atomic<u32> shown{0};       // the text's number in the mode's say_texts, from 1; 0: no message
  std::atomic<bool> paused{false};  // the race stands (the panel in the middle) or runs (a line at the foot)
  std::atomic<u32> serial{0};      // one more whenever the text below changes
  // the text itself, under the lock (the presenting thread copies it when the serial moves)
  std::mutex lock;
  std::string text;
};
Board& TheBoard();

// What the board shows: `shown` 0 for none. Says whether anything changed.
bool Publish(Board& board, u32 shown, const std::string& text, bool paused);
}  // namespace fzgx::mode_say
