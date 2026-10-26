// The game window's look, in one place: the palette, the type and the motion of everything the runner draws over
// or around the game's picture (the strip, frame_paint.cpp; the replay bar and its lanes, replay_bar.cpp,
// replay_lanes.cpp; the observer HUD, observer_hud*.cpp; the panel and the countdown figure, panel.cpp,
// panel_figure.cpp; the course map's rim, minimap_draw.cpp). It is the launcher's own look, value for value
// (direction D, white on black: net/src/Launcher.App/Theme/Theme.axaml and the site's global.css); a change there
// is made here too, by hand: the two programs share no file. port/setup/tests/test_overlay_palette.py holds every
// colour of the palette to the theme's key it copies (the launcher's checkout: port/setup/pins.tsv, REPO_LAUNCHER).
//   - Colour: tones, not strokes. The ground is black and the tones step apart and stay neutral; text is white;
//     one accent (green) is what is active or yours and may fill, with black text on it; gold is the mark only
//     (first place, your own row); amber warns, red is danger and knock-outs. Nothing is outlined; a hairline only
//     between rows inside a surface. No glow, no gradient; a shadow only under what floats.
//   - Type: two roles. Display, Saira Condensed ExtraBold leaning 12 degrees about the line's foot (the font has
//     no italic): the strip's name and title, the HUD's headings, a place figure. Reading, Saira Semi Condensed;
//     figures that change stand on one pitch (tabular: Saira's digits are not, so a number is drawn a digit at a
//     time in cells of the widest digit's width). The font files are the launcher's, beside the runner in Fonts
//     (fzgx/strip.h names them); without a file the emulator's own font is drawn.
//   - Shape: square at rest; soft corners (6 to 9 px at 720 lines) on a panel, a card, a box and the bar's handle.
//   - Motion: about 120 ms, ease-out cubic-bezier(.2, .8, .2, 1).
// Everything over the picture is drawn with the emulator's own on-screen drawing (ImGui's lists, anti-aliased),
// translucent, in the picture's pixels (ImGui's display size): the mouse is mapped into them before it is asked.
// The strip is GDI+ (frame_paint.cpp) and reads the same values. Nothing here reads or writes the game.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>

#include <imgui.h>

