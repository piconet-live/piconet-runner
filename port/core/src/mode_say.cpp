// A mode's message and its resume countdown over the picture (fzgx/mode_say.h; protocol modes.toml `say`, `resume
// after`): once a drawn frame, from the mode's block as the last step left it. The message is the package's say_texts
// entry the block's say word names; the race stands while its pause word is set. A `resume after`'s steps left are
// the practice's countdown figure (fzgx/panel.h Counting: 3, 2, 1 over its length), except in a practice, whose own
// rewind owns the figure. Nothing is written into the game.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fzgx/mode_say.h"

#include <algorithm>

#include "files.h"
#include "fzgx/panel.h"
#include "internal.h"
#include "mode.h"

namespace fzgx::mode_say
{
Board& TheBoard()
{
  static Board board;
  return board;
}

bool Publish(Board& board, u32 shown, const std::string& text, bool paused)
{
  bool changed = board.paused.exchange(paused, std::memory_order_relaxed) != paused;
  if (board.shown.load(std::memory_order_relaxed) != shown)
  {
    {
      std::lock_guard<std::mutex> hold(board.lock);
      board.text = shown != 0 ? text : std::string{};
    }
    board.shown.store(shown, std::memory_order_relaxed);
    board.serial.fetch_add(1, std::memory_order_release);
    changed = true;
  }
  return changed;
}
}  // namespace fzgx::mode_say

namespace fzgx::mode::say
{
namespace
{
bool s_counting = false;  // this file put the countdown's figure up
}

void Frame(Guest& g)
{
  const boot::Match* match = boot::Get();
  const u32 block = g.Read32(layout::MODES + 4);
  mode_say::Board& board = mode_say::TheBoard();
  if (!match || !match->game_mode || block == 0)
  {
    mode_say::Publish(board, 0, {}, false);
    return;
  }
  const Package& p = *match->game_mode;
  const u32 say = g.Read32(block + SAY);
  const bool paused = g.Read32(block + PAUSED) != 0;
  const u32 at = g.Read32(block + RESUME_AT), runs = g.Read32(block + RUNS), length = g.Read32(block + RESUME_LENGTH);
  const bool counting = paused && at != 0 && at > runs && length != 0;
  const bool known = say != 0 && say <= p.say_texts.size();
  // (during a resume's count the message goes to the foot already: the middle is the figure's)
  mode_say::Publish(board, known ? say : 0, known ? p.say_texts[say - 1] : std::string{}, paused && !counting);
  if (TheOptions().practice)
    return;
  // `resume after`: the figure over its length, 3, 2, 1, while the race still stands
  panel::Board& figure = panel::TheBoard();
  if (counting)
  {
    const u32 left = std::min(at - runs, length);
    panel::Counting(figure, (left * 3 + length - 1) / length);
    s_counting = true;
  }
  else if (s_counting)
  {
    s_counting = false;
    if (figure.Now() == panel::Shows::Countdown)
      panel::Down(figure);
  }
}
}  // namespace fzgx::mode::say
