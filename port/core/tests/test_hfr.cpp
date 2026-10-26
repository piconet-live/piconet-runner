// Tests of the pictures between two steps (fzgx/hfr.h): the display's refreshes as fractions of a step, the blend of
// a pose and a view, the corrections, the guesses forward (hfr_ahead.h), and passes of the image's host call on
// plain memory: the request's fractions and guesses, no memory written, and no request without a step before.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "harness.h"

#include <cmath>
#include <cstring>

#include "../src/hfr_ahead.h"
#include "../src/hfr_blend.h"
#include "fzgx/hfr.h"

using namespace fzgx;

namespace
{
bool Near(double a, double b, double e = 1e-4)
{
  return std::fabs(a - b) <= e;
}

hfr::Mtx Turned(double angle, float x, float y, float z)  // a turn about the up axis, then a move
{
  const float c = float(std::cos(angle)), s = float(std::sin(angle));
  return {{c, 0, s, x, 0, 1, 0, y, -s, 0, c, z}};
}

void PutFloat(Guest& g, u32 a, float f)
{
  u32 v;
  std::memcpy(&v, &f, 4);
  g.Write32(a, v);
}
}  // namespace

void TestTheRefreshesBetweenTwoSteps()
{
  BEGIN_TEST();
  double phase = 0;
  CHECK(hfr::Fractions(1.0, &phase).empty());  // 60 Hz: the game's own pictures only
  const std::vector<float> two = hfr::Fractions(2.0, &phase);
  CHECK(two.size() == 1 && Near(two[0], 0.5));
  phase = 0;
  const std::vector<float> four = hfr::Fractions(4.0, &phase);
  CHECK(four.size() == 3 && Near(four[0], 0.25) && Near(four[1], 0.5) && Near(four[2], 0.75));
  // 144 Hz: 2.4 refreshes a step, two or three drawn frames a pass, always between the two true pictures, and
  // over five passes every refresh the display has
  phase = 0;
  std::size_t frames = 0;
  for (int pass = 0; pass < 5; ++pass)
  {
    const std::vector<float> f = hfr::Fractions(2.4, &phase);
    CHECK(f.size() == 2 || f.size() == 3);
    for (float x : f)
      CHECK(x > 0 && x < 1);
    frames += f.size();
  }
  CHECK(frames == 11);  // 12 refreshes in 5 steps: the first on the true picture, 11 between
  // 360 Hz: 6 refreshes a step, 5 between, all of them within MOST_FRAMES (with a most of 3: the first three, and
  // each step's last two refreshes would show the third again)
  phase = 0;
  const std::vector<float> six = hfr::Fractions(6.0, &phase);
  CHECK(six.size() == 5 && Near(six[0], 1.0 / 6) && Near(six[4], 5.0 / 6));
  CHECK(six.size() <= hfr::MOST_FRAMES);
}

void TestTheBlendOfAPoseAndAView()
{
  BEGIN_TEST();
  const double quarter = std::acos(-1.0) / 2;
  const hfr::Mtx a = Turned(0, 0, 0, 0), b = Turned(quarter, 10, 2, -4);
  double arc = 0;
  const hfr::Mtx half = hfr::BlendPose(a, b, 0.5, &arc);
  CHECK(Near(arc, quarter, 1e-5));
  const hfr::Mtx want = Turned(quarter / 2, 5, 1, -2);
  for (int i = 0; i < 12; ++i)
    CHECK(Near(half.m[i], want.m[i], 1e-5));
  // a view: the eye moves in a straight line, not the translation column
  const hfr::Mtx va = hfr::BlendView(a, b, 0.0), vb = hfr::BlendView(a, b, 1.0), vh = hfr::BlendView(a, b, 0.5);
  double ea[3], eb[3], eh[3], e0[3], e1[3];
  hfr::EyeOf(a, e0);
  hfr::EyeOf(b, e1);
  hfr::EyeOf(va, ea);
  hfr::EyeOf(vb, eb);
  hfr::EyeOf(vh, eh);
  for (int c = 0; c < 3; ++c)
    CHECK(Near(ea[c], e0[c], 1e-4) && Near(eb[c], e1[c], 1e-4) && Near(eh[c], (e0[c] + e1[c]) / 2, 1e-4));
  // a matrix with a scale is blended element by element
  hfr::Mtx scaled = a;
  scaled.m[0] = 2;
  CHECK(Near(hfr::BlendPose(scaled, b, 0.5).m[0], (2 + b.m[0]) / 2));
}

