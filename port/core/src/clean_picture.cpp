// The clean picture (a game mode's `hud clean`): clean_picture.h says what it takes off and why each is drawing only.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clean_picture.h"

#include "files.h"
#include "internal.h"
#include "mode_hud.h"
#include "rollback.h"

namespace fzgx::clean_picture
{
namespace
{
constexpr u32 VIEWPORTS = 4;  // the Check task's gaps: a float a viewport
}  // namespace

void Frame(Guest& g)
{
  if (!(g.Read32(layout::MODE_HUD + mode::hud::WINDOW_FLAGS) & mode::hud::HUD_CLEAN))
    return;
  // the name tags: no seat has a player name, and no game mode's tag stands
  for (u32 seat = 0; seat < static_cast<u32>(limits::SEATS); ++seat)
  {
    g.Write8(boot::NAMES + seat * boot::NAME_SIZE, 0);
    g.Write8(layout::MODE_HUD + mode::hud::WINDOW_TAGS + seat, 0);
  }
  // the CHECK sign: every viewport's gap to the rival behind is "none" (main.rel's .bss is there once it is linked)
  if (!g.Ram(net::MODULE_POINTER, 4) || g.Read32(net::MODULE_POINTER) != layout::MAIN_REL)
    return;
  for (u32 vp = 0; vp < VIEWPORTS; ++vp)
    g.Write32(game::CHECK_GAP_AT + vp * 4, 0);  // 0.0f
}
}  // namespace fzgx::clean_picture
