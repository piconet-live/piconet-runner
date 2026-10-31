// The modern HUD (Options::modern_hud or a mode's `hud modern`; the core's modern_hud_frame.cpp says what it shows and
// when): our own energy meter, place, lap counter, race time and lap times over the picture, in the launcher's look
// (overlay.h, the launcher's Direction D: Saira, white on black, the accent and gold at full strength, soft corners, translucent grounds, 120 ms
// ease-out). Drawn with the other overlays before every present on the emulator's on-screen drawing, so it stands over
// whichever picture the presenter shows, Aurora's too. Every length is in the layout's units (fzgx/hud_layout.h), every
// box from its Place, so any element can be moved by a layout and nothing here knows pixels. Nothing here reads or writes the game.
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

constexpr float GROUND = 0.62f;           // the plates' opacity over the picture
constexpr float LOSS_MOTION = 0.42f;      // a hit's mark catches up with the meter this slowly
constexpr float WARN_AT = 0.25f, DANGER_AT = 0.15f;  // of the most energy: the meter warns, then alarms
constexpr float SWEEP = 0.55f;            // seconds a sweep along the meter takes (boosting, refilling)
constexpr float READY_PULSE = 0.55f;      // seconds the flash lasts when a boost becomes available
constexpr float STEADY = 0.15f;           // the state must stand this long to count: a rollback's blink never flashes

// An element's size, in the layout's units before its scale: each near its counterpart's in the game (measured on a
// Classic capture; notes modern-hud/plan.md).
constexpr float ENERGY_W = 276, ENERGY_H = 98;
constexpr float TIME_W = 290, TIME_H = 78;
constexpr float LAP_W = 124, LAP_H = 98;
constexpr float POSITION_W = 183, POSITION_H = 150;
constexpr float SPEED_W = 262, SPEED_H = 62;
constexpr float LAP_ROW = 56;
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
  bool ready = false, seen_ready = false;
  Clock::time_point ready_changed{}, ready_flash{};
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

std::uint32_t Mix(std::uint32_t from, std::uint32_t to, float k)
{
  k = std::clamp(k, 0.0f, 1.0f);
  const auto c = [&](int s) {
    const float x = static_cast<float>(from >> s & 0xFF), y = static_cast<float>(to >> s & 0xFF);
    return static_cast<std::uint32_t>(x + (y - x) * k) << s;
  };
  return c(16) | c(8) | c(0);
}

// The fill's colour by how much is left: the accent, through gold to the warning at a quarter, to danger at 15 per cent.
std::uint32_t LevelColour(float level)
{
  if (level >= 0.5f)
    return ov::ACCENT;
  if (level >= WARN_AT)
    return Mix(ov::WARN, ov::ACCENT, (level - WARN_AT) / (0.5f - WARN_AT));
  return Mix(ov::DANGER, ov::WARN, (level - DANGER_AT) / (WARN_AT - DANGER_AT));
}

struct Meter
{
  float level = 0, loss = 0, boost = 0, seconds = 0, flash = 0;  // flash: 1 at the moment a boost became available, to 0
};