namespace
{
// m (a 4x4 of the request) applied to a point
void Apply(const hfr::Matrix& m, const double p[3], double out[3])
{
  for (int r = 0; r < 3; ++r)
    out[r] = m[4 * r] * p[0] + m[4 * r + 1] * p[1] + m[4 * r + 2] * p[2] + m[4 * r + 3];
}

void Apply(const hfr::Mtx& m, const double p[3], double out[3])
{
  for (int r = 0; r < 3; ++r)
    out[r] = m.m[4 * r] * p[0] + m.m[4 * r + 1] * p[1] + m.m[4 * r + 2] * p[2] + m.m[4 * r + 3];
}
}  // namespace

void TestTheCorrections()
{
  BEGIN_TEST();
  const double quarter = std::acos(-1.0) / 2;
  const hfr::Mtx v_old = Turned(0.1, 3, -1, 7), v_new = Turned(0.3, -2, 0.5, 4);
  const hfr::Mtx m_old = Turned(quarter, 10, 2, -4), m_new = Turned(quarter + 0.2, 12, 2, -5);
  // a point of the world seen from the old view, corrected: as the new view sees it
  const double world[3] = {5, 6, -7};
  double seen_old[3], corrected[3], seen_new[3];
  Apply(v_old, world, seen_old);
  Apply(hfr::CameraCorrection(v_old, v_new), seen_old, corrected);
  Apply(v_new, world, seen_new);
  for (int c = 0; c < 3; ++c)
    CHECK(Near(corrected[c], seen_new[c], 1e-3));
  // a point of a body: where its new pose puts it, seen from the new view
  const double local[3] = {1, 0.5, -2};
  double at_old[3], at_new[3];
  Apply(hfr::Mul(v_old, m_old), local, at_old);
  Apply(hfr::BodyCorrection(v_old, v_new, m_old, m_new), at_old, corrected);
  Apply(hfr::Mul(v_new, m_new), local, at_new);
  for (int c = 0; c < 3; ++c)
    CHECK(Near(corrected[c], at_new[c], 1e-3));
}

void TestAPassOfPicturesBetween()
{
  BEGIN_TEST();
  TestHost host;
  Init(host, Options{});  // (the log's host: the runs of passes with no picture between are said)
  ModData data;
  Guest g(host, data);
  constexpr u32 B = game::REL_BSS_AT;
  const u32 machines = 0x80800000, object = 0x80810000, records = 0x80820000;
  g.Write32(B + 0x6D838, machines);
  g.Write32(B + 0xF68, object);
  g.Write32(B + 0xF6C, records);
  g.Write8(object + 0x49, 1);  // one viewport, the chase camera
  g.Write8(game::MACHINE_COUNT_AT, 2);
  for (u32 i = 0; i < 30; ++i)
    for (u32 k : {0u, 5u, 10u})
      PutFloat(g, machines + i * 0x620 + 0x14C + 4 * k, 1.0f);
  PutFloat(g, machines + 1 * 0x620 + 0x14C + 12, 1.0f);  // machine 1 at x 1
  for (u32 k : {0u, 5u, 10u})  // the camera's view and camera matrices: no turn
  {
    PutFloat(g, records + 0xAC + 4 * k, 1.0f);
    PutFloat(g, records + 0xDC + 4 * k, 1.0f);
  }
  Options o;
  o.refresh_rate = "240";
  std::vector<hfr::Request> asked;
  hfr::Start(o, {[&](hfr::Request q) { asked.push_back(std::move(q)); }, nullptr});
  CHECK(hfr::On());
  const auto now = std::chrono::steady_clock::now();
  // the first pass has no step before it to blend from: no request
  g.Write32(game::GAME_PASSES, 100);
  CHECK(hfr::Call(g, hfr::PLAN, 0, now) == 0);
  hfr::Call(g, hfr::DONE, 1, now);
  CHECK(asked.empty());
  // the next pass: machine 1 moved 2 units along x, the camera 1 along z
  g.Write32(game::GAME_PASSES, 101);
  PutFloat(g, machines + 1 * 0x620 + 0x14C + 12, 3.0f);
  PutFloat(g, records + 0xDC + 44, -1.0f);
  const u32 before = g.Read32(machines + 1 * 0x620 + 0x14C + 12);
  CHECK(hfr::Call(g, hfr::PLAN, 0, now) == 0);  // nothing for the image to draw
  hfr::Call(g, hfr::DONE, 1, now);
  CHECK(g.Read32(machines + 1 * 0x620 + 0x14C + 12) == before);  // nothing written
  CHECK(asked.size() == 1);
  const hfr::Request& q = asked.back();
  CHECK(q.machines == 0x3);  // two machines in the race
  CHECK(q.fractions.size() == 3 && Near(q.fractions[0], 0.25) && Near(q.fractions[2], 0.75));
  CHECK(Near(q.at[1][0], 3.0) && Near(q.at[1][2], -1.0));  // machine 1 in the step's own view
  // half a field after the step: the camera's eye half its step further on (the world z -0.5 more), machine 1 half
  // its own step further on too (x +1)
  const double p[3] = {1, 1, 1};
  double w[3], m[3];
  const hfr::InBetween half = q.predict(0.5f);
  Apply(half.camera, p, w);
  Apply(half.machine[1], p, m);
  CHECK(Near(w[0], 1.0) && Near(w[2], 0.5));
  CHECK(Near(m[0], 2.0) && Near(m[2], 0.5));
  // machine 1's own move in the step's view: half its step, whatever the camera did
  Apply(half.motion[1], p, m);
  CHECK(Near(m[0], 2.0) && Near(m[2], 1.0));
  // the guess holds its own copy of the step: the next pass changes nothing of it
  PutFloat(g, machines + 1 * 0x620 + 0x14C + 12, 9.0f);
  Apply(q.predict(0.5f).machine[1], p, m);
  CHECK(Near(m[0], 2.0));
  PutFloat(g, machines + 1 * 0x620 + 0x14C + 12, 3.0f);
  // a pass that may draw nothing (paused) asks nothing, and the next one has nothing to blend from
  g.Write32(game::GAME_PASSES, 102);
  hfr::Call(g, hfr::PLAN, 1, now);
  hfr::Call(g, hfr::DONE, 1, now);
  g.Write32(game::GAME_PASSES, 103);
  hfr::Call(g, hfr::PLAN, 0, now);
  hfr::Call(g, hfr::DONE, 1, now);
  CHECK(asked.size() == 1);
  // the scrubber's held picture (the same step again: its pass count put back with the memory) and a jump of
  // steps: no request, each counted by its reason; the next step after them has one
  g.Write32(game::GAME_PASSES, 104);
  hfr::Call(g, hfr::PLAN, 0, now);
  hfr::Call(g, hfr::DONE, 1, now);
  CHECK(asked.size() == 2);
  hfr::Call(g, hfr::PLAN, 0, now);
  hfr::Call(g, hfr::DONE, 1, now);
  g.Write32(game::GAME_PASSES, 110);
  hfr::Call(g, hfr::PLAN, 0, now);
  hfr::Call(g, hfr::DONE, 1, now);
  g.Write32(game::GAME_PASSES, 111);
  hfr::Call(g, hfr::PLAN, 0, now);
  hfr::Call(g, hfr::DONE, 1, now);
  CHECK(asked.size() == 3);
  const std::string said = hfr::Report();
  CHECK(said.find("1 the same step again, 1 another step") != std::string::npos);
  CHECK(said.find("1 paused or the camera's own") != std::string::npos);
  // 60 Hz: off, and the call answers nothing
  hfr::Start(Options{}, {});
  CHECK(!hfr::On() && hfr::Call(g, hfr::PLAN, 0, now) == 0);
}

