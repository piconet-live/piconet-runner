// The course map's view (fzgx/minimap.h, "Turning and tilting the view"): the tilt about the looked-at point and its
// range, the limits eased into, and the fit that keeps the whole course inside the square at every turn and tilt
// within the limits (a ring course made here; every pool course of the disc is checked off-line by the same maths).
// SPDX-License-Identifier: GPL-3.0-or-later

#include "harness.h"

#include "fzgx/minimap.h"

#include <cmath>

using namespace fzgx;
using namespace fzgx::minimap;

namespace
{
bool Near(float a, float b, float within = 1e-3f)
{
  return std::fabs(a - b) <= within;
}
float Distance(const Camera& c)
{
  return std::hypot(c.eye.x - c.at.x, c.eye.y - c.at.y, c.eye.z - c.at.z);
}

// A ring road 20 wide about the origin, `n` pieces round.
Road Ring(int n)
{
  Road r;
  for (int i = 0; i < n; ++i)
  {
    const float a = 6.2831853f * static_cast<float>(i) / static_cast<float>(n);
    r.corners.push_back({90 * std::cos(a), 0, 90 * std::sin(a)});
    r.corners.push_back({110 * std::cos(a), 0, 110 * std::sin(a)});
    const u32 k = static_cast<u32>(i) * 2, next = static_cast<u32>((i + 1) % n) * 2;
    r.triangles.insert(r.triangles.end(), {k, k + 1, next, next, k + 1, next + 1});
  }
  return r;
}
}  // namespace

void TestTheCourseMapsViewTilts()
{
  BEGIN_TEST();
  CHECK(TiltOf(0) == 0 && TiltOf(59) == 1 && TiltOf(-59) == -1 && TiltOf(8) == 0);
  // tilted about the looked-at point: the distance and the way it leans kept, the elevation lower by the tilt
  const Camera c{30, {0, 100, 100}, {0, 0, 0}};
  CHECK(Near(Elevation(c), 45));
  const Camera flat = Tilted(c, 20), steep = Tilted(c, -40);
  CHECK(Near(Elevation(flat), 25, 0.01f) && Near(Elevation(steep), 85, 0.01f));
  CHECK(Near(Distance(flat), Distance(c), 0.01f) && Near(flat.eye.x, 0) && flat.eye.z > 0);
  CHECK(Elevation(Tilted(c, 80)) >= TILT_LOWEST - 0.01f && Elevation(Tilted(c, -80)) <= TILT_STEEPEST + 0.01f);
  CHECK(Tilted(c, 0).eye.y == c.eye.y);
  // its range: straight down (as far as TILT_STEEPEST) to TILT_FLATTER flatter, never below TILT_LOWEST
  const Range r = TiltRange(c);
  CHECK(Near(r.lo, 45 - TILT_STEEPEST) && Near(r.hi, TILT_FLATTER));
  const Camera low{30, {0, 30, 100}, {0, 0, 0}};  // (about 16.7 degrees up)
  CHECK(Near(TiltRange(low).hi, Elevation(low) - TILT_LOWEST, 0.01f));
  // a camera straight down: it tilts towards the picture's bottom, its turn kept (no jump from no tilt to a little)
  const Camera down{30, {0, 100, 0}, {0, 0, 0}};
  CHECK(Near(TiltRange(down).lo, 0) && Near(TiltRange(down).hi, TILT_FLATTER));
  const Camera turned = Turned(down, 90);
  const Projected p0 = Project(turned, {10, 0, 5}), p1 = Project(Tilted(turned, 0.01f), {10, 0, 5});
  CHECK(Near(p0.x, p1.x, 1e-3f) && Near(p0.y, p1.y, 1e-3f));
  // the tilt eases into its limits too, and back
  ViewTurn t;
  for (int i = 0; i < 600; ++i)
    t.Step(1, 1.0f / 60, r, TILT_SPEED);
  CHECK(t.angle <= r.hi && t.angle > r.hi - 0.5f);
  for (int i = 0; i < 600; ++i)
    t.Step(-1, 1.0f / 60, r, TILT_SPEED);
  CHECK(t.angle >= r.lo && t.angle < r.lo + 0.5f);
  t.Step(0, TURN_BACK);
  CHECK(t.angle == 0);
}

void TestTheCourseMapStaysInItsSquare()
{
  BEGIN_TEST();
  CHECK(FitScale({0.5f, -0.9f, 1}) == 1);
  CHECK(Near(FitScale({2, 0, NAN, -1}), 0.5f));
  // a ring that fills most of the square at the game's view: at every turn and tilt within the limits, every corner
  // seen (none behind the eye) and inside the square once fitted; the fit moves smoothly with the view
  const Camera game{30, {0, 300, 450}, {0, 0, 0}};
  const MinimapCourse course = Prepare(Ring(64), game, 1);
  CHECK(FitScale(course.points) == 1);
  const Range r = TiltRange(game);
  bool inside = true, seen = true, smooth = true;
  for (float tilt = r.lo; tilt <= r.hi + 0.01f; tilt += 2)
  {
    float before = -1;
    for (float turn = -TURN_LIMIT; turn <= TURN_LIMIT + 0.01f; turn += 1)
    {
      std::vector<float> xy(course.points.size());
      ProjectMany(ViewOf(course, turn, tilt), course.world.data(), xy.size() / 2, xy.data());
      const float scale = FitScale(xy);
      for (const float v : xy)
      {
        seen = seen && std::isfinite(v);
        inside = inside && std::fabs(v * scale) <= 1.0001f;
      }
      smooth = smooth && (before < 0 || std::fabs(scale - before) < 0.02f);
      before = scale;
    }
  }
  CHECK(seen && inside && smooth);
}
