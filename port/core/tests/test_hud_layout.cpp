// Tests of the modern HUD's layout (fzgx/hud_layout.h): anchors at two resolutions, a file over the defaults, and what a
// file cannot do.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "harness.h"

#include <cmath>

#include "fzgx/hud_layout.h"

using namespace fzgx::hud_layout;

namespace
{
bool Near(float a, float b)
{
  return std::abs(a - b) < 0.01f;
}
}  // namespace

void TestTheModernHudLayout()
{
  const Rect p720{0, 0, 1280, 720}, p2160{0, 0, 3840, 2160};
  // every default stays inside the picture at both sizes, and scales exactly with the height
  const Layout d = Default();
  for (const Slot& s : d.slots)
  {
    const Rect a = Place(s, 100, 50, p720), b = Place(s, 100, 50, p2160);
    CHECK(a.x0 >= 0 && a.y0 >= 0 && a.x1 <= 1280 && a.y1 <= 720);
    CHECK(Near(a.x0 * 3, b.x0) && Near(a.y0 * 3, b.y0) && Near(a.x1 * 3, b.x1) && Near(a.y1 * 3, b.y1));
  }
  // anchors: offsets go inward; the picture, black bars left out, is the reference
  const Rect bars{100, 10, 1100, 1010};  // 1000 high: a unit is a pixel
  Slot s;
  s.x = 20, s.y = 30, s.anchor = Anchor::TopLeft;
  Rect r = Place(s, 100, 50, bars);
  CHECK(Near(r.x0, 120) && Near(r.y0, 40) && Near(r.x1, 220) && Near(r.y1, 90));
  s.anchor = Anchor::BottomRight;
  r = Place(s, 100, 50, bars);
  CHECK(Near(r.x1, 1080) && Near(r.y1, 980));
  s.anchor = Anchor::Top;
  r = Place(s, 100, 50, bars);
  CHECK(Near(r.x0, 570) && Near(r.y0, 40));
  s.anchor = Anchor::Centre, s.scale = 2;
  r = Place(s, 100, 50, bars);
  CHECK(Near(r.x1 - r.x0, 200) && Near(r.y1 - r.y0, 100) && Near(r.x0, 520) && Near(r.y0, 490));
  // a file moves what it names; the rest stay
  std::string why;
  Layout l = FromText(R"({"lap":{"anchor":"bottom","x":-5,"y":12,"scale":1.5,"shown":false}})", &why);
  CHECK(why.empty());
  const Slot& lap = l[SlotId::Lap];
  CHECK(lap.anchor == Anchor::Bottom && Near(lap.x, -5) && Near(lap.y, 12) && Near(lap.scale, 1.5f) && !lap.shown);
  CHECK(Near(l[SlotId::Energy].x, d[SlotId::Energy].x) && l[SlotId::Energy].anchor == d[SlotId::Energy].anchor);
  // what is wrong is said and left at its default; ranges are held
  l = FromText(R"({"nope":{},"energy":{"anchor":"up","x":1},"position":{"scale":99,"x":99999}})", &why);
  CHECK(!why.empty());
  CHECK(Near(l[SlotId::Energy].x, d[SlotId::Energy].x));
  CHECK(Near(l[SlotId::Position].scale, 4) && Near(l[SlotId::Position].x, 1000));
  l = FromText("not json", &why);
  CHECK(!why.empty() && Near(l[SlotId::Energy].y, d[SlotId::Energy].y));
}
