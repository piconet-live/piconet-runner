// The modern HUD (Options::modern_hud or a mode's `hud modern`; the core's modern_hud_frame.cpp says what it shows and
// when): our own energy meter, place, lap, times, speed, top 6 and a mode's clock over the picture, the owner's mockup
// (modern-hud/mock/hud.html) drawn for real: large leaning Saira figures with a hard black outline directly on the picture,
// no plates. Lengths below are the mockup's, a picture 1540 by 887; each element's box is placed by its layout slot
// (fzgx/hud_layout.h), so everything scales with the game's picture and any element can be moved by a layout. Drawn with
// the other overlays before every present, so it stands over whichever picture the presenter shows.
//
// The meter's effects are drawing only: nothing here reads or writes the game. They run on this thread from the values
// the core presents, so a step run again by a rollback starts nothing of itself; a rollback's correction shows as the
// game's own HUD would show it. Only HeadPicture knows the video backend.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "adapter_internal.h"

#include <algorithm>
#include <array>
#include <optional>
#include <span>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>

#include <imgui.h>

#include "Core/HW/Memmap.h"
#include "Core/System.h"
#include "VideoCommon/AbstractTexture.h"
#include "VideoCommon/TextureCacheBase.h"
#include "VideoCommon/TextureConfig.h"
#include "VideoCommon/TextureDecoder.h"
#include "VideoCommon/TextureInfo.h"
#include "VideoCommon/VideoConfig.h"

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

constexpr float MOCK_UNIT = 1000.0f / 887.0f;  // layout units to the mockup's pixel
constexpr float TEXT_SHEAR = 0.1763f;          // the mockup's skewX(-10)
constexpr float LOW_AT = 0.25f, VERY_LOW_AT = 0.15f;  // the game's low warnings, of the most energy
constexpr float RUN = 0.55f;                   // seconds the rainbow takes along the bar, when the boost opens
constexpr float STEADY = 0.15f;                // the state must stand this long to count: a rollback's blink never runs it
constexpr float HIT_DROP = 0.006f;             // a fall in one picture of this much of the meter is a hit; a boost's drain is far less
constexpr float SHAKE = 0.2f, SHAKE_APART = 0.25f, CHUNK_LINGERS = 0.25f;
constexpr std::uint32_t LOCKED = 0xE60000;     // the game's meter on lap 1

// The game's rainbow, fixed along the meter, sampled from a full meter in Classic frames.
struct Stop
{
  float at;
  std::uint32_t rgb;
};
constexpr std::array<Stop, 12> STOPS = {{{0, 0xE71010}, {.13f, 0xE71010}, {.24f, 0xE77A10}, {.35f, 0xE7D110}, {.46f, 0xB8E710}, {.57f, 0x10E747},
                                         {.67f, 0x10E0E7}, {.78f, 0x233FE9}, {.85f, 0x3E10E7}, {.90f, 0x7C10E7}, {.95f, 0xD010E7}, {1, 0xE710E7}}};

struct Hud
{
  std::mutex lock;
  fzgx::ModernHudShow shown;
  ov::Eased fade;
  // the drawing thread's
  bool fresh = true;
  float meter = 0, trail = 0, raw = 0;
  bool chunk = false;
  Clock::time_point start = Clock::now(), last{}, hit{}, shake{};
  bool open = false, seen_open = false;
  Clock::time_point open_changed{}, opened{};
  std::array<ov::Eased, 6> rows;
  std::array<fzgx::u32, 6> row_seat{};
  std::uint64_t draws = 0;
  std::chrono::nanoseconds total{}, most{};
};
Hud s_hud;

// An element's box in the mockup's pixels laid by its slot: from the mockup's coordinates to the picture's pixels.
struct Mock
{
  float k = 1, ox = 0, oy = 0, bx = 0, by = 0;
  float X(float x) const { return bx + (x - ox) * k; }
  float Y(float y) const { return by + (y - oy) * k; }
  ImVec2 P(float x, float y) const { return ImVec2(X(x), Y(y)); }
};

