// fzgx/runahead.h: what runs at the frame tick, and in what order. Addresses from the game's frame (main.rel's _prolog
// loop and fn_1_37AA8's draw half: archive/research/plan-frame-interpolation.md 1). Fractions, poses and cameras read
// and written here; hfr_blend.h does the blending.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fzgx/runahead.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <format>
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
constexpr u32 SMOOTH_ENTRY = 48, SMOOTH_OFFSET = 12, SMOOTH_WRITTEN = 36;
constexpr u32 SHADOWS = B + 0x6D83C + 8, SHADOW_SIZE = 0x4C, SHADOW_PLACE = 0x18;
constexpr double FIELD_MS = 1000.0 * 1001.0 / 60000.0;
constexpr double MARGIN_MS = 1.5;  // room kept before the next field beside the step and its drawing
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
};

enum class Stage : u8
{
  OFF,
  AHEAD_MANAGER,  // the simulation manager is being chosen
  AHEAD_STEP,     // the step is being run
  PICTURE,        // a picture's list
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

struct Stats
{
  u64 passes = 0, none_time = 0, none_state = 0, camera_cut = 0, pictures = 0, kept = 0, ahead_inputs_same = 0;
  u64 rng_moved = 0;
  double wait_sum = 0, step_sum = 0, undo_sum = 0, draw_sum = 0, restore_sum = 0, tick_real_sum = 0;
  u64 tick_real = 0;
  std::array<u32, 40> step_hist{}, draw_hist{};
};

struct Run
{
  bool on = false;
  u32 rate = 120, most = 1;  // pictures a second, pictures between a step
  std::function<void(float)> between;
  std::function<void(u32)> ran;
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
  double spent_ms = 0;  // this pass's, of the tick's time to the drawing's end
  Tick::time_point waiting{};
  Mean step_ms, picture_ms, real_ms;
  Stats stats;
  u32 rng_before = 0;
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

// How many pictures fit before the next field beside the step and its drawing: none, or at least one.
u32 PicturesThatFit()
{
  const double room = FIELD_MS - MARGIN_MS - (s.real_ms.any ? s.real_ms.ms : REAL_MS_AT_FIRST) -
                      (s.step_ms.any ? s.step_ms.ms : STEP_MS_AT_FIRST);
  const double one = s.picture_ms.any ? s.picture_ms.ms : PICTURE_MS_AT_FIRST;
  if (room < one)
    return 0;
  return std::min(s.most, static_cast<u32>(room / one));
}
}  // namespace

void Start(std::function<void(float)> between, std::function<void(u32)> ran)
{
  s.ran = std::move(ran);
  s.on = false;
  s.between = std::move(between);
  const char* asked = std::getenv("AURORA_RUNAHEAD");
  if (asked == nullptr || std::atoi(asked) < 120)
    return;
  s.rate = std::min<u32>(static_cast<u32>(std::atoi(asked)), 480);
  s.most = static_cast<u32>(std::lround(s.rate / 60.0)) - 1;
  s.on = true;
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
  if (!s.on)
    return false;
  net::Session* ss = net::s_session.get();
  Guest g(TheHost(), Data());
  if (ss == nullptr || !g.Ram(net::MODULE_POINTER, 4) || g.Read32(net::MODULE_POINTER) != layout::MAIN_REL)
    return false;
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
  // the real step and its drawing, as they cost lately: from the step after the tick before to the drawing's end
  const u32 fit = PicturesThatFit();
  s.tick = Tick::now();
  s.spent_ms = 0;
  if (fit == 0)
  {
    ++s.stats.none_time;
    // (a spike must not keep it off for good: with nothing new measured, the means fall until it tries again)
    s.step_ms.ms *= 0.97;
    s.picture_ms.ms *= 0.97;
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
  s.began = Tick::now();
  s.rng_before = g.Read32(game::DOL_RNG);
  Take(g, s.now);
  KeepWindow(g);
  s.log->SaveStep(g, step, true);
  if (s.log == &s.own)
    s.own.Final(step);
  net::WriteRow(g, *ss, step, RowAhead(g, *ss, step));
  g.Write32(RUN_BEFORE_AT, step + 1);  // the step counts as run before: its sounds and rumble are dropped
  g.Write32(LAST_STEP, 1);             // with all its presentation tasks, as a frame's last step
  for (const u32 a : COUNTERS)
    g.Write32(a, 0x100000 + step);
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
      s.ran(s.step);
      Take(g, s.ahead);
      s.log->SaveStep(g, s.step + 1, false);
      s.log->RestoreTo(g, s.step + 1, s.step);
      BackWindow(g);
      const auto undone = Tick::now();
      s.stats.step_sum += Ms(ran - s.began);
      s.stats.undo_sum += Ms(undone - ran);
      s.step_ms.Add(Ms(undone - s.began));
      s.spent_ms += Ms(undone - s.began);
      Note(s.stats.step_hist, Ms(undone - s.began));
      if (!Judge())
      {
        ++s.stats.camera_cut;
        s.log->SaveAgain(s.step);
        s.inside = false;
        s.stage = Stage::OFF;
        return false;
      }
      s.picture = 1;
      s.began = Tick::now();
      WriteBlend(g, double(s.picture) / (s.pictures + 1));
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
    s.between(static_cast<float>(double(s.picture) / (s.pictures + 1)));
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
    s.spent_ms += Ms(undone - s.began);
    s.stats.wait_sum += Ms(drawn - s.waiting);
    Note(s.stats.draw_hist, Ms(undone - s.began));
    ++s.stats.pictures;
    if (s.picture < s.pictures)
    {
      ++s.picture;
      s.began = Tick::now();
      WriteBlend(g, double(s.picture) / (s.pictures + 1));
      g.Write32(MUTE, 1);
      s.stage = Stage::PICTURE;
      s.index = 0;
      s.tags_done = false;
      return Next(0, call);
    }
    s.log->SaveAgain(s.step);
    s.inside = false;
    s.stage = Stage::OFF;
    return false;
  }
  }
  return false;
}

void TimeTheDrawing(u32 value)
{
  // the image's DRAW_ASK: 2 the drawing has ended. From the tick's end to here, the real step and its drawing.
  if (value == 2 && s.on && s.stage == Stage::OFF && s.tick != Tick::time_point{})
  {
    const double ms = Ms(Tick::now() - s.tick);
    s.real_ms.Add(std::max(0.0, ms - s.spent_ms));
    s.stats.tick_real_sum += std::max(0.0, ms - s.spent_ms);
    ++s.stats.tick_real;
    s.tick = {};
  }
}

std::string Report()
{
  if (!s.on)
    return {};
  const Stats& t = s.stats;
  const double steps = std::max<double>(1, static_cast<double>(t.passes - t.none_time));
  const double pics = std::max<double>(1, static_cast<double>(t.pictures));
  return std::format("fzgx: run ahead: {} pictures a second asked for: {} passes in the race, {} with no room in time, {} out of the race or not next, "
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
  s.log = nullptr;
  s.stage = Stage::OFF;
  s.inside = false;
  s.last_step = 0xFFFFFFFF;
}
}  // namespace fzgx::runahead
