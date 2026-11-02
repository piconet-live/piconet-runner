// The observer HUD's layout and its mouse (observer_hud.h says what it is; observer_hud_draw.cpp draws
// it). The sizes are the design canvas's at 1280 by 720, scaled with the picture's height; on a narrow picture (4:3)
// the racer card keeps clear of the course map by moving left and, when it must, leaving out the lap's time, and
// the tower is cut short above the card (the racer followed keeps its row). Nothing here reads or writes the game.
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
namespace sp = fzgx::spectator;
constexpr fzgx::u32 NONE = 0xFFFFFFFF;

// A row of buttons from the right: `boxes[i]` gets `widths[i]`, the last one ending at `right`.
float FromTheRight(overlay::Box* boxes, const float* widths, int count, float right, float top, float height)
{
  for (int i = count - 1; i >= 0; --i)
  {
    boxes[i] = {right - widths[i], top, right, top + height};
    right -= widths[i];
  }
  return right;
}

// the knocked-out racer's own row (count: none)
fzgx::u32 RowOf(const fzgx::SpectatorShow& show, fzgx::u32 seat)
{
  for (fzgx::u32 k = 0; k < show.count; ++k)
    if (show.rows[k].seat == seat)
      return k;
  return show.count;
}

// The camera row, from the right: Leader and Battle, the chosen group's shots, the groups. Its left edge.
float Cameras(const fzgx::SpectatorShow& show, Layout& l, float top)
{
  const float s = l.s, gutter = 16 * s;
  const overlay::Box& picture = l.picture;
  const float button_h = 32 * s, pad = 24 * s, gap = 8 * s;
  const float follow_w[2] = {ov::Width(ov::Body(13 * s), "Leader") + pad, ov::Width(ov::Body(13 * s), "Battle") + pad};
  overlay::Box follow[2];
  float x = FromTheRight(follow, follow_w, 2, picture.x1 - gutter, top, button_h);
  l.leader = follow[0];
  l.battle = follow[1];
  l.group = GroupOf(show.kind_chosen ? show.kind : 0);
  const Group& chosen = GROUP[static_cast<size_t>(l.group)];
  l.shot_count = chosen.count > 1 ? chosen.count : 0;
  if (l.shot_count > 0)
  {
    const float shot_w[4] = {30 * s, 30 * s, 30 * s, 30 * s};
    x = FromTheRight(l.shots.data(), shot_w, l.shot_count, x - gap, top, button_h);
  }
  float group_w[GROUPS];
  for (int g = 0; g < GROUPS; ++g)
    group_w[g] = ov::Width(ov::Body(13 * s), GROUP[static_cast<size_t>(g)].label) + pad;
  return FromTheRight(l.groups.data(), group_w, GROUPS, x - gap, top, button_h);
}

// The racer card at the bottom middle, clear of the map (after it: Lay), its parts left to right.
void Card(const fzgx::SpectatorShow& show, Layout& l, float clear, fzgx::u32 followed)
{
  const float s = l.s, gutter = 16 * s;
  const overlay::Box& picture = l.picture;
  const char* name = followed < show.count ? show.rows[followed].name : "";
  char words[32];
  std::snprintf(words, sizeof(words), "%.0f", show.card.speed);
  const float place_w = 64 * s;
  float name_w = std::max({170 * s, ov::Width(ov::Strong(19 * s), name), ov::Width(ov::Body(12 * s), show.card.machine)}) + 32 * s;
  float energy_w = 150 * s, speed_w = std::max(96 * s, ov::Width(ov::Figure(24 * s), words) + 32 * s), lap_w = 110 * s;
  const float room = (l.map.x0 - 12 * s) - (picture.x0 + gutter);
  if (place_w + name_w + energy_w + speed_w + lap_w > room)
    lap_w = 0;  // a narrow picture: the lap's time left out
  if (place_w + name_w + energy_w + speed_w + lap_w > room)
    energy_w = 112 * s;
  if (place_w + name_w + energy_w + speed_w + lap_w > room)
    name_w = std::max(120 * s, room - place_w - energy_w - speed_w);
  const float card_w = place_w + name_w + energy_w + speed_w + lap_w, card_h = 58 * s;
  const float card_bottom = picture.y1 - std::max(24 * s, clear + 20 * s);
  float card_x1 = std::min((picture.x0 + picture.x1 + card_w) / 2, l.map.x0 - 12 * s);
  const float card_x0 = std::max(card_x1 - card_w, picture.x0 + gutter);
  card_x1 = card_x0 + card_w;
  l.card = {card_x0, card_bottom - card_h, card_x1, card_bottom};
  float cx = card_x0;
  const auto part = [&](float w) {
    const overlay::Box b{cx, l.card.y0, cx + w, l.card.y1};
    cx += w;
    return b;
  };
  l.place = part(place_w);
  l.name = part(name_w);
  l.energy = part(energy_w);
  l.speed = part(speed_w);
  l.lap = lap_w > 0 ? part(lap_w) : overlay::Box{};
}
}  // namespace

