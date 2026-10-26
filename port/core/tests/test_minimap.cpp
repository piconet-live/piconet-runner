// Our course map (fzgx/minimap.h): the course select's model read as the road (a model file made here, never the
// game's), the game's map camera and the view through it, the road prepared for the host (far triangles first, the
// outline), the dots' pick and order, when the map shows, the map's bit of the HUD mask, and the five previews'
// offsets held equal to the launcher's.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "parts.h"
#include "harness.h"

#include "fzgx/minimap.h"

#include <bit>
#include <cmath>
#include <fstream>
#include <regex>
#include <sstream>

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
void Put16(std::vector<u8>& b, std::size_t at, u16 v)
{
  if (b.size() < at + 2)
    b.resize(at + 2);
  b[at] = static_cast<u8>(v >> 8);
  b[at + 1] = static_cast<u8>(v);
}
void PutF(std::vector<u8>& b, std::size_t at, float v)
{
  Put32(b, at, std::bit_cast<u32>(v));
}

struct Corner
{
  float x, y, z;
};

// A GCMF model of one mesh: one strip in group 0 (the second, when `second`, in group 1, wound the other way),
// corners with a position and a normal, floats or 16-bit numbers.
std::vector<u8> Model(const std::vector<Corner>& strip, bool short_numbers, u32 attributes = 0,
                      const std::vector<Corner>& second = {})
{
  std::vector<u8> m(0x40, 0);
  Put32(m, 0, 0x47434D46);
  Put32(m, 4, attributes | (short_numbers ? 1u : 0u));
  Put16(m, 0x18, 0);  // materials
  Put16(m, 0x1A, 1);  // opaque meshes
  Put16(m, 0x1C, 0);
  Put32(m, 0x20, 0x40);  // the header's size: the mesh follows
  const std::size_t mesh = m.size();
  m.resize(mesh + 0x60, 0);
  m[mesh + 0x13] = second.empty() ? 1 : 3;
  Put32(m, mesh + 0x1C, 0x200 | 0x400);
  const auto group = [&](const std::vector<Corner>& corners) {
    std::vector<u8> g{0, 0x98};
    g.resize(4);
    Put16(g, 2, static_cast<u16>(corners.size()));
    for (const Corner& c : corners)
      for (const float v : {c.x, c.y, c.z, 0.0f, 1.0f, 0.0f})
      {
        const std::size_t at = g.size();
        if (short_numbers)
          Put16(g, at, static_cast<u16>(static_cast<s16>(std::lround(v * 8191))));
        else
          PutF(g, at, v);
      }
    g.push_back(0);
    while (g.size() % 32 != 0)
      g.push_back(0);
    return g;
  };
  const std::vector<u8> first = group(strip);
  Put32(m, mesh + 0x28, static_cast<u32>(first.size()));
  m.insert(m.end(), first.begin(), first.end());
  if (!second.empty())
  {
    const std::vector<u8> other = group(second);
    Put32(m, mesh + 0x2C, static_cast<u32>(other.size()));
    m.insert(m.end(), other.begin(), other.end());
  }
  return m;
}

// A model file of named models.
std::vector<u8> GmaFile(const std::vector<std::pair<std::string, std::vector<u8>>>& models)
{
  std::vector<u8> names;
  std::vector<u8> body;
  std::vector<u8> f;
  const u32 count = static_cast<u32>(models.size());
  Put32(f, 0, count);
  std::vector<std::pair<u32, u32>> entries;
  for (const auto& [name, model] : models)
  {
    entries.push_back({static_cast<u32>(body.size()), static_cast<u32>(names.size())});
    names.insert(names.end(), name.begin(), name.end());
    names.push_back(0);
    body.insert(body.end(), model.begin(), model.end());
  }
  const u32 base = 8 + count * 8 + static_cast<u32>(names.size());
  Put32(f, 4, base);
  for (u32 i = 0; i < count; ++i)
  {
    Put32(f, 8 + i * 8, entries[i].first);
    Put32(f, 12 + i * 8, entries[i].second);
  }
  f.insert(f.end(), names.begin(), names.end());
  f.insert(f.end(), body.begin(), body.end());
  return f;
}