Mock MockOf(const fzgx::ModernHudShow& s, hl::SlotId id, const hl::Rect& picture, float x0, float y0, float x1, float y1)
{
  const hl::Rect r = hl::Place(s.layout[id], (x1 - x0) * MOCK_UNIT, (y1 - y0) * MOCK_UNIT, picture);
  return {(r.x1 - r.x0) / (x1 - x0), x0, y0, r.x0, r.y0};
}

ov::Type Heavy(float size) { return {ov::TheFonts().display, size, false, true}; }  // the mockup's weight 800
ov::Type Bold(float size) { return {ov::TheFonts().semi, size, false, true}; }      // its weight 700

// The mockup's t(): a run at a baseline, ending at x (`end`) or beginning there; the outline max(3, 7% of the size) wide as an SVG stroke
void Text(ImDrawList* list, const Mock& m, const ov::Type& type, float x, float y, std::uint32_t rgb, float a, bool end, const char* text)
{
  const ov::Type at{type.font, type.size * m.k, false, type.tabular};
  const float left = m.X(x) - (end ? ov::Width(at, text) : 0);
  ov::DrawOutlined(list, at, left, m.Y(y), ov::Colour(rgb, a), ov::Colour(ov::GROUND, a), std::max(3.0f, type.size * 0.07f) / 2 * m.k, TEXT_SHEAR, text);
}

// 1'23"456, as the game writes a time
void TimeText(char* to, std::size_t size, fzgx::u32 ms)
{
  std::snprintf(to, size, "%u'%02u\"%03u", ms / 60000, ms / 1000 % 60, ms % 1000);
}

std::uint32_t Mix(std::uint32_t from, std::uint32_t to, float k)
{
  const auto c = [&](int s) {
    const float x = static_cast<float>(from >> s & 0xFF), y = static_cast<float>(to >> s & 0xFF);
    return static_cast<std::uint32_t>(x + (y - x) * k + 0.5f) << s;
  };
  return c(16) | c(8) | c(0);
}

std::uint32_t Rainbow(float f)
{
  for (std::size_t i = 1; i < STOPS.size(); ++i)
    if (f <= STOPS[i].at)
      return Mix(STOPS[i - 1].rgb, STOPS[i].rgb, (f - STOPS[i - 1].at) / (STOPS[i].at - STOPS[i - 1].at));
  return STOPS.back().rgb;
}

// What a meter draws from.
struct MeterState
{
  float level = 0, trail = 0, shake = 0, seconds = 0;
  float run = 0;      // 1 as the boost opens, to 0: the rainbow's front has come 1 - run along the bar
  bool open = false;  // the rainbow, else solid red
  bool refill = false;
  float alpha = 1;
  bool Low() const { return level <= LOW_AT && !refill; }
  float Pulse() const { return 0.5f + 0.5f * std::sin(seconds * (level <= VERY_LOW_AT ? 4.0f : 2.0f) * 6.2831853f); }
  std::uint32_t At(float f) const { return !open || (run > 0 && f > 1 - run) ? LOCKED : Rainbow(f); }
};

// A quad of one mesh with the colour running from its left edge to its right: no seam between neighbours, as anti-aliased pieces would leave.
void Band(ImDrawList* list, ImVec2 a0, ImVec2 a1, ImVec2 b1, ImVec2 b0, ImU32 ca, ImU32 cb)
{
  list->PrimReserve(6, 4);
  const ImDrawIdx base = static_cast<ImDrawIdx>(list->_VtxCurrentIdx);
  const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
  list->PrimWriteVtx(a0, uv, ca);
  list->PrimWriteVtx(a1, uv, ca);
  list->PrimWriteVtx(b1, uv, cb);
  list->PrimWriteVtx(b0, uv, cb);
  list->PrimWriteIdx(base);
  list->PrimWriteIdx(static_cast<ImDrawIdx>(base + 1));
  list->PrimWriteIdx(static_cast<ImDrawIdx>(base + 2));
  list->PrimWriteIdx(base);
  list->PrimWriteIdx(static_cast<ImDrawIdx>(base + 2));
  list->PrimWriteIdx(static_cast<ImDrawIdx>(base + 3));
}

constexpr float METER_X0 = 1190, METER_X1 = 1470, METER_Y0 = 40, METER_Y1 = 92;