// Every kind is a shot of exactly one group, whatever order they are numbered in.
static_assert([] {
  std::array<int, fzgx::spectator::KINDS> seen{};
  for (const Group& group : GROUP)
    for (int i = 0; i < group.count; ++i)
      ++seen[static_cast<size_t>(group.kinds[static_cast<size_t>(i)])];
  for (const int times : seen)
    if (times != 1)
      return false;
  return true;
}());

int GroupOf(fzgx::u32 kind)
{
  for (int g = 0; g < GROUPS; ++g)
    for (int i = 0; i < GROUP[static_cast<size_t>(g)].count; ++i)
      if (static_cast<int>(kind) == GROUP[static_cast<size_t>(g)].kinds[static_cast<size_t>(i)])
        return g;
  return 0;
}

Layout Lay(const fzgx::SpectatorShow& show, const overlay::Box& picture, float clear, float map_side)
{
  Layout l;
  l.picture = picture;
  l.s = ov::Scale(picture.y1 - picture.y0);
  const float s = l.s, gutter = 16 * s, top = picture.y0 + gutter;
  const float row_left = Cameras(show, l, top);
  // the mode button under them; the clean picture's button at the top right
  const sp::View view = static_cast<sp::View>(show.view);
  const sp::View named = view == sp::View::Clean ? sp::View::Game : view;  // (the button says it while it fades)
  const float mode_w = ov::Width(ov::Body(12 * s), sp::ViewName(named)) + 8 * s + ov::Width(ov::Body(12 * s), "Z") + 20 * s;
  // (under the camera row; in the game's HUD at the top, above the game's energy meter)
  const float mode_top = view == sp::View::Observer ? top + 44 * s : top;
  l.mode = {picture.x1 - gutter - mode_w, mode_top, picture.x1 - gutter, mode_top + 26 * s};
  const float ghost_w = ov::Width(ov::Body(13 * s), "Show HUD") + 8 * s + ov::Width(ov::Body(13 * s), "Z") + 24 * s;
  l.ghost = {picture.x1 - gutter - ghost_w, top, picture.x1 - gutter, top + 30 * s};
  // the course map's ground at the bottom right, above the replay bar when it is up
  const float map_pad = 8 * s, map_w = std::max(232 * s, map_side + 2 * map_pad), map_h = map_side + 2 * map_pad;
  const float map_bottom = picture.y1 - std::max(gutter, clear + gutter);
  l.map = {picture.x1 - gutter - map_w, map_bottom - map_h, picture.x1 - gutter, map_bottom};
  const float square_x = std::round((l.map.x0 + l.map.x1 - map_side) / 2), square_y = std::round(l.map.y0 + map_pad);
  l.square = {square_x, square_y, square_x + map_side, square_y + map_side};
  const fzgx::u32 followed = RowOf(show, show.follow);
  Card(show, l, clear, followed);
  // the tower at the left: as many rows as fit above the card where the card reaches under it
  const float tower_w = 232 * s, head_h = 34 * s, row_h = 18 * s;
  l.head = {picture.x0 + gutter, top, picture.x0 + gutter + tower_w, top + head_h};
  float lowest = picture.y1 - gutter;
  if (l.card.x0 < l.head.x1 + 8 * s)
    lowest = std::min(lowest, l.card.y0 - 8 * s);
  const fzgx::u32 fits = static_cast<fzgx::u32>(std::max(0.0f, std::floor((lowest - l.head.y1) / row_h)));
  l.drawn = std::min({show.count, fits, 30u});
  for (fzgx::u32 i = 0; i < l.drawn; ++i)
  {
    l.row[i] = i;
    l.rows[i] = {l.head.x0, l.head.y1 + i * row_h, l.head.x1, l.head.y1 + (i + 1) * row_h};
  }
  if (l.drawn > 0 && followed < show.count && followed >= l.drawn)
    l.row[l.drawn - 1] = followed;
  l.tower = {l.head.x0, l.head.y0, l.head.x1, l.head.y1 + l.drawn * row_h};
  // a racer knocked out: at the top middle, or under the camera row when they would meet
  if (show.role == static_cast<fzgx::u32>(sp::Role::KnockedOut) && show.own < 30)
  {
    const fzgx::u32 own = RowOf(show, show.own);
    char lap[16];
    const fzgx::u32 laps_done = own < show.count ? show.rows[own].laps : 0;
    std::snprintf(lap, sizeof(lap), "Lap %u", std::min<fzgx::u32>(laps_done + 1u, std::max(show.laps, 1u)));
    const float w = 14 * s + 3 * s + 10 * s + ov::Width(ov::Strong(13 * s), own < show.count ? show.rows[own].name : "") +
                    10 * s + ov::Width(ov::Strong(13 * s), "OUT") + 10 * s + ov::Width(ov::Body(13 * s), lap) + 14 * s;
    float x0 = (picture.x0 + picture.x1 - w) / 2, y0 = top;
    const float right = (view == sp::View::Observer ? row_left : l.mode.x0) - 12 * s;
    if (x0 + w > right)
      x0 = right - w;
    if (x0 < l.head.x1 + 12 * s)
    {
      x0 = (picture.x0 + picture.x1 - w) / 2;
      y0 = l.mode.y1 + 8 * s;
    }
    l.out = {x0, y0, x0 + w, y0 + 32 * s};
  }
  return l;
}

