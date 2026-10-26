// SPDX-License-Identifier: GPL-3.0-or-later

#include "hfr_ahead.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace fzgx::hfr
{
namespace
{
constexpr double FIELD_MS = 1000.0 * 1001.0 / 60000.0;

Matrix Four(const Mtx& m)
{
  return {m.m[0], m.m[1], m.m[2], m.m[3], m.m[4], m.m[5], m.m[6], m.m[7], m.m[8], m.m[9], m.m[10], m.m[11], 0, 0, 0, 1};
}

double Apart(const float a[3], const double b[3])
{
  double d2 = 0;
  for (int k = 0; k < 3; ++k)
    d2 += (double(a[k]) - b[k]) * (double(a[k]) - b[k]);
  return std::sqrt(d2);
}

void Add(std::array<u64, Misses::BINS>& bins, double* most, double v, double width = 0.05)
{
  if (!(v >= 0))
    return;
  ++bins[std::min<size_t>(Misses::BINS - 1, size_t(v / width))];
  *most = std::max(*most, v);
}

// "p50 a, p99 b, most c" (each bin's upper edge, `scale` a bin)
std::string Line(const std::array<u64, Misses::BINS>& bins, double most, double scale)
{
  u64 n = 0;
  for (u64 b : bins)
    n += b;
  if (n == 0)
    return "none";
  std::string out;
  for (const double share : {0.5, 0.99})
  {
    u64 seen = 0;
    size_t k = 0;
    for (; k < bins.size(); ++k)
      if ((seen += bins[k]) >= share * double(n))
        break;
    out += std::format("p{:.0f} {:.2f}, ", share * 100, (k + 1) * scale);
  }
  return out + std::format("most {:.2f} (of {})", most, n);
}

// The length of a machine's up vector, or 0 when it is not one (not within 10% of 1, not a number).
double UpLength(const Body& b)
{
  double n2 = 0;
  for (int k = 0; k < 3; ++k)
    n2 += double(b.up[k]) * b.up[k];
  return n2 > 0.81 && n2 < 1.21 ? std::sqrt(n2) : 0;
}
}  // namespace

double RoadPart(const Body& b)
{
  const double n = UpLength(b);
  if (b.hit || !b.road || n == 0)
    return 0;
  double d = 0;
  for (int k = 0; k < 3; ++k)
    d += double(b.places[k]) * b.up[k];
  return std::isfinite(d) ? d / n : 0;
}

std::array<float, 3> MoveOf(const Body& b)
{
  if (b.hit)
    return {0, 0, 0};
  const double part = RoadPart(b);
  std::array<float, 3> r;
  for (int k = 0; k < 3; ++k)
    r[k] = part == 0 ? b.places[k] : float(b.places[k] - part * b.up[k] / UpLength(b));
  return r;
}

Mtx AheadPose(const Mtx& before, const Mtx& now, double t, const float move[3], bool turns)
{
  Mtx r = turns ? BlendPose(before, now, 1.0 + t) : now;
  for (int k = 0; k < 3; ++k)
    r.m[4 * k + 3] = float(now.m[4 * k + 3] + t * move[k]);
  return r;
}

Mtx AheadView(const Mtx& before, const Mtx& now, double t, bool turns)
{
  if (turns)
    return BlendView(before, now, 1.0 + t);
  double a[3], b[3];
  EyeOf(before, a);
  EyeOf(now, b);
  Mtx r = now;
  for (int row = 0; row < 3; ++row)
  {
    double v = 0;
    for (int c = 0; c < 3; ++c)
      v -= double(r.m[4 * row + c]) * (b[c] + t * (b[c] - a[c]));
    r.m[4 * row + 3] = float(v);
  }
  return r;
}

InBetween Ahead(const Basis& b, double t)
{
  InBetween r;
  r.fraction = float(t);
  const Mtx view = AheadView(b.view_before, b.view_now, t, b.view_turns);
  r.camera = Four(CameraCorrection(b.view_now, view));
  const Matrix still = Four({{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0}});
  for (u32 i = 0; i < SEATS; ++i)
  {
    const Body& m = b.body[i];
    if (!m.ok)
    {
      r.machine[i] = r.camera;
      r.motion[i] = still;
      continue;
    }
    const Mtx pose = AheadPose(m.before, m.now, t, MoveOf(m).data(), m.turns);
    r.machine[i] = Four(BodyCorrection(b.view_now, view, m.now, pose));
    r.motion[i] = Four(BodyMotion(b.view_now, m.now, pose));
  }
  return r;
}

void Miss(Misses& m, const Basis& guess, const Mtx& now_view, const std::array<Mtx, SEATS>& now_pose,
          const std::array<bool, SEATS>& ok, u32 followed)
{
  for (u32 i = 0; i < SEATS; ++i)
  {
    const Body& b = guess.body[i];
    if (!b.ok || !ok[i])
      continue;
    const double at[3] = {now_pose[i].m[3], now_pose[i].m[7], now_pose[i].m[11]};
    float by_rule[3], by_places[3], uncut[3];
    const std::array<float, 3> move = MoveOf(b);
    for (int k = 0; k < 3; ++k)
    {
      by_rule[k] = b.now.m[4 * k + 3] + move[k];
      by_places[k] = b.now.m[4 * k + 3] + b.places[k];
      uncut[k] = b.hit ? b.now.m[4 * k + 3] : by_places[k];
    }
    const double rule = Apart(by_rule, at);
    Add(m.rule, &m.most_rule, rule);
    Add(m.places, &m.most_places, Apart(by_places, at));
    Add(m.uncut, &m.most_uncut, Apart(uncut, at));
    m.hits += b.hit;
    if (i != followed)
      continue;
    Add(m.followed, &m.most_followed, rule);
    const double step = std::sqrt(double(b.places[0]) * b.places[0] + double(b.places[1]) * b.places[1] +
                                  double(b.places[2]) * b.places[2]);
    if (step > 0.5)
      Add(m.followed_ms, &m.most_followed_ms, rule / step * FIELD_MS);
  }
  const Mtx view = AheadView(guess.view_before, guess.view_now, 1.0, guess.view_turns);
  double a[3], b[3];
  EyeOf(view, a);
  EyeOf(now_view, b);
  const float eye[3] = {float(a[0]), float(a[1]), float(a[2])};
  Add(m.eye, &m.most_eye, Apart(eye, b));
  Add(m.turn, &m.most_turn, Arc(view, now_view) * 57.29578);
}

std::string MissLine(const Misses& m)
{
  return std::format("a guess one step ahead against the step that came: every machine by the rule {} units, by the "
                     "rule without the road's part taken out {}, by its last two places alone {}; the followed "
                     "machine {} units, {} ms of its travel; the camera's eye {} units, its turn {} degrees; {} of "
                     "the guesses were of machines hit in their step",
                     Line(m.rule, m.most_rule, 0.05), Line(m.uncut, m.most_uncut, 0.05),
                     Line(m.places, m.most_places, 0.05),
                     Line(m.followed, m.most_followed, 0.05), Line(m.followed_ms, m.most_followed_ms, 0.05),
                     Line(m.eye, m.most_eye, 0.05), Line(m.turn, m.most_turn, 0.05), m.hits);
}

void Cut(Cuts& c, const Basis& b, u32 count)
{
  for (u32 i = 0; i < std::min(count, SEATS); ++i)
  {
    const Body& m = b.body[i];
    if (!m.ok)
      continue;
    ++c.guessed;
    if (m.hit || !m.road)
    {
      ++(m.hit ? c.hit : c.air);
      continue;
    }
    ++c.road;
    c.not_up += UpLength(m) == 0;
    Add(c.part, &c.most, std::fabs(RoadPart(m)), 0.01);
  }
}

std::string CutLine(const Cuts& c)
{
  return std::format("the road's rule: of {} machines guessed forward {} hit, {} in the air, {} on the road ({} of "
                     "them with an up vector that is not one, left whole): their moves' part along the road's normal "
                     "taken out {} units",
                     c.guessed, c.hit, c.air, c.road, c.not_up, Line(c.part, c.most, 0.01));
}
}  // namespace fzgx::hfr
