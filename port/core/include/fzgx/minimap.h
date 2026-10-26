// Our own course map in a race: the game's map
// marks only seat 0 and the best computer racer, so its picture is hidden and the host draws ours over the game's
// picture, in the same corner: the course's road in 3D as the game's own map camera sees it, every racer a dot of its
// machine's colour, three of them bigger (you, the leader, the rival: the rival's place is kept, nothing fills it yet).
//
// This is the decision alone, as plain numbers with no game in it, so that the road's reader, the view, the dots'
// pick and order and when the map shows are tested without one (tests/test_minimap.cpp). minimap_frame.cpp reads the
// game and tells the host (Host::MinimapCourseLoaded, Host::ShowMinimap).
//
// The road: the course select's low-detail model of it (stage/selstNN.gma, packed), a band of about a thousand
// corners in the course's own coordinates, the racers' too, except five stored moved to the world's middle (Offset,
// the launcher's PreviewPlacement: the same numbers, held equal by a test). Chosen over the collision course's centre
// line and width (COLI_COURSENN): the model is drawn for a map at this scale, pipes and cylinders and every road of a
// split already shaped, and the launcher's race page shows it with live racers, held to every
// course's road on the real disc (RealDiscCourseMapTests); a ribbon from the checkpoints would be straight pieces
// between sparse centres, flat across pipes.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "fzgx/host.h"