bool Hit(const fzgx::SpectatorShow& show, const Layout& l, float x, float y, fzgx::SpectatorClick* click)
{
  const sp::View view = static_cast<sp::View>(show.view);
  if (view == sp::View::Clean || l.mode.Has(x, y))
  {
    const bool hit = view == sp::View::Clean ? l.ghost.Has(x, y) : true;
    click->overlay = click->overlay || hit;
    return hit;
  }
  if (view != sp::View::Observer)
    return false;
  const auto allowed = [&](int kind) { return (show.kinds >> kind & 1) != 0; };
  for (int g = 0; g < GROUPS; ++g)
    if (l.groups[static_cast<size_t>(g)].Has(x, y))
    {
      const Group& group = GROUP[static_cast<size_t>(g)];
      if (g != l.group || !show.kind_chosen)
        for (int k = 0; k < group.count; ++k)
          if (allowed(group.kinds[static_cast<size_t>(k)]))
          {
            click->kind = group.kinds[static_cast<size_t>(k)];
            break;
          }
      return true;
    }
  for (int i = 0; i < l.shot_count; ++i)
    if (l.shots[static_cast<size_t>(i)].Has(x, y))
    {
      const int kind = GROUP[static_cast<size_t>(l.group)].kinds[static_cast<size_t>(i)];
      if (allowed(kind))
        click->kind = kind;
      return true;
    }
  if (l.leader.Has(x, y) || l.battle.Has(x, y))
  {
    (l.leader.Has(x, y) ? click->leader : click->battle) = show.follow_others;
    return true;
  }
  for (fzgx::u32 i = 0; i < l.drawn; ++i)
    if (l.rows[i].Has(x, y))
    {
      const fzgx::u32 seat = show.rows[l.row[i]].seat;
      if (show.follow_others || seat == show.follow)
        click->follow = seat;
      return true;
    }
  return l.tower.Has(x, y) || l.card.Has(x, y);
}
}  // namespace fzgx_port::observer_hud
