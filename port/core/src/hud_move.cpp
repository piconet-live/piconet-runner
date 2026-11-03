// The classic HUD's moves (hud_move.h).
// SPDX-License-Identifier: GPL-3.0-or-later

#include "hud_move.h"

#include "internal.h"
#include "util.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <format>
#include <limits>
#include <numbers>
#include <utility>
#include <vector>

#include <picojson.h>

namespace fzgx::hud_move
{
namespace
{
// The game's packet (font.h FontDrawPacket) and its queue (font.c fn_1_4F734, fn_1_4FD64)
constexpr u32 PACKET = 0x58;
constexpr u32 IMAGE = 0, X = 4, Y = 8, SCALE_X = 0x10, SCALE_Y = 0x14, U0 = 0x18, V0 = 0x1C, U1 = 0x20, V1 = 0x24,
              ROT_Z = 0x28, FLAGS = 0x30, PIXELS_W = 0x44, PIXELS_H = 0x46;
constexpr u32 SQUEEZED = 0x08000000, GLYPH = 0x400000, PIXELS = 0x800000;  // +0x30: Wide Screen's anchor; the two other paths
constexpr u32 QUEUE = game::REL_DATA_AT + 0x1C504, QUEUED = game::REL_BSS_AT + 0x646D2;
constexpr u32 WIDE = game::REL_BSS_AT + 0x6F5F0;            // fn_1_A6F1C: 1 or 2, the game's Wide Screen
constexpr float SQUEEZE = 0.75f;                               // fn_1_4EDAC's (.rodata 0x2788)
constexpr u32 IMAGES = game::REL_DATA_AT + 0xFCD4, IMAGE_GROUP = 0x28;  // fn_1_4FD64: a group's loaded flag, +0x20 its sizes
constexpr u32 FONTS = game::REL_DATA_AT + 0x1A3B8 + 0xAF0, FONT = 0x38;  // fn_1_48D80: a font's cell, +4 width, +5 height
constexpr u32 SPRITE_STEP = 0x78, LIGHTNING_STEP = game::REL_TEXT_AT + 0xCA218, WRONG_WAY_STEP = game::REL_TEXT_AT + 0xCD6C0;
constexpr u32 WRONG_WAY_IMAGE = 0x9435;
constexpr u32 WINDOW_MOVE = layout::MODE_HUD + 0x300;          // memmap.MODE_HUD_MOVE
constexpr float MOST_OFFSET = 2000;

// The centre of each part's box, measured with "log" (game units before Wide Screen's squeeze; NaN: not measured)
constexpr float N = std::numeric_limits<float>::quiet_NaN();
struct Pivot
{
  float x, y;
};
constexpr std::array<Pivot, PARTS.size()> PIVOTS = [] {
  std::array<Pivot, PARTS.size()> p{};
  for (auto& q : p)
    q = {N, N};
  return p;
}();

bool Number(const picojson::object& o, const char* key, float* to, std::string* why)
{
  const auto it = o.find(key);
  if (it == o.end())
    return true;
  if (!it->second.is<double>() || !std::isfinite(it->second.get<double>()))
  {
    *why = std::string(key) + " is not a number";
    return false;
  }
  *to = std::clamp(static_cast<float>(it->second.get<double>()), -MOST_OFFSET, MOST_OFFSET);
  return true;
}

bool ReadMove(const picojson::object& o, Move* m, std::string* why)
{
  for (const auto& [key, value] : o)
    if (key != "dx" && key != "dy" && key != "angle" && key != "flip" && key != "pivot")
    {
      *why = "no such member " + key;
      return false;
    }
  if (!Number(o, "dx", &m->dx, why) || !Number(o, "dy", &m->dy, why) || !Number(o, "angle", &m->angle, why))
    return false;
  if (const auto it = o.find("flip"); it != o.end())
  {
    const std::string f = it->second.is<std::string>() ? it->second.get<std::string>() : "?";
    if (f != "" && f != "x" && f != "y" && f != "xy" && f != "yx")
    {
      *why = "flip is \"x\", \"y\" or \"xy\"";
      return false;
    }
    m->flip_x = f.find('x') != std::string::npos;
    m->flip_y = f.find('y') != std::string::npos;
  }
  if (const auto it = o.find("pivot"); it != o.end())
  {
    const auto* a = it->second.is<picojson::array>() ? &it->second.get<picojson::array>() : nullptr;
    if (!a || a->size() != 2 || !(*a)[0].is<double>() || !(*a)[1].is<double>())
    {
      *why = "pivot is [x, y]";
      return false;
    }
    m->has_pivot = true;
    m->px = static_cast<float>((*a)[0].get<double>());
    m->py = static_cast<float>((*a)[1].get<double>());
  }
  m->given = true;
  return true;
}

float F(const Guest& g, u32 a)
{
  return std::bit_cast<float>(g.Read32(a));
}

struct Box
{
  float x0 = std::numeric_limits<float>::max(), y0 = std::numeric_limits<float>::max();
  float x1 = std::numeric_limits<float>::lowest(), y1 = std::numeric_limits<float>::lowest();
  bool Empty() const { return x0 > x1; }
  void Add(float x, float y)
  {
    x0 = std::min(x0, x);
    y0 = std::min(y0, y);
    x1 = std::max(x1, x);
    y1 = std::max(y1, y);
  }
  void Add(const Box& b)
  {
    if (!b.Empty())
    {
      Add(b.x0, b.y0);
      Add(b.x1, b.y1);
    }
  }
};

// The quad's size as the game takes it (fn_1_4FD64): the image's own width and height from its group's table, a
// glyph's cell, or the pixels' size the packet carries. Zero when the group is not loaded.
std::pair<float, float> Size(const Guest& g, u32 packet, u32 flags)
{
  const u32 id = g.Read32(packet + IMAGE);
  if (flags & PIXELS)
    return {static_cast<float>(g.Read16(packet + PIXELS_W)), static_cast<float>(g.Read16(packet + PIXELS_H))};
  if (flags & GLYPH)
  {
    const u32 font = FONTS + ((id >> 16) & 0xFF) * FONT;
    return {static_cast<float>(g.Read8(font + 4)), static_cast<float>(g.Read8(font + 5))};
  }
  const u32 group = IMAGES + ((id >> 8) & 0xFFFF) * IMAGE_GROUP;
  if (!g.Ram(group, IMAGE_GROUP) || g.Read32(group) == 0)
    return {0, 0};
  const u32 sizes = g.Read32(group + 0x20);
  if (!g.Ram(sizes + 4, 4))
    return {0, 0};
  const u32 info = g.Read32(sizes + 4) + (id & 0xFF) * 16;
  if (!g.Ram(info + 8, 4))
    return {0, 0};
  return {static_cast<float>(g.Read16(info + 8)), static_cast<float>(g.Read16(info + 10))};
}

// The packet's four corners as fn_1_4EDAC places them, before Wide Screen's squeeze
Box Corners(const Guest& g, u32 packet)
{
  const u32 flags = g.Read32(packet + FLAGS);
  const auto [w, h] = Size(g, packet, flags);
  const auto span = [](u32 anchor, float size) {
    return anchor == 2 ? std::pair{-size / 2, size / 2} : anchor == 3 ? std::pair{-size, 0.0f} : std::pair{0.0f, size};
  };
  const auto [ax0, ax1] = span(flags & 3, w * F(g, packet + SCALE_X));
  const auto [ay0, ay1] = span((flags >> 2) & 3, h * F(g, packet + SCALE_Y));
  const double r = -static_cast<s16>(g.Read16(packet + ROT_Z)) * (2 * std::numbers::pi / 65536.0);
  const float c = static_cast<float>(std::cos(r)), s = static_cast<float>(std::sin(r));
  const float x = F(g, packet + X), y = F(g, packet + Y);
  Box b;
  for (const float lx : {ax0, ax1})
    for (const float ly : {ay0, ay1})
      b.Add(x + lx * c - ly * s, y + lx * s + ly * c);
  return b;
}

u32 MirrorAnchor(u32 a)
{
  return a == 2 ? 2 : a == 3 ? 1 : 3;
}

struct State
{
  std::array<u32, 16> stack{};
  u32 depth = 0;
  std::vector<std::pair<u32, u32>> marks;  // (the queue's count, the part drawing from it on)
  std::array<Box, PARTS.size()> seen{};    // every box a part has drawn in this start
  u32 flushes = 0;
  bool said = false;
};
State s_state;

u32 Top()
{
  return s_state.depth ? s_state.stack[s_state.depth - 1] : NONE;
}

void Mark(const Guest& g)
{
  const u32 at = g.Read16(QUEUED);
  auto& marks = s_state.marks;
  if (!marks.empty() && marks.back().first == at)
    marks.back().second = Top();
  else
    marks.emplace_back(at, Top());
}

void Flush(Guest& g)
{
  const Layout& l = TheLayout();
  const u32 count = g.Read16(QUEUED), queue = g.Read32(QUEUE);
  auto marks = std::move(s_state.marks);
  s_state.marks.clear();
  s_state.marks.emplace_back(0, Top());
  if (count == 0 || !g.Ram(queue, count * PACKET))
    return;
  // each packet's part
  std::vector<u8> part(count, NONE);
  for (std::size_t m = 0; m < marks.size(); ++m)
  {
    const u32 from = marks[m].first, to = m + 1 < marks.size() ? std::min(marks[m + 1].first, count) : count;
    for (u32 i = from; i < to; ++i)
      part[i] = static_cast<u8>(marks[m].second);
  }
  std::array<Box, PARTS.size()> now{};
  for (u32 i = 0; i < count; ++i)
    if (part[i] != NONE)
      now[part[i]].Add(Corners(g, queue + i * PACKET));
  for (std::size_t p = 0; p < PARTS.size(); ++p)
    s_state.seen[p].Add(now[p]);
  const u32 wide = g.Read32(WIDE);
  const bool widescreen = wide == 1 || wide == 2;
  for (std::size_t p = 1; p < PARTS.size(); ++p)
  {
    if (now[p].Empty())
      continue;
    const std::size_t from = p == BOOST && !l.parts[BOOST].given ? ENERGY : p;  // the lightning goes with the meter
    const Move& m = l.parts[from];
    if (!m.given)
      continue;
    float px = m.px, py = m.py;
    if (!m.has_pivot)
    {
      const Pivot& at = PIVOTS[from];
      const Box& b = s_state.seen[from].Empty() ? s_state.seen[p] : s_state.seen[from];
      px = std::isnan(at.x) ? (b.x0 + b.x1) / 2 : at.x;
      py = std::isnan(at.y) ? (b.y0 + b.y1) / 2 : at.y;
    }
    for (u32 i = 0; i < count; ++i)
    {
      if (part[i] != p)
        continue;
      const u32 a = queue + i * PACKET;
      Packet k{F(g, a + X), F(g, a + Y), static_cast<s16>(g.Read16(a + ROT_Z)), g.Read32(a + FLAGS),
               F(g, a + U0),  F(g, a + V0),  F(g, a + U1), F(g, a + V1)};
      k = Apply(m, px, py, widescreen && (k.flags & SQUEEZED), k);
      g.Write32(a + X, std::bit_cast<u32>(k.x));
      g.Write32(a + Y, std::bit_cast<u32>(k.y));
      g.Write16(a + ROT_Z, static_cast<u16>(k.rot));
      g.Write32(a + FLAGS, k.flags);
      g.Write32(a + U0, std::bit_cast<u32>(k.u0));
      g.Write32(a + V0, std::bit_cast<u32>(k.v0));
      g.Write32(a + U1, std::bit_cast<u32>(k.u1));
      g.Write32(a + V1, std::bit_cast<u32>(k.v1));
    }
  }
  if (l.log && ++s_state.flushes % 600 == 0)
  {
    std::string line;
    for (std::size_t p = 1; p < PARTS.size(); ++p)
      if (const Box& b = s_state.seen[p]; !b.Empty())
        line += std::format(" {} {:.1f},{:.1f}-{:.1f},{:.1f} centre {:.1f},{:.1f};", PARTS[p], b.x0, b.y0, b.x1, b.y1,
                            (b.x0 + b.x1) / 2, (b.y0 + b.y1) / 2);
    Notice("fzgx: classic HUD boxes (wide {}):{}", wide, line);
  }
}
}  // namespace

Layout FromText(const std::string& text, std::string* why)
{
  picojson::value v;
  const std::string error = picojson::parse(v, text);
  if (!error.empty() || !v.is<picojson::object>())
  {
    *why = error.empty() ? "not a JSON object" : error;
    return {};
  }
  Layout l;
  for (const auto& [key, value] : v.get<picojson::object>())
  {
    if (key == "log" || key == "about")
    {
      l.log = key == "log" && value.is<bool>() && value.get<bool>();
      continue;
    }
    const auto at = std::find(PARTS.begin() + 1, PARTS.end(), key);
    if (at == PARTS.end() || !value.is<picojson::object>())
    {
      *why = at == PARTS.end() ? "no such part " + key : key + " is not an object";
      return {};
    }
    Move m;
    if (!ReadMove(value.get<picojson::object>(), &m, why))
    {
      *why = key + ": " + *why;
      return {};
    }
    l.parts[at - PARTS.begin()] = m;
    l.any = true;
  }
  l.any = l.any || l.log;
  return l;
}

const Layout& TheLayout()
{
  static const Layout layout = [] {
    const std::string& path = TheOptions().debug.classic_hud_layout;
    if (path.empty())
      return Layout{};
    std::string text, why;
    if (!ReadFileToString(path, text))
    {
      Warn("fzgx: classic HUD layout: {} cannot be read: nothing moves", path);
      return Layout{};
    }
    Layout l = FromText(text, &why);
    if (!why.empty())
      Warn("fzgx: classic HUD layout: {}: {}: nothing moves", path, why);
    else
    {
      u32 n = 0;
      for (const Move& m : l.parts)
        n += m.given;
      Notice("fzgx: classic HUD layout: {} parts moved, from {}", n, path);
    }
    return l;
  }();
  return layout;
}

Packet Apply(const Move& m, float px, float py, bool squeezed, const Packet& p)
{
  Packet k = p;
  float rx = p.x - px, ry = p.y - py;
  if (m.flip_x)
    rx = -rx;
  if (m.flip_y)
    ry = -ry;
  const double t = static_cast<double>(m.angle) * (std::numbers::pi / 180.0);
  const double c = std::cos(t), s = std::sin(t);
  k.x = static_cast<float>(px + rx * c - ry * s + (squeezed ? m.dx / SQUEEZE : m.dx));
  k.y = static_cast<float>(py + rx * s + ry * c + m.dy);
  // the quad: the game turns it by -rot (fn_1_4EDAC's mathutil_mtxA_rotate_z(-rot)); a mirror before it turns the
  // other way, so the packet's own turn changes sign, and the box's side and its texture corners swap
  const s32 units = static_cast<s32>(std::lround(static_cast<double>(m.angle) * (65536.0 / 360.0)));
  const s32 own = m.flip_x != m.flip_y ? -static_cast<s32>(p.rot) : p.rot;
  k.rot = static_cast<s16>(static_cast<u16>(static_cast<u32>(own - units) & 0xFFFF));
  if (m.flip_x)
  {
    k.flags = (k.flags & ~3u) | MirrorAnchor(k.flags & 3);
    std::swap(k.u0, k.u1);
  }
  if (m.flip_y)
  {
    k.flags = (k.flags & ~0xCu) | MirrorAnchor((k.flags >> 2) & 3) << 2;
    std::swap(k.v0, k.v1);
  }
  return k;
}

void Frame(Guest& g)
{
  g.Write32(WINDOW_MOVE, TheLayout().any ? 1 : 0);
}

void Call(Guest& g, u32 op, u32 arg)
{
  if (!TheLayout().any)
    return;
  switch (op)
  {
  case PUSH:
  case SET:
  case SPRITE:
  {
    u32 part = arg < PARTS.size() ? arg : NONE;
    if (op == SPRITE)
    {
      const u32 step = g.Ram(arg, 0x80) ? g.Read32(arg + SPRITE_STEP) : 0;
      part = step == LIGHTNING_STEP ? BOOST
             : step == WRONG_WAY_STEP || (g.Ram(arg, 0x80) && g.Read32(arg + 8 + IMAGE) == WRONG_WAY_IMAGE) ? WRONG_WAY
                                                                                                             : NONE;
      if (part == NONE && TheLayout().log && !s_state.said && step)
      {
        s_state.said = true;
        Notice("fzgx: classic HUD: a sprite of step {:#010x}, image {:#x}, not a part", step, g.Read32(arg + 8));
      }
    }
    if (op == SET)
    {
      if (!s_state.depth)
        break;  // (a story block outside its drawer's PUSH: none)
      s_state.stack[s_state.depth - 1] = part;
    }
    else if (s_state.depth < s_state.stack.size())
      s_state.stack[s_state.depth++] = part;
    Mark(g);
    break;
  }
  case POP:
    if (s_state.depth)
      --s_state.depth;
    Mark(g);
    break;
  case FLUSH:
    s_state.depth = 0;  // (every part's POP came before the queue is drawn)
    Flush(g);
    break;
  default:
    break;
  }
}
}  // namespace fzgx::hud_move
