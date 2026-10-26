// The advanced spectator controls' decisions (fzgx/spectator.h): plain numbers, no game.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fzgx/spectator.h"

#include <cmath>
#include <utility>

namespace fzgx::spectator
{
bool MayScrub(Role role)
{
  return role != Role::KnockedOut;
}

bool IsChase(Kind kind)
{
  return kind == Kind::Chase1 || kind == Kind::Chase2 || kind == Kind::Chase3;
}

bool IsFixed(Kind kind)
{
  return static_cast<u32>(kind) >= static_cast<u32>(Kind::Fixed1);
}

u32 ChaseView(Kind kind)
{
  return IsChase(kind) ? static_cast<u32>(kind) - static_cast<u32>(Kind::Chase1) + 1 : 0;
}

u32 FixedType(Kind kind)
{
  return IsFixed(kind) ? static_cast<u32>(kind) - static_cast<u32>(Kind::Fixed1) : 0;
}

const char* KindName(Kind kind)
{
  switch (kind)
  {
  case Kind::Chase1: return "Chase 1";
  case Kind::Chase2: return "Chase 2";
  case Kind::Chase3: return "Chase 3";
  case Kind::Course: return "TV";
  case Kind::Auto: return "Auto";
  case Kind::Fixed1: return "Fixed 1";
  case Kind::Fixed2: return "Fixed 2";
  case Kind::Fixed3: return "Fixed 3";
  case Kind::Fixed4: return "Fixed 4";
  }
  return "";
}

View NextView(View view)
{
  return static_cast<View>((static_cast<u32>(view) + 1) % VIEWS);
}

const char* ViewName(View view)
{
  switch (view)
  {
  case View::Observer: return "Observer";
  case View::Game: return "Game HUD";
  case View::Clean: return "Clean";
  }
  return "";
}

// Course 41: 47 scenery instances with a collider are placed by the draw from the machine the game's camera
// follows ([data+0x2A83C], written by fn_1_9C724 from the viewport's camera, and only for instances drawn, so the
// camera's place counts too). A replay there replays the race only as its recorder's game
// drew it: its own racer, the chase camera with the views the recording's inputs choose. So nothing is offered.
CourseRule RuleFor(u32 course)
{
  CourseRule rule;
  if (course == 41)
  {
    rule.follow_others = false;
    rule.kinds = 0;
  }
  return rule;
}

bool KindAllowed(const CourseRule& rule, Kind kind)
{
  return (rule.kinds >> static_cast<u32>(kind) & 1) != 0;
}

bool Racing(const Racer& r)
{
  return r.present && !r.out && !r.finished;
}

ResultsPick ReadResultsPick(u32 word)
{
  if ((word >> 31) == 0)  // (the top bit: a pick was written)
    return {};
  return {true, word & 0xFF, (word >> 8) & 0xFF};
}

const char* ResultsWhyText(u32 why)
{
  switch (static_cast<ResultsWhy>(why))
  {
    case ResultsWhy::Own: return "its own machine (finished, racing or put back)";
    case ResultsWhy::Wreck: return "its own wreck, still drawn where it lies";
    case ResultsWhy::Winner: return "the winner: its own machine is gone (exploded or fallen)";
    case ResultsWhy::Course: return "its own seat: the course's rule (its scenery follows the camera's machine)";
    case ResultsWhy::Keep: return "the seat a spectator watches";
    case ResultsWhy::Alone: return "its own seat: its machine is gone and no racer is left to follow";
    case ResultsWhy::Gone: return "the winner: its own wreck went during the results";
  }
  return "an unknown reason";
}

u32 Leader(const Racers& racers)
{
  u32 best = NOBODY, best_racing = NOBODY;
  for (u32 i = 0; i < MOST; ++i)
  {
    const Racer& r = racers[i];
    if (!r.present)
      continue;
    if (best == NOBODY || r.rank < racers[best].rank)
      best = i;
    if (Racing(r) && (best_racing == NOBODY || r.rank < racers[best_racing].rank))
      best_racing = i;
  }
  return best_racing != NOBODY ? best_racing : best;
}

u32 ClosestBattle(const Racers& racers)
{
  // the racers still racing in race order
  std::array<u32, MOST> order{};
  u32 n = 0;
  for (u32 i = 0; i < MOST; ++i)
  {
    if (!Racing(racers[i]))
      continue;
    u32 at = n++;
    for (; at > 0 && racers[order[at - 1]].rank > racers[i].rank; --at)
      order[at] = order[at - 1];
    order[at] = i;
  }
  u32 behind = NOBODY;
  float least = 0;
  for (u32 k = 1; k < n; ++k)
  {
    const float gap = std::fabs(racers[order[k - 1]].distance - racers[order[k]].distance);
    if (behind == NOBODY || gap < least)
    {
      behind = order[k];
      least = gap;
    }
  }
  return behind;
}

u32 ByRank(const Racers& racers, u32 from, int by)
{
  std::array<u32, MOST> order{};
  u32 n = 0, at_from = NOBODY;
  for (u32 i = 0; i < MOST; ++i)
  {
    if (!racers[i].present)
      continue;
    u32 at = n++;
    for (; at > 0 && racers[order[at - 1]].rank > racers[i].rank; --at)
      order[at] = order[at - 1];
    order[at] = i;
  }
  if (n == 0)
    return NOBODY;
  for (u32 k = 0; k < n; ++k)
    if (order[k] == from)
      at_from = k;
  if (at_from == NOBODY)
    return order[0];
  const int to = ((static_cast<int>(at_from) + by) % static_cast<int>(n) + static_cast<int>(n)) % static_cast<int>(n);
  return order[static_cast<u32>(to)];
}

Asked Map(Role role, const Pad& now, const Pad& before)
{
  Asked a;
  const u16 pressed = static_cast<u16>(now.buttons & ~before.buttons);
  a.racer = (pressed & PAD_RIGHT) != 0 ? 1 : (pressed & PAD_LEFT) != 0 ? -1 : 0;
  a.kind = (pressed & PAD_DOWN) != 0 ? 1 : (pressed & PAD_UP) != 0 ? -1 : 0;
  const bool up = now.cstick_y >= CSTICK_PUSHED, up_before = before.cstick_y >= CSTICK_PUSHED;
  const bool down = now.cstick_y <= -CSTICK_PUSHED, down_before = before.cstick_y <= -CSTICK_PUSHED;
  a.variant = up && !up_before ? 1 : down && !down_before ? -1 : 0;
  a.leader = (pressed & PAD_X) != 0;
  a.battle = (pressed & PAD_Y) != 0;
  a.overlay = (pressed & PAD_Z) != 0;
  if (MayScrub(role))
  {
    const bool l = (now.buttons & PAD_L) != 0, r = (now.buttons & PAD_R) != 0;
    a.scrub = l == r ? 0 : l ? -1 : 1;
    a.live = (pressed & PAD_A) != 0;
    a.pause = (pressed & PAD_START) != 0;
  }
  return a;
}

Controls::Controls(Role role, u32 course, u32 first)
    : m_role(role), m_rule(RuleFor(course)), m_first(first), m_follow(first)
{
}

bool Controls::Click(u32 racer)
{
  if (racer >= MOST || (!m_rule.follow_others && racer != m_first))
    return false;
  m_clicked = racer;
  return true;
}

bool Controls::ClickKind(Kind kind)
{
  if (!KindAllowed(m_rule, kind))
    return false;
  m_clicked_kind = static_cast<int>(kind);
  return true;
}

void Controls::ClickOverlay()
{
  m_clicked_overlay = true;
}

void Controls::ClickLeader()
{
  m_clicked_leader = true;
}

void Controls::ClickBattle()
{
  m_clicked_battle = true;
}

void Controls::Seen(u32 racer)
{
  if (racer < MOST)
    m_follow = racer;
}

void Controls::Choose(u32 racer, Follow how)
{
  m_how = how;
  if (racer == NOBODY || racer == m_follow)
    return;
  if (!m_rule.follow_others && racer != m_first)
    return;
  m_follow = racer;
  m_changed = true;
}

bool Controls::SetKind(Kind kind)
{
  if (!KindAllowed(m_rule, kind) || kind == m_kind)
    return false;
  m_kind = kind;
  m_kind_chosen = true;
  m_variant = 0;
  m_kind_changed = true;
  return true;
}

Frame Controls::Next(const Pad& pad, const Racers& racers)
{
  const Asked a = Map(m_role, pad, m_before);
  m_before = pad;
  m_changed = m_kind_changed = false;
  if (a.overlay || std::exchange(m_clicked_overlay, false))
    m_view = NextView(m_view);
  const bool leader = a.leader || std::exchange(m_clicked_leader, false);
  const bool battle = a.battle || std::exchange(m_clicked_battle, false);
  // the racer: a click or a D-pad press picks one; X and Y follow the leader or the battle from then on
  if (const u32 clicked = std::exchange(m_clicked, NOBODY); clicked != NOBODY)
    Choose(clicked, Follow::Chosen);
  else if (a.racer != 0)
    Choose(ByRank(racers, m_follow, a.racer), Follow::Chosen);
  else if (leader)
    Choose(Leader(racers), Follow::Leader);
  else if (battle)
  {
    m_battle_frames = 0;
    Choose(ClosestBattle(racers), Follow::Battle);
  }
  else if (m_how == Follow::Leader)
    Choose(Leader(racers), Follow::Leader);
  else if (m_how == Follow::Battle)
  {
    // held a while, then the closest battle again; at once when the one watched is no longer racing
    const bool racing = m_follow < MOST && Racing(racers[m_follow]);
    if (++m_battle_frames >= BATTLE_HOLD || !racing)
    {
      const u32 next = ClosestBattle(racers);
      if (next != NOBODY && next != m_follow)
        m_battle_frames = 0;
      Choose(next, Follow::Battle);
    }
  }
  // the camera kind: a button, or the D-pad through the kinds the course allows
  if (const int clicked = std::exchange(m_clicked_kind, -1); clicked >= 0)
    SetKind(static_cast<Kind>(clicked));
  else if (a.kind != 0 && m_rule.kinds != 0)
  {
    u32 k = static_cast<u32>(m_kind);
    for (u32 tries = 0; tries < KINDS; ++tries)
    {
      k = (k + KINDS + static_cast<u32>(a.kind)) % KINDS;
      if (KindAllowed(m_rule, static_cast<Kind>(k)))
        break;
    }
    SetKind(static_cast<Kind>(k));
  }
  if (a.variant != 0 && IsFixed(m_kind))
  {
    const u32 n = FIXED_VARIANTS[FixedType(m_kind)];
    m_variant = (m_variant + n + static_cast<u32>(a.variant)) % n;
    m_kind_changed = true;
  }
  Frame f;
  f.follow = m_follow;
  f.kind = m_kind;
  f.kind_chosen = m_kind_chosen;
  f.variant = m_variant;
  f.follow_changed = m_changed;
  f.kind_changed = m_kind_changed;
  f.view = m_view;
  f.overlay = m_view != View::Clean;
  f.scrub = a.scrub;
  f.live = a.live;
  f.pause = a.pause;
  return f;
}
}  // namespace fzgx::spectator
