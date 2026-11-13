// A mode's message over the game's picture (fzgx/mode_say.h; protocol modes.toml `say`, 0.6.5): white text on a dark
// rounded panel in the launcher's look (overlay.h: the reading face, Saira Semi Condensed, with Segoe UI and Yu Gothic
// for what it lacks; pure white on true black; soft corners; a shadow under what floats). While the race stands the
// panel is in the middle of the picture, its text large and broken into lines that fit; while it runs the same message
// is a single smaller line at the picture's foot, a hint. The panel eases in (it rises a little as it comes), eases
// out, moves between the two places and crossfades from one text to the next, each over about a fifth of a second.
// Drawn before every present, on the thread that presents, from the board the game's thread fills once a drawn frame.
// Nothing here reads or writes the game.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "adapter_internal.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>
#include <vector>

#include <imgui.h>

#include "VideoCommon/Present.h"

#include "fzgx/mode_say.h"
#include "overlay.h"

namespace fzgx_port
{
namespace
{
namespace ov = overlay;
using Clock = std::chrono::steady_clock;

constexpr float IN_S = 0.20f, OUT_S = 0.16f, MOVE_S = 0.24f;  // the panel's motions
constexpr float RISE = 10.0f;                                   // how far it rises as it comes (at 720 lines)
// at 720 lines: the text's size and the panel's room, standing (the middle) and running (the foot)
constexpr float BIG = 34.0f, SMALL = 23.0f;
constexpr float PAD_X = 30.0f, PAD_Y = 20.0f, PAD_X_SMALL = 20.0f, PAD_Y_SMALL = 10.0f;
constexpr float WIDEST = 0.62f;        // the standing panel's text at most this share of the picture's width
constexpr float LEADING = 1.22f;       // a line's height of the text's size
constexpr float FOOT = 0.115f;         // the running line's panel stands this share of the picture's height above its foot
constexpr float GROUND = 0.86f;        // the panel's black, how solid
constexpr std::uint32_t MARK = ov::ACCENT;  // the short bar over a standing message: the one accent

struct Shown
{
  std::string text;
  Clock::time_point since{};  // when it came
  Clock::time_point gone{};   // when it began to go (none: it stays)
};

// The panel's place: 1 in the middle (the race stands), 0 at the foot, eased between them over MOVE_S.
struct Place
{
  float from = 0, to = 0;
  Clock::time_point since{};
};

struct State
{
  fzgx::u32 serial = ~0u;
  std::vector<Shown> texts;  // the one shown last, and those still going out
  Place middle;
  bool placed = false;
  bool told = false;
};
State s_say;

float Since(Clock::time_point at, Clock::time_point now)
{
  return std::chrono::duration<float>(now - at).count();
}

// `text` broken into lines no wider than `wide` at this size, at its spaces (a word wider than the line is a line).
std::vector<std::string> Lines(ImFont* font, float size, const std::string& text, float wide)
{
  std::vector<std::string> lines;
  const char* p = text.c_str();
  const char* end = p + text.size();
  while (p < end)
  {
    const char* cut = font->CalcWordWrapPosition(size, p, end, wide);
    if (cut <= p)
      cut = p + 1;
    std::string line(p, cut);
    while (!line.empty() && line.back() == ' ')
      line.pop_back();
    lines.push_back(line);
    p = cut;
    while (p < end && *p == ' ')
      ++p;
  }
  return lines;
}

// One message's panel, `opacity` its fade, `middle` 1 standing to 0 running, `rise` its offset as it comes.
void Panel(ImDrawList* list, const ov::Box& picture, const std::string& text, float opacity, float middle, float rise)
{
  if (opacity <= 0.003f || text.empty())
    return;
  const float h = picture.y1 - picture.y0, w = picture.x1 - picture.x0;
  const float scale = ov::Scale(h);
  ImFont* font = ov::TheFonts().body ? ov::TheFonts().body : ImGui::GetFont();
  const float size = (SMALL + (BIG - SMALL) * middle) * scale;
  const float pad_x = (PAD_X_SMALL + (PAD_X - PAD_X_SMALL) * middle) * scale;
  const float pad_y = (PAD_Y_SMALL + (PAD_Y - PAD_Y_SMALL) * middle) * scale;
  // standing: lines that fit the middle's width; running: one line, as wide as the picture allows
  const float wide = middle > 0.5f ? w * WIDEST : w * 0.9f - 2 * pad_x;
  const std::vector<std::string> lines = Lines(font, size, text, wide);
  float widest = 0;
  for (const std::string& line : lines)
    widest = std::max(widest, font->CalcTextSizeA(size, 1e9f, 0, line.c_str()).x);
  const float line_h = size * LEADING;
  const float mark_h = 4.0f * scale * middle, mark_gap = mark_h > 0 ? 10.0f * scale * middle : 0.0f;
  const float box_w = widest + 2 * pad_x, box_h = lines.size() * line_h + 2 * pad_y + mark_h + mark_gap;
  const float cx = (picture.x0 + picture.x1) / 2;
  const float at_middle = picture.y0 + (h - box_h) / 2, at_foot = picture.y1 - h * FOOT - box_h;
  const float top = std::floor(at_foot + (at_middle - at_foot) * middle + rise * scale);
  const ImVec2 a(std::floor(cx - box_w / 2), top), b(std::floor(cx + box_w / 2), top + box_h);
  const float corner = (ov::CORNER_SMALL + (ov::CORNER - ov::CORNER_SMALL) * middle) * scale;
  // the shadow under it (it floats), then its ground
  for (int i = 3; i >= 1; --i)
  {
    const float spread = i * 3.0f * scale;
    list->AddRectFilled(ImVec2(a.x - spread, a.y - spread + 2 * scale), ImVec2(b.x + spread, b.y + spread + 4 * scale),
                        ov::Colour(ov::GROUND, opacity * 0.10f), corner + spread);
  }
  list->AddRectFilled(a, b, ov::Colour(ov::GROUND, opacity * GROUND), corner);
  float y = a.y + pad_y;
  if (mark_h > 0.5f)
  {
    const float mark_w = 36.0f * scale;
    list->AddRectFilled(ImVec2(std::floor(cx - mark_w / 2), y), ImVec2(std::floor(cx + mark_w / 2), y + mark_h),
                        ov::Colour(MARK, opacity * middle), mark_h / 2);
    y += mark_h + mark_gap;
  }
  ov::SmoothSampling(list);
  for (const std::string& line : lines)
  {
    const float lw = font->CalcTextSizeA(size, 1e9f, 0, line.c_str()).x;
    list->AddText(font, size, ImVec2(std::floor(cx - lw / 2), std::floor(y + (line_h - size) / 2)),
                  ov::Colour(ov::TEXT, opacity), line.c_str());
    y += line_h;
  }
  ov::NearestSampling(list);
}
}  // namespace

void DrawModeSay()
{
  fzgx::mode_say::Board& board = fzgx::mode_say::TheBoard();
  const fzgx::u32 serial = board.serial.load(std::memory_order_acquire);
  if (serial == 0 && s_say.texts.empty())
    return;  // (no mode has said anything: one load a picture)
  if (ImGui::GetCurrentContext() == nullptr)
    return;
  const Clock::time_point now = Clock::now();
  if (serial != s_say.serial)
  {
    s_say.serial = serial;
    std::string text;
    {
      std::lock_guard<std::mutex> hold(board.lock);
      text = board.text;
    }
    for (Shown& old : s_say.texts)
      if (old.gone == Clock::time_point{})
        old.gone = now;
    if (!text.empty())
      s_say.texts.push_back({text, now, {}});
    if (!s_say.told)
    {
      s_say.told = true;
      s_core_host.Log(fzgx::LogLevel::Notice, "fzgx: mode message: the first shown (\"" + text + "\")");
    }
  }
  const float target = board.paused.load(std::memory_order_relaxed) ? 1.0f : 0.0f;
  Place& place = s_say.middle;
  const auto at = [&] { return place.from + (place.to - place.from) * ov::EaseOut(Since(place.since, now) / MOVE_S); };
  if (!s_say.placed)
  {
    s_say.placed = true;
    place = {target, target, now};
  }
  else if (target != place.to)
    place = {at(), target, now};
  const float middle = at();
  std::erase_if(s_say.texts, [&](const Shown& t) { return t.gone != Clock::time_point{} && Since(t.gone, now) > OUT_S; });
  if (s_say.texts.empty())
    return;
  const ov::Box picture = [] {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    if (g_presenter)
    {
      const auto& r = g_presenter->GetTargetRectangle();
      if (r.GetWidth() > 0 && r.GetHeight() > 0)
        return ov::Box{static_cast<float>(r.left), static_cast<float>(r.top), static_cast<float>(r.right),
                       static_cast<float>(r.bottom)};
    }
    return ov::Box{0, 0, display.x, display.y};
  }();
  ImDrawList* list = ImGui::GetForegroundDrawList();
  for (const Shown& t : s_say.texts)
  {
    const float in = ov::EaseOut(Since(t.since, now) / IN_S);
    const float out = t.gone == Clock::time_point{} ? 1.0f : 1.0f - ov::EaseOut(Since(t.gone, now) / OUT_S);
    Panel(list, picture, t.text, std::min(in, out), middle, (1.0f - in) * RISE);
  }
}
}  // namespace fzgx_port
