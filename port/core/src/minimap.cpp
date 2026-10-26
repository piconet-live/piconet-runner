// Our course map's decision (fzgx/minimap.h): the course select's model read as the road, the game's map camera, the
// road prepared for the host, the dots and when the map shows. No game in it.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fzgx/minimap.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <map>
#include <tuple>
#include <utility>

namespace fzgx::minimap
{
namespace
{
constexpr float PI = 3.14159265358979f;

// A reader of big-endian numbers that stops at the file's end: `ok` turns false and every read after gives 0.
struct Cursor
{
  const std::vector<u8>& data;
  std::size_t at = 0;
  bool ok = true;

  bool Has(std::size_t n) const { return ok && at <= data.size() && data.size() - at >= n; }
  u32 U(std::size_t n)
  {
    if (!Has(n))
    {
      ok = false;
      return 0;
    }
    u32 v = 0;
    for (std::size_t i = 0; i < n; ++i)
      v = v << 8 | data[at + i];
    at += n;
    return v;
  }
  u8 U8() { return static_cast<u8>(U(1)); }
  u16 U16() { return static_cast<u16>(U(2)); }
  u32 U32() { return U(4); }
  float F() { return std::bit_cast<float>(U32()); }
  // a float, or in a model of 16-bit numbers a signed fixed-point value with 13 fraction bits
  float Number(bool short_numbers) { return short_numbers ? static_cast<s16>(U16()) / 8191.0f : F(); }
  void Skip(std::size_t n)
  {
    if (!Has(n))
      ok = false;
    else
      at += n;
  }
};

constexpr u32 GCMF = 0x47434D46;
constexpr u32 SHORT_NUMBERS = 0x01, SKINNED = 0x08, EFFECTIVE = 0x10;
constexpr u32 MATRIX_INDEX = 0x1, POSITION = 0x200, NORMAL = 0x400, COLOUR = 0x800, UV0 = 0x2000, UV1 = 0x4000,
              UV2 = 0x8000, NBT = 0x02000000;
constexpr u32 KNOWN = MATRIX_INDEX | POSITION | NORMAL | COLOUR | UV0 | UV1 | UV2 | NBT;

// One mesh's strip groups into `road`, from `c` at the mesh's header; false: a mesh this reader does not know.
bool ReadMesh(Cursor& c, bool short_numbers, const Vec& offset, Road& road)
{
  const std::size_t start = c.at;
  c.at = start + 0x13;
  const u8 groups = c.U8();
  c.at = start + 0x1C;
  const u32 flags = c.U32();
  c.at = start + 0x28;
  std::array<u32, 4> sizes{c.U32(), c.U32(), 0, 0};
  c.at = start + 0x60;
  if ((flags & ~KNOWN) != 0 || (flags & POSITION) == 0 || !c.ok)
    return false;
  for (int group = 0; group < 4 && c.ok; ++group)
  {
    if (group == 2)
    {
      if ((groups & 0x0F) != 0x0F)
        break;
      const std::size_t extra = c.at;  // a header before the third and fourth groups
      c.at = extra + 8;
      sizes[2] = c.U32();
      sizes[3] = c.U32();
      c.at = extra + 0x20;
    }
    if ((groups & (1 << group)) == 0)
      continue;
    const std::size_t end = c.at + sizes[static_cast<std::size_t>(group)];
    if (end > c.data.size())
      return false;
    c.Skip(1);  // a zero byte leads the group
    while (c.ok && c.at < end)
    {
      const u8 command = c.U8();
      if (command == 0)
        break;  // padding after the last strip
      if (command != 0x98 && command != 0x99)
        return false;
      const u16 count = c.U16();
      const u32 first = static_cast<u32>(road.corners.size());
      for (u32 v = 0; v < count && c.ok; ++v)
      {
        if (flags & MATRIX_INDEX)
          c.Skip(1);
        Vec p;
        p.x = c.Number(short_numbers) + offset.x;
        p.y = c.Number(short_numbers) + offset.y;
        p.z = c.Number(short_numbers) + offset.z;
        if (flags & NORMAL)
          c.Skip(short_numbers ? 6 : 12);
        if (flags & NBT)
          c.Skip(36);
        if (flags & COLOUR)
          c.Skip(4);
        for (const u32 uv : {UV0, UV1, UV2})
          if (flags & uv)
            c.Skip(short_numbers ? 4 : 8);
        road.corners.push_back(p);
      }
      // a strip's triangles turn each way in turn, and groups 1 and 3 are wound the other way: all made one way
      for (u32 k = 0; k + 2 < count && c.ok; ++k)
      {
        u32 a = k, b = k + 1;
        if (k % 2 == 1)
          std::swap(a, b);
        u32 last = k + 2;
        if (group % 2 == 1)
          std::swap(b, last);
        road.triangles.insert(road.triangles.end(), {first + a, first + b, first + last});
      }
    }
    c.at = end;
  }
  return c.ok;
}

Vec Sub(const Vec& a, const Vec& b)
{
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
float Dot(const Vec& a, const Vec& b)
{
  return a.x * b.x + a.y * b.y + a.z * b.z;
}
Vec Cross(const Vec& a, const Vec& b)
{
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
float Length(const Vec& a)
{
  return std::sqrt(Dot(a, a));
}
Vec Scaled(const Vec& a, float k)
{
  return {a.x * k, a.y * k, a.z * k};
}
bool Finite(const Vec& a)
{
  return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z);
}

// The camera's axes: right, up, forward, as the game's look-at makes them (main.dol fn_8006F120: a turn about the
// vertical by the eye's direction from the point, atan2(dx, dz), then a tilt, never a roll; straight down, as courses
// 25 and 41 look, the turn is 0 and the world's -Z is up the picture).
struct Axes
{
  Vec right, up, forward;
};
std::optional<Axes> AxesOf(const Camera& c)
{
  const Vec f = Sub(c.at, c.eye);
  const float length = Length(f);
  if (!(length > 1e-3f))
    return std::nullopt;
  const Vec forward = Scaled(f, 1 / length);
  const float dx = c.eye.x - c.at.x, dz = c.eye.z - c.at.z;
  const float yaw = (dx == 0 && dz == 0 ? 0 : std::atan2(dx, dz)) + c.spin * PI / 180;
  const Vec right{std::cos(yaw), 0, -std::sin(yaw)};
  return Axes{right, Cross(right, forward), forward};
}
}  // namespace

Vec Offset(u32 course)
{
  switch (course)
  {
  case 5: return {-526, 0, -1};      // Aeropolis: Multiplex
  case 8: return {1031, 0, -634};    // Lightning: Loop Cross
  case 16: return {-500, 0, 238};    // Casino Palace: Split Oval
  case 26: return {-346, 0, 716};    // Sand Ocean: Surface Slide
  case 29: return {-1137, 0, -332};  // Casino Palace: Double Branches
  default: return {};
  }
}

std::optional<Road> ReadPreview(const std::vector<u8>& gma, u32 course, std::string* why)
{
  Cursor c{gma};
  const u32 count = c.U32(), base = c.U32();
  if (!c.ok || count > 4096 || 8 + std::size_t{count} * 8 > gma.size() || base > gma.size())
  {
    *why = "not a model file (its count or its models' place does not fit it)";
    return std::nullopt;
  }
  const std::size_t names = 8 + std::size_t{count} * 8;
  const Vec offset = Offset(course);
  Road road;
  for (u32 i = 0; i < count; ++i)
  {
    c.at = 8 + std::size_t{i} * 8;
    const u32 model = c.U32(), name_at = c.U32();
    if (model == 0xFFFFFFFF && name_at == 0)
      continue;
    std::string name;
    for (std::size_t k = names + name_at; k < gma.size() && gma[k] != 0 && name.size() < 64; ++k)
      name += static_cast<char>(gma[k]);
    if (name.find("GRID") != std::string::npos)
      continue;
    const std::size_t start = std::size_t{base} + model;
    c.at = start;
    if (c.U32() != GCMF)
    {
      *why = "model " + name + " does not start with GCMF";
      return std::nullopt;
    }
    const u32 attributes = c.U32();
    c.at = start + 0x1A;
    const u32 meshes = u32{c.U16()} + c.U16();
    c.at = start + 0x20;
    const u32 header = c.U32();
    if (!c.ok || (attributes & (SKINNED | EFFECTIVE)) != 0)
      continue;  // (as the launcher: a model of indexed corners is not read)
    c.at = start + header;
    Road model_road;
    bool known = true;
    for (u32 m = 0; m < meshes && known; ++m)
      known = ReadMesh(c, (attributes & SHORT_NUMBERS) != 0, offset, model_road);
    if (!known)
      continue;
    const u32 first = static_cast<u32>(road.corners.size());
    road.corners.insert(road.corners.end(), model_road.corners.begin(), model_road.corners.end());
    for (const u32 t : model_road.triangles)
      road.triangles.push_back(first + t);
  }
  if (road.triangles.empty())
  {
    *why = "no triangle of a road in it";
    return std::nullopt;
  }
  return road;
}

std::optional<Camera> CameraFrom(const u8* b)
{
  const auto f = [&](int i) {
    return std::bit_cast<float>(u32{b[i * 4]} << 24 | u32{b[i * 4 + 1]} << 16 | u32{b[i * 4 + 2]} << 8 | b[i * 4 + 3]);
  };
  Camera c{f(0), {f(1), f(2), f(3)}, {f(4), f(5), f(6)}};
  if (!(c.fov >= 1 && c.fov <= 170) || !Finite(c.eye) || !Finite(c.at) || !AxesOf(c))
    return std::nullopt;
  return c;
}

Camera Fallback(const Road& road)
{
  Vec lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
  for (const Vec& p : road.corners)
  {
    lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
    hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
  }
  Camera c;
  c.at = road.corners.empty() ? Vec{} : Scaled(Vec{lo.x + hi.x, lo.y + hi.y, lo.z + hi.z}, 0.5f);
  float radius = 1;
  for (const Vec& p : road.corners)
    radius = std::max(radius, Length(Sub(p, c.at)));
  const float distance = radius / std::sin(c.fov / 2 * PI / 180);
  const float pitch = 50 * PI / 180;
  c.eye = {c.at.x, c.at.y + distance * std::sin(pitch), c.at.z + distance * std::cos(pitch)};
  return c;
}

Projected Project(const Camera& camera, const Vec& point)
{
  const std::optional<Axes> axes = AxesOf(camera);
  if (!axes)
    return {};
  const Vec d = Sub(point, camera.eye);
  const float depth = Dot(d, axes->forward);
  if (!(depth > 1e-3f))
    return {0, 0, depth > 0 ? 0 : depth};
  const float spread = depth * std::tan(camera.fov / 2 * PI / 180);
  return {Dot(d, axes->right) / spread, Dot(d, axes->up) / spread, depth};
}

void ProjectMany(const Camera& camera, const float* xyz, std::size_t count, float* xy)
{
  const std::optional<Axes> axes = AxesOf(camera);
  const float spread = std::tan(camera.fov / 2 * PI / 180);
  for (std::size_t i = 0; i < count; ++i)
  {
    const Vec d = Sub({xyz[i * 3], xyz[i * 3 + 1], xyz[i * 3 + 2]}, camera.eye);
    const float depth = axes ? Dot(d, axes->forward) : 0;
    const bool seen = depth > 1e-3f;
    xy[i * 2] = seen ? Dot(d, axes->right) / (depth * spread) : NAN;
    xy[i * 2 + 1] = seen ? Dot(d, axes->up) / (depth * spread) : NAN;
  }
}

namespace
{
// The road's corners welded: corners at one place are one (each strip of the model has its own), projected.
struct Welded
{
  std::vector<u32> same;          // a corner of the road: its welded corner
  std::vector<Projected> projected;
  std::vector<Vec> world;
};
Welded Weld(const Road& road, const Camera& camera)
{
  Welded w;
  std::map<std::tuple<long, long, long>, u32> by_place;
  w.same.resize(road.corners.size());
  for (std::size_t i = 0; i < road.corners.size(); ++i)
  {
    const Vec& p = road.corners[i];
    const auto key = std::make_tuple(std::lround(p.x * 16), std::lround(p.y * 16), std::lround(p.z * 16));
    const auto [it, added] = by_place.try_emplace(key, static_cast<u32>(w.projected.size()));
    if (added)
    {
      w.projected.push_back(Project(camera, p));
      w.world.push_back(p);
    }
    w.same[i] = it->second;
  }
  return w;
}

struct Triangle
{
  std::array<u32, 3> c;
  float depth;
  u8 shade;
};
// The triangles to draw, far ones first: none of no area, none twice, none behind the eye; each shaded.
std::vector<Triangle> Triangles(const Road& road, const Welded& w)
{
  std::vector<Triangle> kept;
  std::map<std::array<u32, 3>, bool> seen;
  const Vec light = Scaled(Vec{-0.4f, 1, 0.3f}, 1 / Length(Vec{-0.4f, 1, 0.3f}));
  for (std::size_t t = 0; t + 2 < road.triangles.size(); t += 3)
  {
    std::array<u32, 3> c{};
    bool inside = true;
    for (std::size_t k = 0; k < 3 && inside; ++k)
    {
      const u32 corner = road.triangles[t + k];
      inside = corner < w.same.size();
      c[k] = inside ? w.same[corner] : 0;
    }
    if (!inside || c[0] == c[1] || c[1] == c[2] || c[0] == c[2])
      continue;  // (a strip's turn: no area)
    std::array<u32, 3> sorted = c;
    std::ranges::sort(sorted);
    if (!seen.try_emplace(sorted, true).second)
      continue;  // the same triangle twice (both sides of a band)
    const Projected &a = w.projected[c[0]], &b = w.projected[c[1]], &d = w.projected[c[2]];
    if (a.depth <= 0 || b.depth <= 0 || d.depth <= 0)
      continue;
    const Vec n = Cross(Sub(w.world[c[1]], w.world[c[0]]), Sub(w.world[c[2]], w.world[c[0]]));
    const float length = Length(n);
    const float lit = length > 0 ? std::clamp(std::fabs(Dot(n, light)) / length, 0.0f, 1.0f) : 0.5f;
    kept.push_back({c, (a.depth + b.depth + d.depth) / 3, static_cast<u8>(std::lround(lit * 255))});
  }
  std::ranges::stable_sort(kept, [](const Triangle& x, const Triangle& y) { return x.depth > y.depth; });
  return kept;
}

// An edge's triangles: how many, which way each turns on the picture (the model is wound one way, so a triangle seen
// from its back turns the other way), the last drawn.
struct Edge
{
  u32 count = 0, after = 0;
  bool forward = false, backward = false;
};
// The outline: an edge of one triangle (the band's border), or between a triangle seen from the front and one seen
// from the back (where the band turns away: its silhouette); drawn after the later of its triangles, in that order.
void Outline(const std::map<std::pair<u32, u32>, Edge>& edges, MinimapCourse& out)
{
  std::vector<std::pair<u32, std::pair<u32, u32>>> outline;
  for (const auto& [edge, e] : edges)
    if (e.count == 1 || (e.forward && e.backward))
      outline.push_back({e.after, edge});
  std::ranges::sort(outline);
  for (const auto& [after, edge] : outline)
  {
    out.edges.insert(out.edges.end(), {edge.first, edge.second});
    out.edge_after.push_back(after);
  }
}
}  // namespace

MinimapCourse Prepare(const Road& road, const Camera& camera, u32 course)
{
  MinimapCourse out;
  out.course = course;
  out.camera = {camera.fov, camera.eye.x, camera.eye.y, camera.eye.z, camera.at.x, camera.at.y, camera.at.z};
  const Welded w = Weld(road, camera);
  std::vector<Triangle> kept = Triangles(road, w);
  // the corners the kept triangles use, numbered anew
  std::vector<u32> number(w.projected.size(), NOBODY);
  std::map<std::pair<u32, u32>, Edge> edges;
  for (u32 i = 0; i < kept.size(); ++i)
  {
    Triangle& t = kept[i];
    const Projected &a = w.projected[t.c[0]], &b = w.projected[t.c[1]], &c = w.projected[t.c[2]];
    const float turn = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    for (u32& corner : t.c)
    {
      if (number[corner] == NOBODY)
      {
        number[corner] = static_cast<u32>(out.points.size() / 2);
        out.points.push_back(w.projected[corner].x);
        out.points.push_back(w.projected[corner].y);
        out.world.insert(out.world.end(), {w.world[corner].x, w.world[corner].y, w.world[corner].z});
      }
      corner = number[corner];
    }
    out.triangles.insert(out.triangles.end(), t.c.begin(), t.c.end());
    out.shades.push_back(t.shade);
    for (std::size_t k = 0; k < 3; ++k)
    {
      const u32 p = t.c[k], q = t.c[(k + 1) % 3];
      Edge& e = edges[{std::min(p, q), std::max(p, q)}];
      ++e.count;
      e.after = i;
      (turn >= 0 ? e.forward : e.backward) = true;
    }
  }
  Outline(edges, out);
  return out;
}

MinimapShow Pick(const Racers& racers, u32 count, u32 you, const Camera& camera)
{
  MinimapShow show;
  count = std::min<u32>(count, static_cast<u32>(racers.size()));
  if (you < count && (!racers[you].present || racers[you].out))
    you = NOBODY;
  u32 leader = NOBODY;
  for (u32 i = 0; i < count; ++i)
  {
    const Racer& r = racers[i];
    if (r.present && !r.out && !r.finished && (leader == NOBODY || r.rank < racers[leader].rank))
      leader = i;
  }
  // the others: the better placed over the worse
  std::array<u32, 30> others{};
  u32 n = 0;
  for (u32 i = 0; i < count; ++i)
  {
    const Racer& r = racers[i];
    if (r.present && !r.out && !r.finished && i != leader && i != you)
      others[n++] = i;
  }
  std::stable_sort(others.begin(), others.begin() + n, [&](u32 a, u32 b) { return racers[a].rank > racers[b].rank; });
  const auto add = [&](u32 i, u8 role) {
    const Projected p = Project(camera, racers[i].at);
    if (p.depth <= 0 || !std::isfinite(p.x) || !std::isfinite(p.y))
      return;
    MinimapDot& d = show.dots[show.count++];
    d.seat = static_cast<u8>(i);
    d.role = role;
    d.x = p.x;
    d.y = p.y;
    d.wx = racers[i].at.x;
    d.wy = racers[i].at.y;
    d.wz = racers[i].at.z;
    d.colour = racers[i].colour & 0xFFFFFF;
  };
  for (u32 k = 0; k < n; ++k)
    add(others[k], MinimapDot::Racer);
  // (the rival's dot goes here, between the others and the leader, once there is a rival)
  if (leader != NOBODY && leader != you)
    add(leader, MinimapDot::Leader);
  if (you < count)
    add(you, MinimapDot::You);
  return show;
}

bool Shows(const Seen& seen)
{
  if (seen.watched)
    return seen.phase == 0x29 && seen.overlay;
  return (seen.phase == 0x28 || seen.phase == 0x29) && HudOn(seen.hud_parts) && !seen.practice_paused;
}
}  // namespace fzgx::minimap