namespace fzgx_port::overlay
{
// The palette, 0xRRGGBB, under the theme's names.
constexpr std::uint32_t GROUND = 0x000000;     // GroundColor: the window; a panel's ground over the picture
constexpr std::uint32_t BAR = 0x0D0E0D;        // ColumnColor: the strip's ground; the replay bar's and the lanes'
constexpr std::uint32_t SURFACE = 0x171917;    // WellColor: a card, a box, a button's ground
constexpr std::uint32_t RAISED = 0x232623;     // RaisedColor: a plate on a surface; a track's ground; the line under
                                               // the strip and the window's border (LineBrush); a strip button under
                                               // the pointer (ChromeOverBrush)
constexpr std::uint32_t HOVER = 0x2F332F;      // HoverColor: one tone up under the pointer
constexpr std::uint32_t ROW_LINE = 0x2A2E2A;   // RowLineBrush: a hairline between rows inside a surface
constexpr std::uint32_t SELECT = 0x4D534D;     // SelectBrush: the replay's track where it is buffered, not yet shown
constexpr std::uint32_t TEXT = 0xFFFFFF;       // TextColor
constexpr std::uint32_t SECONDARY = 0xBCC2BC;  // DimColor: the small text, a button's drawing
constexpr std::uint32_t FAINT = 0x9BA29B;      // FaintColor: a label, a gap
constexpr std::uint32_t OFF_TONE = 0x5C615C;        // OffColor: what cannot be pressed; a racer who is out
constexpr std::uint32_t ACCENT = 0x3DFF6A;     // AccentColor: active, yours, played; it may fill
constexpr std::uint32_t ACCENT_OVER = 0x7AFF96, ACCENT_DOWN = 0x2BE356;  // under the pointer, pressed
constexpr std::uint32_t ON_ACCENT = 0x000000;  // text on the accent and on gold
constexpr std::uint32_t GOLD = 0xFFD500;       // GoldColor: the mark only (first place)
constexpr std::uint32_t WARN = 0xFF9F1A;       // WarnColor
constexpr std::uint32_t DANGER = 0xFF4D4D;     // DangerColor: out, knocked out, a scrub beyond the buffer
constexpr std::uint32_t CLOSE_OVER = 0xC42B1C, ON_CLOSE = 0xFFFFFF;  // the close button under the pointer

// The display role's lean: tan 12 degrees. A leaning line reaches past its box on the right by its height times
// this (the launcher's DisplayReach).
constexpr float LEAN = 0.21256f;
// Soft corners, at 720 lines (Scale): a panel or a card, and a button, a box or the bar's handle.
constexpr float CORNER = 8.0f, CORNER_SMALL = 6.0f;

inline ImU32 Colour(std::uint32_t rgb, float opacity)
{
  const int a = static_cast<int>(std::clamp(opacity, 0.0f, 1.0f) * 255 + 0.5f);
  return IM_COL32(rgb >> 16 & 0xFF, rgb >> 8 & 0xFF, rgb & 0xFF, a);
}

struct Box
{
  float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  bool Has(float x, float y) const { return x >= x0 && x < x1 && y >= y0 && y < y1; }
  bool Empty() const { return x1 <= x0 || y1 <= y0; }
};

// The picture's scale: 1 at 720 lines.
inline float Scale(float height)
{
  return std::clamp(height / 720.0f, 0.75f, 2.0f);
}

// cubic-bezier(.2, .8, .2, 1) at time p (0 to 1): the curve's x solved for p by bisection, its y returned.
inline float EaseOut(float p)
{
  p = std::clamp(p, 0.0f, 1.0f);
  const auto bezier = [](float t, float a, float b) {
    const float u = 1 - t;
    return 3 * u * u * t * a + 3 * u * t * t * b + t * t * t;
  };
  float lo = 0, hi = 1, t = p;
  for (int i = 0; i < 20; ++i)
  {
    t = (lo + hi) / 2;
    (bezier(t, 0.2f, 0.2f) < p ? lo : hi) = t;
  }
  return bezier(t, 0.8f, 1.0f);
}

// A value that eases to where it is sent, over MOTION.
struct Eased
{
  using Clock = std::chrono::steady_clock;
  static constexpr float MOTION = 0.120f;
  float from = 0, to = 0;
  Clock::time_point since{};
  float At(Clock::time_point now) const
  {
    const float p = std::chrono::duration<float>(now - since).count() / MOTION;
    return from + (to - from) * EaseOut(p);
  }
  void Send(float target, Clock::time_point now)
  {
    if (target == to)
      return;
    from = At(now);
    to = target;
    since = now;
  }
};

// The fonts (loaded once from the program's Fonts folder, on the thread that draws; the emulator's own where a file
// is missing). overlay.cpp.
struct Fonts
{
  ImFont* body = nullptr;     // Saira Semi Condensed Regular: everything read
  ImFont* semi = nullptr;     // Saira Condensed SemiBold: figures, a name
  ImFont* display = nullptr;  // Saira Condensed ExtraBold: the display role (leaning)
};
const Fonts& TheFonts();

// A run of text: its font (the emulator's when none), its size, whether it leans (the display role) and whether its
// figures stand on one pitch (tabular: a number that changes or lines up).
struct Type
{
  ImFont* font = nullptr;
  float size = 16;
  bool lean = false;
  bool tabular = false;
};
// The roles at a size.
inline Type Display(float size) { return {TheFonts().display, size, true, false}; }
inline Type Body(float size, bool tabular = false) { return {TheFonts().body, size, false, tabular}; }
inline Type Strong(float size, bool tabular = false) { return {TheFonts().semi, size, false, tabular}; }
inline Type Figure(float size) { return {TheFonts().semi, size, false, true}; }

// How wide `text` comes out (a leaning run's reach not counted), and the run drawn with its top left at (x, top),
// cut at `clip` when given (the picture's pixels). overlay.cpp.
float Width(const Type& type, const char* text);
void Draw(ImDrawList* list, const Type& type, float x, float top, ImU32 colour, const char* text,
          const ImVec4* clip = nullptr);
}  // namespace fzgx_port::overlay