void DrawEnergy(ImDrawList* list, const fzgx::ModernHudShow& s, const hl::Rect& picture, float a, const Meter& m)
{
  const hl::Slot& slot = s.layout[hl::SlotId::Energy];
  const float u = hl::UnitOf(slot, picture);
  const ov::Box b = BoxOf(hl::Place(slot, ENERGY_W, ENERGY_H, picture));
  const bool warn = m.level < WARN_AT, alarm = m.level < DANGER_AT;
  const float pulse = alarm ? 0.5f + 0.5f * std::sin(m.seconds * 14.0f) : warn ? 0.5f + 0.5f * std::sin(m.seconds * 6.0f) : 0;
  Plate(list, b, u, a);
  if (alarm)
    list->AddRectFilled(ImVec2(b.x0, b.y0), ImVec2(b.x1, b.y1), ov::Colour(ov::DANGER, a * 0.28f * pulse), 8 * u);
  const float pad = 10 * u;
  const ov::Box track{b.x0 + pad, b.y0 + 9 * u, b.x1 - pad, b.y0 + 45 * u};
  const float w = track.x1 - track.x0, round = 5 * u;
  list->AddRectFilled(ImVec2(track.x0, track.y0), ImVec2(track.x1, track.y1), ov::Colour(ov::RAISED, a), round);
  const auto fill = [&](float from, float to, ImU32 colour) {
    if (to - from >= 0.002f)
      list->AddRectFilled(ImVec2(track.x0 + w * from, track.y0), ImVec2(track.x0 + w * to, track.y1), colour, round);
  };
  // what a hit took: white first, then danger, easing down to the meter
  if (m.loss > m.level + 0.002f)
    fill(0, m.loss, ov::Colour(Mix(ov::DANGER, ov::TEXT, (m.loss - m.level) * 6.0f), a));
  // locked (no boost to be had) the fill stands back; ready it is at full strength
  const std::uint32_t colour = LevelColour(m.level);
  fill(0, m.level, ov::Colour(s.can_boost || m.boost > 0.5f ? colour : Mix(ov::SURFACE, colour, 0.55f), a * (warn ? 0.75f + 0.25f * pulse : 1.0f)));
  const auto sweep = [&](std::uint32_t rgb, float strength) {
    const float at = std::fmod(m.seconds, SWEEP) / SWEEP * (m.level + 0.14f), reach = 0.14f;
    fill(std::max(0.0f, at - reach), std::min(at, m.level), ov::Colour(rgb, a * strength));
  };
  if (m.boost > 0.004f && m.level > 0.01f)
  {
    fill(0, m.level, ov::Colour(ov::TEXT, a * m.boost * 0.8f));
    sweep(colour, m.boost);
  }
  else if (s.pit && m.level < 1.0f)
    sweep(ov::TEXT, 0.85f);
  // the quarters, cut into the bar
  for (int q = 1; q < 4; ++q)
  {
    const float x = std::round(track.x0 + w * static_cast<float>(q) / 4);
    list->AddLine(ImVec2(x, track.y0), ImVec2(x, track.y1), ov::Colour(ov::GROUND, a * 0.7f), std::max(1.0f, 2 * u));
  }
  if (m.flash > 0.004f)
  {
    // a boost has become available: the bar lights and a ring leaves it
    const float grow = (1 - m.flash) * 9 * u;
    fill(0, std::max(m.level, 0.02f), ov::Colour(ov::TEXT, a * m.flash * 0.7f));
    list->AddRect(ImVec2(track.x0 - grow, track.y0 - grow), ImVec2(track.x1 + grow, track.y1 + grow), ov::Colour(ov::ACCENT, a * m.flash), round + grow, 0, 2.5f * u);
  }
  // under the bar: BOOST, lit when a press would boost, and the energy
  const float line = b.y0 + 71 * u;
  const ov::Type word = ov::Display(30 * u);
  const char* const boost_word = "BOOST";
  const float word_w = ov::Width(word, boost_word) + word.size * ov::LEAN;
  const ov::Box chip{track.x0, line - 18 * u, track.x0 + word_w + 16 * u, line + 18 * u};
  if (s.can_boost || m.boost > 0.5f)
  {
    list->AddRectFilled(ImVec2(chip.x0, chip.y0), ImVec2(chip.x1, chip.y1), ov::Colour(m.boost > 0.5f ? ov::TEXT : ov::ACCENT, a), 4 * u);
    ov::Draw(list, word, chip.x0 + 8 * u, line - 0.64f * word.size, ov::Colour(ov::ON_ACCENT, a), boost_word);
  }
  else
  {
    list->AddRect(ImVec2(chip.x0, chip.y0), ImVec2(chip.x1, chip.y1), ov::Colour(ov::OFF_TONE, a), 4 * u, 0, std::max(1.0f, 1.5f * u));
    ov::Draw(list, word, chip.x0 + 8 * u, line - 0.64f * word.size, ov::Colour(ov::OFF_TONE, a), boost_word);
    list->AddLine(ImVec2(chip.x0 + 4 * u, line + 3 * u), ImVec2(chip.x1 - 4 * u, line - 3 * u), ov::Colour(ov::OFF_TONE, a), std::max(1.0f, 2 * u));
  }
  char text[16];
  std::snprintf(text, sizeof(text), "%d", static_cast<int>(std::ceil(std::max(s.energy, 0.0f))));
  const ov::Type figure = ov::Figure(58 * u);
  Right(list, figure, track.x1, line - 0.64f * figure.size, ov::Colour(warn ? colour : ov::TEXT, a), text);
}