namespace
{
// (TestTheGuessesForward's) the road's rule
void RoadsRule()
{
  // on the road the move loses its part along the machine's up vector (the road's normal); in the air, after a hit,
  // or with an up vector that is not one, it does not
  hfr::Body rb;
  rb.places[0] = 4, rb.places[1] = 1, rb.places[2] = 0.5f;
  rb.up[1] = 1;
  rb.road = true;
  CHECK(Near(hfr::RoadPart(rb), 1.0));
  CHECK(hfr::MoveOf(rb)[0] == 4.0f && hfr::MoveOf(rb)[1] == 0.0f && hfr::MoveOf(rb)[2] == 0.5f);
  rb.up[0] = -0.6f, rb.up[1] = 0.8f;  // banked: what is left lies in the road's plane, its part across kept
  const std::array<float, 3> banked = hfr::MoveOf(rb);
  CHECK(Near(hfr::RoadPart(rb), -1.6, 1e-6));
  CHECK(Near(banked[0] * -0.6 + banked[1] * 0.8, 0.0, 1e-6) && banked[2] == 0.5f);
  rb.road = false;
  CHECK(hfr::RoadPart(rb) == 0 && hfr::MoveOf(rb)[1] == 1.0f);
  rb.road = true;
  rb.up[0] = 0, rb.up[1] = 2;  // not a unit vector
  CHECK(hfr::RoadPart(rb) == 0 && hfr::MoveOf(rb)[1] == 1.0f);
  rb.up[1] = std::nanf("");
  CHECK(hfr::RoadPart(rb) == 0 && hfr::MoveOf(rb)[1] == 1.0f);
  rb.up[1] = 1;
  rb.hit = true;
  CHECK(hfr::RoadPart(rb) == 0 && hfr::MoveOf(rb)[1] == 0.0f);
  // the log's count: of the machines guessed forward (not jumped, among the race's), hit, in the air, on the road
  {
    hfr::Basis cut;
    rb.hit = false, rb.ok = true;
    cut.body[0] = rb;           // on the road: 1.0 taken out
    cut.body[1] = rb;
    cut.body[1].road = false;  // in the air
    cut.body[2] = rb;
    cut.body[2].hit = true;    // hit
    cut.body[3] = rb;
    cut.body[3].ok = false;    // jumped
    cut.body[4] = rb;
    cut.body[4].up[1] = 3;     // on the road, its up vector not one
    cut.body[5] = rb;          // not in the race (count 5)
    hfr::Cuts c;
    hfr::Cut(c, cut, 5);
    CHECK(c.guessed == 4 && c.hit == 1 && c.air == 1 && c.road == 2 && c.not_up == 1 && Near(c.most, 1.0, 1e-6));
    CHECK(hfr::CutLine(c).find("of 4 machines guessed forward 1 hit, 1 in the air, 2 on the road (1 of them") !=
          std::string::npos);
  }
}
}  // namespace

