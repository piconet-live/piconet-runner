// Turning and tilting our course map's view (fzgx/minimap.h, "Turning and tilting the view"): the camera turned and
// tilted, the C-stick read, the player's bindings checked, the angles stepped within their limits and eased back,
// and the scale that fits the view's course in the square. Picture only.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fzgx/minimap.h"

#include <algorithm>
#include <cmath>

namespace fzgx::minimap
{
namespace
{
constexpr float PI = 3.14159265358979f;
}  // namespace

Camera CameraOf(const MinimapCourse& course)
{
  const auto& c = course.camera;
  return {c[0], {c[1], c[2], c[3]}, {c[4], c[5], c[6]}};
}

Camera Turned(const Camera& camera, float degrees)
{
  const float a = degrees * PI / 180, cs = std::cos(a), sn = std::sin(a);
  const Vec d{camera.eye.x - camera.at.x, camera.eye.y - camera.at.y, camera.eye.z - camera.at.z};
  Camera turned = camera;
  if (d.x == 0 && d.z == 0)
  {
    turned.spin += degrees;  // (straight down: the picture turns about its middle)
    return turned;
  }
  turned.eye = {camera.at.x + d.x * cs + d.z * sn, camera.eye.y, camera.at.z - d.x * sn + d.z * cs};
  return turned;
}

float Elevation(const Camera& camera)
{
  const float dx = camera.eye.x - camera.at.x, dy = camera.eye.y - camera.at.y, dz = camera.eye.z - camera.at.z;
  return std::atan2(dy, std::sqrt(dx * dx + dz * dz)) * 180 / PI;
}

Camera Tilted(const Camera& camera, float degrees)
{
  if (degrees == 0)
    return camera;
  const Vec d{camera.eye.x - camera.at.x, camera.eye.y - camera.at.y, camera.eye.z - camera.at.z};
  const float across = std::sqrt(d.x * d.x + d.z * d.z), distance = std::sqrt(across * across + d.y * d.y);
  const float was = Elevation(camera);
  const float e = std::clamp(was - degrees, std::min(was, TILT_LOWEST), std::max(was, TILT_STEEPEST)) * PI / 180;
  Camera tilted = camera;
  // the way the eye leans: its own, or for a camera straight down the picture's bottom (its spin then taken into the
  // eye's place, so the picture keeps its turn)
  float hx, hz;
  if (across > distance * 1e-5f)
    hx = d.x / across, hz = d.z / across;
  else
    hx = std::sin(camera.spin * PI / 180), hz = std::cos(camera.spin * PI / 180), tilted.spin = 0;
  tilted.eye = {camera.at.x + hx * std::cos(e) * distance, camera.at.y + std::sin(e) * distance,
                camera.at.z + hz * std::cos(e) * distance};
  return tilted;
}

Camera ViewOf(const MinimapCourse& course, float turn, float tilt)
{
  return Tilted(Turned(CameraOf(course), turn), tilt);
}

Range TiltRange(const Camera& camera)
{
  const float e = Elevation(camera);
  return {std::min(0.0f, e - TILT_STEEPEST), std::max(0.0f, std::min(TILT_FLATTER, e - TILT_LOWEST))};
}

float FitScale(const std::vector<float>& points)
{
  float most = 0;
  for (const float v : points)
    if (std::isfinite(v))
      most = std::max(most, std::fabs(v));
  return most > 1 ? 1 / most : 1;
}

bool CstickBound(const u8* set)
{
  for (u32 s = 0; s < BINDING_SETS; ++s)
  {
    const u8* row = set + s * BINDING_SET_SIZE;
    for (u32 k = 0; k < 4; ++k)
    {
      const u32 half = u32{row[k * 2]} << 8 | row[k * 2 + 1];
      if (half == 0x40 || half == 0x80)
        return true;
    }
    for (u32 k = 0; k < 5; ++k)
    {
      const u8* w = row + 8 + k * 4;
      const u32 word = u32{w[0]} << 24 | u32{w[1]} << 16 | u32{w[2]} << 8 | w[3];
      if (word & 0xF0000)
        return true;
    }
  }
  return false;
}

float TurnOf(std::int8_t cstick_x)
{
  const float x = cstick_x, size = std::fabs(x);
  if (size <= TURN_DEAD)
    return 0;
  const float t = std::min(1.0f, (size - TURN_DEAD) / (TURN_FULL - TURN_DEAD));
  return x < 0 ? -t : t;
}

float ViewTurn::Step(float move, float seconds, Range range, float speed)
{
  seconds = std::clamp(seconds, 0.0f, 0.25f);
  if (move != 0)
  {
    // the room left towards the limit it moves to: full speed until the last EASE of that side's range, then slower
    // in step with the room (an ease into the limit); held to the range (a long frame's step)
    constexpr float EASE = 0.3f;
    const float side = move > 0 ? range.hi : -range.lo, room = move > 0 ? range.hi - angle : angle - range.lo;
    const float pace = side > 0 ? std::clamp(room / (EASE * side), 0.0f, 1.0f) : 0.0f;
    angle = std::clamp(angle + move * speed * seconds * pace, std::min(range.lo, 0.0f), std::max(range.hi, 0.0f));
    back_from = angle;
    back_time = 0;
    return angle;
  }
  if (angle == 0)
    return 0;
  back_time += seconds;
  const float p = std::min(1.0f, back_time / TURN_BACK);
  const float eased = 1 - (1 - p) * (1 - p) * (1 - p);  // ease-out cubic
  angle = p >= 1 ? 0 : back_from * (1 - eased);
  return angle;
}
}  // namespace fzgx::minimap
