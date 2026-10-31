// The modern HUD's layout (Options::modern_hud; the adapter's modern_hud.cpp draws it): where each element stands, as
// named slots, each an anchor on the game's picture, an offset inward and a scale. Nothing here knows pixels: every length
// is in units of a thousandth of the picture's height, so a layout means the same at any resolution, and a box comes only
// from Place. A file (the launch file's "hudLayout") may move any slot; a slot it leaves out keeps its default. The
// names are what a player's layout and, later, a mode's word refer to, so what a mode shows follows the player's layout.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

namespace fzgx::hud_layout
{
enum class Anchor
{
  TopLeft, Top, TopRight, Left, Centre, Right, BottomLeft, Bottom, BottomRight
};

// The elements, in the order of a Layout's slots. Modern-only elements to come are added at the end.
enum class SlotId
{
  Energy, Position, Lap, RaceTime, LapTimes, Count
};
inline constexpr std::size_t SLOTS = static_cast<std::size_t>(SlotId::Count);
inline constexpr std::array<std::string_view, SLOTS> SLOT_NAMES = {"energy", "position", "lap", "race_time", "lap_times"};

struct Slot
{
  Anchor anchor = Anchor::TopLeft;
  float x = 0, y = 0;  // from the anchor, inward (on a centre line: rightward and downward), in units
  float scale = 1;
  bool shown = true;
};

struct Layout
{
  std::array<Slot, SLOTS> slots{};
  const Slot& operator[](SlotId id) const { return slots[static_cast<std::size_t>(id)]; }
};

// The game-like arrangement every player starts from: energy, time and laps at the top right, the place at the bottom left.
Layout Default();

struct Rect
{
  float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};

// The box of an element `w` by `h` units (before the slot's scale) in `slot` on `picture`, in the picture's pixels.
Rect Place(const Slot& slot, float w, float h, const Rect& picture);
// A unit in pixels at the slot's scale.
float UnitOf(const Slot& slot, const Rect& picture);

// A layout from the file's text, over the defaults. Anything unreadable leaves that slot (or all) at its default, and
// says so in `why` (empty: all taken). Ranges: offsets -1000 to 1000 units, scale 0.25 to 4.
Layout FromText(const std::string& text, std::string* why);
}  // namespace fzgx::hud_layout