// The mockup's blade(): a wedge that widens to the right, filling from the thin end.
void DrawBlade(ImDrawList* list, const Mock& m, const MeterState& d)
{
  constexpr float X0 = 1190, X1 = 1450, YB = 92, SLANT = 0.42f;
  const auto height = [](float f) { return 8 + 40 * f; };
  const auto foot = [&](float f) { return m.P(X0 + (X1 - X0) * f + d.shake, YB); };
  const auto head = [&](float f) { return m.P(X0 + (X1 - X0) * f + height(f) * SLANT + d.shake, YB - height(f)); };
  const auto quad = [&](float a, float b, ImU32 c) { list->AddQuadFilled(foot(a), foot(b), head(b), head(a), c); };
  quad(0, 1, ov::Colour(ov::GROUND, d.alpha * 0.62f));
  if (d.trail > d.level)
    quad(d.level, d.trail, ov::Colour(ov::TEXT, d.alpha));
  // the fill, a band between each two places its colour bends or breaks
  std::array<float, STOPS.size() + 3> cuts{};
  std::size_t n = 0;
  cuts[n++] = 0;
  for (const Stop& stop : STOPS)
    if (d.open && stop.at > 0 && stop.at < d.level)
      cuts[n++] = stop.at;
  if (d.open && d.run > 0 && 1 - d.run < d.level)
    cuts[n++] = 1 - d.run;
  cuts[n++] = d.level;
  std::sort(cuts.begin(), cuts.begin() + n);
  for (std::size_t i = 0; i + 1 < n; ++i)
  {
    const float a = cuts[i], b = cuts[i + 1];
    if (b - a > 0.0005f)
      Band(list, foot(a), head(a), head(b), foot(b), ov::Colour(d.At(a + 0.0002f), d.alpha), ov::Colour(d.At(b - 0.0002f), d.alpha));
  }
  // the fill's end, softened by a pixel
  if (d.level > 0.001f && d.level < 1)
  {
    const float soft = 1.5f / ((X1 - X0) * m.k);
    const std::uint32_t end = d.At(d.level - 0.0002f);
    Band(list, foot(d.level), head(d.level), head(d.level + soft), foot(d.level + soft), ov::Colour(end, d.alpha), ov::Colour(end, 0));
  }
  if (d.refill && d.level > 0)
  {
    const float at = std::fmod(d.seconds / 0.5f, 1.0f) * d.level;
    quad(at, std::min(d.level, at + 0.05f), ov::Colour(ov::TEXT, d.alpha));
  }
  const ImU32 line = d.Low() ? ov::Colour(ov::DANGER, d.alpha * (0.5f + 0.5f * d.Pulse())) : ov::Colour(ov::TEXT, d.alpha * 0.55f);
  list->AddQuad(foot(0), foot(1), head(1), head(0), line, 2 * m.k);
}

// The mockup's rev(): slanted cells rising like a rev counter.
void DrawRev(ImDrawList* list, const Mock& m, const MeterState& d)
{
  constexpr int CELLS = 24;
  constexpr float X0 = 1192, YB = 92, W = 8, GAP = 2.8f;
  const float lit = d.level * CELLS, trail = d.trail * CELLS;
  for (int i = 0; i < CELLS; ++i)
  {
    const float f = static_cast<float>(i) / (CELLS - 1), h = 10 + 40 * f * f * 0.6f + 40 * f * 0.4f, slant = h * 0.42f;
    const float x = X0 + static_cast<float>(i) * (W + GAP) + d.shake;
    const ImVec2 a = m.P(x, YB), b = m.P(x + W, YB), c = m.P(x + W + slant, YB - h), e = m.P(x + slant, YB - h);
    const bool on = static_cast<float>(i) < lit, chunk = static_cast<float>(i) < trail;
    std::uint32_t colour = ov::GROUND;
    float opacity = 0.55f;
    if (on)
      colour = d.At(f), opacity = 1;
    else if (chunk)
      colour = ov::TEXT, opacity = 1;
    if (d.refill && on)
    {
      const float place = std::fmod(static_cast<float>(i) - d.seconds * 25, 12.0f);
      if ((place < 0 ? place + 12 : place) < 2)
        colour = ov::TEXT;
    }
    list->AddQuadFilled(a, b, c, e, ov::Colour(colour, d.alpha * opacity));
    if (!on && !chunk)
      list->AddQuad(a, b, c, e, d.Low() ? ov::Colour(ov::DANGER, d.alpha * (0.3f + 0.7f * d.Pulse())) : ov::Colour(ov::TEXT, d.alpha * 0.28f), m.k);
  }
}