const std::vector<Corner> QUAD = {{0, 0, 0}, {0, 0, -1}, {1, 0, 0}, {1, 0, -1}};

bool Near(float a, float b, float within = 1e-3f)
{
  return std::fabs(a - b) <= within;
}

std::vector<u8> CameraBytes(float fov, Vec eye, Vec at)
{
  std::vector<u8> b;
  const float values[7] = {fov, eye.x, eye.y, eye.z, at.x, at.y, at.z};
  for (int i = 0; i < 7; ++i)
    PutF(b, static_cast<std::size_t>(i) * 4, values[i]);
  return b;
}
}  // namespace

void TestTheCourseMapReadsTheRoad()
{
  BEGIN_TEST();
  std::string why;
  // a strip of four corners: two triangles, the second turned back to the first's way
  const std::optional<Road> road = ReadPreview(GmaFile({{"SEL01_GRID", Model({{9, 9, 9}, {9, 9, 8}, {8, 9, 9}}, false)},
                                                     {"SEL01_COURSE", Model(QUAD, false)}}), 1, &why);
  CHECK(road.has_value());
  if (!road)
    return;
  CHECK(road->corners.size() == 4);  // the grid's left out
  CHECK((road->triangles == std::vector<u32>{0, 1, 2, 2, 1, 3}));
  CHECK(Near(road->corners[3].x, 1) && Near(road->corners[3].z, -1));
  // 16-bit numbers (13 bits of fraction); a preview stored moved (Loop Cross) put back on its road
  const std::optional<Road> moved = ReadPreview(GmaFile({{"SEL08_COURSE", Model(QUAD, true)}}), 8, &why);
  CHECK(moved && Near(moved->corners[3].x, 1 + 1031) && Near(moved->corners[3].z, -1 - 634) &&
        Near(moved->corners[0].y, 0));
  // group 1's strips are wound the other way: turned back
  const std::optional<Road> two = ReadPreview(GmaFile({{"C", Model(QUAD, false, 0, QUAD)}}), 1, &why);
  CHECK(two && two->triangles.size() == 12);
  if (two)
    CHECK((std::vector<u32>(two->triangles.begin() + 6, two->triangles.end()) == std::vector<u32>{4, 6, 5, 6, 7, 5}));
  // what is not read: a skinned model (none of its triangles), a file too short, a model without its mark
  CHECK(!ReadPreview(GmaFile({{"C", Model(QUAD, false, 0x08)}}), 1, &why) && why == "no triangle of a road in it");
  CHECK(!ReadPreview(std::vector<u8>{0, 0, 0, 9}, 1, &why));
  std::vector<u8> unmarked = GmaFile({{"C", Model(QUAD, false)}});
  unmarked[unmarked.size() - Model(QUAD, false).size()] = 0;
  CHECK(!ReadPreview(unmarked, 1, &why) && why.find("GCMF") != std::string::npos);
  // a strip cut short by the file's end
  std::vector<u8> cut = GmaFile({{"C", Model(QUAD, false)}});
  cut.resize(cut.size() - 40);
  CHECK(!ReadPreview(cut, 1, &why));
}