void DrawTime(ImDrawList* list, const fzgx::ModernHudShow& s, const hl::Rect& picture, float a)
{
  const hl::Slot& slot = s.layout[hl::SlotId::RaceTime];
  const float u = hl::UnitOf(slot, picture);
  const ov::Box b = BoxOf(hl::Place(slot, TIME_W, TIME_H, picture));
  Plate(list, b, u, a);
  char text[24];
  TimeText(text, sizeof(text), s.race_ms);
  const ov::Type figure = ov::Figure(100 * u);
  Right(list, figure, b.x1 - 12 * u, (b.y0 + b.y1) / 2 - 0.64f * figure.size, ov::Colour(ov::TEXT, a), text);
}

void DrawLap(ImDrawList* list, const fzgx::ModernHudShow& s, const hl::Rect& picture, float a)
{
  const hl::Slot& slot = s.layout[hl::SlotId::Lap];
  const float u = hl::UnitOf(slot, picture);
  const ov::Box b = BoxOf(hl::Place(slot, LAP_W, LAP_H, picture));
  Plate(list, b, u, a);
  const ov::Type word = ov::Display(22 * u), figure = ov::Figure(95 * u), rest = ov::Figure(58 * u);
  char of[16], at[16];
  std::snprintf(at, sizeof(at), "%u", s.lap);
  of[0] = 0;
  if (s.laps != 0)
    std::snprintf(of, sizeof(of), "/%u", s.laps);
  const float mid = b.y0 + 38 * u, wide = ov::Width(rest, of), x = (b.x0 + b.x1 + ov::Width(figure, at) - wide) / 2;
  Right(list, figure, x, mid - 0.64f * figure.size, ov::Colour(ov::TEXT, a), at);
  ov::Draw(list, rest, x, mid - 0.64f * rest.size + 13 * u, ov::Colour(ov::SECONDARY, a), of);
  const float wide_word = ov::Width(word, "LAP");
  ov::Draw(list, word, (b.x0 + b.x1 - wide_word) / 2, b.y1 - 25 * u, ov::Colour(ov::SECONDARY, a), "LAP");
}