// The meters a player chooses from, in hud_layout::Meter's order: a new one is a function and a line.
using MeterDrawer = void (*)(ImDrawList*, const Mock&, const MeterState&);
constexpr std::array<MeterDrawer, hl::METERS> METER_DRAWERS = {DrawBlade, DrawRev};

void DrawEnergy(ImDrawList* list, const fzgx::ModernHudShow& s, const hl::Rect& picture, const MeterState& d)
{
  const Mock m = MockOf(s, hl::SlotId::Energy, picture, METER_X0, METER_Y0, METER_X1, METER_Y1);
  METER_DRAWERS[static_cast<std::size_t>(s.meter)](list, m, d);
}

void DrawTime(ImDrawList* list, const fzgx::ModernHudShow& s, const hl::Rect& picture, float a)
{
  const Mock m = MockOf(s, hl::SlotId::RaceTime, picture, 1100, 110, 1446, 172);
  char text[24];
  TimeText(text, sizeof(text), s.race_ms);
  Text(list, m, Heavy(72), 1446, 166, ov::TEXT, a, true, text);
}

void DrawLap(ImDrawList* list, const fzgx::ModernHudShow& s, const hl::Rect& picture, float a)
{
  const Mock m = MockOf(s, hl::SlotId::Lap, picture, 1060, 30, 1170, 88);
  char at[16], of[16] = {};
  std::snprintf(at, sizeof(at), "%u", s.lap);
  if (s.laps != 0)
    std::snprintf(of, sizeof(of), "/%u", s.laps);
  Text(list, m, Bold(38), 1170, 88, ov::TEXT, a, true, of);
  const float wide = ov::Width(Bold(38 * m.k), of) / m.k;
  Text(list, m, Heavy(76), 1170 - wide - 2, 88, ov::TEXT, a, true, at);
}

// The current lap, and under it the best lap in gold with its number.
void DrawLapTimes(ImDrawList* list, const fzgx::ModernHudShow& s, const hl::Rect& picture, float a)
{
  const Mock m = MockOf(s, hl::SlotId::LapTimes, picture, 1100, 176, 1446, 264);
  char text[24];
  TimeText(text, sizeof(text), s.lap_ms);
  Text(list, m, Bold(46), 1446, 216, ov::TEXT, a, true, text);
  if (s.lap_count == 0)
    return;
  TimeText(text, sizeof(text), s.best_ms);
  Text(list, m, Bold(38), 1446, 258, ov::GOLD, a, true, text);
  for (std::size_t ago = 0; ago < std::min<std::size_t>(s.lap_count, s.lap_ms_ago.size()); ++ago)
    if (s.lap_ms_ago[ago] == s.best_ms)
    {
      std::snprintf(text, sizeof(text), "%zu", s.lap_count - ago);
      Text(list, m, Bold(32), 1446 - 228, 258, ov::TEXT, a, true, text);
      break;
    }
}

void DrawPosition(ImDrawList* list, const fzgx::ModernHudShow& s, const hl::Rect& picture, float a)
{
  const Mock m = MockOf(s, hl::SlotId::Position, picture, 640, 58, 900, 178);
  char place[16], of[16];
  std::snprintf(place, sizeof(place), "%u", s.place);
  std::snprintf(of, sizeof(of), "/%u", s.racers);
  Text(list, m, Heavy(150), 790, 178, ov::TEXT, a, true, place);
  Text(list, m, Bold(44), 800, 178, ov::TEXT, a * 0.9f, false, of);
}

void DrawClock(ImDrawList* list, const fzgx::ModernHudShow& s, const hl::Rect& picture, float a)
{
  const Mock m = MockOf(s, hl::SlotId::Clock, picture, 640, 195, 900, 250);
  char text[16];
  std::snprintf(text, sizeof(text), "%u:%02u", s.clock_seconds / 60, s.clock_seconds % 60);
  const float wide = ov::Width(Heavy(74 * m.k), text) / m.k;
  Text(list, m, Heavy(74), 770 + wide / 2, 250, s.clock_seconds < 10 ? ov::DANGER : ov::TEXT, a, true, text);
}