void TestTheGuessesForward()
{
  BEGIN_TEST();
  // a machine moves on by its last two places', and not at all when it was hit
  hfr::Body b;
  b.before = Turned(0, 0, 0, 0);
  b.now = Turned(0.2, 4, 0, 0);
  for (int k = 0; k < 3; ++k)
    b.places[k] = b.now.m[4 * k + 3] - b.before.m[4 * k + 3];
  CHECK(hfr::MoveOf(b)[0] == 4.0f);
  b.hit = true;
  CHECK(hfr::MoveOf(b)[0] == 0.0f);
  // the pose half a step on: its turn carried on by the shorter arc, its place by the move
  const float move[3] = {2, 0, 1};
  const hfr::Mtx ahead = hfr::AheadPose(b.before, b.now, 0.5, move, true);
  const hfr::Mtx want = Turned(0.3, 5, 0, 0.5);
  for (int i = 0; i < 12; ++i)
    CHECK(Near(ahead.m[i], want.m[i], 1e-5));
  // a turn not carried on keeps the step's
  const hfr::Mtx kept = hfr::AheadPose(b.before, b.now, 0.5, move, false);
  CHECK(Near(kept.m[0], b.now.m[0]) && Near(kept.m[3], 5.0));
  // a view: its eye on in a straight line, with and without its turn
  const hfr::Mtx va = Turned(0, 0, 0, 0), vb = Turned(0.1, 1, 0, 2);
  double e0[3], e1[3], e[3];
  hfr::EyeOf(va, e0);
  hfr::EyeOf(vb, e1);
  for (const bool turns : {true, false})
  {
    hfr::EyeOf(hfr::AheadView(va, vb, 0.75, turns), e);
    for (int c = 0; c < 3; ++c)
      CHECK(Near(e[c], e1[c] + 0.75 * (e1[c] - e0[c]), 1e-4));
  }
  // the corrections at t: the world's against the step's view; a machine that jumped stays with the world
  hfr::Basis basis;
  basis.view_before = va;
  basis.view_now = vb;
  basis.view_turns = true;
  basis.body[0] = b;
  basis.body[0].ok = true;
  basis.body[0].hit = false;
  basis.body[0].turns = true;
  basis.body[1] = basis.body[0];
  basis.body[1].ok = false;
  const hfr::InBetween r = hfr::Ahead(basis, 0.5);
  CHECK(r.machine[1] == r.camera && !(r.machine[0] == r.camera));
  // seen through the correction, a point of machine 0 is where the view half a step on sees the pose half a step on
  const double local[3] = {1, 0.5, -2};
  double at_step[3], corrected[3], at_ahead[3];
  Apply(hfr::Mul(vb, b.now), local, at_step);
  Apply(r.machine[0], at_step, corrected);
  Apply(hfr::Mul(hfr::AheadView(va, vb, 0.5, true), hfr::AheadPose(b.before, b.now, 0.5, b.places, true)), local,
        at_ahead);
  for (int c = 0; c < 3; ++c)
    CHECK(Near(corrected[c], at_ahead[c], 1e-3));
  // how far a guess missed: a machine that went on as guessed misses by nothing, one that stopped by its move
  hfr::Misses misses;
  std::array<hfr::Mtx, hfr::SEATS> poses{};
  std::array<bool, hfr::SEATS> ok{};
  poses[0] = hfr::AheadPose(b.before, b.now, 1.0, b.places, true);
  poses[2] = b.now;
  basis.body[2] = basis.body[0];
  ok[0] = ok[2] = true;
  hfr::Miss(misses, basis, hfr::AheadView(va, vb, 1.0, true), poses, ok, 2);
  CHECK(Near(misses.most_rule, 4.0, 1e-4) && Near(misses.most_eye, 0.0, 1e-4));
  CHECK(Near(misses.most_followed_ms, 1000.0 * 1001.0 / 60000.0, 1e-3));  // a whole field of its travel
  CHECK(hfr::MissLine(misses).find("most 4.00 (of 2)") != std::string::npos);
  RoadsRule();
}
