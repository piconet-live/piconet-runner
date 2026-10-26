// The racer timeline's lanes (fzgx/host.h Timeline; the core's timeline.cpp), over the picture just above the replay
// bar and fading with it (replay_bar.cpp, overlay.h): a lane for the followed racer and for the first three of the
// race's order, drawn as a git graph on its side along the bar's track. Ported from an earlier
// prototype drawn with GDI in a window of its own.
//   - A lane: a line in the seat's colour, as bright as the racer's energy, dashed where the computer drives; it
//     stops at the buffered edge, with an end mark at the finish (white) or out (red). At its left the racer's place
//     at the picture and name; the followed racer's lane is marked.
//   - Events on a lane, square marks: KO, out, finish large; lead, fastest lap small; a lap a small outline; a rail
//     hit a grey outline; a hit an outline in the other machine's colour. A KO whose attacker has a lane: a red line
//     from the attacker's lane into the victim's. Under the pointer a mark is outlined in white and a line above
//     the lanes reads it ("KO P4 RAPH by P2 FIFA").
//   - A click on a lane follows that racer (the same follow target as the standings and the D-pad); a click on an
//     event jumps 3 s before it for those who may scrub (spectators, replays), and follows its racer for a racer who
//     is out of a live race (never a jump: a design rule).
// Nothing here reads or writes the game.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "adapter_internal.h"
#include "overlay.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <vector>