void DrawSpeed(ImDrawList* list, const fzgx::ModernHudShow& s, const hl::Rect& picture, float a)
{
  const Mock m = MockOf(s, hl::SlotId::Speed, picture, 1200, 775, 1460, 850);
  char text[16];
  std::snprintf(text, sizeof(text), "%d", static_cast<int>(std::lround(s.speed)));
  Text(list, m, Heavy(88), 1390, 850, ov::TEXT, a, true, text);
  Text(list, m, Bold(32), 1398, 850, ov::TEXT, a, false, "km/h");
}

// A racer's head as the game's FaceDisp copied it this frame. The one function that knows where the picture is: here the
// emulator's texture cache, by the copy's address; Aurora's would be g_gxState.copyTextures[address] with add_texture.
// The reference is kept to the next frame's, since the drawing happens after this returns. None until the first copy.
ImTextureRef HeadPicture(std::size_t entry, fzgx::u32 address, bool* has)
{
  static std::array<RcTcacheEntry, 6> held;
  *has = false;
  held[entry] = {};
  if (g_texture_cache == nullptr || address == 0)
    return {};
  auto& memory = Core::System::GetInstance().GetMemory();
  const fzgx::u32 at = address & 0x01FFFFFF;
  if (at + 64 * 64 * 2 > memory.GetRamSizeReal())
    return {};
  const TextureInfo info(0, std::span<const fzgx::u8>(memory.GetRAM() + at, 64 * 64 * 2), {}, at, TextureFormat::RGB5A3,
                         TLUTFormat::IA8, 64, 64, false, {}, {}, std::nullopt);
  held[entry] = g_texture_cache->GetTexture(g_ActiveConfig.iSafeTextureCache_ColorSamples, info);
  if (!held[entry] || held[entry]->texture == nullptr || !held[entry]->IsEfbCopy())
    return {};
  *has = true;
  return ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(held[entry]->texture.get())));
}

// The mockup's Portraits rows: a black slanted box, the head cut by the slant, the racer's colour on its left edge, the place
// overlapping, the name to the right. No gap, no energy.
void DrawTop6(ImDrawList* list, const fzgx::ModernHudShow& s, const hl::Rect& picture, float a, Clock::time_point now)
{
  constexpr float X = 56, Y0 = 48, APART = 90, H = 74, W = 80, SL = 14, EDGE = 8, SQUARE = W + SL;
  const Mock m = MockOf(s, hl::SlotId::Top6, picture, 20, Y0, 340, Y0 + 5 * APART + H);
  for (std::size_t i = 0; i < s.heads.size(); ++i)
  {
    const auto& head = s.heads[i];
    bool has = false;
    const ImTextureRef picture_of = HeadPicture(i, head.address, &has);
    if (head.address == 0 || head.row >= 6)
      continue;
    // a racer changing rows slides; a new racer on this entry just stands there
    ov::Eased& row = s_hud.rows[i];
    const float want = static_cast<float>(head.row);
    if (s_hud.row_seat[i] != head.seat)
    {
      s_hud.row_seat[i] = head.seat;
      row.from = row.to = want;
    }
    else
      row.Send(want, now);
    const float y = Y0 + row.At(now) * APART;
    const ImVec2 bl = m.P(X, y + H), br = m.P(X + W, y + H), tr = m.P(X + W + SL, y), tl = m.P(X + SL, y);
    list->AddQuadFilled(bl, br, tr, tl, ov::Colour(ov::GROUND, a * 0.72f));
    if (has)
    {
      const float top = 8 / SQUARE, bottom = (H + 8) / SQUARE;
      list->AddImageQuad(picture_of, bl, br, tr, tl, ImVec2(0, bottom), ImVec2(W / SQUARE, bottom), ImVec2(1, top), ImVec2(SL / SQUARE, top), ov::Colour(ov::TEXT, a));
    }
    if (head.colour != 0)  // (a black dot, a computer racer's: no edge)
      list->AddQuadFilled(m.P(X - EDGE, y + H), bl, tl, m.P(X + SL - EDGE, y), ov::Colour(head.colour, a));
    char place[8];
    std::snprintf(place, sizeof(place), "%u", head.row + 1);
    Text(list, m, Heavy(38), X - 14 + SL * 0.2f, y + H - 2, head.row == 0 ? ov::GOLD : ov::TEXT, a, true, place);
    Text(list, m, ov::Type{ov::TheFonts().semi, 27, false, false}, X + W + SL + 8, y + 46, ov::TEXT, a, false, head.name.data());
  }
}