void TestTheCourseMapsCamera()
{
  BEGIN_TEST();
  const std::optional<Camera> c = CameraFrom(CameraBytes(30, {0, 100, 100}, {0, 0, 0}).data());
  CHECK(c && Near(c->fov, 30));
  if (!c)
    return;
  // the point looked at is the middle; +X right; farther along -Z up the picture; the field of view's edge at 1
  const Projected middle = Project(*c, {0, 0, 0});
  CHECK(Near(middle.x, 0) && Near(middle.y, 0) && Near(middle.depth, std::sqrt(20000.0f), 0.01f));
  CHECK(Project(*c, {10, 0, 0}).x > 0);
  CHECK(Project(*c, {0, 0, -10}).y > 0);
  const float edge = std::tan(15 * 3.14159265f / 180) * middle.depth;
  const Vec up{0, 1 / std::sqrt(2.0f), -1 / std::sqrt(2.0f)};
  CHECK(Near(Project(*c, {up.x * edge, up.y * edge, up.z * edge}).y, 1));
  CHECK(Project(*c, {0, 200, 200}).depth <= 0);  // behind the eye
  // straight down (two courses' cameras): no turn, -Z up the picture
  const std::optional<Camera> down = CameraFrom(CameraBytes(30, {5, 500, 5}, {5, 0, 5}).data());
  CHECK(down && Project(*down, {5, 0, -50}).y > 0 && Near(Project(*down, {5, 0, -50}).x, 0) &&
        Project(*down, {50, 0, 5}).x > 0);
  // unusable: the eye on the point, no field of view, not a number
  CHECK(!CameraFrom(CameraBytes(30, {1, 2, 3}, {1, 2, 3}).data()));
  CHECK(!CameraFrom(CameraBytes(0, {0, 100, 100}, {0, 0, 0}).data()));
  CHECK(!CameraFrom(CameraBytes(std::nanf(""), {0, 100, 100}, {0, 0, 0}).data()));
  // the view without one: the whole road inside the square
  Road road;
  road.corners = {{-300, 0, -200}, {400, 50, 100}, {0, -20, 600}};
  road.triangles = {0, 1, 2};
  const Camera fallback = Fallback(road);
  for (const Vec& p : road.corners)
  {
    const Projected q = Project(fallback, p);
    CHECK(q.depth > 0 && std::fabs(q.x) <= 1 && std::fabs(q.y) <= 1);
  }
}

void TestTheCourseMapsRoadForTheHost()
{
  BEGIN_TEST();
  const Camera c = *CameraFrom(CameraBytes(30, {0, 100, 100}, {0, 0, 0}).data());
  // a flat square of two strips that share an edge's corners: one band, its border the outline
  Road flat;
  flat.corners = {{-10, 0, 0}, {-10, 0, -10}, {0, 0, 0}, {0, 0, -10}, {0, 0, 0}, {0, 0, -10}, {10, 0, 0}, {10, 0, -10}};
  flat.triangles = {0, 1, 2, 2, 1, 3, 4, 5, 6, 6, 5, 7};
  const MinimapCourse a = Prepare(flat, c, 7);
  CHECK(a.course == 7);
  CHECK(a.points.size() == 12);  // six corners: the shared two once
  CHECK(a.triangles.size() == 12 && a.shades.size() == 4);
  CHECK(a.edges.size() == 12 && a.edge_after.size() == 6);  // the border: six edges, not the inner ones
  for (const u8 shade : a.shades)
    CHECK(shade > 200);  // facing the light from above
  // far triangles first
  const auto depth_of = [&](const MinimapCourse& m, u32 t) {
    float y = 0;
    for (int k = 0; k < 3; ++k)
      y += m.points[m.triangles[t * 3 + static_cast<u32>(k)] * 2 + 1];
    return y;  // (higher on this picture: farther)
  };
  for (u32 t = 1; t < 4; ++t)
    CHECK(depth_of(a, t - 1) >= depth_of(a, t) - 1e-4f);
  // the edges in drawing order, each after a triangle that has it
  for (std::size_t e = 0; e + 1 < a.edge_after.size(); ++e)
    CHECK(a.edge_after[e] <= a.edge_after[e + 1]);
  // a closed box: no border, its silhouette the outline; the same triangle twice is drawn once
  Road box;
  box.corners = {{-5, -5, -5}, {5, -5, -5}, {5, 5, -5}, {-5, 5, -5}, {-5, -5, 5}, {5, -5, 5}, {5, 5, 5}, {-5, 5, 5}};
  box.triangles = {0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7, 0, 1, 5, 0, 5, 4, 3, 7, 6, 3, 6, 2, 0, 4, 7, 0, 7, 3, 1, 2, 6,
                   1, 6, 5, 1, 6, 5};
  const MinimapCourse b = Prepare(box, c, 1);
  CHECK(b.triangles.size() == 36);
  CHECK(!b.edges.empty() && b.edges.size() < 2 * 18);  // the silhouette, not every edge
  // a road behind the eye is left out
  Road behind;
  behind.corners = {{0, 200, 200}, {1, 200, 200}, {0, 201, 200}};
  behind.triangles = {0, 1, 2};
  CHECK(Prepare(behind, c, 1).triangles.empty());
}

