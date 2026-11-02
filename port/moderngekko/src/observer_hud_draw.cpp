// The observer HUD's drawing (observer_hud.h says what it is; observer_hud.cpp lays it out): ImGui's background list,
// anti-aliased, on the GPU as the course map and the panel are. The design canvas's sizes at 720 lines times the
// layout's scale; the launcher's palette and type (overlay.h): the tower's head and the card's place in the display
// face, leaning; names strong; labels and the rest read; figures on one pitch. Panels and cards soft-cornered on
// translucent grounds, buttons square; the one that is on fills with the accent, black on it; tones under the
// pointer, no outlines. The course map's ground before the map, the rest after it. Nothing here reads or writes the
// game.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "observer_hud.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "fzgx/observer.h"
#include "fzgx/spectator.h"

namespace fzgx_port::observer_hud
{
namespace
{
namespace ov = overlay;
namespace ob = fzgx::observer;
namespace sp = fzgx::spectator;

enum class Align
{
  Left,
  Middle,
  Right,
};

// `text` in `type`, its line's middle at `mid`; cut at `clip_x1` when given.
void Words(ImDrawList* list, const ov::Type& type, float x, float mid, ImU32 colour, const char* text,
           Align align = Align::Left, float clip_x1 = 0)
{
  const float w = ov::Width(type, text);
  if (align == Align::Middle)
    x -= w / 2;
  else if (align == Align::Right)
    x -= w;
  const float top = std::round(mid - type.size / 2);
  x = std::round(x);
  if (clip_x1 > 0)
  {
    const ImVec4 clip(x, top - type.size, clip_x1, top + 2 * type.size);
    ov::Draw(list, type, x, top, colour, text, &clip);
  }
  else
    ov::Draw(list, type, x, top, colour, text);
}

void Fill(ImDrawList* list, const ov::Box& b, ImU32 colour, float rounding = 0, ImDrawFlags corners = ImDrawFlags_RoundCornersAll)
{
  if (!b.Empty())
    list->AddRectFilled(ImVec2(b.x0, b.y0), ImVec2(b.x1, b.y1), colour, rounding, corners);
}

float Mid(const ov::Box& b)
{
  return (b.y0 + b.y1) / 2;
}

// a button in a run: the accent when on (black on it), one tone up under the pointer, off when the course forbids
// it; the run's ends keep the ground's corners
void Button(ImDrawList* list, const ov::Box& b, float s, const char* words, bool on, bool allowed, bool hover, float a,
            ImDrawFlags corners)
{
  if (on)
    Fill(list, b, ov::Colour(ov::ACCENT, a), ov::CORNER_SMALL * s, corners);
  else if (hover && allowed)
    Fill(list, b, ov::Colour(ov::HOVER, a), ov::CORNER_SMALL * s, corners);
  const ImU32 ink = on ? ov::Colour(ov::ON_ACCENT, a) : ov::Colour(allowed ? ov::SECONDARY : ov::OFF_TONE, a);
  Words(list, on ? ov::Strong(13 * s) : ov::Body(13 * s), (b.x0 + b.x1) / 2, Mid(b), ink, words, Align::Middle);
}

// the ground behind a run of buttons, soft-cornered
void Ground(ImDrawList* list, const ov::Box& first, const ov::Box& last, float s, float a)
{
  Fill(list, {first.x0, first.y0, last.x1, last.y1}, ov::Colour(ov::GROUND, PANEL * a), ov::CORNER_SMALL * s);
}

// a run of buttons: its ground, then each button; the first and the last take the ground's corners on their side
template <class Each>
void Run(ImDrawList* list, const ov::Box* boxes, int count, float s, float a, Each each)
{
  if (count <= 0)
    return;
  Ground(list, boxes[0], boxes[count - 1], s, a);
  for (int i = 0; i < count; ++i)
  {
    ImDrawFlags corners = ImDrawFlags_RoundCornersNone;
    if (i == 0)
      corners |= ImDrawFlags_RoundCornersLeft;
    if (i == count - 1)
      corners |= ImDrawFlags_RoundCornersRight;
    each(i, boxes[i], count == 1 ? ImDrawFlags_RoundCornersAll : corners);
  }
}

void Tower(ImDrawList* list, const fzgx::SpectatorShow& show, const Layout& l, float a, float hx, float hy)
{
  const float s = l.s, corner = ov::CORNER * s;
  Fill(list, l.tower, ov::Colour(ov::GROUND, PANEL * a), corner);
  Fill(list, l.head, ov::Colour(ov::ACCENT, a), corner, l.drawn > 0 ? ImDrawFlags_RoundCornersTop : ImDrawFlags_RoundCornersAll);
  char words[32];
  if (show.laps != 0)
    std::snprintf(words, sizeof(words), "LAP %u / %u", show.lap, show.laps);
  else
    std::snprintf(words, sizeof(words), "LAP %u", show.lap);
  Words(list, ov::Display(18 * s), l.head.x0 + 10 * s, Mid(l.head), ov::Colour(ov::ON_ACCENT, a), words);
  std::snprintf(words, sizeof(words), "%u RACING", show.racing);
  Words(list, ov::Strong(12 * s, true), l.head.x1 - 10 * s, Mid(l.head), ov::Colour(ov::ON_ACCENT, a), words, Align::Right);
  for (fzgx::u32 i = 0; i < l.drawn; ++i)
  {
    const ov::Box& b = l.rows[i];
    const fzgx::SpectatorRow& r = show.rows[l.row[i]];
    const bool followed = r.seat == show.follow, out = r.out;
    const bool last = i + 1 == l.drawn;
    const ImDrawFlags corners = last ? ImDrawFlags_RoundCornersBottom : ImDrawFlags_RoundCornersNone;
    if (followed)
      Fill(list, b, ov::Colour(ov::ACCENT, 0.18f * a), last ? corner : 0, corners);
    else if (b.Has(hx, hy) && show.follow_others)
      Fill(list, b, ov::Colour(ov::HOVER, 0.9f * a), last ? corner : 0, corners);
    else if (l.row[i] % 2 == 1)
      Fill(list, b, ov::Colour(ov::TEXT, 0.03f * a), last ? corner : 0, corners);
    if (l.row[i] != i)  // the racer followed, below the rows that fit: a rule over it
      Fill(list, {b.x0 + 8 * s, b.y0, b.x1 - 8 * s, b.y0 + std::max(1.0f, s)}, ov::Colour(ov::ROW_LINE, a));
    const float mid = Mid(b);
    const std::uint32_t ink = out ? ov::OFF_TONE : ov::TEXT;
    std::snprintf(words, sizeof(words), "%u", r.rank + 1u);
    Words(list, ov::Figure(13 * s), b.x0 + 28 * s, mid, ov::Colour(ink, a), words, Align::Right);
    const ov::Box swatch{b.x0 + 35 * s, mid - 5.5f * s, b.x0 + 38 * s, mid + 5.5f * s};
    Fill(list, swatch, ov::Colour(r.colour, out ? 0.5f * a : a));
    const ob::Gap gap{static_cast<ob::GapKind>(r.gap_kind), r.gap_ms, r.gap_laps};
    ob::GapText(gap, words, sizeof(words));
    const float gap_x = b.x1 - 10 * s;
    Words(list, ov::Body(13 * s), b.x0 + 45 * s, mid, ov::Colour(ink, a), r.name, Align::Left, gap_x - 52 * s);
    const std::uint32_t gap_ink = gap.kind == ob::GapKind::Out ? ov::DANGER : gap.kind == ob::GapKind::Leader ? ov::GOLD : ov::FAINT;
    Words(list, ov::Body(11 * s, true), gap_x, mid, ov::Colour(gap_ink, a), words, Align::Right);
  }
}

void Cameras(ImDrawList* list, const fzgx::SpectatorShow& show, const Layout& l, float a, float hx, float hy)
{
  const auto allowed = [&](int kind) { return (show.kinds >> kind & 1) != 0; };
  Run(list, l.groups.data(), GROUPS, l.s, a, [&](int g, const ov::Box& b, ImDrawFlags corners) {
    const Group& group = GROUP[static_cast<size_t>(g)];
    bool any = false;
    for (int k = 0; k < group.count; ++k)
      any = any || allowed(group.kinds[static_cast<size_t>(k)]);
    Button(list, b, l.s, group.label, g == l.group && show.kinds != 0, any, b.Has(hx, hy), a, corners);
  });
  Run(list, l.shots.data(), l.shot_count, l.s, a, [&](int i, const ov::Box& b, ImDrawFlags corners) {
    const int kind = GROUP[static_cast<size_t>(l.group)].kinds[static_cast<size_t>(i)];
    char words[4];
    std::snprintf(words, sizeof(words), "%d", i + 1);
    Button(list, b, l.s, words, show.kind_chosen && static_cast<int>(show.kind) == kind, allowed(kind), b.Has(hx, hy), a,
           corners);
  });
  const ov::Box follow[2] = {l.leader, l.battle};
  Run(list, follow, 2, l.s, a, [&](int i, const ov::Box& b, ImDrawFlags corners) {
    Button(list, b, l.s, i == 0 ? "Leader" : "Battle", show.how == (i == 0 ? 1u : 2u), show.follow_others, b.Has(hx, hy), a,
           corners);
  });
}

void Card(ImDrawList* list, const fzgx::SpectatorShow& show, const Layout& l, float a)
{
  const fzgx::SpectatorRow* r = nullptr;
  for (fzgx::u32 k = 0; k < show.count; ++k)
    if (show.rows[k].seat == show.follow)
      r = &show.rows[k];
  if (r == nullptr)
    return;
  const float s = l.s, pad = 16 * s, corner = ov::CORNER * s;
  Fill(list, l.card, ov::Colour(ov::GROUND, CARD * a), corner);
  // the place on the racer's colour, in the display face
  Fill(list, l.place, ov::Colour(r->colour, a), corner, ImDrawFlags_RoundCornersLeft);
  char words[32];
  std::snprintf(words, sizeof(words), "%u", r->rank + 1u);
  Words(list, ov::Display(34 * s), (l.place.x0 + l.place.x1) / 2 - 3 * s, Mid(l.place), ov::Colour(ov::ON_ACCENT, a), words,
        Align::Middle);
  // name and machine
  Words(list, ov::Strong(19 * s), l.name.x0 + pad, Mid(l.name) - 9 * s, ov::Colour(ov::TEXT, a), r->name, Align::Left,
        l.name.x1 - pad);
  Words(list, ov::Body(12 * s), l.name.x0 + pad, Mid(l.name) + 11 * s, ov::Colour(ov::FAINT, a), show.card.machine,
        Align::Left, l.name.x1 - pad);
  const auto rule = [&](const ov::Box& b) {
    if (!b.Empty())
      Fill(list, {b.x0, b.y0 + 10 * s, b.x0 + std::max(1.0f, s), b.y1 - 10 * s}, ov::Colour(ov::ROW_LINE, a));
  };
  // energy
  rule(l.energy);
  const int energy = static_cast<int>(std::lround(r->energy * 100));
  Words(list, ov::Body(11 * s), l.energy.x0 + pad, Mid(l.energy) - 7 * s, ov::Colour(ov::FAINT, a), "ENERGY");
  std::snprintf(words, sizeof(words), "%d", energy);
  Words(list, ov::Body(11 * s, true), l.energy.x1 - pad, Mid(l.energy) - 7 * s, ov::Colour(r->out ? ov::OFF_TONE : ov::TEXT, a),
        words, Align::Right);
  const ov::Box track{l.energy.x0 + pad, Mid(l.energy) + 6 * s, l.energy.x1 - pad, Mid(l.energy) + 11 * s};
  Fill(list, track, ov::Colour(ov::RAISED, a), 2.5f * s);
  Fill(list, {track.x0, track.y0, track.x0 + (track.x1 - track.x0) * std::clamp(r->energy, 0.0f, 1.0f), track.y1},
       ov::Colour(r->energy < 0.25f ? ov::DANGER : ov::ACCENT, a), 2.5f * s);
  // speed
  rule(l.speed);
  std::snprintf(words, sizeof(words), "%.0f", std::max(0.0f, show.card.speed));
  Words(list, ov::Figure(24 * s), l.speed.x1 - pad, Mid(l.speed) - 6 * s, ov::Colour(ov::TEXT, a), words, Align::Right);
  Words(list, ov::Body(11 * s), l.speed.x1 - pad, Mid(l.speed) + 14 * s, ov::Colour(ov::FAINT, a), "KM/H", Align::Right);
  // the lap and its time; finished: the race's time; out: says so
  if (l.lap.Empty())
    return;
  rule(l.lap);
  char label[16];
  if (r->out)
    std::snprintf(words, sizeof(words), "OUT");
  else
    ob::ClockText(show.card.lap_ms, words, sizeof(words));
  if (r->finished)
    std::snprintf(label, sizeof(label), "FINISH");
  else
    std::snprintf(label, sizeof(label), "LAP %u", show.card.lap);
  Words(list, ov::Figure(16 * s), l.lap.x1 - pad, Mid(l.lap) - 7 * s, ov::Colour(r->out ? ov::DANGER : ov::TEXT, a), words,
        Align::Right);
  Words(list, ov::Body(11 * s, true), l.lap.x1 - pad, Mid(l.lap) + 13 * s, ov::Colour(ov::FAINT, a), label, Align::Right);
}

// a racer knocked out: their colour, name, OUT and the lap they went out on
void Out(ImDrawList* list, const fzgx::SpectatorShow& show, const Layout& l)
{
  if (l.out.Empty())
    return;
  const fzgx::SpectatorRow* r = nullptr;
  for (fzgx::u32 k = 0; k < show.count; ++k)
    if (show.rows[k].seat == show.own)
      r = &show.rows[k];
  if (r == nullptr)
    return;
  const float s = l.s, mid = Mid(l.out);
  Fill(list, l.out, ov::Colour(ov::GROUND, PANEL), ov::CORNER * s);
  float x = l.out.x0 + 14 * s;
  Fill(list, {x, mid - 7 * s, x + 3 * s, mid + 7 * s}, ov::Colour(r->colour, 1));
  x += 13 * s;
  Words(list, ov::Strong(13 * s), x, mid, ov::Colour(ov::TEXT, 1), r->name);
  x += ov::Width(ov::Strong(13 * s), r->name) + 10 * s;
  Words(list, ov::Strong(13 * s), x, mid, ov::Colour(ov::DANGER, 1), "OUT");
  x += ov::Width(ov::Strong(13 * s), "OUT") + 10 * s;
  char lap[16];
  std::snprintf(lap, sizeof(lap), "Lap %u", std::min<fzgx::u32>(r->laps + 1u, std::max(show.laps, 1u)));
  Words(list, ov::Body(13 * s), x, mid, ov::Colour(ov::FAINT, 1), lap);
}

// a small button with its key: the mode button, and the clean picture's Show HUD (a plate on the ground, no outline)
void KeyButton(ImDrawList* list, const ov::Box& b, float s, const char* words, float size, std::uint32_t ink,
               float ground, float a)
{
  Fill(list, b, ov::Colour(ov::GROUND, ground * a), ov::CORNER_SMALL * s);
  const float key = ov::Width(ov::Body(size), "Z");
  Words(list, ov::Body(size), b.x1 - 10 * s - key - 8 * s, Mid(b), ov::Colour(ink, a), words, Align::Right);
  Words(list, ov::Body(size), b.x1 - 10 * s, Mid(b), ov::Colour(ov::FAINT, a), "Z", Align::Right);
}
}  // namespace

void DrawGround(ImDrawList* list, const Layout& l, float observer)
{
  if (observer > 0.004f)
    Fill(list, l.map, ov::Colour(ov::GROUND, MAP * observer), ov::CORNER * l.s);
}

void Draw(ImDrawList* list, const fzgx::SpectatorShow& show, const Layout& l, const Fades& a, float hx, float hy)
{
  if (a.observer > 0.004f)
  {
    Tower(list, show, l, a.observer, hx, hy);
    Cameras(list, show, l, a.observer, hx, hy);
    Card(list, show, l, a.observer);
  }
  if (a.mode > 0.004f)
    KeyButton(list, l.mode, l.s, sp::ViewName(static_cast<sp::View>(show.view == 2 ? 1 : show.view)), 12 * l.s,
              ov::SECONDARY, MODE, a.mode);
  if (a.clean > 0.004f)
    KeyButton(list, l.ghost, l.s, "Show HUD", 13 * l.s, ov::TEXT, PANEL,
              a.clean * (GHOST + (1 - GHOST) * a.ghost_hover));
  Out(list, show, l);
}
}  // namespace fzgx_port::observer_hud
