// Our course map over the game's picture (fzgx/host.h MinimapCourse, MinimapShow; the core's fzgx/minimap.h decides
// what it shows and when): in the corner where the game's own map picture sat (bottom right, above the speed), a
// square two thirds of its size, clear of the speed figure: the course's road in 3D as the game's map camera sees it,
// one silhouette in the game's map colour (white, translucent), every racer a dot of its machine's colour; you and
// the leader bigger, you on top with a white ring.
//   - The road comes once a course and the dots once a displayed frame, from the core on the game's thread
//     (MinimapCourseLoaded, ShowMinimap); both are drawn before every present on the thread that presents
//     (DrawMinimap, from DrawOverlays, under the replay bar and the spectator overlay).
//   - Placed on the game's picture (the presenter's target rectangle: a window of another shape has bars beside or
//     above the picture) and drawn by minimap_draw.cpp with the emulator's own on-screen drawing (ImGui's background
//     list, on the GPU, as the other overlays): the road a picture made once a course and size (anti-aliased by
//     4x4 samples), then the dots.
//   - It eases in and out with the HUD (overlay.h's motion: 120 ms, ease-out).
//   - The C-stick held left or right turns the view about the course, up or down tilts it (fzgx/minimap.h ViewTurn:
//     within the view's limits, the course scaled to fit the square; let go, back to the game's view in 250 ms),
//     stepped here by the time between drawings.
//   - Nothing here reads or writes the game.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "adapter_internal.h"

#include <chrono>
#include <memory>
#include <mutex>

#include <imgui.h>

#include "fzgx/minimap.h"
#include "minimap_draw.h"
#include "observer_hud.h"
#include "overlay.h"
#include "VideoCommon/Present.h"

namespace fzgx_port
{
namespace
{
namespace ov = overlay;

struct Map
{
  std::mutex lock;
  std::shared_ptr<const fzgx::MinimapCourse> course;  // the road (none: no map)
  fzgx::MinimapShow shown;                            // the dots, as the core last told them
  ov::Eased fade;                                     // 0 hidden to 1 shown
  minimap_draw::RoadPicture road;                     // the road's picture (the drawing thread's alone)
  fzgx::minimap::ViewTurn view;                       // the view's turn (the drawing thread's alone)
  fzgx::minimap::ViewTurn tilt;                       // ... and tilt
  ov::Eased::Clock::time_point drawn{};               // when it was last drawn
};
Map s_map;

// The game's picture in the display's pixels: the presenter's target rectangle, or the whole display before it has one.
ov::Box Picture(const ImVec2& display)
{
  if (g_presenter)
  {
    const auto& r = g_presenter->GetTargetRectangle();
    if (r.GetWidth() > 0 && r.GetHeight() > 0)
      return {static_cast<float>(r.left), static_cast<float>(r.top), static_cast<float>(r.right),
              static_cast<float>(r.bottom)};
  }
  return {0, 0, display.x, display.y};
}
}  // namespace

void MinimapCourseLoaded(const fzgx::MinimapCourse& course)
{
  auto copy = std::make_shared<const fzgx::MinimapCourse>(course);
  std::lock_guard<std::mutex> hold(s_map.lock);
  s_map.course = std::move(copy);
}

void ShowMinimap(const fzgx::MinimapShow& show)
{
  std::lock_guard<std::mutex> hold(s_map.lock);
  if (show.on || !s_map.shown.on)
    s_map.shown = show;
  else
    s_map.shown.on = false;  // (the dots stay where they were while it fades)
  s_map.fade.Send(show.on ? 1.0f : 0.0f, ov::Eased::Clock::now());
}

void DrawMinimap()
{
  if (ImGui::GetCurrentContext() == nullptr)
    return;
  std::shared_ptr<const fzgx::MinimapCourse> course;
  fzgx::MinimapShow show;
  float alpha;
  {
    std::lock_guard<std::mutex> hold(s_map.lock);
    alpha = s_map.fade.At(ov::Eased::Clock::now());
    if (!s_map.course || alpha <= 0.004f)
      return;
    course = s_map.course;
    show = s_map.shown;
  }
  if (show.course != course->course)
    return;
  const ov::Box picture = Picture(ImGui::GetIO().DisplaySize);
  // (the observer HUD moves it to its own corner: spectator_overlay.cpp)
  const ov::Box square = MovedMapSquare(minimap_draw::Place(picture));
  if (square.Empty())
    return;
  // the view's turn by the time since the last drawing (ViewTurn takes at most a quarter second of it: a map that
  // shows again after a while is back at the game's angle)
  const auto now = ov::Eased::Clock::now();
  const bool first = s_map.drawn == ov::Eased::Clock::time_point{};
  const float seconds = first ? 0 : std::chrono::duration<float>(now - s_map.drawn).count();
  s_map.drawn = now;
  minimap_draw::View view;
  view.angle = s_map.view.Step(show.on ? show.turn : 0.0f, seconds);
  view.tilt = s_map.tilt.Step(show.on ? show.tilt : 0.0f, seconds,
                              fzgx::minimap::TiltRange(fzgx::minimap::CameraOf(*course)), fzgx::minimap::TILT_SPEED);
  minimap_draw::Draw(ImGui::GetBackgroundDrawList(), s_map.road, course, show, square, alpha, view);
}
}  // namespace fzgx_port
