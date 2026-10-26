// The game's and the mod's addresses, offsets and masks for the core's sources, by their names: the facts table
// (port/facts/game.tsv; generated: port/facts/generated/facts.h, which fzgx/guest.h includes). No game address is
// typed in this folder as a number. This file is the two short names the core's sources say them by.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <cstdint>

#include "fzgx/guest.h"

namespace fzgx
{
namespace table = layout;  // the mod's memory: the data window's names, and every offset's address (NAME_AT)
// game::NAME is the header's own fzgx::game: main memory, main.rel, a machine's block, the checksum's masks
}  // namespace fzgx