// The meter's effects, from the values presented.
MeterState Effects(const fzgx::ModernHudShow& s, Clock::time_point now, float a)
{
  Hud& h = s_hud;
  const auto since = [&](Clock::time_point t) { return std::chrono::duration<float>(now - t).count(); };
  const float passed = h.last == Clock::time_point{} ? 0 : since(h.last);
  h.last = now;
  const float raw = std::clamp(s.energy / std::max(s.max_energy, 1.0f), 0.0f, 1.0f);
  if (h.fresh)
  {
    h.fresh = false;
    h.meter = h.trail = h.raw = raw;
    h.chunk = false;
    h.open = h.seen_open = s.boost_open;
    h.opened = {};
  }
  if (h.raw - raw >= HIT_DROP)
  {
    // a hit: the chunk stays where the meter was and lingers; the shake once for a burst of them
    h.trail = std::max(h.chunk ? h.trail : h.meter, h.raw);
    h.chunk = true;
    h.hit = now;
    if (since(h.shake) >= SHAKE_APART)
      h.shake = now;
  }
  h.raw = raw;
  h.meter += (raw - h.meter) * std::min(1.0f, passed / 0.05f);
  if (h.chunk && since(h.hit) >= CHUNK_LINGERS)
    h.trail -= (h.trail - h.meter) * std::min(1.0f, passed * 60 * 0.03f) + 0.0002f * passed * 60;
  if (!h.chunk || h.trail <= h.meter)
    h.trail = h.meter, h.chunk = false;
  // the rainbow's run: once the new state has stood a moment, so a step run again never starts it
  if (s.boost_open != h.seen_open)
    h.seen_open = s.boost_open, h.open_changed = now;
  if (h.seen_open != h.open && since(h.open_changed) >= STEADY)
  {
    h.open = h.seen_open;
    if (h.open)
      h.opened = now;
  }
  MeterState d;
  d.level = h.meter, d.trail = h.trail, d.open = h.open, d.alpha = a;
  d.seconds = since(h.start);
  d.refill = s.pit && h.meter < 1;
  if (h.opened != Clock::time_point{})
    d.run = std::max(0.0f, 1 - since(h.opened) / RUN);
  if (const float t = since(h.shake); h.shake != Clock::time_point{} && t < SHAKE)
    d.shake = std::sin(t * 90) * 6 * (1 - t / SHAKE);
  return d;
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
      s_hud.fresh = true;
      return;
    }
    s = s_hud.shown;
  }
  const hl::Rect picture{x0, y0, x1, y1};
  ImDrawList* list = ImGui::GetBackgroundDrawList();
  const MeterState meter = Effects(s, began, a);
  const auto up = [&](hl::SlotId id) { return s.shown[static_cast<std::size_t>(id)]; };
  ov::SmoothSampling(list);  // (sheared text and scaled heads; all of it one sampler change)
  if (up(hl::SlotId::Top6))
    DrawTop6(list, s, picture, a, began);
  if (up(hl::SlotId::Energy))
    DrawEnergy(list, s, picture, meter);
  if (up(hl::SlotId::Lap))
    DrawLap(list, s, picture, a);
  if (up(hl::SlotId::RaceTime))
    DrawTime(list, s, picture, a);
  if (up(hl::SlotId::LapTimes))
    DrawLapTimes(list, s, picture, a);
  if (up(hl::SlotId::Position))
    DrawPosition(list, s, picture, a);
  if (up(hl::SlotId::Clock))
    DrawClock(list, s, picture, a);
  if (up(hl::SlotId::Speed))
    DrawSpeed(list, s, picture, a);
  ov::NearestSampling(list);
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
