// The modern HUD (Options::modern_hud or a mode's `hud modern`; the core's modern_hud_frame.cpp says what it shows and
// when): our own energy meter, place, lap counter, race time and lap times over the picture, in the launcher's look
// (overlay.h: Saira, white on black, the accent and gold at full strength, soft corners, translucent grounds, 120 ms
// ease-out). Drawn with the other overlays before every present on the emulator's on-screen drawing, so it stands over
// whichever picture the presenter shows, Aurora's too. Every length is in the layout's units (fzgx/hud_layout.h), every
// box from its Place, so any element can be moved by a layout and nothing here knows pixels. Nothing here reads or writes the game.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "adapter_internal.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <mutex>

#include <imgui.h>

#include "fzgx/host.h"
#include "fzgx/hud_layout.h"
#include "overlay.h"

namespace fzgx_port
{
namespace
{
namespace ov = overlay;
namespace hl = fzgx::hud_layout;
using Clock = ov::Eased::Clock;

constexpr float GROUND = 0.62f;           // the plates' opacity over the picture
constexpr float LOSS_MOTION = 0.42f;      // a hit's mark catches up with the meter this slowly
constexpr float LOW = 0.25f;              // of the most energy: the meter warns, as the game's flashes
constexpr float BOOST_SWEEP = 0.55f;      // seconds a sweep along the meter takes while boosting

// An element's size, in the layout's units before its scale.
constexpr float ENERGY_W = 430, ENERGY_H = 46;
constexpr float TIME_W = 232, TIME_H = 54;
constexpr float LAP_W = 148, LAP_H = 54;
constexpr float POSITION_W = 210, POSITION_H = 118;
constexpr float LAP_ROW = 30;
constexpr std::size_t LAP_ROWS_MOST = 4;

struct Hud
{
  std::mutex lock;
  fzgx::ModernHudShow shown;
  ov::Eased fade;
  // the drawing thread's
  float meter = -1, loss = 0;
  Clock::time_point last{};
  ov::Eased boost;
  std::uint64_t draws = 0;
  std::chrono::nanoseconds total{}, most{};
};
Hud s_hud;

ov::Box BoxOf(const hl::Rect& r)
{
  return {std::round(r.x0), std::round(r.y0), std::round(r.x1), std::round(r.y1)};
}

void Plate(ImDrawList* list, const ov::Box& b, float u, float a)
{
  list->AddRectFilled(ImVec2(b.x0, b.y0), ImVec2(b.x1, b.y1), ov::Colour(ov::GROUND, GROUND * a), 8 * u);
}

void Right(ImDrawList* list, const ov::Type& type, float right, float top, ImU32 colour, const char* text)
{
  ov::Draw(list, type, right - ov::Width(type, text), top, colour, text);
}

// 1:23.456: minutes without a leading zero
void TimeText(char* to, std::size_t size, fzgx::u32 ms)
{
  std::snprintf(to, size, "%u:%02u.%03u", ms / 60000, ms / 1000 % 60, ms % 1000);
}

void DrawEnergy(ImDrawList* list, const fzgx::ModernHudShow& s, const hl::Rect& picture, float a, float meter, float loss, float boost, float seconds)
{
  const hl::Slot& slot = s.layout[hl::SlotId::Energy];
  const float u = hl::UnitOf(slot, picture);
  const ov::Box b = BoxOf(hl::Place(slot, ENERGY_W, ENERGY_H, picture));
  Plate(list, b, u, a);
  const float pad = 14 * u, number = 74 * u;
  const float mid = (b.y0 + b.y1) / 2, half = 7 * u;
  const ov::Box track{b.x0 + pad, mid - half, b.x1 - pad - number, mid + half};
  const float w = track.x1 - track.x0, round = half;
  list->AddRectFilled(ImVec2(track.x0, track.y0), ImVec2(track.x1, track.y1), ov::Colour(ov::RAISED, a), round);
  const auto fill = [&](float from, float to, ImU32 colour) {
    if (to - from >= 0.002f)
      list->AddRectFilled(ImVec2(track.x0 + w * from, track.y0), ImVec2(track.x0 + w * to, track.y1), colour, round);
  };
  const float low_pulse = 0.72f + 0.28f * std::sin(seconds * 6.0f);
  const bool low = meter < LOW;
  if (loss > meter)
    fill(0, loss, ov::Colour(ov::DANGER, a));
  fill(0, meter, ov::Colour(low ? ov::DANGER : ov::ACCENT, a * (low ? low_pulse : 1.0f)));
  if (boost > 0.004f && meter > 0.01f)
  {
    // boosting: the fill goes white from its tail as the boost comes in, with a sweep along it
    fill(0, meter, ov::Colour(ov::TEXT, a * boost * 0.85f));
    const float at = std::fmod(seconds, BOOST_SWEEP) / BOOST_SWEEP * meter, reach = std::min(0.12f, meter);
    fill(std::max(0.0f, at - reach), at, ov::Colour(ov::ACCENT, a * boost));
  }
  char text[16];
  std::snprintf(text, sizeof(text), "%d", static_cast<int>(std::ceil(std::max(s.energy, 0.0f))));
  const ov::Type figure = ov::Figure(32 * u);
  Right(list, figure, b.x1 - pad, mid - 0.64f * figure.size, ov::Colour(low ? ov::DANGER : ov::TEXT, a), text);
}

void DrawTime(ImDrawList* list, const fzgx::ModernHudShow& s, const hl::Rect& picture, float a)
{
  const hl::Slot& slot = s.layout[hl::SlotId::RaceTime];
  const float u = hl::UnitOf(slot, picture);
  const ov::Box b = BoxOf(hl::Place(slot, TIME_W, TIME_H, picture));
  Plate(list, b, u, a);
  char text[24];
  TimeText(text, sizeof(text), s.race_ms);
  const ov::Type figure = ov::Figure(38 * u);
  Right(list, figure, b.x1 - 14 * u, (b.y0 + b.y1) / 2 - 0.64f * figure.size, ov::Colour(ov::TEXT, a), text);
}

void DrawLap(ImDrawList* list, const fzgx::ModernHudShow& s, const hl::Rect& picture, float a)
{
  const hl::Slot& slot = s.layout[hl::SlotId::Lap];
  const float u = hl::UnitOf(slot, picture);
  const ov::Box b = BoxOf(hl::Place(slot, LAP_W, LAP_H, picture));
  Plate(list, b, u, a);
  const float mid = (b.y0 + b.y1) / 2;
  const ov::Type word = ov::Display(24 * u), figure = ov::Figure(34 * u), rest = ov::Figure(22 * u);
  ov::Draw(list, word, b.x0 + 12 * u, mid - 0.64f * word.size, ov::Colour(ov::SECONDARY, a), "LAP");
  char of[16], at[16];
  std::snprintf(at, sizeof(at), "%u", s.lap);
  of[0] = 0;
  if (s.laps != 0)
    std::snprintf(of, sizeof(of), "/%u", s.laps);
  const float right = b.x1 - 12 * u, wide = ov::Width(rest, of);
  ov::Draw(list, rest, right - wide, mid - 0.64f * rest.size + 4 * u, ov::Colour(ov::SECONDARY, a), of);
  Right(list, figure, right - wide, mid - 0.64f * figure.size, ov::Colour(ov::TEXT, a), at);
}

void DrawLapTimes(ImDrawList* list, const fzgx::ModernHudShow& s, const hl::Rect& picture, float a)
{
  const std::size_t done = std::min<std::size_t>(s.lap_count, s.lap_ms_ago.size());
  if (done == 0)
    return;
  const std::size_t rows = std::min(done, LAP_ROWS_MOST);
  const hl::Slot& slot = s.layout[hl::SlotId::LapTimes];
  const float u = hl::UnitOf(slot, picture);
  const ov::Box b = BoxOf(hl::Place(slot, TIME_W, LAP_ROW * static_cast<float>(rows) + 12, picture));
  Plate(list, b, u, a);
  const ov::Type figure = ov::Figure(23 * u), word = ov::Body(19 * u, true);
  // oldest shown first; the best lap in the accent once there are two to compare
  for (std::size_t r = 0; r < rows; ++r)
  {
    const std::size_t ago = rows - 1 - r;
    const fzgx::u32 ms = s.lap_ms_ago[ago];
    const float mid = b.y0 + (6 + LAP_ROW * (static_cast<float>(r) + 0.5f)) * u;
    char n[16], text[24];
    std::snprintf(n, sizeof(n), "%zu", s.lap_count - ago);
    TimeText(text, sizeof(text), ms);
    const bool best = done > 1 && ms == s.best_ms;
    ov::Draw(list, word, b.x0 + 14 * u, mid - 0.64f * word.size, ov::Colour(ov::FAINT, a), n);
    Right(list, figure, b.x1 - 14 * u, mid - 0.64f * figure.size, ov::Colour(best ? ov::ACCENT : ov::TEXT, a), text);
  }
}

void DrawPosition(ImDrawList* list, const fzgx::ModernHudShow& s, const hl::Rect& picture, float a)
{
  const hl::Slot& slot = s.layout[hl::SlotId::Position];
  const float u = hl::UnitOf(slot, picture);
  const ov::Box b = BoxOf(hl::Place(slot, POSITION_W, POSITION_H, picture));
  Plate(list, b, u, a);
  char place[16], of[16];
  std::snprintf(place, sizeof(place), "%u", s.place);
  std::snprintf(of, sizeof(of), "/%u", s.racers);
  const ov::Type big = ov::Display(92 * u), small = ov::Figure(30 * u);
  const float x = b.x0 + 16 * u, top = (b.y0 + b.y1) / 2 - 0.66f * big.size;
  const float wide = ov::Width(big, place) + big.size * ov::LEAN * 0.6f;
  ov::Draw(list, big, x, top, ov::Colour(s.place == 1 ? ov::GOLD : ov::TEXT, a), place);
  ov::Draw(list, small, x + wide + 6 * u, b.y1 - 16 * u - small.size * 1.1f, ov::Colour(ov::SECONDARY, a), of);
}
}  // namespace

void ShowModernHud(const fzgx::ModernHudShow& show)
{
  std::lock_guard<std::mutex> hold(s_hud.lock);
  if (show.on)
    s_hud.shown = show;
  s_hud.fade.Send(show.on ? 1.0f : 0.0f, Clock::now());
}

void DrawModernHud(float x0, float y0, float x1, float y1)
{
  const auto began = Clock::now();
  fzgx::ModernHudShow s;
  float a;
  {
    std::lock_guard<std::mutex> hold(s_hud.lock);
    a = s_hud.fade.At(began);
    if (a <= 0.004f)
    {
      s_hud.meter = -1;
      return;
    }
    s = s_hud.shown;
  }
  const hl::Rect picture{x0, y0, x1, y1};
  ImDrawList* list = ImGui::GetBackgroundDrawList();
  // the meter eases over the motion (a rollback's correction too); a loss leaves its mark to catch up slowly
  const float seconds = std::chrono::duration<float>(began.time_since_epoch()).count();
  const float passed = s_hud.last == Clock::time_point{} ? 0 : std::chrono::duration<float>(began - s_hud.last).count();
  s_hud.last = began;
  const float target = std::clamp(s.energy / std::max(s.max_energy, 1.0f), 0.0f, 1.0f);
  if (s_hud.meter < 0)
    s_hud.meter = s_hud.loss = target;
  s_hud.meter += (target - s_hud.meter) * std::min(1.0f, passed / ov::Eased::MOTION * 2.0f);
  s_hud.loss = std::max(s_hud.meter, s_hud.loss - passed / LOSS_MOTION * std::max(0.05f, s_hud.loss - s_hud.meter) * 2.0f);
  s_hud.boost.Send(s.boost ? 1.0f : 0.0f, began);
  const float boost = s_hud.boost.At(began);
  const auto up = [&](hl::SlotId id) { return s.shown[static_cast<std::size_t>(id)]; };
  if (up(hl::SlotId::Energy))
    DrawEnergy(list, s, picture, a, s_hud.meter, s_hud.loss, boost, seconds);
  if (up(hl::SlotId::RaceTime))
    DrawTime(list, s, picture, a);
  if (up(hl::SlotId::Lap))
    DrawLap(list, s, picture, a);
  if (up(hl::SlotId::LapTimes))
    DrawLapTimes(list, s, picture, a);
  if (up(hl::SlotId::Position))
    DrawPosition(list, s, picture, a);
  const auto took = Clock::now() - began;
  s_hud.total += took;
  s_hud.most = std::max<std::chrono::nanoseconds>(s_hud.most, took);
  // for the log: the cost of the drawing, every minute of pictures
  if (++s_hud.draws % 3600 == 0)
  {
    char line[120];
    std::snprintf(line, sizeof(line), "fzgx: modern HUD: drawn %llu times, mean %lld ns, most %lld ns", static_cast<unsigned long long>(s_hud.draws),
                  static_cast<long long>(s_hud.total.count() / static_cast<long long>(s_hud.draws)), static_cast<long long>(s_hud.most.count()));
    s_core_host.Log(fzgx::LogLevel::Notice, line);
  }
}
}  // namespace fzgx_port