void TestTheCourseMapsDots()
{
  BEGIN_TEST();
  const Camera c = *CameraFrom(CameraBytes(30, {0, 1000, 1000}, {0, 0, 0}).data());
  Racers r{};
  for (u32 i = 0; i < 6; ++i)
  {
    r[i].present = true;
    r[i].rank = 5 - i;  // racer 5 leads
    r[i].at = {static_cast<float>(i) * 10, 0, 0};
    r[i].colour = 0x100000 * (i + 1) | 0xFF000000;
  }
  r[1].out = true;
  r[2].finished = true;
  // you are racer 0 (last): the others by place, the leader, then you
  MinimapShow show = Pick(r, 6, 0, c);
  CHECK(show.count == 4);
  CHECK(show.dots[0].seat == 3 && show.dots[0].role == MinimapDot::Racer);
  CHECK(show.dots[1].seat == 4 && show.dots[1].role == MinimapDot::Racer);
  CHECK(show.dots[2].seat == 5 && show.dots[2].role == MinimapDot::Leader);
  CHECK(show.dots[3].seat == 0 && show.dots[3].role == MinimapDot::You);
  CHECK(show.dots[3].colour == 0x100000);  // the machine's colour, no more
  CHECK(show.dots[2].x > show.dots[3].x && Near(show.dots[3].y, 0));
  // you lead: one dot, yours; a finished you keeps it; an out you has none
  show = Pick(r, 6, 5, c);
  CHECK(show.count == 4 && show.dots[3].seat == 5 && show.dots[3].role == MinimapDot::You);
  for (u32 k = 0; k < show.count; ++k)
    CHECK(show.dots[k].role != MinimapDot::Leader);
  show = Pick(r, 6, 2, c);
  CHECK(show.count == 5 && show.dots[4].seat == 2 && show.dots[4].role == MinimapDot::You &&
        show.dots[3].seat == 5 && show.dots[3].role == MinimapDot::Leader);
  show = Pick(r, 6, 1, c);
  CHECK(show.count == 4 && show.dots[3].role == MinimapDot::Leader);
  // nobody followed, a racer behind the eye, racers beyond the count
  show = Pick(r, 6, NOBODY, c);
  CHECK(show.count == 4 && show.dots[3].role == MinimapDot::Leader);
  r[3].at = {0, 3000, 3000};
  CHECK(Pick(r, 6, NOBODY, c).count == 3);
  CHECK(Pick(r, 3, NOBODY, c).count == 1);  // racer 0 alone races of the first three: it leads
  // no rival's dot yet: the role is kept, never given
  for (u32 you = 0; you < 6; ++you)
  {
    const MinimapShow s = Pick(r, 6, you, c);
    for (u32 k = 0; k < s.count; ++k)
      CHECK(s.dots[k].role != MinimapDot::Rival);
  }
}

