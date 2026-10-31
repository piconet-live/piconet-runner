// "Show inputs": m-overlay's numbers (fzgx/inputs.h): a stick's lean and its map, a trigger.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "harness.h"

#include "fzgx/inputs.h"

#include <cmath>

using namespace fzgx::inputs;

namespace
{
bool Near(float a, float b)
{
  return std::abs(a - b) < 0.01f;
}
}  // namespace

void TestInputsStickAtRestStandsInItsGate()
{
  BEGIN_TEST();
  const auto q = Quad(STICK, Translate(0, 0, STICK_RANGE));
  // the cell over the gate's: the skin's gate at (22, 52), the centre 22 + 64 and 52 + 64
  CHECK(Near(q[0].x, 22) && Near(q[0].y, 52) && Near(q[2].x, 150) && Near(q[2].y, 180));
}

void TestInputsStickUpMovesUpAndLeansAway()
{
  BEGIN_TEST();
  const Point up = Translate(0, 72, STICK_RANGE);
  CHECK(Near(up.y, 1) && Near(up.x, 0));
  const Point diagonal = Translate(40, 40, STICK_RANGE);  // (the skin's 0.25 amplifies towards a diagonal)
  CHECK(diagonal.x > 40 / STICK_RANGE && Near(diagonal.x, diagonal.y));
  const auto q = Quad(STICK, up);
  CHECK(q[2].y < 180);
  CHECK(q[1].x - q[0].x < q[2].x - q[3].x);  // the far edge narrower
}

void TestInputsProjectiveMapsCornersAndBack()
{
  BEGIN_TEST();
  const auto q = Quad(CSTICK, Translate(40, -30, CSTICK_RANGE));
  const Projective map(q);
  const std::array<Point, 4> corners{{{0, 0}, {1, 0}, {1, 1}, {0, 1}}};
  for (std::size_t i = 0; i < 4; ++i)
  {
    const Point at = map.At(corners[i].x, corners[i].y);
    CHECK(Near(at.x, q[i].x) && Near(at.y, q[i].y));
    const Point back = map.Back(q[i].x, q[i].y);
    CHECK(Near(back.x, corners[i].x) && Near(back.y, corners[i].y));
  }
}

void TestInputsTriggerIsFullAtItsRangeOrAPress()
{
  BEGIN_TEST();
  CHECK(Near(Trigger(0, false), 0) && Near(Trigger(150, false), 1) && Near(Trigger(10, true), 1));
  CHECK(Near(Trigger(50, false), 0.4f));
}
