// A game mode's values on the launcher's status board (D14; mode_hud.h Board, mode_board.h): what the mode's `board` lines
// name, read from the mode's block as a picture is about to be drawn. The block is the simulation's, so a watched race
// and a replay show the same values at the same step, and a practice's rewind the values of the moment it shows. The
// board is written only by this process and read only by the launcher: nothing of a step reads it, and the game's
// memory is not written.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mode_board.h"

#include <algorithm>
#include <cstring>

#include "board_net.h"
#include "files.h"
#include "fzgx/board.h"
#include "mode_hud.h"
#include "net_internal.h"

namespace fzgx::mode::hud
{
namespace
{
constexpr u32 SCOPES = 3, TEAMS_SHOWN = modes::TEAMS, SEATS_SHOWN = modes::SEATS;
static_assert(static_cast<u32>(modes::BOARD) * SEATS_SHOWN == 240 && static_cast<u32>(modes::BOARD) == 8 &&
              static_cast<u32>(modes::BOARD_LABEL) == 32 && TEAMS_SHOWN == 30, "board.toml's arrays hold the api's");

u32 HexByte(const std::string& hash, size_t i)
{
  const auto digit = [](char c) { return static_cast<u32>(c <= '9' ? c - '0' : c - 'a' + 10); };
  return digit(hash[2 * i]) << 4 | digit(hash[2 * i + 1]);
}
}  // namespace

board::ModeHead BoardHead(const Package& p)
{
  board::ModeHead head;
  head.serial = 1;
  // what does not fit is cut at a character's start
  size_t length = std::min(p.name.size(), head.name.size());
  while (length > 0 && length < p.name.size() && (static_cast<u8>(p.name[length]) & 0xC0) == 0x80)
    --length;
  head.name_length = static_cast<u32>(length);
  std::memcpy(head.name.data(), p.name.data(), length);
  for (size_t i = 0; i < head.hash.size() && 2 * i + 1 < p.hash.size(); ++i)
    head.hash[i] = static_cast<u8>(HexByte(p.hash, i));
  std::array<u16, SCOPES> column{};
  for (const BoardLine& line : p.board)
  {
    if (head.count >= head.lines.size())
      break;
    board::ModeLine& out = head.lines[head.count++];
    out.scope = static_cast<board::ModeBoardScope>(line.scope);
    out.type = static_cast<board::ModeBoardType>(line.type);
    out.column = column[line.scope]++;
    out.label_length = static_cast<u32>(line.label.size());
    std::memcpy(out.label.data(), line.label.data(), line.label.size());
  }
  return head;
}

void BoardValues(const Guest& g, const Package& p, u32 block, board::ModeValues* out)
{
  std::array<u32, SCOPES> column{};
  for (const BoardLine& line : p.board)
  {
    const u32 c = column[line.scope]++;
    if (line.scope == 0)
      out->globals[c].value = static_cast<s32>(g.Read32(block + GLOBALS + 4 * line.word));
    else if (line.scope == 1)
      for (u32 seat = 0; seat < SEATS_SHOWN; ++seat)
        out->seats[seat * 8 + c].value = static_cast<s32>(g.Read32(block + SeatWord(seat, line.word)));
    else
      for (u32 team = 0; team < TEAMS_SHOWN; ++team)
        out->teams[team * 8 + c].value = static_cast<s32>(g.Read32(block + TeamWord(team, line.word)));
  }
}

void Board(const Guest& g)
{
  if (!board::Taken())
    return;
  const boot::Match* m = boot::Get();
  const u32 block = g.Read32(layout::MODES + 4);
  if (!m || !m->game_mode || block == 0 || !g.Ram(net::MODULE_POINTER, 4) || g.Read32(net::MODULE_POINTER) != layout::MAIN_REL ||
      g.Read16(layout::MAIN_REL + game::PHASE) < PHASE_INTRO)
    return;
  const Package& p = *m->game_mode;
  if (!board::ModeHeadWritten())
    board::PutModeHead(BoardHead(p));
  if (p.board.empty())
    return;
  static board::ModeValues values;  // (its 2 KiB: this game's one race at a time)
  BoardValues(g, p, block, &values);
  values.frame = g.Read32(net::STEP);
  board::PutModeValues(values);
}
}  // namespace fzgx::mode::hud