void TestWhenTheCourseMapShows()
{
  BEGIN_TEST();
  constexpr u32 RACE_HUD = 0xFF9FFFFF;
  CHECK(Hidden(RACE_HUD) == 0xFF1FFFFF && HudOn(RACE_HUD) && HudOn(Hidden(RACE_HUD)));
  CHECK(!HudOn(0) && !HudOn(RACE_HUD & 0x3C2));  // out or finished; the results
  CHECK(Shows({.phase = 0x29, .hud_parts = Hidden(RACE_HUD)}));
  CHECK(Shows({.phase = 0x28, .hud_parts = Hidden(RACE_HUD)}));
  CHECK(!Shows({.phase = 0x27, .hud_parts = RACE_HUD}));  // the course intro
  CHECK(!Shows({.phase = 0x2A, .hud_parts = RACE_HUD}));  // the results
  CHECK(!Shows({.phase = 0x29, .hud_parts = 0}));
  CHECK(!Shows({.phase = 0x29, .hud_parts = RACE_HUD, .practice_paused = true}));
  // watched: with the overlay, whatever the HUD (behind the chase camera the image hides it)
  CHECK(Shows({.phase = 0x29, .hud_parts = 0, .watched = true, .overlay = true}));
  CHECK(!Shows({.phase = 0x29, .hud_parts = RACE_HUD, .watched = true, .overlay = false}));
  CHECK(!Shows({.phase = 0x2A, .hud_parts = RACE_HUD, .watched = true, .overlay = true}));
}

void TestTheCourseMapsOffsetsAreTheLaunchers()
{
  BEGIN_TEST();
  std::ifstream file(LauncherFile("net/src/Launcher.Core/Files/PreviewPlacement.cs"));
  CHECK(file.is_open());
  std::stringstream text;
  text << file.rdbuf();
  const std::string s = text.str();
  const std::regex row(R"(\[(\d+)\] = new\((-?\d+), (-?\d+), (-?\d+)\))");
  int rows = 0;
  for (auto it = std::sregex_iterator(s.begin(), s.end(), row); it != std::sregex_iterator(); ++it, ++rows)
  {
    const Vec o = Offset(static_cast<u32>(std::stoi((*it)[1])));
    CHECK(Near(o.x, std::stof((*it)[2])) && Near(o.y, std::stof((*it)[3])) && Near(o.z, std::stof((*it)[4])));
  }
  CHECK(rows == 5);
  int moved = 0;
  for (u32 course = 0; course < 50; ++course)
    moved += Offset(course).x != 0 || Offset(course).z != 0;
  CHECK(moved == rows);
}

