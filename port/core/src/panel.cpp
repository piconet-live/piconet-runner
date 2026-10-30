// The panel over the picture before a race (fzgx/panel.h): what it shows and when, and its lines.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fzgx/panel.h"

#include <algorithm>

namespace fzgx::panel
{
namespace
{
// The game's mode that holds a course (MD_GAME), and the phases of it in which the course is drawn: the course intro
// (COURSE_VIEW), the race's start, the race.
constexpr u32 MODE_GAME = 2, PHASE_RACE = 0x29;

bool Show(Board& board, Shows shows)
{
  return board.shows.exchange(static_cast<u32>(shows), std::memory_order_relaxed) != static_cast<u32>(shows);
}
}  // namespace

void Board::Reset()
{
  shows = 0;
  ready = 0;
  of = 0;
  progress = 0;
  until_race = false;
  until_live = false;
  figure = 0;
}

Board& TheBoard()
{
  static Board board;
  return board;
}

void Begin(Board& board, bool until_race, bool until_live)
{
  board.Reset();
  board.until_race = until_race;
  board.until_live = until_live;
  Show(board, Shows::Loading);
}

bool CourseShown(u32 mode, u32 phase)
{
  return mode == MODE_GAME && phase >= PHASE_INTRO && phase <= PHASE_RACE;
}

bool Frame(Board& board, u32 mode, u32 phase)
{
  if (board.until_race || board.until_live || board.Now() != Shows::Loading || !CourseShown(mode, phase))
    return false;
  return Show(board, Shows::Nothing);
}

bool RaceBegins(Board& board)
{
  if (!board.until_race || board.Now() != Shows::Loading)
    return false;
  return Show(board, Shows::Nothing);
}

bool CatchingUp(Board& board, u32 done, u32 of)
{
  if (!board.until_live || board.Now() != Shows::Loading || of == 0)
    return false;
  const u32 at = HELD_LOAD + static_cast<u32>(u64{FULL - HELD_LOAD} * std::min(done, of) / of);
  return board.progress.exchange(std::max(at, board.progress.load(std::memory_order_relaxed)),
                                 std::memory_order_relaxed) < at;
}

bool Live(Board& board)
{
  if (!board.until_live || board.Now() != Shows::Loading)
    return false;
  board.until_live = false;
  return Show(board, Shows::Nothing);
}

bool AtGate(Board& board, u32 waited_ms, u32 ready, u32 of)
{
  if (waited_ms < WAIT_SHOWN_MS)
    return false;
  const bool count = board.ready.exchange(ready, std::memory_order_relaxed) != ready;
  const bool all = board.of.exchange(of, std::memory_order_relaxed) != of;
  return Show(board, Shows::Waiting) || count || all;
}

bool Down(Board& board)
{
  return Show(board, Shows::Nothing);
}

bool Counting(Board& board, u32 figure)
{
  const bool changed = board.figure.exchange(figure, std::memory_order_relaxed) != figure;
  return Show(board, Shows::Countdown) || changed;
}

void Saved(Board& board, u32 along)
{
  board.saved_along.store(std::min(along, FULL), std::memory_order_relaxed);
  board.saves.fetch_add(1, std::memory_order_relaxed);
}

std::vector<std::string> Lines(Shows shows, u32 ready, u32 of)
{
  if (shows == Shows::Loading)
    return {"LOADING"};
  if (shows == Shows::Countdown)
    return {std::to_string(ready)};
  if (shows != Shows::Waiting)
    return {};
  std::vector<std::string> lines{"WAITING FOR PLAYERS"};
  if (of != 0)
    lines.push_back(std::to_string(std::min(ready, of)) + "/" + std::to_string(of));
  return lines;
}

}  // namespace fzgx::panel
