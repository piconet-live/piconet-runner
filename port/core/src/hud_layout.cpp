// The modern HUD's layout (fzgx/hud_layout.h).
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fzgx/hud_layout.h"

#include <algorithm>
#include <cmath>

#include <picojson.h>

namespace fzgx::hud_layout
{
namespace
{
constexpr std::array<std::string_view, 9> ANCHORS = {"top_left", "top", "top_right", "left", "centre", "right", "bottom_left", "bottom", "bottom_right"};
constexpr float MOST_OFFSET = 1000, LEAST_SCALE = 0.25f, MOST_SCALE = 4;

bool Number(const picojson::object& o, const char* key, float least, float most, float* to, std::string* why)
{
  const auto it = o.find(key);
  if (it == o.end())
    return true;
  if (!it->second.is<double>() || !std::isfinite(it->second.get<double>()))
  {
    *why = std::string(key) + " is not a number";
    return false;
  }
  *to = std::clamp(static_cast<float>(it->second.get<double>()), least, most);
  return true;
}

bool Read(const picojson::object& o, Slot* slot, std::string* why)
{
  Slot s = *slot;
  if (const auto it = o.find("anchor"); it != o.end())
  {
    const auto at = it->second.is<std::string>() ? std::find(ANCHORS.begin(), ANCHORS.end(), it->second.get<std::string>()) : ANCHORS.end();
    if (at == ANCHORS.end())
    {
      *why = "no such anchor";
      return false;
    }
    s.anchor = static_cast<Anchor>(at - ANCHORS.begin());
  }
  if (!Number(o, "x", -MOST_OFFSET, MOST_OFFSET, &s.x, why) || !Number(o, "y", -MOST_OFFSET, MOST_OFFSET, &s.y, why) ||
      !Number(o, "scale", LEAST_SCALE, MOST_SCALE, &s.scale, why))
    return false;
  if (const auto it = o.find("shown"); it != o.end())
  {
    if (!it->second.is<bool>())
    {
      *why = "shown is not true or false";
      return false;
    }
    s.shown = it->second.get<bool>();
  }
  *slot = s;
  return true;
}
}  // namespace

Layout Default()
{
  Layout l;
  const auto put = [&](SlotId id, Anchor a, float x, float y) {
    Slot& s = l.slots[static_cast<std::size_t>(id)];
    s.anchor = a, s.x = x, s.y = y;
  };
  put(SlotId::Energy, Anchor::TopRight, 28, 28);
  put(SlotId::RaceTime, Anchor::TopRight, 28, 86);
  put(SlotId::Lap, Anchor::TopRight, 268, 86);
  put(SlotId::LapTimes, Anchor::TopRight, 28, 150);
  put(SlotId::Position, Anchor::BottomLeft, 28, 28);
  return l;
}

float UnitOf(const Slot& slot, const Rect& picture)
{
  return (picture.y1 - picture.y0) / 1000.0f * slot.scale;
}

Rect Place(const Slot& slot, float w, float h, const Rect& picture)
{
  const float base = (picture.y1 - picture.y0) / 1000.0f, unit = UnitOf(slot, picture);
  const float pw = w * unit, ph = h * unit, dx = slot.x * base, dy = slot.y * base;
  const int a = static_cast<int>(slot.anchor);
  const int column = a % 3, row = a / 3;
  const float x0 = column == 0 ? picture.x0 + dx : column == 1 ? (picture.x0 + picture.x1 - pw) / 2 + dx : picture.x1 - dx - pw;
  const float y0 = row == 0 ? picture.y0 + dy : row == 1 ? (picture.y0 + picture.y1 - ph) / 2 + dy : picture.y1 - dy - ph;
  return {x0, y0, x0 + pw, y0 + ph};
}

Layout FromText(const std::string& text, std::string* why)
{
  Layout l = Default();
  why->clear();
  picojson::value root;
  if (!picojson::parse(root, text).empty() || !root.is<picojson::object>())
  {
    *why = "not a JSON object";
    return l;
  }
  for (const auto& [name, value] : root.get<picojson::object>())
  {
    const auto at = std::find(SLOT_NAMES.begin(), SLOT_NAMES.end(), name);
    std::string problem;
    if (at == SLOT_NAMES.end())
      problem = "no such slot";
    else if (!value.is<picojson::object>())
      problem = "not an object";
    else
      Read(value.get<picojson::object>(), &l.slots[static_cast<std::size_t>(at - SLOT_NAMES.begin())], &problem);
    if (!problem.empty() && why->empty())
      *why = name + ": " + problem;
  }
  return l;
}
}  // namespace fzgx::hud_layout
