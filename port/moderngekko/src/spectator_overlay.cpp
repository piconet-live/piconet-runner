// The spectator overlay (fzgx/host.h SpectatorShow, SpectatorClick; the core's spectator_frame.cpp and
// observer_frame.cpp): over the game's picture while a race is watched, the observer HUD (observer_hud.h: the timing
// tower, the camera groups, the racer card, the course map in its corner; or the game's own HUD; or the clean
// picture), drawn with the emulator's own on-screen drawing, as the panel before a race is (panel.cpp).
//   - What it shows comes from the core once a displayed frame, on the game's thread (ShowSpectatorOverlay); it is
//     drawn before every present, on the thread that presents (DrawOverlays); the mouse comes from the game window's
//     messages, on the window's thread (SpectatorMouse): the picture's window lets them through to it.
//   - DrawOverlays draws every overlay over the picture in order: the replay bar and its timeline lanes (on the
//     foreground list: over everything; not in the clean picture), the course map's ground, our course map
//     (minimap.cpp: in the observer HUD's corner, clear of the bar, or the game's map corner otherwise, sliding
//     between them), then the HUD. SpectatorMouse hands the game window's mouse to them in the same order.
//   - The layout the drawing used is kept for the mouse; every change of view fades (overlay.h's motion).
//   - Nothing here reads or writes the game.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "adapter_internal.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <utility>

#include <imgui.h>

#include "minimap_draw.h"
#include "observer_hud.h"
#include "overlay.h"
#include "VideoCommon/Present.h"

#include "fzgx/spectator.h"

#ifdef _WIN32
#include <windows.h>
#include <windowsx.h>
#endif