void TestTheCourseMapsViewTurns()
{
  BEGIN_TEST();
  // the stick: nothing inside the dead zone (a stick at rest, the clamp's small values), full tilt at TURN_FULL
  CHECK(TurnOf(0) == 0 && TurnOf(10) == 0 && TurnOf(-10) == 0);
  CHECK(TurnOf(59) == 1 && TurnOf(-59) == -1 && TurnOf(52) == 1);
  CHECK(TurnOf(31) > 0.4f && TurnOf(31) < 0.6f && Near(TurnOf(-31), -TurnOf(31)));
  // the bindings: the defaults (.rodata 0x108) bind nothing to the C-stick; an axis row of 0x40 or 0x80, or a button
  // row with a bit of 0xF0000, in any of the three sets, does
  std::vector<u8> b;
  const u32 defaults[21] = {0x00100100, 0x00200002, 0x100, 0x200, 0x400, 0x10, 0x800,
                            0x00100100, 0x00020001, 0x100, 0x200, 0x80000000, 0x80000000, 0x800,
                            0x00100100, 0x00020008, 0x400000, 0x800000, 0x80000000, 0x80000000, 0x100};
  for (u32 i = 0; i < 21; ++i)
    Put32(b, i * 4, defaults[i]);
  CHECK(b.size() == BINDING_SETS * BINDING_SET_SIZE);
  CHECK(!CstickBound(b.data()));
  for (u16 axis : {u16{0x40}, u16{0x80}})
  {
    std::vector<u8> bound = b;
    Put16(bound, BINDING_SET_SIZE * 2 + 6, axis);  // the third set's fourth axis row
    CHECK(CstickBound(bound.data()));
  }
  for (u32 bit : {0x10000u, 0x20000u, 0x40000u, 0x80000u})
  {
    std::vector<u8> bound = b;
    Put32(bound, BINDING_SET_SIZE + 8 + 4 * 4, 0x800 | bit);  // the second set's last button row
    CHECK(CstickBound(bound.data()));
  }
  std::vector<u8> other = b;
  Put16(other, 0, 0x20);
  Put32(other, 8, 0x100000);  // (beside the C-stick's bits)
  CHECK(!CstickBound(other.data()));
  // turning: TURN_SPEED a second at full tilt, slowing into the limit (TURN_LIMIT, never past it); let go, back to 0
  // in TURN_BACK, eased (most of the way in the first half)
  ViewTurn v;
  CHECK(v.Step(0, 1.0f / 60) == 0);
  for (int i = 0; i < 20; ++i)
    v.Step(1, 1.0f / 60);
  CHECK(Near(v.angle, 60, 0.01f));  // (full speed until the last 30% of the way)
  for (int i = 0; i < 10; ++i)
    v.Step(1, 1.0f / 60);
  CHECK(v.angle > 70 && v.angle < TURN_LIMIT);
  float a = v.Step(0, TURN_BACK / 2);
  CHECK(a > 0 && a < 90 * 0.2f);
  a = v.Step(0, TURN_BACK / 2);
  CHECK(a == 0 && v.Step(0, 1.0f / 60) == 0);
  ViewTurn w;
  for (int i = 0; i < 600; ++i)
    w.Step(-1, 1.0f / 60);  // held ten seconds: at the limit, not past it
  CHECK(w.angle >= -TURN_LIMIT && w.angle < -TURN_LIMIT + 0.5f);
  w.Step(-1, 0.25f);  // (a long frame)
  CHECK(w.angle >= -TURN_LIMIT);
  w.Step(0, TURN_BACK);
  CHECK(w.angle == 0);
  // the camera turned about the point looked at: the eye's height and distance kept; a quarter turn moves +X from
  // the picture's right to its middle; straight down, the picture itself turns
  const Camera c{30, {0, 100, 100}, {0, 0, 0}};
  const Camera q = Turned(c, 90);
  CHECK(Near(q.eye.y, 100) && Near(std::hypot(q.eye.x, q.eye.z), 100, 0.01f));
  CHECK(Project(c, {10, 0, 0}).x > 0.01f && std::fabs(Project(q, {10, 0, 0}).x) < 1e-4f);
  CHECK(Near(Project(Turned(c, 360), {10, 0, 0}).x, Project(c, {10, 0, 0}).x));
  const Camera down{30, {0, 100, 0}, {0, 0, 0}};
  const Projected p0 = Project(down, {10, 0, 0}), p1 = Project(Turned(down, 90), {10, 0, 0});
  CHECK(p0.x > 0.01f && std::fabs(p1.x) < 1e-4f && std::fabs(p1.y) > 0.01f);
  // many points at once: as one at a time; behind the eye not a number
  const float xyz[9] = {10, 0, 0, -20, 5, 30, 0, 200, 200};
  float xy[6] = {};
  ProjectMany(q, xyz, 3, xy);
  for (int i = 0; i < 2; ++i)
  {
    const Projected one = Project(q, {xyz[i * 3], xyz[i * 3 + 1], xyz[i * 3 + 2]});
    CHECK(Near(xy[i * 2], one.x, 1e-5f) && Near(xy[i * 2 + 1], one.y, 1e-5f));
  }
  ProjectMany(c, xyz, 3, xy);
  CHECK(std::isnan(xy[4]) && std::isnan(xy[5]));
  // the host's copy of the camera
  MinimapCourse course;
  course.camera = {30, 1, 2, 3, 4, 5, 6};
  const Camera back = CameraOf(course);
  CHECK(back.fov == 30 && back.eye.z == 3 && back.at.x == 4);
}
