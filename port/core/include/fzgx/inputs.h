// "Show inputs": the numbers of m-overlay's default skin (bkacjios/m-overlay, MIT: port/moderngekko/art/inputs;
// source/overlay/skins/default.lua and source/modules/games/core.lua), in its own 512 by 256 units, and the map that
// leans a stick's picture. Numbers only: the adapter's inputs_overlay.cpp draws from them, and the launcher draws the
// same skin (Launcher.App/Controls/PadSkin.cs). The pad it is given is PADStatus as the game reads it, already through
// PADClamp, which is what these ranges are made for.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace fzgx::inputs
{
constexpr float WIDTH = 512, HEIGHT = 256, CELL = 128;
// What PADClamp leaves (the game's table at 0x8008FF40: stick 72, C-stick 59, triggers cut to 30..180, then less 30).
constexpr float STICK_RANGE = 72, CSTICK_RANGE = 59, TRIGGER_RANGE = 150;

struct Point
{
  float x = 0, y = 0;
};

// A stick: its gate's cell, where its picture's centre rests, how far it travels.
struct StickPlace
{
  float gate_x, gate_y, centre_x, centre_y, travel;
};
constexpr StickPlace STICK{22, 52, 64 + 22, 64 + 8, 44};
constexpr StickPlace CSTICK{48 + 128, 52, 64 + 48 + 128, 64 + 18, 32};
constexpr std::uint32_t CSTICK_COLOUR = 0xFFEB00;

// A round button: PADStatus's bit, the art's name, its cell, its colour.
struct Button
{
  std::uint16_t bit;
  const char* art;
  float x, y;
  std::uint32_t colour;
};
constexpr std::array<Button, 6> BUTTONS{{{0x0100, "a", 12 + 64 + 256, 48, 0x00E196},
                                         {0x0200, "b", 16 + 256, 92, 0xE60000},
                                         {0x0400, "x", 138 + 256, 32, 0xFFFFFF},
                                         {0x0800, "y", 60 + 256, -16, 0xFFFFFF},
                                         {0x0010, "z", 128 + 256, -32, 0xA54BA5},
                                         {0x1000, "start", 256, 26, 0xFFFFFF}}};
constexpr float DPAD_X = 108, DPAD_Y = 144;
struct DPadPress
{
  std::uint16_t bit;
  const char* art;
};
constexpr std::array<DPadPress, 4> DPAD{{{0x0001, "d-pad-pressed-left"},
                                         {0x0002, "d-pad-pressed-right"},
                                         {0x0008, "d-pad-pressed-up"},
                                         {0x0004, "d-pad-pressed-down"}}};
constexpr std::uint16_t PAD_L = 0x0040, PAD_R = 0x0020;

// A clamped stick into -1 to 1, amplified towards a diagonal (core.translateJoyStick, translateCStick).
inline Point Translate(int stick_x, int stick_y, float range)
{
  const float x = stick_x / range, y = stick_y / range;
  const float near = 1 - std::abs(std::abs(x) - std::abs(y));
  const float angle = std::atan2(x, y), mag = std::sqrt(x * x + y * y);
  return {x + std::sin(angle) * mag * near * 0.25f, y + std::cos(angle) * mag * near * 0.25f};
}

// A clamped trigger into 0 to 1; a full press is 1 (core.translateTriggers, and the skin's L and R).
inline float Trigger(int value, bool pressed)
{
  return pressed ? 1.0f : std::min(1.0f, value / TRIGGER_RANGE);
}

// The stick's cell leaning away the further it is pushed: top left, top right, bottom right, bottom left.
inline std::array<Point, 4> Quad(const StickPlace& place, Point stick)
{
  const float angle = std::atan2(stick.x, stick.y), mag = std::sqrt(stick.x * stick.x + stick.y * stick.y);
  const float far = mag * 12, near = mag * 20;
  const std::array<Point, 4> corners{{{far, near}, {CELL - far, near}, {CELL, CELL}, {0, CELL}}};
  const float cx = place.centre_x + place.travel * stick.x, cy = place.centre_y + place.travel * (1 - stick.y);
  const float c = std::cos(angle), s = std::sin(angle);
  std::array<Point, 4> quad;
  for (std::size_t i = 0; i < 4; ++i)
  {
    const float dx = corners[i].x - 64, dy = corners[i].y - 64;
    quad[i] = {cx + dx * c - dy * s, cy + dx * s + dy * c};
  }
  return quad;
}

// The one projective map of the unit square onto a quad, as m-overlay's perspective shader draws, and back.
class Projective
{
public:
  explicit Projective(const std::array<Point, 4>& q)
  {
    const float sx = q[0].x - q[1].x + q[2].x - q[3].x, sy = q[0].y - q[1].y + q[2].y - q[3].y;
    const float dx1 = q[1].x - q[2].x, dx2 = q[3].x - q[2].x, dy1 = q[1].y - q[2].y, dy2 = q[3].y - q[2].y;
    const float den = dx1 * dy2 - dx2 * dy1;
    g_ = den == 0 ? 0 : (sx * dy2 - dx2 * sy) / den;
    h_ = den == 0 ? 0 : (dx1 * sy - sx * dy1) / den;
    a_ = q[1].x - q[0].x + g_ * q[1].x;
    b_ = q[3].x - q[0].x + h_ * q[3].x;
    c_ = q[0].x;
    d_ = q[1].y - q[0].y + g_ * q[1].y;
    e_ = q[3].y - q[0].y + h_ * q[3].y;
    f_ = q[0].y;
  }
  Point At(float u, float v) const
  {
    const float w = g_ * u + h_ * v + 1;
    return {(a_ * u + b_ * v + c_) / w, (d_ * u + e_ * v + f_) / w};
  }
  // The picture's place of a point (outside 0 to 1: not on the quad).
  Point Back(float x, float y) const
  {
    const float u = (e_ - f_ * h_) * x + (c_ * h_ - b_) * y + (b_ * f_ - c_ * e_);
    const float v = (f_ * g_ - d_) * x + (a_ - c_ * g_) * y + (c_ * d_ - a_ * f_);
    const float w = (d_ * h_ - e_ * g_) * x + (b_ * g_ - a_ * h_) * y + (a_ * e_ - b_ * d_);
    return {u / w, v / w};
  }

private:
  float a_, b_, c_, d_, e_, f_, g_, h_;
};
}  // namespace fzgx::inputs
