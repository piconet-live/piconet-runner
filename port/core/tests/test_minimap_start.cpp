// The course map's start mark (fzgx/minimap.h Start, RoadAcross, StartMark, AddMark): the line read from a course
// file made here (never the game's), the road's extent across it from the preview's triangles, and the mark's place,
// size and direction on the map.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "harness.h"

#include "fzgx/minimap.h"

#include <bit>
#include <cmath>

using namespace fzgx;
using namespace fzgx::minimap;

namespace
{
void Put32(std::vector<u8>& b, std::size_t at, u32 v)
{
  if (b.size() < at + 4)
    b.resize(at + 4);
  for (int i = 0; i < 4; ++i)
    b[at + static_cast<std::size_t>(i)] = static_cast<u8>(v >> (24 - 8 * i));
}
void PutF(std::vector<u8>& b, std::size_t at, float v)
{
  Put32(b, at, std::bit_cast<u32>(v));
}
void PutVec(std::vector<u8>& b, std::size_t at, const Vec& v)
{
  PutF(b, at, v.x), PutF(b, at + 4, v.y), PutF(b, at + 8, v.z);
}
bool Near(float a, float b, float within = 1e-3f)
{
  return std::fabs(a - b) <= within;
}

// A course file of two track nodes, the first with a checkpoint a width of `widths` (a split start), each starting
// at `origin` facing `normal`.
std::vector<u8> CourseFile(const Vec& origin, const Vec& normal, const std::vector<float>& widths)
{
  std::vector<u8> f(0x100, 0);
  Put32(f, 0x08, 2);
  Put32(f, 0x0C, 0x40);  // the nodes
  Put32(f, 0x40, static_cast<u32>(widths.size()));
  Put32(f, 0x44, 0x80);  // the first node's checkpoints
  Put32(f, 0x48, 0xF0);  // (its root segment: not read)
  for (std::size_t i = 0; i < widths.size(); ++i)
  {
    const std::size_t at = 0x80 + i * 0x50;
    PutVec(f, at + 0x0C, normal);
    PutVec(f, at + 0x18, origin);
    PutF(f, at + 0x48, widths[i]);
  }
  Put32(f, 0x4C, 1);  // the second node: one checkpoint, after the first's
  Put32(f, 0x50, 0x80 + static_cast<u32>(widths.size()) * 0x50);
  f.resize(std::max<std::size_t>(f.size(), 0x80 + (widths.size() + 1) * 0x50));
  return f;
}

// A flat road from x `left` to `right` at height `y`, from z 100 to z -100 (two triangles), added to `road`.
void Band(Road& road, float left, float right, float y)
{
  const u32 first = static_cast<u32>(road.corners.size());
  road.corners.insert(road.corners.end(), {{left, y, 100}, {right, y, 100}, {left, y, -100}, {right, y, -100}});
  road.triangles.insert(road.triangles.end(), {first, first + 1, first + 2, first + 2, first + 1, first + 3});
}
}  // namespace

void TestTheCourseMapsStartLine()
{
  BEGIN_TEST();
  std::string why;
  // the first checkpoint's plane; the normal made a unit; a split start's width is its widest road's
  const std::optional<Start> s = ReadStart(CourseFile({1, 2, 3}, {0, 0, -1.5f}, {0.25f, 45, 40}), &why);
  CHECK(s.has_value());
  if (!s)
    return;
  CHECK(Near(s->at.x, 1) && Near(s->at.y, 2) && Near(s->at.z, 3));
  CHECK(Near(s->forward.x, 0) && Near(s->forward.y, 0) && Near(s->forward.z, -1));
  CHECK(Near(s->width, 45));
  // nothing usable: too short, no node, no checkpoint, not a plane, no width
  CHECK(!ReadStart(std::vector<u8>(8, 0), &why) && !why.empty());
  std::vector<u8> none = CourseFile({}, {0, 0, -1}, {60});
  Put32(none, 0x08, 0);
  CHECK(!ReadStart(none, &why));
  std::vector<u8> outside = CourseFile({}, {0, 0, -1}, {60});
  Put32(outside, 0x44, 0xFFFFFF00);
  CHECK(!ReadStart(outside, &why));
  CHECK(!ReadStart(CourseFile({}, {0, 0, 0}, {60}), &why));
  CHECK(!ReadStart(CourseFile({}, {0, 0, -1}, {0}), &why));
}

