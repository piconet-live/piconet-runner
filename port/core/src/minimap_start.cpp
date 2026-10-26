// The course map's start mark (fzgx/minimap.h Start, RoadAcross, StartMark, AddMark): where the start/finish line
// is, from the course file's first checkpoint; how wide the drawn road is there, from the preview's own triangles;
// and the mark's shape, sized on the map, the game's own (fzgx/minimap.h MARK_THICK).
//   - The line: the course file (stage/COLI_COURSEnn, unpacked, big-endian; the layout piconet-tools' ColiTrackReader
//     reads, as the Manifold project describes it): its track nodes in lap order, the first node's checkpoints,
//     the first checkpoint's start plane (the road's middle where lap distance 0 begins and the normal pointing the
//     way the race runs) and width. A split start (course 24's three roads) has a checkpoint a road: they
//     share the plane, and the widest gives the width.
//   - The width drawn: the checkpoint's is the collision road's, and on a cylinder or a pipe it is measured around
//     it (course 33: 181 for a cylinder 120 across); the preview cut by the start plane gives what the map
//     shows. Only triangles near the line count (within the road's width of its middle, above and below), so a
//     road passing over or under the line elsewhere is not taken for it.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fzgx/minimap.h"

#include "fzgx/guest.h"

#include <algorithm>
#include <bit>
#include <cmath>

