// The course map's place and drawing (minimap_draw.h).
// SPDX-License-Identifier: GPL-3.0-or-later

#include "minimap_draw.h"

#include "fzgx/minimap.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>

#include <imgui_internal.h>  // (ImGui::RegisterUserTexture, as panel.cpp)

namespace fzgx_port::minimap_draw
{
namespace
{
namespace ov = overlay;

constexpr int SAMPLES = 4;          // a pixel's samples across (and down)
constexpr int SAMPLES_TURNING = 2;  // ... while the view turns (made again every frame then: a quarter of the work)

ImU32 Rgb(fzgx::u32 rgb, float opacity)
{
  return ov::Colour(rgb, opacity);
}

// Sets every sample whose middle lies in triangle a-b-c (in samples) to `value`: row by row from the top corner, the
// span between the long edge and the other two, each crossing stepped by its slope.
void Fill(std::vector<std::uint8_t>& samples, int across, ImVec2 a, ImVec2 b, ImVec2 c, std::uint8_t value)
{
  if (a.y > b.y)
    std::swap(a, b);
  if (b.y > c.y)
    std::swap(b, c);
  if (a.y > b.y)
    std::swap(a, b);
  if (!(c.y - a.y > 1e-6f))
    return;
  const float long_slope = (c.x - a.x) / (c.y - a.y);
  const float top_slope = b.y - a.y > 1e-6f ? (b.x - a.x) / (b.y - a.y) : 0;
  const float bottom_slope = c.y - b.y > 1e-6f ? (c.x - b.x) / (c.y - b.y) : 0;
  const int y0 = std::max(0, static_cast<int>(std::ceil(a.y - 0.5f)));
  const int y1 = std::min(across - 1, static_cast<int>(std::floor(c.y - 0.5f)));
  for (int y = y0; y <= y1; ++y)
  {
    const float cy = y + 0.5f;
    const float x_long = a.x + (cy - a.y) * long_slope;
    const float x_short = cy < b.y ? a.x + (cy - a.y) * top_slope : b.x + (cy - b.y) * bottom_slope;
    const int x0 = std::max(0, static_cast<int>(std::ceil(std::min(x_long, x_short) - 0.5f)));
    const int x1 = std::min(across - 1, static_cast<int>(std::floor(std::max(x_long, x_short) - 0.5f)));
    if (x0 <= x1)
      std::memset(&samples[std::size_t(y) * across + x0], value, std::size_t(x1 - x0 + 1));
  }
}

// A texture that is no longer drawn goes back in two steps (as panel.cpp's): the emulator's drawing destroys its own
// half at the next present, then the rest is freed (FreeRetired).
void Retire(RoadPicture& road)
{
  if (!road.texture)
    return;
  if (road.texture->Status == ImTextureStatus_OK)
  {
    road.texture->SetStatus(ImTextureStatus_WantDestroy);
    road.texture->UnusedFrames = 1;
  }
  road.old.push_back(road.texture);
  road.texture = nullptr;
}

void FreeRetired(RoadPicture& road)
{
  std::erase_if(road.old, [](ImTextureData* texture) {
    if (texture->Status != ImTextureStatus_Destroyed && texture->Status != ImTextureStatus_WantCreate)
      return false;
    ImGui::UnregisterUserTexture(texture);
    IM_DELETE(texture);
    return true;
  });
}

// The pixels: the road in its colour at its opacity, the mark over it opaque (straight alpha, as ImGui blends).
void Paint(ImTextureData* texture, const Coverage& coverage, std::uint32_t mark_colour)
{
  for (std::size_t i = 0; i < coverage.road.size(); ++i)
  {
    const float m = coverage.mark[i] / 255.0f, r = coverage.road[i] / 255.0f * ROAD_OPACITY, a = m + r;
    unsigned char* p = texture->Pixels + i * 4;
    for (int k = 0; k < 3; ++k)
    {
      const int shift = 16 - 8 * k;
      const float mix = a > 0 ? ((mark_colour >> shift & 0xFF) * m + (ROAD >> shift & 0xFF) * r) / a : 0;
      p[k] = static_cast<unsigned char>(std::lround(mix));
    }
    p[3] = static_cast<unsigned char>(std::lround(std::min(a, 1.0f) * 255));
  }
}

ImTextureData* TextureOf(const Coverage& coverage, std::uint32_t mark_colour, int side)
{
  ImTextureData* texture = IM_NEW(ImTextureData)();
  texture->Create(ImTextureFormat_RGBA32, side, side);
  Paint(texture, coverage, mark_colour);
  texture->UseColors = true;
  ImGui::RegisterUserTexture(texture);
  return texture;
}

// The texture's pixels made again in place: the emulator's drawing loads them at its next present.
void Repaint(ImTextureData* texture, const Coverage& coverage, std::uint32_t mark_colour)
{
  Paint(texture, coverage, mark_colour);
  if (texture->Status != ImTextureStatus_OK && texture->Status != ImTextureStatus_WantUpdates)
    return;  // (not made yet: it is made from these pixels)
  const auto w = static_cast<unsigned short>(texture->Width), h = static_cast<unsigned short>(texture->Height);
  const ImTextureRect all{0, 0, w, h};
  texture->Updates.clear();
  texture->Updates.push_back(all);
  texture->UpdateRect = all;
  texture->SetStatus(ImTextureStatus_WantUpdates);
}

// The road's corners (or the mark's) seen at the view's turn and tilt: the course's own points at the game's view,
// else through the turned and tilted camera (behind the eye: not a number).
std::vector<float> PointsAt(const fzgx::MinimapCourse& course, const std::vector<float>& points,
                            const std::vector<float>& world, View view)
{
  if ((view.angle == 0 && view.tilt == 0) || world.size() / 3 != points.size() / 2)
    return points;
  std::vector<float> seen(points.size());
  fzgx::minimap::ProjectMany(fzgx::minimap::ViewOf(course, view.angle, view.tilt), world.data(), seen.size() / 2,
                             seen.data());
  return seen;
}

// The road and the mark in the view, at `samples` a pixel across, scaled to fit the square (`scale`, fzgx/minimap.h
// FitScale: road and mark together, so neither is cut off at any angle).
Coverage CoverageAt(const fzgx::MinimapCourse& course, int side, View view, int samples, float* scale)
{
  std::vector<float> road = PointsAt(course, course.points, course.world, view);
  std::vector<float> mark = PointsAt(course, course.mark, course.mark_world, view);
  std::vector<float> all(road);
  all.insert(all.end(), mark.begin(), mark.end());
  *scale = fzgx::minimap::FitScale(all);
  if (*scale != 1)
  {
    for (float& v : road)
      v *= *scale;
    for (float& v : mark)
      v *= *scale;
  }
  return Silhouette(course, road, mark, side, samples);
}
}  // namespace

ov::Box Place(const ov::Box& picture)
{
  const float w = picture.x1 - picture.x0, h = picture.y1 - picture.y0;
  const float side = std::round(std::min(0.23f * h, 0.20f * w));
  const float cx = picture.x1 - std::max(0.19f * h, side / 2 + 0.02f * w);
  const float lowest = picture.y0 + (SPEED_TOP - SPEED_GAP) * h;  // the square's bottom at most here
  const float cy = std::min(picture.y0 + 0.71f * h, lowest - side / 2);
  const float x0 = std::round(cx - side / 2), y0 = std::floor(cy - side / 2);
  return {x0, y0, x0 + side, y0 + side};
}

Coverage Silhouette(const fzgx::MinimapCourse& course, const std::vector<float>& points,
                    const std::vector<float>& mark, int side, int samples)
{
  if (side <= 0 || samples <= 0)
    return {};
  const int across = side * samples;
  std::vector<std::uint8_t> sampled(std::size_t(across) * across, 0);
  const float half = across / 2.0f;
  // the triangles of `corners` (x, y each) as `value`: the road 1, then the mark 2 over it
  const auto fill = [&](const std::vector<fzgx::u32>& triangles, const std::vector<float>& corners,
                        std::uint8_t value) {
    const std::size_t count = corners.size() / 2;
    const auto corner = [&](fzgx::u32 i) {
      return ImVec2(half + corners[std::size_t{i} * 2] * half, half - corners[std::size_t{i} * 2 + 1] * half);
    };
    for (std::size_t t = 0; t + 2 < triangles.size(); t += 3)
    {
      const fzgx::u32* c = &triangles[t];
      if (c[0] >= count || c[1] >= count || c[2] >= count)
        continue;
      const ImVec2 p[3] = {corner(c[0]), corner(c[1]), corner(c[2])};
      if (std::isfinite(p[0].x) && std::isfinite(p[1].x) && std::isfinite(p[2].x))  // (none: behind the eye)
        Fill(sampled, across, p[0], p[1], p[2], value);
    }
  };
  fill(course.triangles, points, 1);
  fill(course.mark_triangles, mark, 2);
  Coverage out;
  out.road.resize(std::size_t(side) * side);
  out.mark.resize(std::size_t(side) * side);
  const int whole = samples * samples;
  for (int y = 0; y < side; ++y)
    for (int x = 0; x < side; ++x)
    {
      int road = 0, marked = 0;
      for (int v = 0; v < samples; ++v)
        for (int u = 0; u < samples; ++u)
        {
          const std::uint8_t s = sampled[std::size_t(y * samples + v) * across + x * samples + u];
          road += s == 1, marked += s == 2;
        }
      out.road[std::size_t(y) * side + x] = static_cast<std::uint8_t>((road * 255 + whole / 2) / whole);
      out.mark[std::size_t(y) * side + x] = static_cast<std::uint8_t>((marked * 255 + whole / 2) / whole);
    }
  return out;
}

void Draw(ImDrawList* list, RoadPicture& road, const std::shared_ptr<const fzgx::MinimapCourse>& course,
          const fzgx::MinimapShow& show, const ov::Box& b, float alpha, View view)
{
  FreeRetired(road);
  const int side = static_cast<int>(std::lround(b.x1 - b.x0));
  const bool still = view.angle == 0 && view.tilt == 0;
  if (road.course != course || road.side != side || !road.texture)
  {
    Retire(road);
    road.course = course;
    road.side = side;
    road.view = view;
    road.texture = TextureOf(CoverageAt(*course, side, view, still ? SAMPLES : SAMPLES_TURNING, &road.scale),
                             course->mark_colour, side);
  }
  else if (road.view.angle != view.angle || road.view.tilt != view.tilt)
  {
    road.view = view;
    Repaint(road.texture, CoverageAt(*course, side, view, still ? SAMPLES : SAMPLES_TURNING, &road.scale),
            course->mark_colour);
  }
  if (road.texture)  // (the road's opacity is in the pixels: the mark's is whole)
    list->AddImage(road.texture->GetTexRef(), ImVec2(b.x0, b.y0), ImVec2(b.x0 + side, b.y0 + side), ImVec2(0, 0),
                   ImVec2(1, 1), Rgb(0xFFFFFF, alpha));
  const fzgx::minimap::Camera seen = fzgx::minimap::ViewOf(*course, view.angle, view.tilt);
  // the dots, in the core's order (the last on top), sized by the map's side: a racer 0.011 of it, you and the leader
  // 0.0175 (a fifth smaller than the map's shrink alone would make them), each with a dark rim; placed at
  // the road's scale
  const float half = (b.x1 - b.x0) / 2 * road.scale, cx = (b.x0 + b.x1) / 2, cy = (b.y0 + b.y1) / 2;
  const float across = b.x1 - b.x0;
  const ImU32 rim = Rgb(ov::GROUND, 0.85f * alpha);
  for (fzgx::u32 i = 0; i < show.count && i < show.dots.size(); ++i)
  {
    const fzgx::MinimapDot& d = show.dots[i];
    const bool big = d.role != fzgx::MinimapDot::Racer;
    const float r = (big ? 0.0175f : 0.011f) * across, edge = std::max(1.0f, 0.005f * across);
    float x = d.x, y = d.y;
    if (!still)
    {
      const auto q = fzgx::minimap::Project(seen, {d.wx, d.wy, d.wz});
      if (!(q.depth > 0))
        continue;
      x = q.x, y = q.y;
    }
    const ImVec2 p(cx + x * half, cy - y * half);
    list->AddCircleFilled(p, r + edge, rim, 20);
    list->AddCircleFilled(p, r, Rgb(d.colour, alpha), 20);
    if (d.role == fzgx::MinimapDot::You)
      list->AddCircle(p, r + 1.4f * edge, Rgb(ov::TEXT, alpha), 24, 1.2f * edge);
  }
}
}  // namespace fzgx_port::minimap_draw