void TestTheCourseMapsStartMark()
{
  BEGIN_TEST();
  const Start s{{0, 0, 0}, {0, 0, -1}, 64};
  // the road across the line: the band holding the middle (two bands meeting are one road), not a road beside it
  // nor one passing over far above
  Road road;
  Band(road, -30, 0, 0);
  Band(road, 0, 25, 0);
  Band(road, 200, 260, 0);
  Band(road, -300, 300, 500);
  std::optional<Across> a = RoadAcross(road, s);
  CHECK(a && Near(a->left, -30) && Near(a->right, 25) && Near(a->top, 0));
  // a road raised over the line's middle (a cylinder's start is on its axis): the mark lies on its top
  Road raised;
  Band(raised, -30, 30, 12);
  const std::optional<Across> up = RoadAcross(raised, s);
  CHECK(up && Near(up->top, 12));
  Road away;
  Band(away, 200, 260, 0);
  CHECK(!RoadAcross(away, s));

  // the game's map camera looking straight down from 1000 m: the map's side spans 2 * 1000 * tan(15) metres there,
  // and the map's up is the course's -z, the way this line faces
  Camera camera;
  camera.fov = 30;
  camera.eye = {0, 1000, 0};
  camera.at = {0, 0, 0};
  const float side = 2 * 1000 * std::tan(15 * 3.14159265f / 180);
  // the bar across the road, past it by MARK_OVER_FLAG on the arrow's side and MARK_OVER on the other, MARK_THICK
  // deep about the line; from the bar's end on that side a shaft along the road and an arrowhead, its tip MARK_TIP
  // ahead of the line
  const Road mark = StartMark(s, a, camera, false);
  CHECK(mark.corners.size() == 9 && mark.triangles.size() == 15);
  CHECK(Near(mark.corners[0].x, -30 - MARK_OVER_FLAG * side, 0.01f));
  CHECK(Near(mark.corners[1].x, 25 + MARK_OVER * side, 0.01f));
  CHECK(Near(mark.corners[0].z - mark.corners[3].z, MARK_THICK * side, 0.01f));
  CHECK(Near(mark.corners[4].x - mark.corners[3].x, MARK_THICK * side, 0.01f));
  CHECK(Near(mark.corners[6].x, mark.corners[3].x));
  // a half arrowhead: the shaft's road side runs straight to the tip; only the barb away from the road flares out
  CHECK(Near(mark.corners[8].x, mark.corners[4].x) && Near(mark.corners[8].z, -MARK_TIP * side, 0.01f));
  CHECK(Near(mark.corners[4].x - mark.corners[7].x, MARK_HEAD_WIDTH * side, 0.01f));
  CHECK(mark.corners[7].x < mark.corners[3].x);
  CHECK(Near(mark.corners[7].z, -(MARK_TIP - MARK_HEAD) * side, 0.01f));
  CHECK((std::vector<u32>(mark.triangles.begin() + 6, mark.triangles.end()) ==
         std::vector<u32>{3, 4, 5, 3, 5, 6, 5, 7, 8}));
  const Road right = StartMark(s, a, camera, true);  // (mirrored: the barb out to the right)
  CHECK(Near(right.corners[0].x, -30 - MARK_OVER * side, 0.01f));
  CHECK(Near(right.corners[1].x, 25 + MARK_OVER_FLAG * side, 0.01f));
  CHECK(Near(right.corners[8].x, right.corners[1].x - MARK_THICK * side, 0.01f));
  CHECK(Near(right.corners[7].x - right.corners[4].x, MARK_HEAD_WIDTH * side, 0.01f));
  CHECK(right.corners[7].x > right.corners[2].x);
  CHECK(right.triangles[6] == 2 && right.triangles[9] == 2);
  // without the road's extent: the checkpoint's width about the middle
  const Road plain = StartMark(s, std::nullopt, camera, false);
  CHECK(Near(plain.corners[0].x, -32 - MARK_OVER_FLAG * side, 0.01f));
  // the game's pennant sides
  CHECK(!PennantRight(1) && PennantRight(16) && PennantRight(37) && !PennantRight(45) && !PennantRight(0));

  // on the map: the pennant points the way the race runs (the line's forward, seen through the camera)
  MinimapCourse course;
  AddMark(mark, camera, course);
  CHECK(course.mark.size() == 18 && course.mark_world.size() == 27 && course.mark_triangles == mark.triangles);
  CHECK(course.mark_colour == 0xFFFB00);
  const Projected at = Project(camera, s.at), ahead = Project(camera, {0, 0, -50});
  const float fx = ahead.x - at.x, fy = ahead.y - at.y;
  const float bar_x = (course.mark[0] + course.mark[2]) / 2, bar_y = (course.mark[1] + course.mark[3]) / 2;
  CHECK((course.mark[16] - bar_x) * fx + (course.mark[17] - bar_y) * fy > 0);
  CHECK(fy > 0);  // (the map's up, for this camera)
  // the same through a camera turned half round (the C-stick): still ahead of the bar
  const Camera turned = Turned(camera, 180);
  MinimapCourse back;
  AddMark(mark, turned, back);
  const Projected at2 = Project(turned, s.at), ahead2 = Project(turned, {0, 0, -50});
  CHECK((back.mark[16] - (back.mark[0] + back.mark[2]) / 2) * (ahead2.x - at2.x) +
            (back.mark[17] - (back.mark[1] + back.mark[3]) / 2) * (ahead2.y - at2.y) > 0);
}
