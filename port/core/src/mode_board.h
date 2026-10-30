// A game mode's values on the status board, the parts with no board behind them (mode_board.cpp; for its tests).
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "fzgx/board.h"
#include "mode.h"

namespace fzgx::mode::hud
{
// The mode's identity and its lines, as the board's ModeHead holds them; `column` is a line's place in its scope.
board::ModeHead BoardHead(const Package& p);
// The lines' values at this moment into `out`: the block read, nothing written.
void BoardValues(const Guest& g, const Package& p, u32 block, board::ModeValues* out);
}  // namespace fzgx::mode::hud