// The current lap, then the laps done, newest last: as the game's LAP TIME and its list under it.
void DrawLapTimes(ImDrawList* list, const fzgx::ModernHudShow& s, const hl::Rect& picture, float a)
{
  const std::size_t done = std::min<std::size_t>(s.lap_count, s.lap_ms_ago.size());
  const std::size_t rows = std::min(done, LAP_ROWS_MOST);
  const hl::Slot& slot = s.layout[hl::SlotId::LapTimes];
  const float u = hl::UnitOf(slot, picture);
  const ov::Box b = BoxOf(hl::Place(slot, TIME_W, LAP_ROW * static_cast<float>(rows + 1) + 8, picture));
  Plate(list, b, u, a);
  const ov::Type figure = ov::Figure(80 * u), word = ov::Display(28 * u), number = ov::Figure(42 * u);
  const auto row_mid = [&](std::size_t r) { return b.y0 + (4 + LAP_ROW * (static_cast<float>(r) + 0.5f)) * u; };
  char text[24];
  TimeText(text, sizeof(text), s.lap_ms);
  ov::Draw(list, word, b.x0 + 12 * u, row_mid(0) - 0.64f * word.size, ov::Colour(ov::SECONDARY, a), "LAP");
  Right(list, figure, b.x1 - 12 * u, row_mid(0) - 0.64f * figure.size, ov::Colour(ov::TEXT, a), text);
  // the best lap in the accent, once there are two to compare
  for (std::size_t r = 0; r < rows; ++r)
  {
    const std::size_t ago = rows - 1 - r;
    const fzgx::u32 ms = s.lap_ms_ago[ago];
    char n[16];
    std::snprintf(n, sizeof(n), "%zu", s.lap_count - ago);
    TimeText(text, sizeof(text), ms);
    const float mid = row_mid(r + 1);
    ov::Draw(list, number, b.x0 + 12 * u, mid - 0.64f * number.size, ov::Colour(ov::FAINT, a), n);
    Right(list, figure, b.x1 - 12 * u, mid - 0.64f * figure.size, ov::Colour(done > 1 && ms == s.best_ms ? ov::ACCENT : ov::TEXT, a), text);
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
  const ov::Type big = ov::Display(205 * u), small = ov::Figure(42 * u);
  const float wide = ov::Width(big, place) + big.size * ov::LEAN * 0.5f;
  ov::Draw(list, big, (b.x0 + b.x1 - wide) / 2, b.y0 + 66 * u - 0.64f * big.size, ov::Colour(s.place == 1 ? ov::GOLD : ov::TEXT, a), place);
  Right(list, small, b.x1 - 12 * u, b.y1 - 44 * u, ov::Colour(ov::SECONDARY, a), of);
}

void DrawSpeed(ImDrawList* list, const fzgx::ModernHudShow& s, const hl::Rect& picture, float a)
{
  const hl::Slot& slot = s.layout[hl::SlotId::Speed];
  const float u = hl::UnitOf(slot, picture);
  const ov::Box b = BoxOf(hl::Place(slot, SPEED_W, SPEED_H, picture));
  Plate(list, b, u, a);
  char text[16];
  std::snprintf(text, sizeof(text), "%d", static_cast<int>(std::lround(s.speed)));
  ov::Type big = ov::Display(100 * u);
  big.tabular = true;
  const ov::Type unit = ov::Body(26 * u);
  const float mid = (b.y0 + b.y1) / 2, right = b.x1 - 10 * u, unit_w = ov::Width(unit, "km/h");
  ov::Draw(list, unit, right - unit_w, mid - 0.64f * unit.size + 13 * u, ov::Colour(ov::SECONDARY, a), "km/h");
  Right(list, big, right - unit_w - 14 * u, mid - 0.64f * big.size, ov::Colour(s.boosting ? ov::ACCENT : ov::TEXT, a), text);
}

// The heads, a spike behind HUD_HEADS (1: squares, 2: slanted), HUD_HEADS_DUMP a folder for their PNGs: the textures
// the game's FaceDisp copied this frame, found in the emulator's texture cache by the game's address. Picture only.
void DrawHeads(ImDrawList* list, const fzgx::ModernHudShow& s, const hl::Rect& picture, float a)
{
  static const char* const mode = std::getenv("HUD_HEADS");
  static const char* const dump = std::getenv("HUD_HEADS_DUMP");
  static fzgx::u32 frames = 0;
  static std::array<RcTcacheEntry, 6> held;  // alive until the next frame's: the drawing happens after this returns
  if (mode == nullptr || g_texture_cache == nullptr)
    return;
  ++frames;
  auto& memory = Core::System::GetInstance().GetMemory();
  const float u = (picture.y1 - picture.y0) / 1000;
  ov::SmoothSampling(list);
  for (std::size_t i = 0; i < s.heads.size(); ++i)
  {
    const auto& head = s.heads[i];
    held[i] = {};
    if (head.address == 0 || head.row >= 6)
      continue;
    const fzgx::u32 at = head.address & 0x01FFFFFF;
    if (at + 64 * 64 * 2 > memory.GetRamSizeReal())
      continue;
    const TextureInfo info(0, std::span<const fzgx::u8>(memory.GetRAM() + at, 64 * 64 * 2), {}, at, TextureFormat::RGB5A3,
                           TLUTFormat::IA8, 64, 64, false, {}, {}, std::nullopt);
    held[i] = g_texture_cache->GetTexture(g_ActiveConfig.iSafeTextureCache_ColorSamples, info);
    if (!held[i] || held[i]->texture == nullptr)
      continue;
    const AbstractTexture* texture = held[i]->texture.get();
    if (frames % 600 == 1)
      std::fprintf(stderr, "fzgx: head entry %zu: seat %u row %u rank %u address %08x copy %d, %ux%u\n", i, head.seat, head.row,
                   head.rank, head.address, held[i]->IsEfbCopy() ? 1 : 0, texture->GetWidth(), texture->GetHeight());
    if (dump != nullptr && (frames == 1800 || frames == 1815 || frames == 1830))
      texture->Save(std::string(dump) + "/head-" + std::to_string(head.row + 1) + "-" + std::to_string(frames) + ".png", 0);
    const float size = 92 * u, x = picture.x0 + 24 * u, y = picture.y0 + 90 * u + head.row * (size + 8 * u);
    const ImTextureRef ref(static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(texture)));
    const ImU32 white = ov::Colour(0xFFFFFF, a);
    if (mode[0] == '2')
    {
      const float lean = 0.22f;  // of the size: the top moves right; the picture is cropped by the slant, not sheared
      list->AddImageQuad(ref, ImVec2(x + lean * size, y), ImVec2(x + size, y), ImVec2(x + size - lean * size, y + size), ImVec2(x, y + size),
                         ImVec2(lean, 0), ImVec2(1, 0), ImVec2(1 - lean, 1), ImVec2(0, 1), white);
    }
    else
      list->AddImage(ref, ImVec2(x, y), ImVec2(x + size, y + size), ImVec2(0, 0), ImVec2(1, 1), white);
  }
  ov::NearestSampling(list);
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
  s_hud.boost.Send(s.boosting ? 1.0f : 0.0f, began);
  // the flash: from the values shown, once the new state has stood a moment, so a step run again never starts it
  if (s.can_boost != s_hud.seen_ready)
  {
    s_hud.seen_ready = s.can_boost;
    s_hud.ready_changed = began;
  }
  if (s_hud.seen_ready != s_hud.ready && std::chrono::duration<float>(began - s_hud.ready_changed).count() >= STEADY)
  {
    s_hud.ready = s_hud.seen_ready;
    if (s_hud.ready)
      s_hud.ready_flash = began;
  }
  Meter meter;
  meter.level = s_hud.meter, meter.loss = s_hud.loss, meter.boost = s_hud.boost.At(began), meter.seconds = seconds;
  if (s_hud.ready_flash != Clock::time_point{})
    meter.flash = std::max(0.0f, 1 - std::chrono::duration<float>(began - s_hud.ready_flash).count() / READY_PULSE);
  const auto up = [&](hl::SlotId id) { return s.shown[static_cast<std::size_t>(id)]; };
  if (up(hl::SlotId::Energy))
    DrawEnergy(list, s, picture, a, meter);
  if (up(hl::SlotId::RaceTime))
    DrawTime(list, s, picture, a);
  if (up(hl::SlotId::Lap))
    DrawLap(list, s, picture, a);
  if (up(hl::SlotId::LapTimes))
    DrawLapTimes(list, s, picture, a);
  if (up(hl::SlotId::Position))
    DrawPosition(list, s, picture, a);
  if (up(hl::SlotId::Speed))
    DrawSpeed(list, s, picture, a);
  DrawHeads(list, s, picture, a);
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