namespace fzgx_port
{
namespace
{
namespace ov = overlay;
constexpr fzgx::u32 NONE = 0xFFFFFFFF;
constexpr fzgx::u32 LANES = 4;
constexpr fzgx::u32 BEFORE_STEPS = 180;  // a click on an event: 3 s before it

struct Lanes
{
  std::mutex lock;
  fzgx::Timeline line;
  bool have = false;
  BarPlace place;                    // the bar as last drawn
  float alpha = 0;
  std::array<fzgx::u32, LANES> racers{};
  fzgx::u32 count = 0;
  fzgx::u32 follow = NONE;
  bool may_scrub = true;
  float mouse_x = -1, mouse_y = -1;
};
Lanes s_lanes;

float Lane(const BarPlace& p)
{
  return 18 * p.s;
}

float XOf(const BarPlace& p, fzgx::u32 step)
{
  const fzgx::u32 span = std::max<fzgx::u32>(p.last - p.first, 1);
  const fzgx::u32 at = std::clamp(step, p.first, p.first + span) - p.first;
  return p.x0 + (p.x1 - p.x0) * static_cast<float>(at) / static_cast<float>(span);
}

float YOf(const Lanes& l, fzgx::u32 k)
{
  return l.place.top - Lane(l.place) * (static_cast<float>(l.count - k) - 0.5f);
}

ImU32 SeatColour(const fzgx::Timeline& line, fzgx::u32 racer, float bright, float a)
{
  const fzgx::u32 c = line.colour[racer] & 0xFFFFFF;
  const auto part = [&](int shift) { return static_cast<int>((c >> shift & 0xFF) * bright); };
  return IM_COL32(part(16), part(8), part(0), static_cast<int>(255 * a));
}

// The lanes' racers: the followed one and the first three of the order, each once.
void Choose(Lanes& l)
{
  l.count = 0;
  const auto add = [&](fzgx::u32 r) {
    if (r >= l.line.racers || l.count >= LANES)
      return;
    for (fzgx::u32 k = 0; k < l.count; ++k)
      if (l.racers[k] == r)
        return;
    l.racers[l.count++] = r;
  };
  for (fzgx::u32 k = 0; k < std::min<fzgx::u32>(3, l.line.racers); ++k)
    add(l.line.order[k]);
  add(l.follow);
}

fzgx::u32 LaneOf(const Lanes& l, fzgx::u32 racer)
{
  for (fzgx::u32 k = 0; k < l.count; ++k)
    if (l.racers[k] == racer)
      return k;
  return NONE;
}

fzgx::u32 PlaceOf(const fzgx::Timeline& line, fzgx::u32 racer)
{
  for (fzgx::u32 k = 0; k < line.racers; ++k)
    if (line.order[k] == racer)
      return k + 1;
  return 0;
}

// The event under the point, on a lane (NONE: none).
std::size_t EventAt(const Lanes& l, float x, float y)
{
  const float half = 6 * l.place.s;
  for (std::size_t i = 0; i < l.line.events.size(); ++i)
  {
    const fzgx::TimelineEvent& e = l.line.events[i];
    const fzgx::u32 k = LaneOf(l, e.racer);
    if (k == NONE || std::abs(XOf(l.place, e.step) - x) > half || std::abs(YOf(l, k) - y) > half)
      continue;
    return i;
  }
  return static_cast<std::size_t>(NONE);
}

void Words(const fzgx::Timeline& line, const fzgx::TimelineEvent& e, char* out, std::size_t size)
{
  const auto who = [&](fzgx::u32 r, char* to, std::size_t n) {
    std::snprintf(to, n, "P%u %s", PlaceOf(line, r), line.names[r][0] ? line.names[r] : "?");
  };
  char a[32], b[32] = "";
  who(e.racer, a, sizeof(a));
  if (e.other < line.racers)
    who(e.other, b, sizeof(b));
  switch (e.kind)
  {
  case fzgx::TimelineEvent::KO: std::snprintf(out, size, "KO %s%s%s", a, b[0] ? " by " : "", b); break;
  case fzgx::TimelineEvent::Out: std::snprintf(out, size, "Out %s", a); break;
  case fzgx::TimelineEvent::Retired: std::snprintf(out, size, "Retired %s", a); break;
  case fzgx::TimelineEvent::Finish: std::snprintf(out, size, "Finish %s, place %u", a, e.place); break;
  case fzgx::TimelineEvent::Lap: std::snprintf(out, size, "Lap %u %s", e.value, a); break;
  case fzgx::TimelineEvent::Lead: std::snprintf(out, size, "Lead %s", a); break;
  case fzgx::TimelineEvent::FastLap:
    std::snprintf(out, size, "Best lap %s %u:%02u.%03u", a, e.value / 60000, e.value / 1000 % 60, e.value % 1000);
    break;
  case fzgx::TimelineEvent::Rail: std::snprintf(out, size, "Rail %s x%u", a, e.value + 1); break;
  case fzgx::TimelineEvent::Hit: std::snprintf(out, size, "Hit %s with %s x%u", a, b, e.value + 1); break;
  default: out[0] = 0; break;
  }
}

void DrawLane(ImDrawList* list, const Lanes& l, fzgx::u32 k, float a)
{
  const fzgx::Timeline& line = l.line;
  const BarPlace& p = l.place;
  const fzgx::u32 r = l.racers[k];
  const float y = YOf(l, k), s = p.s;
  if (r == l.follow)
    list->AddRectFilled(ImVec2(0, y - Lane(p) / 2), ImVec2(p.x1 + 8 * s, y + Lane(p) / 2), ov::Colour(ov::TEXT, 0.08f * a));
  char tag[40];
  std::snprintf(tag, sizeof(tag), "%u  %s", PlaceOf(line, r), line.names[r][0] ? line.names[r] : "?");
  ov::Draw(list, ov::Body(13 * s, true), 16 * s, y - 7 * s, ov::Colour(r == l.follow ? ov::TEXT : ov::FAINT, a), tag);
  // the line, a segment a sample: bright as the energy, dashed where the computer drives
  const fzgx::u32 end = std::min(line.end[r], line.front);
  const std::size_t n = line.racers ? line.samples.size() / line.racers : 0;
  for (std::size_t i = 0; i < n; ++i)
  {
    const fzgx::u32 from = line.first + static_cast<fzgx::u32>(i) * fzgx::Timeline::SAMPLE_EVERY;
    if (from >= end)
      break;
    const fzgx::u32 to = std::min(from + fzgx::Timeline::SAMPLE_EVERY, end);
    const fzgx::u8 v = line.samples[i * line.racers + r];
    const float bright = 0.35f + 0.65f * static_cast<float>(v & 0x7F) / 127.0f;
    const float x0 = XOf(p, from), x1 = XOf(p, to), half = (v & 0x80 ? 1.5f : 2.5f) * s;
    if ((v & 0x80) != 0)
      for (float x = x0; x < x1; x += 6 * s)
        list->AddRectFilled(ImVec2(x, y - half), ImVec2(std::min(x + 3 * s, x1), y + half), SeatColour(line, r, bright, a));
    else
      list->AddRectFilled(ImVec2(x0, y - half), ImVec2(x1, y + half), SeatColour(line, r, bright, a));
  }
  if (line.end[r] != NONE && line.end[r] <= line.front)
  {
    const float x = XOf(p, line.end[r]);
    list->AddRectFilled(ImVec2(x - 1.5f * s, y - 6 * s), ImVec2(x + 1.5f * s, y + 6 * s),
                        ov::Colour(line.out[r] ? ov::DANGER : ov::TEXT, a));
  }
}

void DrawEvent(ImDrawList* list, const Lanes& l, const fzgx::TimelineEvent& e, bool hover, float a)
{
  const fzgx::u32 k = LaneOf(l, e.racer);
  if (k == NONE)
    return;
  const BarPlace& p = l.place;
  const float s = p.s, x = XOf(p, e.step), y = YOf(l, k);
  using E = fzgx::TimelineEvent;
  float half = 3 * s;
  bool filled = true;
  ImU32 c = SeatColour(l.line, e.racer, 1, a);
  switch (e.kind)
  {
  case E::KO: case E::Out: case E::Retired: half = 5 * s; c = ov::Colour(ov::DANGER, a); break;
  case E::Finish: half = 5 * s; c = ov::Colour(ov::TEXT, a); break;
  case E::Lead: case E::FastLap: c = ov::Colour(ov::ACCENT, a); break;
  case E::Lap: filled = false; c = ov::Colour(ov::TEXT, a); break;
  case E::Rail: filled = false; c = ov::Colour(ov::FAINT, a); break;
  case E::Hit: filled = false; c = e.other < l.line.racers ? SeatColour(l.line, e.other, 1, a) : ov::Colour(ov::TEXT, a); break;
  default: return;
  }
  if (e.kind == E::KO && e.other < l.line.racers && LaneOf(l, e.other) != NONE)
    list->AddLine(ImVec2(x - 10 * s, YOf(l, LaneOf(l, e.other))), ImVec2(x, y), ov::Colour(ov::DANGER, 0.8f * a), 1.5f * s);
  if (filled)
    list->AddRectFilled(ImVec2(x - half, y - half), ImVec2(x + half, y + half), c);
  else
    list->AddRect(ImVec2(x - half, y - half), ImVec2(x + half, y + half), c, 0, 0, 1.5f * s);
  if (hover)
    list->AddRect(ImVec2(x - half - 2 * s, y - half - 2 * s), ImVec2(x + half + 2 * s, y + half + 2 * s),
                  ov::Colour(ov::TEXT, a), 0, 0, 1.5f * s);
}
}  // namespace

void ShowReplayTimeline(const fzgx::Timeline& line)
{
  std::lock_guard<std::mutex> hold(s_lanes.lock);
  s_lanes.line = line;
  s_lanes.have = true;
}

void ReplayLanesFollow(fzgx::u32 follow, bool may_scrub)
{
  std::lock_guard<std::mutex> hold(s_lanes.lock);
  s_lanes.follow = follow;
  s_lanes.may_scrub = may_scrub;
}

bool ReplayLaneName(fzgx::u32 racer, char* out, std::size_t size)
{
  std::lock_guard<std::mutex> hold(s_lanes.lock);
  if (!s_lanes.have || racer >= s_lanes.line.racers || s_lanes.line.names[racer][0] == 0)
    return false;
  std::snprintf(out, size, "%s", s_lanes.line.names[racer]);
  return true;
}

void DrawReplayLanes(const BarPlace& place, float alpha)
{
  std::lock_guard<std::mutex> hold(s_lanes.lock);
  Lanes& l = s_lanes;
  l.place = place;
  l.alpha = alpha;
  if (!l.have || alpha <= 0.004f || ImGui::GetCurrentContext() == nullptr)
  {
    SetReplayLanesHeight(l.have ? Lane(place) * LANES : 0);
    return;
  }
  Choose(l);
  SetReplayLanesHeight(Lane(place) * static_cast<float>(l.count) + 24 * place.s);
  ImDrawList* list = ImGui::GetForegroundDrawList();
  const float top = place.top - Lane(place) * static_cast<float>(l.count);
  list->AddRectFilled(ImVec2(0, top), ImVec2(place.width, place.top), ov::Colour(ov::BAR, 0.7f * alpha));
  for (fzgx::u32 k = 0; k < l.count; ++k)
    DrawLane(list, l, k, alpha);
  const std::size_t hover = EventAt(l, l.mouse_x, l.mouse_y);
  for (std::size_t i = 0; i < l.line.events.size(); ++i)
    DrawEvent(list, l, l.line.events[i], i == hover, alpha);
  if (hover != static_cast<std::size_t>(NONE))
  {
    char words[96];
    Words(l.line, l.line.events[hover], words, sizeof(words));
    const float s = place.s;
    const ov::Type read = ov::Body(13 * s, true);
    const float w = ov::Width(read, words);
    const float x = std::clamp(l.mouse_x - w / 2 - 6 * s, 0.0f, place.width - w - 12 * s), y = top - 24 * s;
    list->AddRectFilled(ImVec2(x, y), ImVec2(x + w + 12 * s, y + 21 * s), ov::Colour(ov::SURFACE, 0.92f * alpha),
                        ov::CORNER_SMALL * s);
    ov::Draw(list, read, x + 6 * s, y + 4 * s, ov::Colour(ov::TEXT, alpha), words);
  }
}

bool ReplayLanesMouse(unsigned message, float x, float y, fzgx::SpectatorClick* click)
{
  std::lock_guard<std::mutex> hold(s_lanes.lock);
  Lanes& l = s_lanes;
  l.mouse_x = x;
  l.mouse_y = y;
  if (message != 0x0201 || !l.have || l.alpha < 0.5f || l.count == 0)  // (WM_LBUTTONDOWN, the lanes up)
    return false;
  const float top = l.place.top - Lane(l.place) * static_cast<float>(l.count);
  if (y < top || y >= l.place.top)
    return false;
  if (const std::size_t i = EventAt(l, x, y); i != static_cast<std::size_t>(NONE))
  {
    const fzgx::TimelineEvent& e = l.line.events[i];
    if (l.may_scrub)
      click->jump = e.step > l.line.first + BEFORE_STEPS ? e.step - BEFORE_STEPS : l.line.first;
    else
      click->follow = e.racer;
    return true;
  }
  const fzgx::u32 k = std::min<fzgx::u32>(static_cast<fzgx::u32>((y - top) / Lane(l.place)), l.count - 1);
  click->follow = l.racers[k];
  return true;
}
}  // namespace fzgx_port
