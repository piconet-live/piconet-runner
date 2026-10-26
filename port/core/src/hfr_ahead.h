// The guesses forward from a step (fzgx/hfr.h): each machine's pose and the chase camera's view a fraction of a
// field after the step, from the step and the one before, and how far such a guess missed the step that came.
// Not simulation state: nothing here needs to be the same on two hosts.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <string>

#include "fzgx/hfr.h"
#include "hfr_blend.h"

namespace fzgx::hfr
{
// A machine at the step and the one before.
struct Body
{
  Mtx before{}, now{};
  float places[3]{};     // its move from the step before (the last two places)
  float up[3]{};         // its up vector at the step (+0x1BC: the road's normal under it, as the race keeps it)
  bool road = false;     // on the road at the step (its flags' bit 0x2 clear: some hover corner finds the road)
  bool ok = false;       // not jumped (hfr.cpp's guards): guessed forward; else kept where the step has it
  bool turns = false;    // its turn a step is small enough to go on with (MOST_TURN)
  bool hit = false;      // a wall or a machine hit it in the step (the game's counts of both): not moved on
};

// The step's machines and its chase camera, and the one before.
struct Basis
{
  Mtx view_before{}, view_now{};
  bool view_turns = false;
  std::array<Body, SEATS> body{};
};

// A turn a step beyond which nothing is turned further (radians): a guess would be the shorter arc's of a turn that
// may have gone the other way.
constexpr double MOST_TURN = 0.785;
// The camera's (radians): its turns are slower and smoother than a machine's; a faster one is a swing whose guess
// would overshoot (hfr.cpp cuts at 30 degrees a step).
constexpr double MOST_VIEW_TURN = 0.35;

// A machine's move a step ahead: its move from the step before, none when it was hit in the step (its way after a
// hit is not that of the step before: not moved on rather than into the wall). Its momentum (+0x94 / its weight) was
// tried first and missed more: one step ahead p99 4 units against 0.7 to 0.9 by the last two places (gate hpg3).
// On the road the move loses its part along the machine's up vector (RoadPart), the rollback smoothing's rule
// (port/image/routines/smooth.py): on a banked or curved road the straight move from the last two places is a
// chord of the surface, and carried on it floats the machine (and its shadow) off the road or into it.
std::array<float, 3> MoveOf(const Body& b);
// The part of the move from the step before along the machine's up vector (signed, units) that MoveOf leaves out:
// all of it for a machine on the road, none for one in the air or hit, or when its up vector is not one (a length
// not within 10% of 1, not a number).
double RoadPart(const Body& b);
// A pose `t` steps after the step: its turn a step on by the shorter arc (when `turns`), its place moved by `move`.
Mtx AheadPose(const Mtx& before, const Mtx& now, double t, const float move[3], bool turns);
// A view `t` steps after the step: its eye on in a straight line, its turn on by the shorter arc (when `turns`).
Mtx AheadView(const Mtx& before, const Mtx& now, double t, bool turns);
// The corrections of a picture `t` of a field after the step's own picture, against the step's view.
InBetween Ahead(const Basis& b, double t);

// How far the guesses one step ahead missed the step that came (one whole field: the farthest a picture is guessed
// ahead is 3/4 of it at 240 Hz), in world units and in milliseconds of the machine's own travel.
struct Misses
{
  static constexpr int BINS = 400;          // 0.05 units (or ms) each, the last one all beyond
  // every machine's by the rule (MoveOf) and by its last two places alone (hit or not); the followed machine's in
  // units and in ms of its travel; the camera's eye in units and its turn in degrees
  // and by the rule without the road's part taken out (the earlier rule, for comparison)
  std::array<u64, BINS> rule{}, places{}, followed{}, followed_ms{}, eye{}, turn{}, uncut{};
  double most_rule = 0, most_places = 0, most_followed = 0, most_followed_ms = 0, most_eye = 0, most_turn = 0,
         most_uncut = 0;
  u64 hits = 0;  // guesses of machines hit in their step (counted as any other)
};
// `guess` the basis made a step ago, `now_view` and `now_pose` the step that came, `ok` its machines that did not
// jump, `followed` the machine the camera follows (or SEATS).
void Miss(Misses& m, const Basis& guess, const Mtx& now_view, const std::array<Mtx, SEATS>& now_pose,
          const std::array<bool, SEATS>& ok, u32 followed);
// "every machine by the rule p50 a, p99 b, most c (of n) units; ..." for the log's line.
std::string MissLine(const Misses& m);

// How often the road's rule took a part out of a guessed move: of the machines guessed forward (not jumped, among
// the race's `count`), those hit, those in the air, those on the road (and of those, the ones whose up vector is not
// one: left whole); the parts taken out (0.01 units a bin).
struct Cuts
{
  std::array<u64, Misses::BINS> part{};
  double most = 0;
  u64 guessed = 0, hit = 0, air = 0, road = 0, not_up = 0;
};
void Cut(Cuts& c, const Basis& b, u32 count);
// "the road's rule: ..." for the log's line.
std::string CutLine(const Cuts& c);
}  // namespace fzgx::hfr