namespace fzgx::minimap
{
namespace
{
u32 Be32(const std::vector<u8>& d, std::size_t at)
{
  return u32{d[at]} << 24 | u32{d[at + 1]} << 16 | u32{d[at + 2]} << 8 | d[at + 3];
}
float BeF(const std::vector<u8>& d, std::size_t at)
{
  return std::bit_cast<float>(Be32(d, at));
}
Vec BeVec(const std::vector<u8>& d, std::size_t at)
{
  return {BeF(d, at), BeF(d, at + 4), BeF(d, at + 8)};
}
Vec Add(const Vec& a, const Vec& b)
{
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}
Vec Sub(const Vec& a, const Vec& b)
{
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
Vec Scaled(const Vec& a, float k)
{
  return {a.x * k, a.y * k, a.z * k};
}
float Dot(const Vec& a, const Vec& b)
{
  return a.x * b.x + a.y * b.y + a.z * b.z;
}
Vec Cross(const Vec& a, const Vec& b)
{
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
bool Finite(const Vec& a)
{
  return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z);
}

// The line's own axes: right across the road (level: the course's up crossed in), up, forward.
struct Frame
{
  Vec right, up, forward;
};
Frame FrameOf(const Start& s)
{
  Vec right = Cross(s.forward, {0, 1, 0});
  const float length = std::sqrt(Dot(right, right));
  right = length > 1e-3f ? Scaled(right, 1 / length) : Vec{1, 0, 0};  // (a line facing straight up or down)
  return {right, Cross(right, s.forward), s.forward};
}
}  // namespace

std::optional<Start> ReadStart(const std::vector<u8>& coli, std::string* why)
{
  const std::size_t size = coli.size();
  if (size < file::COLI_NODES + 8)
  {
    *why = "too short for a course file";
    return std::nullopt;
  }
  const u32 nodes = Be32(coli, file::COLI_NODES), node = Be32(coli, file::COLI_NODES + 4);
  if (nodes == 0 || node > size || size - node < file::COLI_NODE_SIZE)
  {
    *why = "no track node in it";
    return std::nullopt;
  }
  const u32 checkpoints = Be32(coli, node), first = Be32(coli, node + 4);
  if (checkpoints == 0 || checkpoints > 64 || first > size ||
      (size - first) / file::COLI_CHECKPOINT_SIZE < checkpoints)
  {
    *why = "the first track node has no checkpoint in the file";
    return std::nullopt;
  }
  Start s;
  s.at = BeVec(coli, first + file::COLI_CHECKPOINT_ORIGIN);
  const Vec normal = BeVec(coli, first + file::COLI_CHECKPOINT_NORMAL);
  const float length = std::sqrt(Dot(normal, normal));
  if (!Finite(s.at) || !(length > 0.5f && length < 2))
  {
    *why = "the first checkpoint's start plane is not a plane";
    return std::nullopt;
  }
  s.forward = Scaled(normal, 1 / length);
  for (u32 i = 0; i < checkpoints; ++i)
  {
    const float w = BeF(coli, first + i * file::COLI_CHECKPOINT_SIZE + file::COLI_CHECKPOINT_WIDTH);
    if (std::isfinite(w) && w < 10000)
      s.width = std::max(s.width, w);
  }
  if (!(s.width > 0))
  {
    *why = "the first checkpoint has no width";
    return std::nullopt;
  }
  return s;
}

std::optional<Across> RoadAcross(const Road& road, const Start& start)
{
  const Frame f = FrameOf(start);
  const float reach = std::max(start.width, 40.0f);  // across and up or down from the middle
  struct Span
  {
    float lo, hi, top;
  };
  std::vector<Span> spans;
  const std::size_t corners = road.corners.size();
  for (std::size_t t = 0; t + 2 < road.triangles.size(); t += 3)
  {
    const u32* c = &road.triangles[t];
    if (c[0] >= corners || c[1] >= corners || c[2] >= corners)
      continue;
    Vec p[3];
    float d[3];
    for (int k = 0; k < 3; ++k)
    {
      const Vec r = Sub(road.corners[c[k]], start.at);
      p[k] = {Dot(r, f.right), Dot(r, f.up), 0};
      d[k] = Dot(r, f.forward);
    }
    // where the triangle's edges cross the plane: two points (none when it lies all on one side)
    float u[2], v[2];
    int n = 0;
    for (int k = 0; k < 3 && n < 2; ++k)
    {
      const int j = (k + 1) % 3;
      if ((d[k] < 0) == (d[j] < 0) || d[k] == d[j])
        continue;
      const float s = d[k] / (d[k] - d[j]);
      u[n] = p[k].x + (p[j].x - p[k].x) * s;
      v[n] = p[k].y + (p[j].y - p[k].y) * s;
      ++n;
    }
    if (n < 2 || std::fabs(v[0]) > reach || std::fabs(v[1]) > reach || std::min(u[0], u[1]) > reach ||
        std::max(u[0], u[1]) < -reach)
      continue;
    spans.push_back({std::min(u[0], u[1]), std::max(u[0], u[1]), std::max(v[0], v[1])});
  }
  if (spans.empty())
    return std::nullopt;
  // the spans joined where they meet (a metre's gap is the same road), and the joined stretch holding the middle,
  // else the nearest to it within a quarter of the road's width
  std::sort(spans.begin(), spans.end(), [](const Span& a, const Span& b) { return a.lo < b.lo; });
  std::optional<Across> best;
  float best_off = start.width / 4;
  for (std::size_t i = 0; i < spans.size();)
  {
    Across a{spans[i].lo, spans[i].hi, spans[i].top};
    std::size_t j = i + 1;
    for (; j < spans.size() && spans[j].lo <= a.right + 1; ++j)
      a.right = std::max(a.right, spans[j].hi), a.top = std::max(a.top, spans[j].top);
    i = j;
    const float off = a.left > 0 ? a.left : a.right < 0 ? -a.right : 0;
    if (off <= best_off)
      best = a, best_off = off;
  }
  return best;
}

bool PennantRight(u32 course)
{
  switch (course)
  {
  case 16: case 17: case 21: case 24: case 26: case 27: case 28: case 29: case 31: case 32: case 36: case 37:
    return true;
  default:
    return false;
  }
}

Road StartMark(const Start& start, const std::optional<Across>& across, const Camera& camera, bool pennant_right)
{
  const Frame f = FrameOf(start);
  const Vec at = Add(start.at, Scaled(f.up, across ? across->top : 0));
  // a share of the map's side in metres at the line: the side spans 2 tan(fov / 2) of the depth
  const float depth = Project(camera, at).depth;
  const float side = 2 * std::max(depth, 1.0f) * std::tan(camera.fov / 2 * 3.14159265358979f / 180);
  const float thick = MARK_THICK * side, flag = MARK_OVER_FLAG * side, over = MARK_OVER * side;
  const float left = (across ? across->left : -start.width / 2) - (pennant_right ? over : flag);
  const float right = (across ? across->right : start.width / 2) + (pennant_right ? flag : over);
  const float out = pennant_right ? 1.0f : -1.0f;               // outwards, across the road
  const float end = pennant_right ? right : left, inner = end - out * thick;  // the shaft's outer and road-side edges
  const float neck = (MARK_TIP - MARK_HEAD) * side, head = MARK_HEAD_WIDTH * side;
  const auto corner = [&](float u, float v) { return Add(at, Add(Scaled(f.right, u), Scaled(f.forward, v))); };
  Road mark;
  mark.corners = {
      corner(left, -thick / 2), corner(right, -thick / 2),               // 0, 1: the bar's back
      corner(right, thick / 2), corner(left, thick / 2),                 // 2, 3: its front
      corner(inner, thick / 2), corner(inner, neck), corner(end, neck),  // 4, 5, 6: the shaft, from the bar's end
      corner(inner + out * head, neck),                                  // 7: the head's barb, out from the road
      corner(inner, MARK_TIP * side),                                    // 8: its tip, on the shaft's road side
  };
  const u32 front = pennant_right ? 2 : 3;
  mark.triangles = {0, 1, 2, 0, 2, 3, front, 4, 5, front, 5, 6, 5, 7, 8};
  return mark;
}

void AddMark(const Road& mark, const Camera& camera, MinimapCourse& out)
{
  out.mark_world.clear();
  for (const Vec& c : mark.corners)
    out.mark_world.insert(out.mark_world.end(), {c.x, c.y, c.z});
  out.mark.assign(mark.corners.size() * 2, 0);
  ProjectMany(camera, out.mark_world.data(), mark.corners.size(), out.mark.data());
  out.mark_triangles = mark.triangles;
  out.mark_colour = file::MAP_START_COLOUR;
}
}  // namespace fzgx::minimap
