// The game's own drop shadow, left out while the rays light the race: game_shadow.h says why it is drawing only.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "game_shadow.h"

#include <algorithm>

#include "internal.h"
#include "rollback.h"

namespace fzgx::game_shadow
{
namespace
{
constexpr u32 PLACED = 0x4;  // the record's flags (+0): placed this frame
}  // namespace

void Frame(Guest& g, bool rays)
{
  if (!rays || !g.Ram(net::MODULE_POINTER, 4) || g.Read32(net::MODULE_POINTER) != layout::MAIN_REL)
    return;
  const u32 records = g.Read32(game::CAR_SHADOWS_AT);
  const u32 count = std::min<u32>(g.Read8(game::MACHINE_COUNT_AT), static_cast<u32>(limits::SEATS));
  if (records == 0 || count == 0 || !g.Ram(records, count * game::CAR_SHADOW_SIZE))
    return;
  for (u32 racer = 0; racer < count; ++racer)
  {
    const u32 at = records + racer * game::CAR_SHADOW_SIZE;
    const u32 flags = g.Read32(at);
    if (flags & PLACED)
      g.Write32(at, flags & ~PLACED);
  }
}
}  // namespace fzgx::game_shadow