namespace fzgx::minimap
{
struct Vec
{
  float x = 0, y = 0, z = 0;
};

// The road's model: corners and triangles (three corners each).
struct Road
{
  std::vector<Vec> corners;
  std::vector<u32> triangles;
};

// What a course's preview is moved by to lie on its road (the launcher's PreviewPlacement); zero for most.
Vec Offset(u32 course);

// The course select's model (stage/selstNN.gma, unpacked) as the road: every model of the file but its grid (a name
// with "GRID"), static meshes' triangle strips (GxUtils' layout of the GCMF block, as piconet-tools' GmaReader reads it),
// moved by Offset(course). Nothing, and `why` says it, when the file is not such a model or holds no triangle.
std::optional<Road> ReadPreview(const std::vector<u8>& gma, u32 course, std::string* why);

// The game's map camera for a course (main.rel .data 0x1DFB0, 0x1C bytes a course: the field of view in degrees,
// then the eye and the point it looks at), which the game's map picture and its markers are drawn through
// (fn_1_7926C, fn_1_D07E0) on a square picture.
struct Camera
{
  float fov = 30;
  Vec eye, at;
  float spin = 0;  // degrees the picture is turned about its middle (Turned, for a camera looking straight down)
};
constexpr u32 CAMERA_COURSES = 46;  // the game's table: a course number from 46 on takes course 0's camera
constexpr u32 CAMERA_SIZE = 0x1C;
// The camera from its 0x1C bytes (big-endian floats). None when it cannot look (not finite, the eye on the point, a
// field of view outside 1 to 170 degrees).
std::optional<Camera> CameraFrom(const u8* bytes);
// The view when the game's camera is unusable: the road's middle seen from 50 degrees above, its whole extent in view.
Camera Fallback(const Road& road);

// A point through the camera onto the square map: x right and y up, -1 to 1 across the square at the field of view
// (the game's picture's edges); `depth` is the distance in front of the eye (0 or less: behind it, not shown).
struct Projected
{
  float x = 0, y = 0, depth = 0;
};
Projected Project(const Camera& camera, const Vec& point);
// `count` points (x, y, z each) through the camera at once, as Project: x, y each into `xy`, not a number for a point
// behind the eye.
void ProjectMany(const Camera& camera, const float* xyz, std::size_t count, float* xy);

// The road for the host (MinimapCourse): projected, the triangles behind the eye left out, the rest far ones first
// (the map draws them in that order: a road that passes over another hides it), each with its shade in a light from
// above (as the launcher's map: 0.55 edge-on to 1 facing it, either side), and the band's outline: the edges only one
// triangle has.
MinimapCourse Prepare(const Road& road, const Camera& camera, u32 course);

// The start mark (minimap_start.cpp): the game's map picture marks the start/finish line
// in yellow, a bar across the road with a pennant at one end pointing the way the race runs; ours is a bar and an
// arrow, in 3D over the road (it turns with the view). The line: where lap distance 0 begins, the course file's
// first track node's first checkpoint (its start plane: the road's middle and the forward normal; file::COLI_*). On
// every course of the disc it is the course's origin, facing -z; the racers' coordinates are the course file's.
struct Start
{
  Vec at;               // the road's middle on the line
  Vec forward;          // the way the race runs (unit)
  float width = 0;      // the checkpoint's road width (the widest of a split start's)
};
// From the course file (stage/COLI_COURSEnn, unpacked). None, and `why` says it, when it holds no usable line.
std::optional<Start> ReadStart(const std::vector<u8>& coli, std::string* why);
// The road's extent across the line as the preview draws it: the start plane through the road's triangles near the
// line, the stretch that holds its middle (metres right of `at`, left negative), and the height of its top over
// `at` (a cylinder's start lies on its axis). None when the plane meets no road there.
struct Across
{
  float left = 0, right = 0, top = 0;
};
std::optional<Across> RoadAcross(const Road& road, const Start& start);
// The mark's shape, after the game's map pictures (C??_MAP's colour picture on every course's stNN.tpl, measured
// against this line and the road on all 31 pool courses, and laid beside ours by a probe),
// in shares of the map's side at the line, since the game paints it the same size on a thin road and a wide one: a
// bar MARK_THICK deep across the road, MARK_OVER_FLAG past the road's edge on the arrow's side and MARK_OVER past
// the other; from the bar's end on the arrow's side a shaft as thick along the road, ending in a half arrowhead
// (as the game's: only the barb away from the road) MARK_HEAD long and MARK_HEAD_WIDTH across from the shaft's road
// side, which runs straight on to the tip MARK_TIP ahead of the line: it points the way the race runs (the game's
// points forward on every course). In the game's yellow (file::MAP_START_COLOUR), opaque.
constexpr float MARK_THICK = 0.024f, MARK_OVER_FLAG = 0.045f, MARK_OVER = 0.027f;
constexpr float MARK_TIP = 0.15f, MARK_HEAD = 0.055f, MARK_HEAD_WIDTH = 0.06f;
// The arrow's side, the game's own per course (its pennant's; it differs: left of the road on course 1, right on
// course 16): on the right on 16, 17, 21, 24, 26, 27, 28, 29, 31, 32, 36 and 37, on the left on every other.
bool PennantRight(u32 course);
// The mark's corners in the course and its triangles, seen through `camera` (the game's map camera: the sizes are
// taken at the line's depth); `across` the road's extent there (none: the checkpoint's width about the middle);
// the arrow on the right of the road (seen the way the race runs) or the left.
Road StartMark(const Start& start, const std::optional<Across>& across, const Camera& camera, bool pennant_right);
// The mark into the host's course (MinimapCourse::mark, mark_world, mark_triangles), projected through `camera`.
void AddMark(const Road& mark, const Camera& camera, MinimapCourse& out);

// A racer as the map reads it.
struct Racer
{
  bool present = false;  // a machine with a progress record
  bool out = false;      // out of the race (the game's verdict): no dot
  bool finished = false;
  u32 rank = 0;          // its place, 0 first
  Vec at;                // the drawn body's place (the machine's drawn matrix: rollback smoothing included)
  u32 colour = 0;        // 0xRRGGBB
};
using Racers = std::array<Racer, 30>;
constexpr u32 NOBODY = 0xFFFFFFFF;

// The dots, in the order they are drawn (the last on top): every racer still racing, then the leader, then you; the
// rival's place between the others and the leader is kept (none yet). Out racers have none; finished racers have
// none either (the map shows the race still run), but you keep yours. The leader: the first placed of those still
// racing; when that is you, your dot is the one. `you`: the racer the picture follows (NOBODY: none).
MinimapShow Pick(const Racers& racers, u32 count, u32 you, const Camera& camera);

// Turning and tilting the view: while the C-stick is held left or right the map's
// camera turns about the looked-at point (around the course's up axis), at up to TURN_SPEED degrees a second by the
// stick's tilt; held up it tilts flatter (more from the side), down towards straight down, at up to TILT_SPEED. Let
// go, each eases back to the game's own view in TURN_BACK seconds. Picture only: with the default controls the race
// never reads the C-stick and the online input record (.bss 0xA88) never carries it; when the player's settings bind
// it to a racing action (CstickBound) the stick moves nothing, and while the spectator controls are up (their C up
// and down choose the camera's height) it does not tilt.
constexpr float TURN_SPEED = 180;  // degrees a second at full tilt
constexpr float TILT_SPEED = 90;
constexpr float TURN_BACK = 0.25f;  // seconds back to the game's view
// The view's limits, a design choice: the turn at most TURN_LIMIT degrees either way from the game's view; the
// tilt from straight down (the eye at most TILT_STEEPEST degrees above the looked-at point: at 90 the view's sides
// would be undefined) to TILT_FLATTER degrees flatter than the game's map camera, never lower than TILT_LOWEST
// degrees above the road's plane. Near a limit the view slows and eases into it (it never stops dead).
constexpr float TURN_LIMIT = 90, TILT_FLATTER = 30, TILT_LOWEST = 10, TILT_STEEPEST = 89.5f;
// The C-stick as the game clamps it (PADStatus, .bss 0x998: x at +4, y at +5, up positive; PADClamp leaves -59 to
// 59, 0 at rest): a dead zone so a stick at rest never moves the view, full speed from TURN_FULL out.
constexpr float TURN_DEAD = 10, TURN_FULL = 52;
// The view's turn asked from the stick's x, -1 (left) to 1 (right); its tilt from its y, -1 (down: steeper) to 1
// (up: flatter); 0 inside the dead zone.
float TurnOf(std::int8_t cstick_x);
inline float TiltOf(std::int8_t cstick_y)
{
  return TurnOf(cstick_y);
}
// Port 1's control bindings (.bss 0xAA0, 0x54 bytes: three sets of 0x1C, one a controller setting of the options,
// fn_1_21E4 reads them; defaults .rodata 0x108): four 16-bit rows (a stick's axis: 0x40 and 0x80 are the C-stick's)
// and five 32-bit rows (buttons: 0xF0000 are the C-stick's directions). Whether any set binds the C-stick.
constexpr u32 BINDINGS = 0xAA0, BINDING_SETS = 3, BINDING_SET_SIZE = 0x1C;
bool CstickBound(const u8* bindings);
// The course's map camera as MinimapCourse carries it; that camera turned by `degrees` about the looked-at point
// (the eye kept at its height and distance); tilted by `degrees` flatter (negative: steeper) about the looked-at
// point (the distance kept, the eye's elevation kept between TILT_LOWEST and TILT_STEEPEST); and both, as drawn.
Camera CameraOf(const MinimapCourse& course);
Camera Turned(const Camera& camera, float degrees);
Camera Tilted(const Camera& camera, float degrees);
Camera ViewOf(const MinimapCourse& course, float turn, float tilt);
// The eye's elevation above the looked-at point, in degrees (90: straight down).
float Elevation(const Camera& camera);
// The tilt's range for a camera, in degrees: down to straight down (negative), up to TILT_FLATTER flatter or the
// lowest elevation, whichever comes first (0 when the camera is already there).
struct Range
{
  float lo = 0, hi = 0;
};
Range TiltRange(const Camera& camera);
// The view's angle in degrees on one axis, stepped by the time a drawn frame took: moving while `move` is not 0
// (by `speed` a second at full tilt, slowed near the range's ends: moving out, the speed falls with the room left,
// so it eases into a limit and never passes it), else easing back to 0 (ease-out over TURN_BACK).
struct ViewTurn
{
  float angle = 0, back_from = 0, back_time = 0;
  float Step(float move, float seconds, Range range = {-TURN_LIMIT, TURN_LIMIT}, float speed = TURN_SPEED);
};
// The map's fit: the scale (at most 1) that brings every point (x, y each, -1 to 1 the square's edges; not a number:
// left out) inside the square about its middle. A turned or tilted view can reach past the square the game's view
// fills; drawn at this scale no road is cut off. It moves with the view smoothly (the largest of continuous values).
float FitScale(const std::vector<float>& points);

// Whether the map shows this frame, as the game's HUD does.
struct Seen
{
  u32 phase = 0;            // the race mode's phase: the countdown (0x28) and the race (0x29) show it
  u32 hud_parts = 0;        // the HUD part mask of viewport 0 (.data 0x3D544), the map's own bit cleared or not
  bool watched = false;     // the spectator controls' overlay is up (a replay as the race, a spectator, a racer out)
  bool overlay = true;      // ... and shown (Z, the overlay's Hide): with it the map
  bool practice_paused = false;  // a practice stands at a moment (its bar is up)
};
bool Shows(const Seen& seen);

// The game's map part of the HUD part mask (fn_1_79654 tests it): cleared so that the game draws no map picture.
// Only that routine reads the bit (every reader of .data 0x3D544 in main.rel); the game writes the mask only with
// 0 (out, finished), its own .data value, or ANDs (the results keep 0x3C2): HudOn tells the HUD on from the other
// bits, so clearing this one changes nothing else.
constexpr u32 MAP_PART = 0x00800000;
constexpr u32 RESULTS_PARTS = 0x3FF;  // the low parts the results keep; the race's HUD has bits above them
constexpr u32 Hidden(u32 parts)
{
  return parts & ~MAP_PART;
}
constexpr bool HudOn(u32 parts)
{
  return (parts & ~RESULTS_PARTS) != 0;
}
}  // namespace fzgx::minimap
