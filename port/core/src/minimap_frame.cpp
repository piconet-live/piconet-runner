// Our course map in the game (fzgx/minimap.h says what it shows and why): at every game frame of a race or replay
// start, from the host's frame tick (core.cpp FrameTick: the mode machine's start, before the frame's steps and
// before the network hook, so what it reads is what the last frame drew: the drawn bodies with the smoothing's
// offsets still on them, the HUD parts as drawn).
//   - The game's map picture: its bit of the HUD part mask (game::HUD_PARTS, viewport 0) is cleared, and in the
//     word the image keeps the race's own mask in (control block +60: frame_spectate.py `spechud` keeps it,
//     frame_hud.py `hudstate` and the spectator's controls put it back), so neither puts the bit back. Only the
//     map's draw (fn_1_79654) reads the bit; the mask is drawing's alone and no checksum covers it. The core
//     clears it from the course intro on, before any step of the race, so every moment a rollback, a practice's
//     rewind or the scrubber goes back to has it cleared too.
//   - The road: once a course, from the player's own disc (stage/selstNN.gma.lz) through the game's own map camera
//     for the course (game::MAP_CAMERAS), to the host; with it the start mark, at the course file's start line
//     (stage/COLI_COURSENN.lz, fzgx/minimap.h ReadStart). For the log, the line is checked against the racers: where
//     they stand at the race's first step (behind it) and which way they have gone two seconds later (along it).
//   - The dots: once a frame, every racer's drawn body (game::MACHINE_DRAWN_X), its machine's colour from the
//     race's set-up (timeline.cpp SetupColours), its place, out and finished as the game says; "you" is the racer
//     the picture follows: the spectator controls' (a replay as the race, a spectator, a racer out), the image's own
//     spectating's (+252, while +56 is on), else the stand-in seat (the slot's +4: the local seat live, the racer a
//     replay follows).
//   - The view's turn and tilt: port 1's C-stick x and y (PADStatus as the game clamped it) while the player's
//     controls leave the C-stick unbound (fzgx/minimap.h CstickBound), for the host to turn and tilt the view by;
//     no tilt while the spectator controls are up (their C up and down are the camera's height).
// Picture only: what is read changes nothing, and the one word written is drawing's.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "net_internal.h"
#include "net_modes.h"
#include "out_test.h"
#include "files.h"

#include "fzgx/minimap.h"

#include <bit>
#include <format>
#include <optional>

