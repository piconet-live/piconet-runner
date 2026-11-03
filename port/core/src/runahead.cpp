// fzgx/runahead.h: what runs at the frame tick, and in what order. Addresses from the game's frame (main.rel's _prolog
// loop and fn_1_37AA8's draw half: archive/research/plan-frame-interpolation.md 1). Fractions, poses and cameras read
// and written here; hfr_blend.h does the blending.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fzgx/runahead.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <format>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "fzgx/core.h"
#include "fzgx/spectator.h"
#include "hfr_blend.h"
#include "internal.h"
#include "net_internal.h"

namespace fzgx::runahead
{
namespace
{
using hfr::Mtx;
using net::Rollback;
using Tick = std::chrono::steady_clock;

constexpr u32 B = game::REL_BSS_AT;
constexpr u32 T = game::REL_TEXT_AT;
constexpr u32 CONTROL = layout::CONTROL;
constexpr u32 SEATS = 30, BLOCK = game::MACHINE_SIZE, POSE = 0x14C, RECORD = 0x1FC;
constexpr u32 MACHINES = game::MACHINES_AT, CAMERA_OBJECT = B + 0xF68, CAMERA_RECORDS = B + 0xF6C;
constexpr u32 SIM_MANAGER = B + 0x3C00 + 0x21F88, DRAW_MANAGER = B + 0x3C00 + 0x220A4;
constexpr u32 PAUSE_WORD = B + 0x26B50, HUD_OFF = B + 0x3C10;
constexpr u32 COUNTERS[] = {0x801A66A0, 0x801A66A4, 0x801A66A8, 0x801A66C0, 0x801A66C8};
constexpr u32 RUN_BEFORE_AT = CONTROL + 492, LAST_STEP = CONTROL + 460, MUTE = CONTROL + 488, RUNNING = CONTROL + 120;
// AURORA_RUNAHEAD_SKIP (off by default): the step ahead leaves out the simulation manager's task lists whose work it
// undoes unseen. Bit n = list n of the manager's list mask (+0x10, read by fn_1_407C; the test switch +312 masks the
// same way). Everything the step ahead writes is undone but what Take reads (the machines' pose, flags, speed and the
// camera), so a list after the car task that neither the camera nor the machines' pose reads is work thrown away:
// 10 the burner, 11 the effect update, 23 BackGroundCalc (its writes come after the car task and are undone); and the
// flag-0 lists a frame's last step runs: 5 the machine sound update (already returns at once in a muted step: mute.py),
// 12 Check and Face (the face board's memory is not undone: it advanced twice a frame), 22 the per-player
// presentation, 25 GameResultCtrl, 31 RumbleMain. Kept: 0 to 9 (the race; 8 PhysObjCalc pushes machines on courses
// 37, 38, 41), 13 (the Starter beside the HUD timers), 20 and 21 (the cameras, CameraExec, StageCalc).
// AURORA_RUNAHEAD_SKIP=1 that set; a number (0x...) another mask, for tests.
constexpr u32 SKIP_LISTS = 1u << 5 | 1u << 10 | 1u << 11 | 1u << 12 | 1u << 22 | 1u << 23 | 1u << 25 | 1u << 31;
constexpr u32 LIST_MASK = 0x10;  // the simulation manager's own: list n is left out with bit n
constexpr u32 SMOOTH_ENTRY = 48, SMOOTH_OFFSET = 12, SMOOTH_WRITTEN = 36;
constexpr u32 SHADOWS = B + 0x6D83C + 8, SHADOW_SIZE = 0x4C, SHADOW_PLACE = 0x18;
constexpr double FIELD_MS = 1000.0 * 1001.0 / 60000.0;
constexpr double MARGIN_MS = 1.5;  // room kept before the next field beside the step and its drawing
constexpr u32 HIGH_WINDOW = 120;  // frames a cost counts as recent: two seconds
double s_high_share = 0.5;  // AURORA_TEST_HIGH_SHARE: a test's choice
constexpr double LATE_NOTICED_MS = 1.0, GRID_LOST_MS = 40.0;
constexpr double STEP_MS_AT_FIRST = 3.0, PICTURE_MS_AT_FIRST = 3.0, REAL_MS_AT_FIRST = 7.0;
// The guards' numbers, as the older pictures between (hfr.cpp)
constexpr double JUMP_MARGIN = 5.0, SPEED_PER_STEP_DISTANCE = 216.0, CAMERA_STEP_SHARE = 3.0, CAMERA_MARGIN = 10.0,
                 MOST_CAMERA_TURN = 0.5236;
constexpr u32 JUMP_FLAGS = game::MACHINE_FLAG_RETIRED | game::MACHINE_FLAG_BROKEN_DOWN | 0x40000000u | 0x800u;

// One call of a picture's list. `then`: the host looks at the answer (below)
enum class After : u8
{
  NOTHING,
  SKIP_TWO_IF_ZERO,  // fn_1_8DB94: when it answers 0 the two calls after it are not made
};
struct Draw
{
  u32 at, r3;
  After after = After::NOTHING;
};
// The draw half of fn_1_37AA8 for the one viewport (from its T+37C18, less the REPLAY overlay, which no race has), then
// the main loop's tail, less the sound, the casino screens' hook, the debug read-back, the delayed-free pass and the
// depth read-back. The HUD (fn_1_C7224, with the name tags before it) is added where its flag lets it be.
constexpr u32 CALL_HUD = T + 0xC7224;
const std::array<Draw, 22> PICTURE = {{{T + 0x3CC4, 0},
                                       {T + 0x435C, 0},  // the draw manager (r3 filled in)
                                       {T + 0x8DB94, 0, After::SKIP_TWO_IF_ZERO},
                                       {T + 0x86254, 0},
                                       {T + 0x106EB0, 0},
                                       {T + 0x5948, 0},
                                       {T + 0x9A770, 0},
                                       {CALL_HUD, 0},
                                       {T + 0x407C, 0},
                                       {T + 0x627C, 0},
                                       {T + 0xC6F8C, 0},
                                       {T + 0x545B8, 0},
                                       {T + 0x76218, 0},
                                       {T + 0x728B0, 0},
                                       {T + 0x76218, 1},
                                       {T + 0x4DE44, 0},
                                       {T + 0x4DEC0, 0},
                                       {T + 0x4FD64, 0},
                                       {0x8006CD50, 0},
                                       {0x8006CDA8, 0},
                                       {T + 0x4FD64, 0},
                                       {0x80007C60, 0}}};
constexpr u32 FRAME_BEGIN = 0x8006FD1C, DRAW_DONE_WAIT = 0x8006FDEC;

struct Machine
{
  Mtx pose{};
  u32 flags = 0, since = 0, flags2 = 0;
  float speed = 0;
};
struct Camera
{
  u8 mode = 0, follow = 0;
  u32 state = 0;
  float fov[2]{}, eye[3]{};
  Mtx a{}, b{};
};
struct State
{
  std::array<Machine, SEATS> machine;
  Camera camera;
  u32 count = 0;  // the race's machines (the seats after them hold no machine)
};

enum class Stage : u8
{
  OFF,
  AHEAD_MANAGER,  // the simulation manager is being chosen
  AHEAD_STEP,     // the step is being run
  PICTURE,        // a picture's list
  READ,           // the wait for the list before it, as the game's main loop waits before its frame begin
  BEGIN,          // its frame begin
  WAIT,           // the wait for it
};

// A mean of the last ones, an eighth of each new one.
struct Mean
{
  double ms = 0;
  bool any = false;
  void Add(double v)
  {
    ms = any ? ms + (v - ms) / 8 : v;
    any = true;
  }
};

// A high of the last HIGH_WINDOW frames: what a cost can be, which a mean is not. Not the highest: the three costs' highest
// never come together, and their sum leaves no pictures at all (measured). What gets past is met by the cover (Room). With
// nothing recent the cost is `first`, and pictures try again.
struct High
{
  struct Seen
  {
    u32 frame;
    float ms;
  };
  std::array<Seen, HIGH_WINDOW> seen{};
  u32 next = 0, count = 0;
  void Add(u32 frame, double ms)
  {
    seen[next] = {frame, static_cast<float>(ms)};
    next = (next + 1) % HIGH_WINDOW;
    count = std::min(count + 1, HIGH_WINDOW);
  }
  double Of(u32 frame, double first) const
  {
    std::array<float, HIGH_WINDOW> recent;
    u32 n = 0;
    for (u32 k = 0; k < count; ++k)
      if (frame - seen[k].frame < HIGH_WINDOW)
        recent[n++] = seen[k].ms;
    if (n == 0)
      return first;
    const u32 at = std::min(n - 1, static_cast<u32>(n * s_high_share));
    std::nth_element(recent.begin(), recent.begin() + at, recent.begin() + n);
    return recent[at];
  }
};

struct Stats
{
  u64 passes = 0, none_time = 0, none_state = 0, camera_cut = 0, pictures = 0, kept = 0, ahead_inputs_same = 0;
  u64 rng_moved = 0;
  // AURORA_TEST_SKIP_COMPARE: each step ahead run whole, then again with the lists left out; what Take read compared
  u64 compared = 0, compared_machines = 0, compared_camera = 0;
  u64 ahead_checked = 0, ahead_wrong = 0;  // the step ahead against the true step that came after it
  double ahead_most = 0;
  double wait_sum = 0, step_sum = 0, undo_sum = 0, draw_sum = 0, restore_sum = 0, tick_real_sum = 0;
  u64 tick_real = 0;
  std::array<u32, 40> step_hist{}, draw_hist{};
  u64 input_changed = 0, input_sent_early = 0;
  u64 stopped_after_ahead = 0, ticks_late = 0, ticks_late_after_pictures = 0, redraws = 0, cover_cut = 0;
  double tick_late_most = 0, cover_most = 0;
  double bound_step = 0, bound_picture = 0, bound_real = 0, bound_cover = 0, bound_late = 0;
};

struct Run
{
  bool on = false;
  bool no_read_wait = false;  // AURORA_TEST_NO_READ_WAIT: a test's control, the fault back
  u32 rate = 120, most = 1;  // pictures a second, pictures between a step
  std::function<void(float)> between;
  std::function<void(u32)> ran;
  std::function<void(const RedrawRequest&)> redraw;
  bool redrawing = false;  // AURORA_REDRAW: the host draws the pictures from the true frame's own draws
  RedrawRequest request;
  u32 manager = 0, name_tags = 0;
  bool tags_done = false;
  Stage stage = Stage::OFF;
  bool inside = false;
  u32 last_step = 0xFFFFFFFF;
  u32 step = 0, pictures = 0, picture = 0, index = 0;
  std::vector<u8> window;
  std::array<u32, 5> counters{};
  State now, ahead;
  std::array<bool, SEATS> moves{};
  Rollback own;  // (never with a race's log at once: Begin)
  Rollback* log = nullptr;
  Tick::time_point tick{}, began{};
  double blend = 0;  // this picture's share of the way, by when it can be shown
  double spent_ms = 0;  // this pass's, of the tick's time to the drawing's end
  Tick::time_point waiting{};
  Mean step_ms, picture_ms, real_ms;
  High step_high, picture_high, real_high, gap_high;
  // The last tick, and how late this one was; what the costs above did not cover.
  u32 frame = 0;
  Tick::time_point due{};
  double late_ms = 0, cover_ms = 0;
  bool pictures_last = false;
  // AURORA_RUNAHEAD_COVER_SPENT=1 (off by default): a late tick after a pass with pictures is kept in hand only up to what
  // that pass itself spent on this thread (its step ahead), not whole: most late ticks follow slow frames the pass did not make
  bool cover_spent = false;
  Stats stats;
  u32 rng_before = 0;
  u32 skip = 0;            // the lists the step ahead leaves out (AURORA_RUNAHEAD_SKIP), 0: none
  u32 mask_before = 0;     // the manager's list mask as the frame left it
  bool compare = false;    // AURORA_TEST_SKIP_COMPARE
  bool second = false;     // the comparison's second run (with the lists left out) is the one running
  State whole;             // the comparison's first run: the step ahead with every list
  bool ahead_good = false;
  u32 ahead_for = 0;
  std::FILE* path = nullptr;  // AURORA_RUNAHEAD_PATH: what each picture holds, for measuring
  // AURORA_RUNAHEAD_TIMES=<file> (a measuring tool): each pass's moments on the steady clock in us, and the bounds it was
  // judged by: P,step,tick,fit,step_high,real_high,cover,late | A,step,begin,end (the step ahead) | D,step,tick_end,drawn (the real step)
  std::FILE* times = nullptr;
  std::FILE* input_log = nullptr;  // AURORA_TEST_INPUT_LOG: each frame's tick and the moments its input was sent
  bool input_late = false;         // AURORA_TEST_INPUT_LATE: a test's control, the input left to the frame's own step
  Tick::time_point logged_tick{}, logged_early{};
  u32 logged_step = 0xFFFFFFFF;
  // AURORA_TEST_LEAK=<file>: every 16th pass, all of RAM and the locked cache before it against after its undo. What
  // differs is what a pass left behind, for the file: where and how many, per 256 bytes.
  std::FILE* leak_log = nullptr;
  bool force = false;  // AURORA_TEST_FORCE_PICTURES, or the comparison: pictures whatever they cost
  std::vector<u8> leak_ram, leak_scratch;
  bool leak_armed = false;
  u64 leak_passes = 0;
  std::map<u32, u32> leak_words;
} s;

float F(const Guest& g, u32 a)
{
  const u32 v = g.Read32(a);
  float f;
  std::memcpy(&f, &v, 4);
  return f;
}

void WriteF(Guest& g, u32 a, float f)
{
  u32 v;
  std::memcpy(&v, &f, 4);
  g.Write32(a, v);
}

Mtx ReadMtx(const Guest& g, u32 a)
{
  Mtx r;
  for (int i = 0; i < 12; ++i)
    r.m[i] = F(g, a + 4 * i);
  return r;
}

void WriteMtx(Guest& g, u32 a, const Mtx& m)
{
  for (int i = 0; i < 12; ++i)
    WriteF(g, a + 4 * i, m.m[i]);
}

long long Us(Tick::time_point t)
{
  return static_cast<long long>(std::chrono::duration_cast<std::chrono::microseconds>(t.time_since_epoch()).count());
}

double Ms(Tick::duration d)
{
  return std::chrono::duration<double, std::milli>(d).count();
}

void Note(std::array<u32, 40>& hist, double ms)
{
  ++hist[static_cast<size_t>(std::clamp(ms * 2, 0.0, 39.0))];  // half a millisecond a bin
}

std::string Percentiles(const std::array<u32, 40>& hist)
{
  u64 n = 0;
  for (u32 v : hist)
    n += v;
  if (n == 0)
    return "none";
  std::string out;
  for (const double share : {0.5, 0.9, 0.99})
  {
    u64 seen = 0;
    size_t k = 0;
    for (; k < hist.size(); ++k)
    {
      seen += hist[k];
      if (static_cast<double>(seen) >= share * static_cast<double>(n))
        break;
    }
    out += std::format("{}p{:.0f} {:.1f}", out.empty() ? "" : ", ", share * 100, (k + 1) / 2.0);
  }
  return out + " ms";
}

void Take(const Guest& g, State& st)
{
  const u32 machines = g.Read32(MACHINES);
  st.count = std::min<u32>(g.Read8(game::MACHINE_COUNT_AT), SEATS);
  for (u32 i = 0; i < SEATS; ++i)
  {
    const u32 m = machines + i * BLOCK;
    Machine& a = st.machine[i];
    a = {ReadMtx(g, m + POSE), g.Read32(m + game::MACHINE_FLAGS), g.Read32(m + game::MACHINE_SINCE),
         g.Read32(m + game::MACHINE_FLAGS2), F(g, m + game::MACHINE_SPEED)};
  }
  const u32 object = g.Read32(CAMERA_OBJECT), r = g.Read32(CAMERA_RECORDS);
  Camera& c = st.camera;
  c.mode = g.Read8(object + 0x48);
  c.follow = g.Read8(r + 2);
  c.state = g.Read32(r + 0xA8);
  c.fov[0] = F(g, r + 8);
  c.fov[1] = F(g, r + 0xC);
  for (int k = 0; k < 3; ++k)
    c.eye[k] = F(g, r + 0x10 + 4 * k);
  c.a = ReadMtx(g, r + 0xAC);
  c.b = ReadMtx(g, r + 0xDC);
}

double Move(const Mtx& a, const Mtx& b)
{
  double d2 = 0;
  for (int k = 3; k < 12; k += 4)
    d2 += (double(b.m[k]) - a.m[k]) * (double(b.m[k]) - a.m[k]);
  return std::sqrt(d2);
}

// Which machines move and whether the camera may: a jump stays where the true picture has it, a cut leaves the pass
// without pictures between.
bool Judge()
{
  const Camera &c = s.now.camera, &d = s.ahead.camera;
  for (u32 i = 0; i < SEATS; ++i)
  {
    const Machine &a = s.now.machine[i], &b = s.ahead.machine[i];
    const double limit = 2.0 * std::fabs(double(b.speed)) / SPEED_PER_STEP_DISTANCE + JUMP_MARGIN;
    s.moves[i] = b.since >= a.since && ((b.flags ^ a.flags) & JUMP_FLAGS) == 0 &&
                 ((b.flags2 ^ a.flags2) & game::MACHINE_FLAGS2_FALLEN) == 0 && Move(a.pose, b.pose) <= limit;
    s.stats.kept += !s.moves[i];
  }
  if (c.mode != d.mode || c.follow != d.follow || c.state != d.state || d.mode == 9 || d.mode == 10 || d.mode == 6)
    return false;
  double eye = 0;
  for (int k = 0; k < 3; ++k)
    eye += (double(d.eye[k]) - c.eye[k]) * (double(d.eye[k]) - c.eye[k]);
  const double followed = c.follow < SEATS ? Move(s.now.machine[c.follow].pose, s.ahead.machine[c.follow].pose) : 0;
  return std::sqrt(eye) <= CAMERA_STEP_SHARE * followed + CAMERA_MARGIN && hfr::Arc(c.b, d.b) <= MOST_CAMERA_TURN;
}

// How far between the two steps a picture is drawn: by its turn, or later when it cannot be shown before that (what it
// shows then is where the machine is by the time it is on screen). None past LATEST_BLEND: the true picture is too near.
// (SHOWN_AFTER_MS: from the picture's end here to its turn on Aurora's thread, measured: pictures came 2.9 ms past due)
constexpr double LATEST_BLEND = 0.9, SHOWN_AFTER_MS = 2.9;
bool ChooseBlend()
{
  const double turn = double(s.picture) / (s.pictures + 1);
  const double by = (Ms(Tick::now() - s.tick) + (s.picture_ms.any ? s.picture_ms.ms : PICTURE_MS_AT_FIRST) + SHOWN_AFTER_MS) / FIELD_MS;
  s.blend = std::max(turn, by);
  if (s.force)
    s.blend = std::min(s.blend, LATEST_BLEND);
  return s.blend <= LATEST_BLEND;
}

// A line for the measure of the on-screen path: the followed machine and the camera as a picture holds them.
void TracePath(const Guest& g, u32 step, double t)
{
  if (s.path == nullptr)
    return;
  const u32 r = g.Read32(CAMERA_RECORDS), follow = g.Read8(r + 2);
  if (follow >= SEATS)
    return;
  const Mtx pose = ReadMtx(g, g.Read32(MACHINES) + follow * BLOCK + POSE), a = ReadMtx(g, r + 0xAC), b = ReadMtx(g, r + 0xDC);
  std::fprintf(s.path, "%u,%.3f,%.4f,%.4f,%.4f,%u,%.3f,%.3f,%.3f", step, t, pose.m[3], pose.m[7], pose.m[11],
               s.now.machine[follow].flags, F(g, r + 0x10), F(g, r + 0x14), F(g, r + 0x18));
  for (const Mtx* m : {&a, &b})
    for (const float v : m->m)
      std::fprintf(s.path, ",%.6f", v);
  std::fprintf(s.path, "\n");
}

// The state of a picture t of the way from the true picture to the step ahead: machines' poses, what follows them,
// the camera. All of it undone with the rest.
void WriteBlend(Guest& g, double t)
{
  const u32 machines = g.Read32(MACHINES), shadows = g.Read32(SHADOWS);
  for (u32 i = 0; i < SEATS; ++i)
  {
    if (!s.moves[i])
      continue;
    const Mtx &a = s.now.machine[i].pose, &b = s.ahead.machine[i].pose;
    const Mtx blend = hfr::BlendPose(a, b, t);
    WriteMtx(g, machines + i * BLOCK + POSE, blend);
    // the machine's drawn transform (image: sites_drawn.py): its effects move with the body
    const u32 entry = layout::SMOOTH + i * SMOOTH_ENTRY;
    const bool had = g.Read8(entry + SMOOTH_WRITTEN) != 0;
    for (int k = 0; k < 3; ++k)
    {
      const float was = had ? F(g, entry + SMOOTH_OFFSET + 4 * k) : 0.0f;
      WriteF(g, entry + SMOOTH_OFFSET + 4 * k, was + blend.m[4 * k + 3] - a.m[4 * k + 3]);
    }
    g.Write8(entry + SMOOTH_WRITTEN, 1);
    // its shadow, placed in the step at the contact point (the record's matrix; image: sites_shadow.py)
    if (shadows != 0)
      for (int k = 0; k < 3; ++k)
      {
        const u32 at = shadows + i * SHADOW_SIZE + SHADOW_PLACE + 16 * k;
        WriteF(g, at, F(g, at) + blend.m[4 * k + 3] - a.m[4 * k + 3]);
      }
  }
  const Camera &c = s.now.camera, &d = s.ahead.camera;
  const u32 r = g.Read32(CAMERA_RECORDS);
  const float u = static_cast<float>(t);
  WriteF(g, r + 8, c.fov[0] + (d.fov[0] - c.fov[0]) * u);
  WriteF(g, r + 0xC, c.fov[1] + (d.fov[1] - c.fov[1]) * u);
  for (int k = 0; k < 3; ++k)
    WriteF(g, r + 0x10 + 4 * k, c.eye[k] + (d.eye[k] - c.eye[k]) * u);
  WriteMtx(g, r + 0xAC, hfr::BlendView(c.a, d.a, t));
  WriteMtx(g, r + 0xDC, hfr::BlendView(c.b, d.b, t));
  TracePath(g, s.step, t);
}

void KeepWindow(const Guest& g)
{
  s.window.resize(layout::WINDOW_END - layout::WINDOW);
  g.Data().CopyFrom(s.window.data(), layout::WINDOW, s.window.size());
  for (size_t k = 0; k < s.counters.size(); ++k)
    s.counters[k] = g.Read32(COUNTERS[k]);
}

void BackWindow(Guest& g)
{
  g.Data().CopyTo(layout::WINDOW, s.window.data(), s.window.size());
  for (size_t k = 0; k < s.counters.size(); ++k)
    g.Write32(COUNTERS[k], s.counters[k]);
}

// The manager wrapper of the frame routine (frame_replay.py) has no symbol of its own: its first words are unique.
u32 FindManager(const Guest& g)
{
  constexpr std::array<u32, 4> WORDS = {0x7C0802A6, 0x90010010, 0x39400000, 0x813F0078};
  for (u32 a = layout::IMAGE; a + 16 < layout::CONTROL; a += 4)
  {
    bool same = true;
    for (u32 k = 0; k < WORDS.size() && same; ++k)
      same = g.Read32(a + 4 * k) == WORDS[k];
    if (same)
      return a;
  }
  return 0;
}

// The image's hook before the HUD (sites.py `tags`): `stwu 1, -16(1)`, `stw 3, 8(1)`, `bl name_tags`, `lwz 3, 8(1)`.
u32 FindNameTags(const Guest& g)
{
  for (u32 a = layout::IMAGE; a + 16 < layout::CONTROL; a += 4)
    if (g.Read32(a) == 0x9421FFF0 && g.Read32(a + 4) == 0x90610008 && (g.Read32(a + 8) & 0xFC000003) == 0x48000001 &&
        g.Read32(a + 12) == 0x80610008)
    {
      const u32 word = g.Read32(a + 8) & 0x03FFFFFC;
      return a + 8 + (word >= 0x02000000 ? word - 0x04000000 : word);
    }
  return 0;
}

// The rows step t runs with, as NormalStep will write them: a live game's own record is its pad now, unless its input
// delay has it waiting already.
net::Row RowAhead(Guest& g, net::Session& ss, u32 t)
{
  net::Row row = net::RowFor(ss, t);
  if (!ss.playback && ss.my_index >= 0 && ss.my_inputs.find(t) == ss.my_inputs.end() && t >= net::Have(ss))
  {
    std::array<u8, net::RECORD> pad{};
    if (ss.autopilot)
    {
      pad[1] = 0x80;
      pad[5] = 1;
    }
    else
      g.CopyFrom(pad.data(), g.Read32(net::MODULE_POINTER) + net::PAD_RECORD, net::RECORD);
    std::copy(pad.begin(), pad.end(), row.begin() + ss.my_index * net::RECORD);
  }
  return row;
}

// The words the step ahead runs with, written after the frame's own step and before the step ahead's first call. All of
// them are put back: the row and the manager's mask by the undo log (RestoreTo) and Unmask, the window's by BackWindow.
void PrepareAhead(Guest& g, net::Session& ss, u32 step, u32 skip)
{
  net::WriteRow(g, ss, step, RowAhead(g, ss, step));
  g.Write32(RUN_BEFORE_AT, step + 1);  // the step counts as run before: its sounds and rumble are dropped
  g.Write32(LAST_STEP, 1);             // with the rivals' drawn offsets, as a frame's last step (smooth.py)
  for (const u32 a : COUNTERS)
    g.Write32(a, 0x100000 + step);
  const u32 manager = g.Read32(SIM_MANAGER);
  s.mask_before = g.Read32(manager + LIST_MASK);
  if (skip != 0)
    g.Write32(manager + LIST_MASK, s.mask_before | skip);
}

void Unmask(Guest& g)
{
  g.Write32(g.Read32(SIM_MANAGER) + LIST_MASK, s.mask_before);
}

bool SameBits(const void* a, const void* b, size_t n)
{
  return std::memcmp(a, b, n) == 0;
}

// The comparison: what Take read from the whole step ahead against the one with the lists left out, bit for bit.
void Compare(const State& whole, const State& part, u32 step)
{
  ++s.stats.compared;
  bool machines = whole.count != part.count;
  for (u32 i = 0; i < SEATS; ++i)
  {
    const Machine &a = whole.machine[i], &b = part.machine[i];
    machines |= !SameBits(&a.pose, &b.pose, sizeof a.pose) || a.flags != b.flags || a.since != b.since ||
                a.flags2 != b.flags2 || !SameBits(&a.speed, &b.speed, sizeof a.speed);
  }
  const Camera &c = whole.camera, &d = part.camera;
  const bool camera = c.mode != d.mode || c.follow != d.follow || c.state != d.state || !SameBits(c.fov, d.fov, sizeof c.fov) ||
                      !SameBits(c.eye, d.eye, sizeof c.eye) || !SameBits(&c.a, &d.a, sizeof c.a) || !SameBits(&c.b, &d.b, sizeof c.b);
  s.stats.compared_machines += machines;
  s.stats.compared_camera += camera;
  if ((machines || camera) && s.stats.compared_machines + s.stats.compared_camera <= 20)
    Notice("fzgx: run ahead: step {} ahead with lists 0x{:08X} left out: {}{}differ from the whole step's", step, s.skip,
           machines ? "machines " : "", camera ? "camera " : "");
}

constexpr u32 RAM_START = 0x80000000, RAM_BYTES = 24 * 1024 * 1024, SCRATCH_START = 0xE0000000;

void LeakBefore(Guest& g)
{
  if (s.leak_log == nullptr || (s.stats.passes & 15) != 0)
    return;
  s.leak_ram.resize(RAM_BYTES);
  g.CopyFrom(s.leak_ram.data(), RAM_START, RAM_BYTES);
  const u32 size = g.GetHost().ScratchSize();
  s.leak_scratch.resize(size);
  if (const u8* scratch = g.GetHost().Scratch(); scratch != nullptr)
    std::memcpy(s.leak_scratch.data(), scratch, size);
  s.leak_armed = true;
}

void LeakAfter(Guest& g)
{
  if (!s.leak_armed)
    return;
  s.leak_armed = false;
  ++s.leak_passes;
  static std::vector<u8> now(RAM_BYTES);
  g.CopyFrom(now.data(), RAM_START, RAM_BYTES);
  if (s.leak_passes == 8)  // one pass word by word, past the display buffers: what its words are
    for (u32 k = 0; k + 4 <= RAM_BYTES && RAM_START + k < 0x81000000; k += 4)
      if (std::memcmp(&now[k], &s.leak_ram[k], 4) != 0)
      {
        u32 was, is;
        std::memcpy(&was, &s.leak_ram[k], 4);
        std::memcpy(&is, &now[k], 4);
        std::fprintf(s.leak_log, "W %08X %08X %08X\n", RAM_START + k, was, is);
      }
  if (s.leak_passes == 1)  // the undo log's untracked ranges, for telling those apart
    for (u32 r = 0, n = std::min<u32>(g.Read32(net::UNTRACKED), 4096); r < n; ++r)
      std::fprintf(s.leak_log, "U %08X %08X\n", g.Read32(net::UNTRACKED + 4 + 8 * r), g.Read32(net::UNTRACKED + 8 + 8 * r));
  for (u32 k = 0; k + 4 <= RAM_BYTES; k += 4)
    if (std::memcmp(&now[k], &s.leak_ram[k], 4) != 0)
      ++s.leak_words[(RAM_START + k) & ~0xFFu];
  if (const u8* scratch = g.GetHost().Scratch(); scratch != nullptr)
    for (u32 k = 0; k + 4 <= s.leak_scratch.size(); k += 4)
      if (std::memcmp(scratch + k, &s.leak_scratch[k], 4) != 0)
        ++s.leak_words[(SCRATCH_START + k) & ~0xFFu];
}

Matrix Four(const Mtx& m)
{
  return {m.m[0], m.m[1], m.m[2], m.m[3], m.m[4], m.m[5], m.m[6], m.m[7], m.m[8], m.m[9], m.m[10], m.m[11], 0, 0, 0, 1};
}

// The pictures between the true picture and the step ahead, for the host: each machine's and the camera's correction at its fraction.
// Only read from the two states kept; nothing of the game's is touched.
void SendRedraw()
{
  RedrawRequest& q = s.request;
  const Mtx& view = s.now.camera.b;
  q.machines = 0;
  for (u32 i = 0; i < SEATS; ++i)
  {
    const Mtx& m = s.now.machine[i].pose;
    if (i < s.now.count)
      q.machines |= 1u << i;
    for (int r = 0; r < 3; ++r)
      q.at[i][r] = float(double(view.m[4 * r]) * m.m[3] + double(view.m[4 * r + 1]) * m.m[7] + double(view.m[4 * r + 2]) * m.m[11] +
                         view.m[4 * r + 3]);
  }
  q.pictures = std::min(s.most, PICTURES_MOST);
  for (u32 k = 0; k < q.pictures; ++k)
  {
    RedrawPicture& p = q.picture[k];
    const double t = double(k + 1) / (q.pictures + 1);
    p.fraction = float(t);
    const Mtx picture_view = hfr::BlendView(s.now.camera.b, s.ahead.camera.b, t);
    p.camera = Four(hfr::CameraCorrection(view, picture_view));
    for (u32 i = 0; i < SEATS; ++i)
      p.machine[i] = s.moves[i] ? Four(hfr::BodyCorrection(view, picture_view, s.now.machine[i].pose,
                                                           hfr::BlendPose(s.now.machine[i].pose, s.ahead.machine[i].pose, t))) :
                                  p.camera;
  }
  s.redraw(q);
  ++s.stats.redraws;
}

// The room left in this field for what is run ahead, with the real step and all it needs after it still on time: from
// recent highs, not means, and from where this tick actually is on the wall clock.
double Room(double used_ms)
{
  return FIELD_MS - MARGIN_MS - s.cover_ms - s.late_ms - used_ms - s.real_high.Of(s.frame, REAL_MS_AT_FIRST);
}

// How many pictures fit before the next field beside the step and its drawing: none, or at least one.
u32 PicturesThatFit()
{
  if (s.force)  // (a test's, and the comparison takes the time: its passes are to have pictures in them)
    return s.most;
  const double room = Room(s.step_high.Of(s.frame, STEP_MS_AT_FIRST));
  const double one = s.picture_high.Of(s.frame, PICTURE_MS_AT_FIRST);
  s.stats.bound_step += s.step_high.Of(s.frame, STEP_MS_AT_FIRST);
  s.stats.bound_picture += one;
  s.stats.bound_real += s.real_high.Of(s.frame, REAL_MS_AT_FIRST);
  s.stats.bound_cover += s.cover_ms;
  s.stats.bound_late += s.late_ms;
  if (room < one)
    return 0;
  return std::min(s.most, static_cast<u32>(room / one));
}

// One more picture still leaves the real step on time, by what has been spent so far.
bool PictureStillFits()
{
  if (s.force)
    return true;
  return Room(Ms(Tick::now() - s.tick)) >= s.picture_high.Of(s.frame, PICTURE_MS_AT_FIRST);
}

// How late this tick is: its gap from the tick before against the usual gap (a race's frames follow the relay's clock, a
// field is not always FIELD_MS). A late one that came after pictures says by how much the costs did not cover the frame,
// which is kept in hand and falls slowly.
void PlaceTheTick(Tick::time_point now)
{
  ++s.frame;
  s.cover_ms *= 0.99;
  const double gap = s.due == Tick::time_point{} ? FIELD_MS : Ms(now - s.due);
  s.due = now;
  s.gap_high.Add(s.frame, gap);
  const double late = std::clamp(gap - s.gap_high.Of(s.frame, FIELD_MS), 0.0, GRID_LOST_MS);
  s.late_ms = late < LATE_NOTICED_MS ? 0.0 : late;
  if (s.late_ms > 0)
  {
    ++s.stats.ticks_late;
    s.stats.tick_late_most = std::max(s.stats.tick_late_most, late);
    if (s.pictures_last)
    {
      ++s.stats.ticks_late_after_pictures;
      const double kept = s.cover_spent ? std::min(late, s.spent_ms) : late;
      s.stats.cover_cut += kept < late ? 1 : 0;
      s.cover_ms = std::max(s.cover_ms, kept);
      s.stats.cover_most = std::max(s.stats.cover_most, kept);
    }
  }
  s.pictures_last = false;
}
}  // namespace

void Start(std::function<void(float)> between, std::function<void(u32)> ran, std::function<void(const RedrawRequest&)> redraw)
{
  s.ran = std::move(ran);
  s.redraw = std::move(redraw);
  const char* redrawing = std::getenv("AURORA_REDRAW");
  s.redrawing = redrawing != nullptr && redrawing[0] == '1' && s.redraw != nullptr;
  s.on = false;
  s.between = std::move(between);
  s.input_late = std::getenv("AURORA_TEST_INPUT_LATE") != nullptr;
  const char* cover_spent = std::getenv("AURORA_RUNAHEAD_COVER_SPENT");
  s.cover_spent = cover_spent != nullptr && cover_spent[0] == '1';
  if (const char* path = std::getenv("AURORA_TEST_LEAK"))
    s.leak_log = std::fopen(path, "w");
  s.force = s.leak_log != nullptr || std::getenv("AURORA_TEST_FORCE_PICTURES") != nullptr;
  if (const char* share = std::getenv("AURORA_TEST_HIGH_SHARE"))
    s_high_share = std::atof(share);
  if (const char* path = std::getenv("AURORA_TEST_INPUT_LOG"))
    s.input_log = std::fopen(path, "w");
  const char* asked = std::getenv("AURORA_RUNAHEAD");
  if (asked == nullptr || std::atoi(asked) < 120)
    return;
  s.rate = std::min<u32>(static_cast<u32>(std::atoi(asked)), 480);
  s.most = static_cast<u32>(std::lround(s.rate / 60.0)) - 1;
  s.on = true;
  s.no_read_wait = std::getenv("AURORA_TEST_NO_READ_WAIT") != nullptr;
  s.skip = 0;
  if (const char* skip = std::getenv("AURORA_RUNAHEAD_SKIP"); skip != nullptr && skip[0] != '\0')
  {
    const unsigned long v = std::strtoul(skip, nullptr, 0);
    s.skip = v == 1 ? SKIP_LISTS : static_cast<u32>(v);
  }
  s.compare = s.skip != 0 && std::getenv("AURORA_TEST_SKIP_COMPARE") != nullptr;
  if (s.skip != 0)
    Notice("fzgx: run ahead: the step ahead leaves out the task lists 0x{:08X}{}", s.skip,
           s.compare ? "; each also run whole and compared (AURORA_TEST_SKIP_COMPARE)" : "");
  if (const char* path = std::getenv("AURORA_RUNAHEAD_PATH"))
    s.path = std::fopen(path, "w");
  if (const char* path = std::getenv("AURORA_RUNAHEAD_TIMES"))
    s.times = std::fopen(path, "w");
  Notice("fzgx: run ahead: {} pictures a second asked for, {} between two steps at most", s.rate, s.most);
}

bool On()
{
  return s.on;
}

bool Inside()
{
  return s.inside;
}

bool Begin()
{
  if (s.input_log != nullptr)
  {
    s.logged_tick = Tick::now();
    s.logged_early = {};
  }
  if (!s.on)
    return false;
  net::Session* ss = net::s_session.get();
  Guest g(TheHost(), Data());
  if (ss == nullptr || !g.Ram(net::MODULE_POINTER, 4) || g.Read32(net::MODULE_POINTER) != layout::MAIN_REL)
    return false;
  PlaceTheTick(Tick::now());
  const u32 step = g.Read32(net::STEP);
  const bool consecutive = step == s.last_step + 1;
  s.last_step = step;
  const u32 object = g.Read32(CAMERA_OBJECT);
  if (g.Read32(RUNNING) == 0 || g.Read16(game::PHASE_AT) != 0x29 || g.Read32(layout::END_OVER) != 0 ||
      g.Read32(CONTROL + 8) != 0 || net::ScrubOn(*ss) || ss->settled || !consecutive ||
      g.Read32(PAUSE_WORD) != 0 || g.Read32(layout::DRAW_SKIP) != 0 || !g.Ram(object, 0x50) ||
      (g.Read32(object) >> 31) != 0 || g.Read8(object + 0x49) > 1)
  {
    ++s.stats.none_state;
    return false;
  }
  ++s.stats.passes;
  // Everything below moves the frame's own step later in the field; the input does not wait for it.
  if (!s.input_late)
  {
    net::SendInputBeforeRunAhead(g, *ss, step);
    ++s.stats.input_sent_early;
    if (s.input_log != nullptr)
      s.logged_early = Tick::now();
  }
  // the real step and its drawing, as they cost lately: from the step after the tick before to the drawing's end
  u32 fit = PicturesThatFit();
  if (s.redrawing && !s.force)  // a picture costs this thread nothing: only the step ahead has to fit
    fit = Room(s.step_high.Of(s.frame, STEP_MS_AT_FIRST)) >= 0 ? s.most : 0;
  s.tick = Tick::now();
  if (s.times != nullptr)
    std::fprintf(s.times, "P,%u,%lld,%u,%.2f,%.2f,%.2f,%.2f\n", step, Us(s.due), fit, s.step_high.Of(s.frame, STEP_MS_AT_FIRST),
                 s.real_high.Of(s.frame, REAL_MS_AT_FIRST), s.cover_ms, s.late_ms);
  s.spent_ms = 0;
  if (fit == 0)
  {
    ++s.stats.none_time;
    return false;
  }
  if (s.manager == 0)
  {
    s.manager = FindManager(g);
    s.name_tags = FindNameTags(g);
    if (s.manager == 0)
    {
      Notice("fzgx: run ahead: the step wrapper is not in the image: off");
      s.on = false;
      return false;
    }
  }
  // A race's undo log when it has one; a playback that runs straight has this file's own.
  if (ss->rollback.Active())
  {
    if (s.own.Active())
      s.own.Stop();
    s.log = &ss->rollback;
  }
  else
  {
    if (!s.own.Active())
      s.own.Start(g);
    s.log = &s.own;
  }
  s.step = step;
  s.index = 0;
  s.pictures = fit;
  s.picture = 0;
  s.inside = true;
  s.pictures_last = true;
  s.began = Tick::now();
  s.rng_before = g.Read32(game::DOL_RNG);
  Take(g, s.now);
  TracePath(g, step, 0);
  if (s.ahead_good && s.ahead_for == step)
  {
    double most = 0;
    for (u32 i = 0; i < SEATS; ++i)
      most = std::max(most, Move(s.ahead.machine[i].pose, s.now.machine[i].pose));
    ++s.stats.ahead_checked;
    s.stats.ahead_wrong += most != 0;
    s.stats.ahead_most = std::max(s.stats.ahead_most, most);
  }
  s.ahead_good = false;
  LeakBefore(g);
  s.began = Tick::now();
  KeepWindow(g);
  s.log->SaveStep(g, step, true);
  if (s.log == &s.own)
    s.own.Final(step);
  s.second = false;
  PrepareAhead(g, *ss, step, s.compare ? 0 : s.skip);  // the comparison runs the whole step first
  s.stage = Stage::AHEAD_MANAGER;
  return true;
}

bool Next(u32 returned, Call* call)
{
  Guest g(TheHost(), Data());
  switch (s.stage)
  {
  case Stage::OFF:
    return false;
  case Stage::AHEAD_MANAGER:
    s.stage = Stage::AHEAD_STEP;
    *call = {T + 0x435C, g.Read32(SIM_MANAGER)};
    return true;
  case Stage::AHEAD_STEP:
    if (s.index == 0)
    {
      s.index = 1;
      *call = {s.manager, 0};
      return true;
    }
    {
      s.index = 0;
      const auto ran = Tick::now();
      if (s.compare && !s.second)
      {
        // the whole step ahead taken and undone; the same step again with the lists left out
        Take(g, s.whole);
        s.log->SaveStep(g, s.step + 1, false);
        s.log->RestoreTo(g, s.step + 1, s.step);
        Unmask(g);
        BackWindow(g);
        s.second = true;
        PrepareAhead(g, *net::s_session, s.step, s.skip);
        *call = {T + 0x435C, g.Read32(SIM_MANAGER)};
        return true;
      }
      s.ran(s.step);
      Take(g, s.ahead);
      if (s.compare)
        Compare(s.whole, s.ahead, s.step);
      s.ahead_good = true;
      s.ahead_for = s.step + 1;
      s.log->SaveStep(g, s.step + 1, false);
      s.log->RestoreTo(g, s.step + 1, s.step);
      Unmask(g);
      BackWindow(g);
      const auto undone = Tick::now();
      s.stats.step_sum += Ms(ran - s.began);
      if (s.times != nullptr)
        std::fprintf(s.times, "A,%u,%lld,%lld\n", s.step, Us(s.began), Us(undone));
      s.stats.undo_sum += Ms(undone - ran);
      s.step_ms.Add(Ms(undone - s.began));
      s.step_high.Add(s.frame, Ms(undone - s.began));
      s.spent_ms += Ms(undone - s.began);
      Note(s.stats.step_hist, Ms(undone - s.began));
      if (!Judge())
      {
        ++s.stats.camera_cut;
        LeakAfter(g);
        s.log->SaveAgain(s.step);
        s.inside = false;
        s.stage = Stage::OFF;
        return false;
      }
      if (s.redrawing)
      {
        SendRedraw();
        LeakAfter(g);
        s.log->SaveAgain(s.step);
        s.inside = false;
        s.stage = Stage::OFF;
        return false;
      }
      s.picture = 1;
      s.began = Tick::now();
      if (!PictureStillFits() || !ChooseBlend())
      {
        ++s.stats.none_time;
        ++s.stats.stopped_after_ahead;
        LeakAfter(g);
        s.log->SaveAgain(s.step);
        s.inside = false;
        s.stage = Stage::OFF;
        return false;
      }
      WriteBlend(g, s.blend);
      g.Write32(MUTE, 1);
      s.stage = Stage::PICTURE;
      s.index = 0;
      s.tags_done = false;
    }
    [[fallthrough]];
  case Stage::PICTURE:
    if (s.index < PICTURE.size() && PICTURE[s.index].after == After::NOTHING && s.index > 0 &&
        PICTURE[s.index - 1].after == After::SKIP_TWO_IF_ZERO && returned == 0)
      s.index += 2;
    if (s.index < PICTURE.size())
    {
      const Draw& d = PICTURE[s.index];
      if (d.at == CALL_HUD && !s.tags_done)
      {
        // the name tags, as the image's hook does before the HUD
        s.tags_done = true;
        if (s.name_tags != 0)
        {
          *call = {s.name_tags, 0};
          return true;
        }
      }
      if (d.at == CALL_HUD && g.Read8(HUD_OFF) != 0)
        ++s.index;
      const Draw& e = PICTURE[s.index];
      *call = {e.at, e.at == T + 0x435C ? g.Read32(DRAW_MANAGER) : e.r3};
      ++s.index;
      return true;
    }
    // The frame begin keeps the read pointer of the list being read for the next time it hands it over, so the GP must
    // be done with it: a pointer kept early has the GP read the end of an old list again as if it were new, and decode
    // garbage (Unknown Opcode, the draw-done lost, the game's watchdog reboots). The main loop's own wait comes before
    // its frame begin; here the true picture's list is still being read.
    if (!s.no_read_wait)
    {
      s.stage = Stage::READ;
      *call = {DRAW_DONE_WAIT, 0};
      return true;
    }
    [[fallthrough]];
  case Stage::READ:
    s.between(static_cast<float>(s.blend));
    s.stage = Stage::BEGIN;
    *call = {FRAME_BEGIN, 0};
    return true;
  case Stage::BEGIN:
    s.stage = Stage::WAIT;
    s.waiting = Tick::now();
    *call = {DRAW_DONE_WAIT, 0};
    return true;
  case Stage::WAIT:
  {
    const auto drawn = Tick::now();
    if (g.Read32(game::DOL_RNG) != s.rng_before)
      ++s.stats.rng_moved;
    s.log->SaveStep(g, s.step + 1, false);
    s.log->RestoreTo(g, s.step + 1, s.step);
    BackWindow(g);
    const auto undone = Tick::now();
    s.stats.draw_sum += Ms(drawn - s.began);
    s.stats.restore_sum += Ms(undone - drawn);
    s.picture_ms.Add(Ms(undone - s.began));
    s.picture_high.Add(s.frame, Ms(undone - s.began));
    s.spent_ms += Ms(undone - s.began);
    s.stats.wait_sum += Ms(drawn - s.waiting);
    Note(s.stats.draw_hist, Ms(undone - s.began));
    ++s.stats.pictures;
    ++s.picture;
    if (s.picture <= s.pictures && PictureStillFits() && ChooseBlend())
    {
      s.began = Tick::now();
      WriteBlend(g, s.blend);
      g.Write32(MUTE, 1);
      s.stage = Stage::PICTURE;
      s.index = 0;
      s.tags_done = false;
      return Next(0, call);
    }
    LeakAfter(g);
    s.log->SaveAgain(s.step);
    s.inside = false;
    s.stage = Stage::OFF;
    return false;
  }
  }
  return false;
}

void NoteInputSent(u32 step, bool changed)
{
  if (s.logged_early != Tick::time_point{} && s.input_log != nullptr)
    s.stats.input_changed += changed;
  if (s.input_log == nullptr || s.logged_step == step)
    return;
  s.logged_step = step;
  const auto us = [](Tick::time_point t) {
    return std::chrono::duration_cast<std::chrono::microseconds>(t.time_since_epoch()).count();
  };
  std::fprintf(s.input_log, "%u,%lld,%lld,%lld,%d\n", step, static_cast<long long>(us(s.logged_tick)),
               static_cast<long long>(s.logged_early == Tick::time_point{} ? 0 : us(s.logged_early)),
               static_cast<long long>(us(Tick::now())), changed ? 1 : 0);
}

void TimeTheDrawing(u32 value)
{
  // the image's DRAW_ASK: 2 the drawing has ended. From the tick's end to here, the real step and its drawing.
  if (value == 2 && s.on && s.stage == Stage::OFF && s.tick != Tick::time_point{})
  {
    const double ms = Ms(Tick::now() - s.tick);
    if (s.times != nullptr)
      std::fprintf(s.times, "D,%u,%lld,%lld\n", s.last_step, Us(s.tick), Us(Tick::now()));
    s.real_ms.Add(std::max(0.0, ms - s.spent_ms));
    s.real_high.Add(s.frame, std::max(0.0, ms - s.spent_ms));
    s.stats.tick_real_sum += std::max(0.0, ms - s.spent_ms);
    ++s.stats.tick_real;
    s.tick = {};
  }
}

std::string Report()
{
  if (s.input_log != nullptr)
  {
    std::fclose(s.input_log);
    s.input_log = nullptr;
  }
  if (s.leak_log != nullptr)
  {
    std::fprintf(s.leak_log, "# passes compared %llu; address,words differed over them\n", static_cast<unsigned long long>(s.leak_passes));
    for (const auto& [at, words] : s.leak_words)
      std::fprintf(s.leak_log, "%08X,%u\n", at, words);
    std::fclose(s.leak_log);
    s.leak_log = nullptr;
  }
  if (!s.on)
    return {};
  const Stats& t = s.stats;
  const double steps = std::max<double>(1, static_cast<double>(t.passes - t.none_time));
  const double pics = std::max<double>(1, static_cast<double>(t.pictures));
  if (s.path != nullptr)
    std::fflush(s.path);
  const double asked = std::max<double>(1, static_cast<double>(t.passes));
  return std::format("fzgx: run ahead: {} requests of pictures to the renderer (AURORA_REDRAW {})\n", t.redraws, s.redrawing ? "on" : "off") +
         std::format("fzgx: run ahead: the bounds on average: step {:.2f}, picture {:.2f}, real {:.2f}, kept in hand {:.2f}, tick late {:.2f} ms\n",
                     t.bound_step / asked, t.bound_picture / asked, t.bound_real / asked, t.bound_cover / asked, t.bound_late / asked) +
         std::format("fzgx: run ahead: ticks late on the wall clock {} ({} after pictures), by at most {:.2f} ms; most kept in hand {:.2f} ms; stopped after the step ahead {}\n",
                     t.ticks_late, t.ticks_late_after_pictures, t.tick_late_most, t.cover_most, t.stopped_after_ahead) +
         (s.cover_spent ? std::format("fzgx: run ahead: kept in hand up to the pass's own spent ms (AURORA_RUNAHEAD_COVER_SPENT): {} late ticks kept less than whole\n", t.cover_cut) : std::string()) +
         std::format("fzgx: run ahead: input sent at the tick {} times; the pad record differed later {} times\n", t.input_sent_early, t.input_changed) +
         std::format("fzgx: run ahead: step ahead against the true step: {} compared, {} differed, by at most {:.4f}\n", t.ahead_checked, t.ahead_wrong, t.ahead_most) +
         std::format("fzgx: run ahead: task lists left out of the step ahead 0x{:08X}\n", s.skip) +
         (s.compare ? std::format("fzgx: run ahead: the step ahead whole against with the lists left out: {} compared, machines differed {}, camera differed {}\n",
                                  t.compared, t.compared_machines, t.compared_camera)
                    : std::string()) +
         std::format("fzgx: run ahead: {} pictures a second asked for: {} passes in the race, {} with no room in time, {} out of the race or not next, "
                     "{} camera cuts; {} pictures between; a step ahead {:.2f} ms + its undo {:.2f} ms ({}); a picture {:.2f} ms, of it the draw-done wait {:.2f}, + its undo {:.2f} ms ({}); "
                     "the real step and its drawing {:.2f} ms; {} machines kept for a jump; pictures that moved the executable's generator (undone): {}",
                     s.rate, t.passes, t.none_time, t.none_state, t.camera_cut, t.pictures, t.step_sum / steps, t.undo_sum / steps,
                     Percentiles(t.step_hist), t.draw_sum / pics, t.wait_sum / pics, t.restore_sum / pics, Percentiles(t.draw_hist),
                     t.tick_real ? t.tick_real_sum / static_cast<double>(t.tick_real) : 0.0, t.kept, t.rng_moved);
}

void Stop()
{
  if (s.own.Active())
    s.own.Stop();
  if (s.path != nullptr)
    std::fclose(s.path);
  s.path = nullptr;
  if (s.times != nullptr)
    std::fclose(s.times);
  s.times = nullptr;
  s.log = nullptr;
  s.stage = Stage::OFF;
  s.inside = false;
  s.last_step = 0xFFFFFFFF;
}
}  // namespace fzgx::runahead
