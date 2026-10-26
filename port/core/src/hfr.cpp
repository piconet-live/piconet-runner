// The pictures between two steps: fzgx/hfr.h. The state read and the guards are those of the frame-interpolation spike;
// the corrections are made against the step's own view and poses, each guessed forward
// (hfr_ahead.h).
// Offsets from main.rel's .bss (B) are the game's; addresses come from the facts table.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fzgx/hfr.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <format>
#include <memory>

#include "game_facts.h"
#include "hfr_ahead.h"
#include "hfr_blend.h"
#include "internal.h"

namespace fzgx::hfr
{
namespace
{
constexpr u32 B = game::REL_BSS_AT;
constexpr u32 MACHINES = B + 0x6D838, CAMERA_OBJECT = B + 0xF68, CAMERA_RECORDS = B + 0xF6C;
// a chase record's view matrix is at +0xDC (the one fn_1_5948 copies for the drawing), its camera matrix at +0xAC
constexpr u32 BLOCK = 0x620, POSE = 0x14C, RECORD = 0x1FC;
// in a machine's block: its flags, its speed (km/h), the frames since its start or restore, its flags 2 (the
// machine-state map, port/facts/machine-state.csv)
constexpr u32 FLAGS_AT = 0x0, SPEED_AT = 0x17C, SINCE_AT = 0x47C, FLAGS2_AT = 0x58C;
constexpr double FIELD_MS = 1000.0 * 1001.0 / 60000.0;

// The guards' numbers (Judge).
// A machine's speed is 216 x |momentum| / weight km/h (+0x17C, written at its step's start), so speed / 216 is the
// distance it covers in a step. A move further than twice that, and JUMP_MARGIN units more, is a jump.
constexpr double SPEED_PER_STEP_DISTANCE = 216.0, JUMP_MARGIN = 5.0;
// unless it goes on as the step before's move did: within a quarter of that move, and at least 5 units
constexpr double STEADY_SHARE = 0.25, STEADY_MARGIN = 5.0;
// The flag bits whose change in a step is a jump, in the order Report says them (Run::kept_bit): the game's
// "destroyed" (fn_1_25004 sets it), "retired" (out of the race: the ranking's), retired or destroyed, and out of
// energy (its inputs are zeroed).
constexpr u32 FLAG_DESTROYED = 0x40000000u, FLAG_RETIRED = 0x08000000u, FLAG_OUT = 0x800u, FLAG_BROKEN_DOWN = 0x80u;
constexpr std::array<u32, 4> JUMP_FLAGS = {FLAG_DESTROYED, FLAG_RETIRED, FLAG_OUT, FLAG_BROKEN_DOWN};
constexpr u32 JUMP_FLAG_MASK = FLAG_DESTROYED | FLAG_RETIRED | FLAG_OUT | FLAG_BROKEN_DOWN;
static_assert(JUMP_FLAG_MASK == 0x48000880u);
// flags 2 (+0x58C): out of contact (a fall, a destruction)
constexpr u32 FLAGS2_OUT_OF_CONTACT = 0x10u;
// A camera that jumped: its eye moved more than 3 times the followed machine's move and 10 units, or it turned more
// than 30 degrees (in radians).
constexpr double CAMERA_STEP_SHARE = 3.0, CAMERA_MARGIN = 10.0, MOST_CAMERA_TURN = 0.5236;

struct Machine
{
  Mtx pose;
  u32 flags = 0, since = 0, flags2 = 0;
  float speed = 0;
  float up[3]{};            // +0x1BC its up vector (the road's normal under it)
  u8 walls = 0, knock = 0;  // +0x213 its wall hits, +0x4C4 the countdown after a machine's hit
};

struct Camera
{
  bool chase = false;
  u8 mode = 0, follow = 0;
  u32 state = 0;
  float fov[2]{}, eye[3]{};
  Mtx view_a{}, view_b{};
};

struct State
{
  bool valid = false;
  u32 passes = 0, machines = 0, records = 0, viewports = 0, count = 0;
  Machine machine[SEATS];
  Camera camera[4];
};

// Why a pass has its true picture only (Report; the runs of such passes in the log)
enum Why : u32
{
  NO_REFRESH,       // the display shows no picture between (60 Hz)
  NOTHING_BEFORE,   // the first pass, after a pass that may draw nothing (paused), or state not readable
  SAME_STEP,        // the step before shown again (the scrubber's held picture: paused, a frame step)
  OTHER_STEP,       // not the next step (a scrub, a jump, steps run again)
  MOVED_IN_MEMORY,  // the machines' or cameras' blocks moved (a new race)
  SPLIT_SCREEN,     // more than one viewport
  NOT_CHASE,        // the camera is not the chase camera (the intro's, the results', the TV's)
  CAMERA_CHANGED,   // its kind, the racer it follows or its state changed
  FOLLOWED_JUMPED,  // the machine it follows jumped (a respawn)
  CAMERA_JUMPED,    // its eye or its turn went further than the followed machine can take it
  WHYS
};
constexpr const char* WHY_SAID[WHYS] = {"no refresh between",  "nothing before it", "the same step again",
                                        "another step",        "moved in memory",   "split screen",
                                        "not the chase camera", "a camera change",   "the followed machine jumped",
                                        "the camera jumped"};
constexpr u32 MOST_GAP_LINES = 80, MOST_FAR_LINES = 60;

struct Run
{
  bool on = false;
  u32 fixed = 0;          // debug.drawnFrames
  double per_step = 1;    // pictures a step; 0: the display's
  Video video;
  double phase = 0;
  State before, now;
  bool machine_ok[SEATS]{}, camera_ok[4]{};
  Why camera_why = NOTHING_BEFORE;  // camera 0's, when not ok
  u64 requests = 0, pictures = 0, passes = 0, machine_jumps = 0, camera_jumps = 0, blocked = 0, none = 0;
  // the machines kept where they were, by the first reason: an older "since", a flag bit, the +0x58C bit, the move
  u64 kept_since = 0, kept_flags = 0, kept_flags2 = 0, kept_far = 0, passed_steady = 0;
  // the machines' places a step before `before` (its passes less 1, when valid): each machine's move in the step
  // before, for the guard's steady rule
  std::array<std::array<float, 3>, SEATS> older{};
  bool older_valid = false;
  std::array<u64, 4> kept_bit{};  // the passes in which each of JUMP_FLAGS changed in a machine
  u32 far_lines = 0;  // lines said for a machine that moved too far (the first few)
  u64 why[WHYS]{};
  std::shared_ptr<const Basis> last;  // the last request's: how far its guess missed, at the next pass
  Misses misses;
  Cuts cuts;
  // a run of passes with no picture between: its first pass, the step then, its reason, its length, the camera mode
  struct Gap
  {
    u64 pass = 0;
    u32 step = 0, length = 0, mode = 0;
    Why why = NO_REFRESH;
    double eye = 0, room = 0, arc = 0;  // a camera that jumped: its eye's move, what was allowed, its turn
  } gap;
  u64 gaps[4]{};  // runs of 1, 2 to 4, 5 to 59, 60 and more passes
  u32 gap_lines = 0;
} s;

float F(const Guest& g, u32 a)
{
  const u32 v = g.Read32(a);
  float f;
  std::memcpy(&f, &v, 4);
  return f;
}

Mtx ReadMtx(const Guest& g, u32 a)
{
  Mtx r;
  for (int i = 0; i < 12; ++i)
    r.m[i] = F(g, a + 4 * i);
  return r;
}

bool InRam(const Guest& g, u32 a, u32 n)
{
  return a != 0 && g.Ram(a, n) != nullptr;
}

void Take(const Guest& g, State& st)
{
  st.valid = false;
  st.passes = g.Read32(game::GAME_PASSES);
  st.machines = g.Read32(MACHINES);
  st.records = g.Read32(CAMERA_RECORDS);
  st.count = std::min<u32>(g.Read8(game::MACHINE_COUNT_AT), SEATS);
  const u32 object = g.Read32(CAMERA_OBJECT);
  if (!InRam(g, st.machines, SEATS * BLOCK) || !InRam(g, object, 0x50) ||
      (st.records && !InRam(g, st.records, 4 * RECORD)))
    return;
  for (u32 i = 0; i < SEATS; ++i)
  {
    const u32 m = st.machines + i * BLOCK;
    Machine& a = st.machine[i];
    a = {ReadMtx(g, m + POSE), g.Read32(m + FLAGS_AT), g.Read32(m + SINCE_AT), g.Read32(m + FLAGS2_AT), F(g, m + SPEED_AT)};
    for (int k = 0; k < 3; ++k)
      a.up[k] = F(g, m + 0x1BC + 4 * k);
    a.walls = g.Read8(m + 0x213);
    a.knock = g.Read8(m + 0x4C4);
  }
  const u8 mode = g.Read8(object + 0x48);
  st.viewports = mode == 0xB ? 1 : (g.Read32(object) >> 31) != 0 ? 1 : std::min<u32>(g.Read8(object + 0x49), 4);
  for (u32 v = 0; v < 4; ++v)
  {
    Camera& c = st.camera[v];
    // fn_1_5948 takes the TV record in modes 9 and 10 and in mode 6: only the chase views are blended
    c.chase = st.records != 0 && v < st.viewports && mode != 9 && mode != 10 && mode != 6;
    c.mode = mode;
    if (!c.chase)
      continue;
    const u32 r = st.records + v * RECORD;
    c.follow = g.Read8(r + 2);
    c.state = g.Read32(r + 0xA8);
    c.fov[0] = F(g, r + 8);
    c.fov[1] = F(g, r + 0xC);
    for (int k = 0; k < 3; ++k)
      c.eye[k] = F(g, r + 0x10 + 4 * k);
    c.view_a = ReadMtx(g, r + 0xAC);
    c.view_b = ReadMtx(g, r + 0xDC);
  }
  st.valid = true;
}

double Distance2(const Mtx& a, const Mtx& b)
{
  double d2 = 0;
  for (int k = 3; k < 12; k += 4)
    d2 += (double(b.m[k]) - a.m[k]) * (double(b.m[k]) - a.m[k]);
  return d2;
}

// The guards: a machine that jumped stays where the old frame has it in the pictures between; a camera that jumped
// (a cut) leaves the pass with its true picture only. Each machine and each camera is judged to a verdict
// (JudgeMachine, JudgeCamera), which Count records: the guards' state, the counters Report says, the log's lines.

// Why a machine is kept where it was, the first that holds (Run's kept_ counters, in this order)
enum class Kept : u8
{
  NO,      // it moves in the pictures between
  SINCE,   // its "since" went back (a restore, a respawn)
  FLAGS,   // one of JUMP_FLAGS changed
  FLAGS2,  // FLAGS2_OUT_OF_CONTACT changed
  FAR,     // it moved further than its speed takes it, and not steadily
};

struct MachineVerdict
{
  Kept kept = Kept::NO;
  bool passed_steady = false;  // it moved past the speed's limit, and on as the step before's move: it moves
  u32 changed = 0;             // which of JUMP_FLAGS changed
  double limit = 0;            // how far its speed takes it in a step, and the margin
};

// Whether a move far past what its speed allows goes on as the step before's did: the change of the move is within
// a quarter of the move before, and STEADY_MARGIN units (a machine that really moves that fast passes, a jump does
// not). The change's square; -1: not steady, or no step before.
double SteadyChange2(u32 i, const Machine& a, const Machine& b)
{
  if (!s.older_valid)
    return -1;
  double before2 = 0, change2 = 0;
  for (int k = 0; k < 3; ++k)
  {
    const double was = double(a.pose.m[4 * k + 3]) - s.older[i][k], is = double(b.pose.m[4 * k + 3]) - a.pose.m[4 * k + 3];
    before2 += was * was;
    change2 += (is - was) * (is - was);
  }
  const double room = std::max(STEADY_MARGIN, STEADY_SHARE * std::sqrt(before2));
  return change2 <= room * room ? change2 : -1;
}

MachineVerdict JudgeMachine(u32 i)
{
  const Machine &a = s.before.machine[i], &b = s.now.machine[i];
  MachineVerdict v;
  v.limit = 2.0 * std::fabs(double(b.speed)) / SPEED_PER_STEP_DISTANCE + JUMP_MARGIN;
  v.changed = (b.flags ^ a.flags) & JUMP_FLAG_MASK;
  const bool fast = Distance2(a.pose, b.pose) > v.limit * v.limit;
  const bool steady = SteadyChange2(i, a, b) >= 0;
  v.passed_steady = fast && steady;
  v.kept = b.since < a.since                                ? Kept::SINCE
           : v.changed != 0                                 ? Kept::FLAGS
           : ((b.flags2 ^ a.flags2) & FLAGS2_OUT_OF_CONTACT) ? Kept::FLAGS2
           : fast && !steady                                ? Kept::FAR
                                                            : Kept::NO;
  return v;
}

// One of the first lines of a machine kept for its move: where it is in the view (x right, y up, z towards the eye),
// on the screen or not.
void SayFar(u32 i, const MachineVerdict& v)
{
  const Machine &a = s.before.machine[i], &b = s.now.machine[i];
  const Mtx& view = s.now.camera[0].view_b;
  float at[3];
  for (int r = 0; r < 3; ++r)
    at[r] = view.m[4 * r] * b.pose.m[3] + view.m[4 * r + 1] * b.pose.m[7] + view.m[4 * r + 2] * b.pose.m[11] + view.m[4 * r + 3];
  Notice("fzgx: high refresh rate: machine {} kept where it was at pass {}: it moved {:.1f} units in a step, {:.1f} "
         "allowed (its speed {:.1f}, flags {:08X}, +0x58C {:08X}); in the view at {:.0f} {:.0f} {:.0f}", i,
         s.now.passes, std::sqrt(Distance2(a.pose, b.pose)), v.limit, b.speed, b.flags, b.flags2, at[0], at[1], at[2]);
}

// A machine's verdict in a pass that follows the one before (`next`), or not: the guard's state, and in the next
// pass the counters and the first lines of moves too far.
void Count(u32 i, const MachineVerdict& v, bool next)
{
  s.machine_ok[i] = next && v.kept == Kept::NO;
  if (!next)
    return;
  s.passed_steady += v.passed_steady;
  if (v.kept != Kept::NO)
  {
    ++s.machine_jumps;
    ++(v.kept == Kept::SINCE ? s.kept_since : v.kept == Kept::FLAGS ? s.kept_flags : v.kept == Kept::FLAGS2 ? s.kept_flags2 : s.kept_far);
  }
  if (v.kept == Kept::FAR && s.far_lines < MOST_FAR_LINES)
  {
    ++s.far_lines;
    SayFar(i, v);
  }
  for (size_t k = 0; k < JUMP_FLAGS.size(); ++k)
    s.kept_bit[k] += (v.changed & JUMP_FLAGS[k]) != 0;
}

struct CameraVerdict
{
  Why why = WHYS;                     // WHYS: it may be blended
  double eye = 0, room = 0, arc = 0;  // its eye's move, what was allowed, its turn (when it was measured)
};

// A camera, after every machine is judged (it follows one).
CameraVerdict JudgeCamera(u32 v, bool next)
{
  const State &o = s.before, &n = s.now;
  const Camera &a = o.camera[v], &b = n.camera[v];
  if (!o.valid || !n.valid)
    return {NOTHING_BEFORE};
  if (n.passes == o.passes)
    return {SAME_STEP};
  if (n.passes != o.passes + 1)
    return {OTHER_STEP};
  if (!next)
    return {MOVED_IN_MEMORY};
  if (!a.chase || !b.chase)
    return {NOT_CHASE};
  if (a.mode != b.mode || a.follow != b.follow || a.state != b.state || b.follow >= SEATS)
    return {CAMERA_CHANGED};
  if (!s.machine_ok[b.follow])
    return {FOLLOWED_JUMPED};
  double eye2 = 0;
  for (int k = 0; k < 3; ++k)
    eye2 += (double(b.eye[k]) - a.eye[k]) * (double(b.eye[k]) - a.eye[k]);
  const double step2 = Distance2(o.machine[b.follow].pose, n.machine[b.follow].pose);
  CameraVerdict c;
  c.eye = std::sqrt(eye2);
  c.room = CAMERA_STEP_SHARE * std::sqrt(step2) + CAMERA_MARGIN;
  c.arc = Arc(a.view_a, b.view_a);
  c.why = c.eye > c.room || c.arc > MOST_CAMERA_TURN ? CAMERA_JUMPED : WHYS;
  return c;
}

void Count(u32 v, const CameraVerdict& c, bool next)
{
  s.camera_ok[v] = c.why == WHYS;
  if (v == 0)
  {
    s.camera_why = c.why;
    if (c.why == CAMERA_JUMPED)
      s.gap.eye = c.eye, s.gap.room = c.room, s.gap.arc = c.arc;
  }
  s.camera_jumps += next && s.now.camera[v].chase && c.why != WHYS;
}

void Judge()
{
  const State &o = s.before, &n = s.now;
  const bool next = o.valid && n.valid && n.passes == o.passes + 1 && n.machines == o.machines && n.records == o.records;
  for (u32 i = 0; i < SEATS; ++i)
    Count(i, JudgeMachine(i), next);
  for (u32 v = 0; v < 4; ++v)
    Count(v, JudgeCamera(v, next), next);
}

// A pass with its true picture only, for this reason: counted, and runs of them said in the log (the first ones).
void Gap(const Guest& g, Why why)
{
  ++s.none;
  ++s.why[why];
  Run::Gap& r = s.gap;
  if (r.length == 0)
  {
    r.pass = s.passes;
    r.step = g.Read32(layout::STEP_AT);
    r.why = why;
    r.mode = s.now.valid ? s.now.camera[0].mode : 0xFF;
  }
  ++r.length;
}

// A pass with pictures between: the run before it ends.
void GapEnds()
{
  Run::Gap& r = s.gap;
  if (r.length == 0)
    return;
  ++s.gaps[r.length == 1 ? 0 : r.length < 5 ? 1 : r.length < 60 ? 2 : 3];
  if (r.why != NO_REFRESH && s.gap_lines < MOST_GAP_LINES)
  {
    ++s.gap_lines;
    Notice("fzgx: high refresh rate: {} pass{} with no picture between from pass {} (before step {}): {}, camera "
           "mode {}{}", r.length, r.length == 1 ? "" : "es", r.pass, r.step, WHY_SAID[r.why], r.mode,
           r.why == CAMERA_JUMPED ? std::format(" (its eye moved {:.1f} units, {:.1f} allowed; it turned {:.1f} "
                                                "degrees)", r.eye, r.room, r.arc * 57.29578)
                                  : std::string());
  }
  r = {};
}

// The step's machines and camera and the one before's, for the guesses forward (hfr_ahead.h): a machine moves on by
// its move from the step before, or not at all when a wall or a machine hit it in the step (the game's counts); on
// the road (flags bit 0x2 clear) less its part along its up vector.
std::shared_ptr<const Basis> BasisOf()
{
  const State &o = s.before, &n = s.now;
  auto b = std::make_shared<Basis>();
  b->view_before = o.camera[0].view_b;
  b->view_now = n.camera[0].view_b;
  b->view_turns = Arc(b->view_before, b->view_now) < MOST_VIEW_TURN;
  for (u32 i = 0; i < SEATS; ++i)
  {
    const Machine &a = o.machine[i], &m = n.machine[i];
    Body& body = b->body[i];
    body.before = a.pose;
    body.now = m.pose;
    body.ok = s.machine_ok[i];
    body.turns = Arc(a.pose, m.pose) < MOST_TURN;
    body.hit = m.walls != a.walls || m.knock > a.knock;
    body.road = (m.flags & 0x2u) == 0;
    for (int k = 0; k < 3; ++k)
    {
      body.places[k] = m.pose.m[4 * k + 3] - a.pose.m[4 * k + 3];
      body.up[k] = m.up[k];
    }
  }
  return b;
}

void Plan(Guest& g, bool blocked)
{
  ++s.passes;
  if (blocked)
  {
    ++s.blocked;
    s.now.valid = false;
    GapEnds();
    return;
  }
  Take(g, s.now);
  Judge();
  // how far the last request's guess one step ahead missed this step (the camera not cut, the next step)
  if (s.last && s.now.valid && s.before.valid && s.now.passes == s.before.passes + 1 && s.camera_ok[0])
  {
    std::array<Mtx, SEATS> poses;
    std::array<bool, SEATS> ok;
    for (u32 i = 0; i < SEATS; ++i)
      poses[i] = s.now.machine[i].pose, ok[i] = s.machine_ok[i];
    Miss(s.misses, *s.last, s.now.camera[0].view_b, poses, ok, s.now.camera[0].follow);
  }
  s.last.reset();
  std::vector<float> plan;
  if (s.fixed != 0)
  {
    for (u32 j = 1; j <= s.fixed; ++j)
      plan.push_back(float(j) / float(s.fixed + 1));
  }
  else
  {
    double per_step = s.per_step;
    if (per_step == 0)
      per_step = s.video.display_hz ? s.video.display_hz() * FIELD_MS / 1000.0 : 1.0;
    plan = Fractions(per_step, &s.phase);
  }
  // (more than the emulator draws, MOST_FRAMES: it draws them evenly spread, as it does when they do not fit its
  // time: VideoCommon/FrameRedraw.cpp Even)
  // nothing to blend from (the first pass, after a pause or a jump of passes), a camera cut, split screen: the
  // pass has its true picture only
  const Why why = plan.empty()                         ? NO_REFRESH
                  : !s.before.valid || !s.now.valid     ? NOTHING_BEFORE
                  : s.now.viewports != 1                ? SPLIT_SCREEN
                  : !s.camera_ok[0]                     ? s.camera_why
                                                        : WHYS;
  if (why != WHYS)
    return Gap(g, why);
  GapEnds();
  Request q;
  const Mtx& view = s.now.camera[0].view_b;
  for (u32 i = 0; i < SEATS; ++i)
  {
    const Mtx& m = s.now.machine[i].pose;
    if (i < s.now.count)
    {
      q.machines |= 1u << i;
      for (int r = 0; r < 3; ++r)
        q.at[i][r] = float(double(view.m[4 * r]) * m.m[3] + double(view.m[4 * r + 1]) * m.m[7] +
                           double(view.m[4 * r + 2]) * m.m[11] + view.m[4 * r + 3]);
    }
  }
  q.fractions = plan;
  s.last = BasisOf();
  Cut(s.cuts, *s.last, s.now.count);
  q.predict = [basis = s.last](float t) { return Ahead(*basis, t); };
  ++s.requests;
  s.pictures += q.fractions.size();
  if (s.video.request)
    s.video.request(std::move(q));
}
}  // namespace

std::vector<float> Fractions(double per_step, double* phase)
{
  std::vector<float> out;
  if (!(per_step > 1.05))
  {
    *phase = 0;
    return out;
  }
  const double every = 1.0 / per_step;
  if (!(*phase >= 0 && *phase < every))
    *phase = 0;
  // the refreshes after the true picture (0) and before the next (1); one within 2% of either is that picture's
  double at = *phase;
  for (; at < 1.0; at += every)
    if (at > 0.02 && at < 0.98)
      out.push_back(float(at));
  *phase = at - 1.0;
  if (!(*phase >= 0 && *phase < every))
    *phase = 0;
  return out;
}

void Start(const Options& options, Video video)
{
  s = Run{};
  s.video = std::move(video);
  s.fixed = options.debug.drawn_frames;
  const std::string& r = options.refresh_rate;
  s.per_step = r == "120" ? 2.0 : r == "240" ? 4.0 : r == "display" ? 0.0 : 1.0;
  if (options.debug.redraw_per_step > 0)
    s.per_step = options.debug.redraw_per_step;  // (a test: a display's rate this one is not)
  s.on = s.fixed != 0 || s.per_step != 1.0;
}

bool On()
{
  return s.on;
}

u32 Call(Guest& g, u32 op, u32 frame, Clock::time_point)
{
  if (!s.on)
  {
    if (op == DONE)
      s.now.valid = false;
    return 0;
  }
  if (op == PLAN)
    Plan(g, frame != 0);
  else if (op == DONE)
  {
    // (the step before's places, for the next pass's guard: only when the next pass is the next step)
    s.older_valid = s.before.valid && s.now.valid && s.now.passes == s.before.passes + 1 &&
                    s.now.machines == s.before.machines;
    for (u32 i = 0; i < SEATS; ++i)
      for (int k = 0; k < 3; ++k)
        s.older[i][k] = s.before.machine[i].pose.m[4 * k + 3];
    s.before = s.now;
  }
  return 0;
}

std::string Report()
{
  if (!s.on)
    return {};
  GapEnds();
  std::string why;
  for (u32 k = 0; k < WHYS; ++k)
    if (s.why[k] != 0)
      why += std::format("{}{} {}", why.empty() ? "" : ", ", s.why[k], WHY_SAID[k]);
  return std::format("fzgx: high refresh rate: {} passes, {} with a request for {} pictures between, {} with none "
                     "(nothing to blend from, a camera cut, split screen), {} paused or the camera's own; machines "
                     "kept where they were {} times ({} an older since, {} a flag: {} {} {} {}, {} +0x58C, {} moved "
                     "too far), {} moves past the speed's limit passed as steady, cameras {} times; the passes with none: {}; "
                     "their runs: {} "
                     "of 1 pass, {} of 2 to 4, {} of 5 to 59, {} of 60 and more",
                     s.passes, s.requests, s.pictures, s.none, s.blocked, s.machine_jumps, s.kept_since, s.kept_flags,
                     s.kept_bit[0], s.kept_bit[1], s.kept_bit[2], s.kept_bit[3],
                     s.kept_flags2, s.kept_far, s.passed_steady, s.camera_jumps,
                     why.empty() ? "none" : why, s.gaps[0], s.gaps[1], s.gaps[2], s.gaps[3]) +
         "; " + CutLine(s.cuts) + "; " + MissLine(s.misses);
}
}  // namespace fzgx::hfr

namespace fzgx
{
u32 HfrCall(u32 op, u32 frame)
{
  Guest g(TheHost(), Data());
  return hfr::Call(g, op, frame, TheHost().Now());
}
}  // namespace fzgx