namespace fzgx::net
{
namespace
{
namespace mm = minimap;
constexpr u32 SPECTATING = CONTROL + 56, HUD_KEPT = CONTROL + 60, SPECTATED = CONTROL + 252;  // (spectator_frame.cpp)
constexpr u32 PAD_STATUS = BSS + 0x998;  // PADStatus of port 0, clamped: the C-stick's x at +4 (spectator_frame.cpp)
constexpr u32 COURSE_INTRO = 0x27;
constexpr u32 FINISHED = 0x10000;

struct MinimapState
{
  bool loaded = false;  // the road was read (or found unreadable: no map)
  u32 course = 0;       // the course shown (0: none)
  mm::Camera camera;
  Timeline colours;     // the seats' colours (only `colour` is used)
  bool shown = false;   // the host was last told to show it
  // for the log: displayed frames of the countdown and the race, those that showed the map, the most dots, the frames
  // with a dot of yours and with a leader's
  u32 race_frames = 0, shown_frames = 0, most_dots = 0, you_frames = 0, leader_frames = 0, turned_frames = 0;
  // the start line's check: the racers' places along it at the race's first displayed frame, and two seconds on
  std::optional<mm::Start> start;
  u32 race_seen = 0;
  mm::Racers at_go{};
  std::string start_check;
};
MinimapState s_map;

void Load(Guest& g, u32 module)
{
  s_map.loaded = true;
  const u32 course = g.Read32(boot::RACE + 4);
  SetupColours(s_map.colours);
  std::string why;
  std::optional<mm::Road> road;
  if (const std::vector<u8> gma = mm::ReadPreviewFile(course, &why); !gma.empty())
    road = mm::ReadPreview(gma, course, &why);
  if (!road)
  {
    Warn("fzgx: course map: no map of course {}: {}", course, why);
    return;
  }
  std::array<u8, mm::CAMERA_SIZE> bytes{};
  const u32 index = course < mm::CAMERA_COURSES ? course : 0;  // (the game's own clamp)
  g.CopyFrom(bytes.data(), module + game::MAP_CAMERAS + index * mm::CAMERA_SIZE, mm::CAMERA_SIZE);
  const std::optional<mm::Camera> camera = mm::CameraFrom(bytes.data());
  s_map.camera = camera.value_or(mm::Fallback(*road));
  MinimapCourse prepared = mm::Prepare(*road, s_map.camera, course);
  Notice("fzgx: course map: course {}, {} triangles, {} outline edges, {}", course, prepared.triangles.size() / 3,
         prepared.edges.size() / 2, camera ? "through the game's map camera" : "the game's map camera unusable: from above");
  std::string no_start;
  if (const std::vector<u8> coli = mm::ReadCourseFile(course, &no_start); !coli.empty())
    s_map.start = mm::ReadStart(coli, &no_start);
  if (s_map.start)
  {
    const std::optional<mm::Across> across = mm::RoadAcross(*road, *s_map.start);
    const mm::Road mark = mm::StartMark(*s_map.start, across, s_map.camera, mm::PennantRight(course));
    mm::AddMark(mark, s_map.camera, prepared);
    const mm::Projected p = mm::Project(s_map.camera, s_map.start->at);
    Notice("fzgx: course map: start line at ({:.1f}, {:.1f}, {:.1f}) facing ({:.2f}, {:.2f}, {:.2f}), on the map at "
           "({:.3f}, {:.3f}); the road across it {}", s_map.start->at.x, s_map.start->at.y, s_map.start->at.z,
           s_map.start->forward.x, s_map.start->forward.y, s_map.start->forward.z, p.x, p.y,
           across ? std::format("{:.1f} to {:.1f} m", across->left, across->right)
                  : std::format("not found in the preview: the checkpoint's {:.1f} m", s_map.start->width));
  }
  else
    Warn("fzgx: course map: no start mark on course {}: {}", course, no_start);
  s_map.course = course;
  TheHost().MinimapCourseLoaded(prepared);
}

// The racer the picture follows (see the head).
u32 You(Guest& g)
{
  const Session* s = s_session.get();
  if (s && s->spectator.shown)
    return s->spectator.followed;
  if (g.Read32(SPECTATING) != 0)
    return g.Read32(SPECTATED);
  return g.Read32(SEAT_SLOT + 4);
}

mm::Racers ReadRacers(Guest& g, u32 module, u32 machines, u32 count)
{
  mm::Racers racers{};
  const auto f = [&](u32 at) { return std::bit_cast<float>(g.Read32(at)); };
  for (u32 i = 0; i < count; ++i)
  {
    const u32 base = machines + i * game::MACHINE_SIZE;
    const u32 progress = g.Read32(base + game::MACHINE_PROGRESS);
    if (progress == 0)
      continue;
    mm::Racer& r = racers[i];
    r.present = true;
    r.out = out_test::Out(g, module, base);
    r.finished = (g.Read32(base) & FINISHED) != 0;
    r.rank = g.Read8(progress + game::PROGRESS_RANK);
    r.at = {f(base + game::MACHINE_DRAWN_X), f(base + game::MACHINE_DRAWN_Y), f(base + game::MACHINE_DRAWN_Z)};
    r.colour = s_map.colours.colour[i];
  }
  return racers;
}

// The start line against the racers (for the log): at the race's first displayed frame their places along the line's
// direction (behind it: below 0), and START_CHECK displayed frames on how far each has gone along it.
constexpr u32 START_CHECK = 120;
void CheckStart(const mm::Racers& racers, u32 count)
{
  const mm::Start& st = *s_map.start;
  const auto along = [&](const mm::Vec& p) {
    return (p.x - st.at.x) * st.forward.x + (p.y - st.at.y) * st.forward.y + (p.z - st.at.z) * st.forward.z;
  };
  if (s_map.race_seen++ == 0)
  {
    s_map.at_go = racers;
    return;
  }
  if (s_map.race_seen <= START_CHECK)
    return;
  u32 racing = 0, behind = 0, ahead = 0;
  float least = 1e30f, most = -1e30f;
  for (u32 i = 0; i < count; ++i)
  {
    const mm::Racer &a = s_map.at_go[i], &b = racers[i];
    if (!a.present || !b.present || a.out || b.out)
      continue;
    ++racing;
    behind += along(a.at) < 0;
    const float gone = along(b.at) - along(a.at);
    ahead += gone > 0;
    least = std::min(least, gone), most = std::max(most, gone);
  }
  s_map.start_check = std::format("{} of {} racers behind the line at the start, {} gone along it {} frames on "
                                  "({:.0f} to {:.0f} m)", behind, racing, ahead, START_CHECK, least, most);
  Notice("fzgx: course map: start line check: {}", s_map.start_check);
}

void Tell(const MinimapShow& show)
{
  if (!show.on && !s_map.shown)
    return;
  s_map.shown = show.on;
  TheHost().ShowMinimap(show);
}
}  // namespace

void MinimapFrame(Guest& g)
{
  if (!g.Ram(MODULE_POINTER, 4) || g.Read32(MODULE_POINTER) != layout::MAIN_REL)
    return;
  const u32 module = layout::MAIN_REL;
  // the game's map picture: never drawn
  if (const u32 parts = g.Read32(module + game::HUD_PARTS); parts & mm::MAP_PART)
    g.Write32(module + game::HUD_PARTS, mm::Hidden(parts));
  if (const u32 kept = g.Read32(HUD_KEPT); kept & mm::MAP_PART)
    g.Write32(HUD_KEPT, mm::Hidden(kept));

  const u32 phase = g.Read16(module + PHASE);
  if (phase < COURSE_INTRO)
    return;
  if (!s_map.loaded)
    Load(g, module);
  if (s_map.course == 0)
    return;
  const Session* s = s_session.get();
  mm::Seen seen;
  seen.phase = phase;
  seen.hud_parts = g.Read32(module + game::HUD_PARTS);
  seen.watched = s && s->spectator.shown;
  seen.overlay = !s || s->spectator.overlay;
  seen.practice_paused = s && s->practice && PracticePaused();
  MinimapShow show;
  s_map.race_frames += phase == 0x28 || phase == 0x29;
  const u32 machines = g.Read32(module + MACHINES);
  const u32 count = std::min<u32>(g.Read8(module + MACHINE_COUNT), 30);
  if (phase == game::PHASE_RACE && machines != 0 && count != 0 && s_map.start && s_map.race_seen <= START_CHECK)
    CheckStart(ReadRacers(g, module, machines, count), count);
  if (mm::Shows(seen) && machines != 0 && count != 0)
  {
    show = mm::Pick(ReadRacers(g, module, machines, count), count, You(g), s_map.camera);
    show.on = true;
    // the view's turn: port 1's C-stick left or right, unless the player's controls give the C-stick a racing action
    std::array<u8, mm::BINDING_SETS * mm::BINDING_SET_SIZE> bindings{};
    g.CopyFrom(bindings.data(), module + BSS + mm::BINDINGS, static_cast<u32>(bindings.size()));
    if (!mm::CstickBound(bindings.data()))
    {
      show.turn = mm::TurnOf(static_cast<std::int8_t>(g.Read8(module + PAD_STATUS + 4)));
      if (!seen.watched)  // (the spectator controls' C up and down choose the camera's height)
        show.tilt = mm::TiltOf(static_cast<std::int8_t>(g.Read8(module + PAD_STATUS + 5)));
    }
    s_map.turned_frames += show.turn != 0 || show.tilt != 0;
    ++s_map.shown_frames;
    s_map.most_dots = std::max(s_map.most_dots, show.count);
    for (u32 k = 0; k < show.count; ++k)
    {
      s_map.you_frames += show.dots[k].role == MinimapDot::You;
      s_map.leader_frames += show.dots[k].role == MinimapDot::Leader;
    }
  }
  show.course = s_map.course;
  Tell(show);
}

void MinimapSummary()
{
  if (s_map.course == 0)
    return;
  Notice("fzgx: course map: shown in {} of {} frames of the countdown and the race; at most {} dots; a dot of yours "
         "in {}, the leader's in {}; the view turned or tilted in {}", s_map.shown_frames, s_map.race_frames,
         s_map.most_dots, s_map.you_frames, s_map.leader_frames, s_map.turned_frames);
}
}  // namespace fzgx::net