namespace fzgx_port
{
namespace
{
namespace sp = fzgx::spectator;
namespace ov = overlay;
namespace hud = observer_hud;
constexpr fzgx::u32 NONE = 0xFFFFFFFF;

struct Overlay
{
  std::mutex lock;
  fzgx::SpectatorShow shown;      // as the core last told it
  fzgx::SpectatorShow last;       // ... the last that showed something (drawn while it fades out)
  fzgx::SpectatorClick clicked;   // what the mouse asked, not yet taken
  hud::Layout laid;               // the layout as last drawn (the mouse's)
  bool drawn = false;
  float hover_x = -1, hover_y = -1;
  // the drawing thread's: the views' fades, and where the course map goes
  ov::Eased observer, mode, clean, ghost;
  ov::Box map_to;
  float map_mix = 0;
};
Overlay s_overlay;

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

bool Clean(const fzgx::SpectatorShow& show)
{
  return show.on && show.view == static_cast<fzgx::u32>(sp::View::Clean);
}
}  // namespace

void ShowSpectatorOverlay(const fzgx::SpectatorShow& show)
{
  {
    std::lock_guard<std::mutex> hold(s_overlay.lock);
    s_overlay.shown = show;
  }
  ReplayLanesFollow(show.on ? show.follow : NONE, !show.on || show.may_scrub);
}

fzgx::SpectatorClick TakeSpectatorClicks()
{
  std::lock_guard<std::mutex> hold(s_overlay.lock);
  return std::exchange(s_overlay.clicked, fzgx::SpectatorClick{});
}

ov::Box MovedMapSquare(const ov::Box& square)
{
  const float m = s_overlay.map_mix;
  if (m <= 0.0f)
    return square;
  const float x0 = std::round(square.x0 + (s_overlay.map_to.x0 - square.x0) * m);
  const float y0 = std::round(square.y0 + (s_overlay.map_to.y0 - square.y0) * m);
  return {x0, y0, x0 + (square.x1 - square.x0), y0 + (square.y1 - square.y0)};
}

void DrawOverlays()
{
  if (ImGui::GetCurrentContext() == nullptr)
    return;
  fzgx::SpectatorShow show;
  float hx, hy;
  {
    std::lock_guard<std::mutex> hold(s_overlay.lock);
    show = s_overlay.shown;
    if (show.on)
      s_overlay.last = show;
    hx = s_overlay.hover_x;
    hy = s_overlay.hover_y;
  }
  // the replay bar and its lanes (none in the clean picture)
  const ImVec2 display = ImGui::GetIO().DisplaySize;
  BarPlace place;
  float bar = 0;
  if (!Clean(show))
  {
    bar = DrawReplayBar(&place);
    DrawReplayLanes(place, bar);
  }
  const float clear = bar > 0 ? bar * (display.y - place.top + ReplayLanesHeight()) : 0.0f;
  // the views' fades
  const auto now = ov::Eased::Clock::now();
  const fzgx::u32 view = show.on ? show.view : NONE;
  s_overlay.observer.Send(view == static_cast<fzgx::u32>(sp::View::Observer) ? 1.0f : 0.0f, now);
  s_overlay.mode.Send(show.on && !Clean(show) ? 1.0f : 0.0f, now);
  s_overlay.clean.Send(Clean(show) ? 1.0f : 0.0f, now);
  hud::Fades a{s_overlay.observer.At(now), s_overlay.mode.At(now), s_overlay.clean.At(now), 0};
  const ov::Box picture = Picture(display);
  const ov::Box corner = minimap_draw::Place(picture);
  const hud::Layout l = hud::Lay(show, picture, clear, corner.x1 - corner.x0);
  s_overlay.ghost.Send(l.ghost.Has(hx, hy) ? 1.0f : 0.0f, now);
  a.ghost_hover = s_overlay.ghost.At(now);
  // the course map: its ground, then the map, moved into the HUD's corner as the HUD comes
  s_overlay.map_to = l.square;
  s_overlay.map_mix = a.observer;
  ImDrawList* list = ImGui::GetBackgroundDrawList();
  if (show.on)
    hud::DrawGround(list, l, a.observer);
  DrawMinimap();
  {
    const float h = picture.y1 - picture.y0;
    DrawInputs(corner.x0 - 0.02f * h, picture.y1 - clear - 0.02f * h, h);
  }
  if (show.on)
    hud::Draw(list, show, l, a, hx, hy);
  else if (a.observer > 0.004f || a.mode > 0.004f || a.clean > 0.004f)
  {
    fzgx::SpectatorShow last;
    {
      std::lock_guard<std::mutex> hold(s_overlay.lock);
      last = s_overlay.last;
    }
    last.role = 0;  // (the knocked-out mark goes at once)
    hud::Draw(list, last, hud::Lay(last, picture, clear, corner.x1 - corner.x0), a, -1, -1);
  }
  std::lock_guard<std::mutex> hold(s_overlay.lock);
  s_overlay.laid = l;
  s_overlay.drawn = show.on;
}

bool SpectatorMouse(void* handle, unsigned message, std::intptr_t lparam)
{
#ifdef _WIN32
  const HWND window = static_cast<HWND>(handle);
  static bool tracking = false;
  if (message == WM_MOUSEMOVE && !tracking)
  {
    TRACKMOUSEEVENT leave{};
    leave.cbSize = sizeof(leave);
    leave.dwFlags = TME_LEAVE;
    leave.hwndTrack = window;
    tracking = TrackMouseEvent(&leave) != FALSE;
  }
  if (message == WM_MOUSELEAVE)
  {
    tracking = false;
    ReplayBarMouse(handle, message, -1, -1);
    ReplayLanesMouse(message, -1, -1, nullptr);
    std::lock_guard<std::mutex> hold(s_overlay.lock);
    s_overlay.hover_x = s_overlay.hover_y = -1;
    return false;
  }
  POINT at{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
  // the picture's window: the overlay's pixels are its own
  if (const HWND picture = FindWindowExW(window, nullptr, L"FzgxPicture", nullptr))
    MapWindowPoints(window, picture, &at, 1);
  const float x = static_cast<float>(at.x), y = static_cast<float>(at.y);
  bool clean;
  {
    std::lock_guard<std::mutex> hold(s_overlay.lock);
    clean = Clean(s_overlay.shown);
  }
  if (!clean)
  {
    if (ReplayBarMouse(handle, message, x, y))
      return true;
    fzgx::SpectatorClick lane;
    if (ReplayLanesMouse(message, x, y, &lane))
    {
      std::lock_guard<std::mutex> hold(s_overlay.lock);
      fzgx::SpectatorClick& c = s_overlay.clicked;
      if (lane.follow != NONE)
        c.follow = lane.follow;
      if (lane.jump != NONE)
        c.jump = lane.jump;
      return true;
    }
  }
  std::lock_guard<std::mutex> hold(s_overlay.lock);
  const fzgx::SpectatorShow& show = s_overlay.shown;
  if (!show.on)
    return false;
  s_overlay.hover_x = x;
  s_overlay.hover_y = y;
  if (message != WM_LBUTTONDOWN || !s_overlay.drawn)
    return false;
  fzgx::SpectatorClick hit;
  if (!hud::Hit(show, s_overlay.laid, x, y, &hit))
    return false;
  fzgx::SpectatorClick& c = s_overlay.clicked;
  if (hit.follow != NONE)
    c.follow = hit.follow;
  if (hit.kind >= 0)
    c.kind = hit.kind;
  c.overlay = c.overlay || hit.overlay;
  c.leader = c.leader || hit.leader;
  c.battle = c.battle || hit.battle;
  return true;
#else
  (void)handle, (void)message, (void)lparam;
  return false;
#endif
}
}  // namespace fzgx_port
