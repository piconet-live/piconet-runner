// The game window's full screen, and the place the window had when it was last open (frame.h; frame.cpp is the
// window's frame and calls in here). On the window's thread only.
// Full screen is a window with no frame, the size of its monitor: the frame's styles are taken off and the window is
// laid over the monitor, and put back as it was (its place, its size, maximised or not) on the way out. That much is
// what the emulator's own window did (DolphinNoGUI/PlatformWin32.cpp, ToggleFullscreen); with the frame ours, the
// three ways into it (F11, Alt+Enter, the strip's button) and Escape out of it are answered here, because two more
// things hang on it: the video backend is told (screen.cpp: it asks for the backend's own, exclusive full screen
// while the window has the focus, and falls back to this borderless window where the driver refuses), and the
// window's place is kept.
// The kept place (fzgx/place.h: the file's text, and whether a kept window is on a monitor of now) is written into
// the game's user folder when the window was moved or resized, was maximised or put back, went to full screen or
// left it, and when it closes; a player's window opens there at the next start, maximised or in full screen as it
// was. A window whose monitor is gone opens at the usual place. A test game's window (a launch file with a "debug"
// setting) keeps no place and takes none: its test places it.
// SPDX-License-Identifier: GPL-3.0-or-later

#ifdef _WIN32

#include "adapter_internal.h"
#include "frame.h"

#include <optional>
#include <vector>

#include "Common/WindowFrame.h"

#include "fzgx/place.h"

namespace fzgx_port::frame
{
namespace
{
namespace place = fzgx::place;

struct Screen
{
  bool full = false;            // the window is in full screen
  LONG_PTR windowed_style = 0;  // its style and its rectangle before, put back on the way out
  RECT windowed_rect{};
  // Escape left full screen and is still held: its repeats are not the emulator's Escape, which closes the game
  bool escape_held = false;
  // What of the window's place is kept and when: a player's window keeps it, a drag writes at its end, a window
  // being put back writes nothing until it has its states (fzgx/place.h, where this is tested)
  place::Tracker place;
};
Screen s_screen;

std::string UserFolder()
{
  const std::u8string folder = s_state.user_folder.u8string();
  return std::string(folder.begin(), folder.end());
}

// Maximised, or minimised from maximised (it comes back so).
bool Maximized(HWND window)
{
  WINDOWPLACEMENT placement{};
  placement.length = sizeof(placement);
  if (!GetWindowPlacement(window, &placement))
    return false;
  return placement.showCmd == SW_SHOWMAXIMIZED ||
         (placement.showCmd == SW_SHOWMINIMIZED && (placement.flags & WPF_RESTORETOMAXIMIZED) != 0);
}

BOOL CALLBACK AddWorkArea(HMONITOR monitor, HDC, LPRECT, LPARAM areas)
{
  MONITORINFO info{};
  info.cbSize = sizeof(info);
  if (GetMonitorInfoW(monitor, &info))
    reinterpret_cast<std::vector<place::Rect>*>(areas)->push_back(
        {info.rcWork.left, info.rcWork.top, info.rcWork.right, info.rcWork.bottom});
  return TRUE;
}
}  // namespace

bool InFullscreen()
{
  return s_screen.full;
}

void SetFullscreen(HWND window, bool on)
{
  if (on == s_screen.full)
    return;
  if (on)
  {
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    if (!GetWindowRect(window, &s_screen.windowed_rect) ||
        !GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor))
      return;
    s_screen.windowed_style = GetWindowLongPtrW(window, GWL_STYLE);
    s_screen.full = true;
    SetWindowLongPtrW(window, GWL_STYLE, s_screen.windowed_style & ~static_cast<LONG_PTR>(WS_OVERLAPPEDWINDOW));
    SetWindowPos(window, HWND_TOP, monitor.rcMonitor.left, monitor.rcMonitor.top,
                 monitor.rcMonitor.right - monitor.rcMonitor.left, monitor.rcMonitor.bottom - monitor.rcMonitor.top,
                 SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
  }
  else
  {
    // the thread that presents is told first: a present that came between the window's new size and the telling
    // took the window for one in full screen still, and asked the backend for its own full screen again, for a
    // surface that is a window's
    TellFullscreen(false);
    s_screen.full = false;
    const RECT& was = s_screen.windowed_rect;
    SetWindowLongPtrW(window, GWL_STYLE, s_screen.windowed_style);
    SetWindowPos(window, nullptr, was.left, was.top, was.right - was.left, was.bottom - was.top,
                 SWP_NOOWNERZORDER | SWP_FRAMECHANGED | SWP_NOZORDER);
  }
  // (on the way in it is told last: the surface is the screen's size by then)
  if (on)
    TellFullscreen(true);
  KeepPlace(window);
}

bool FullscreenKey(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
  const bool held_down = (static_cast<ULONG_PTR>(lparam) & (1u << 30)) != 0;  // the key repeats: one press, one change
  if (message == WM_KEYDOWN && wparam == VK_F11)
  {
    if (!held_down)
      SetFullscreen(window, !s_screen.full);
    return true;
  }
  // Escape leaves full screen; in a window it is the emulator's own key, which closes the game. A key held down
  // repeats: the repeats of the press that left full screen are that press still, and close nothing. A new press,
  // or the key let go, ends that.
  if (message == WM_KEYUP && wparam == VK_ESCAPE)
    s_screen.escape_held = false;
  if (message == WM_KEYDOWN && wparam == VK_ESCAPE)
  {
    if (s_screen.full)
    {
      SetFullscreen(window, false);
      s_screen.escape_held = true;
      return true;
    }
    if (s_screen.escape_held && held_down)
      return true;
    s_screen.escape_held = false;
    // (the emulator's own handling of the key, next, stops the game: said before it does)
    if (!held_down)
      NoteStop("Escape was pressed in the game's window", fzgx::board::StopReason::Escape);
  }
  if (message == WM_SYSKEYDOWN && wparam == VK_RETURN && (GetKeyState(VK_MENU) & 0x8000) != 0)
  {
    if (!held_down)
      SetFullscreen(window, !s_screen.full);
    return true;
  }
  if (message == WM_COMMAND && LOWORD(wparam) == WindowFrame::TOGGLE_FULLSCREEN)
  {
    SetFullscreen(window, !s_screen.full);
    return true;
  }
  return false;
}

bool PuttingBack()
{
  return s_screen.place.Restoring();
}

void KeepsPlace(bool keeps)
{
  s_screen = {};
  s_screen.place = place::Tracker(keeps);
}

bool RestorePlace(HWND window, int strip_height)
{
  if (!s_screen.place.Keeps())
    return false;
  const std::optional<place::Kept> kept = place::ReadKept(UserFolder());
  if (!kept)
    return false;
  std::vector<place::Rect> areas;
  EnumDisplayMonitors(nullptr, nullptr, &AddWorkArea, reinterpret_cast<LPARAM>(&areas));
  if (!place::OnAScreen(kept->window, areas, strip_height))
  {
    s_core_host.Log(fzgx::LogLevel::Notice, "fzgx: the window's last place is on no monitor that is connected now: it opens "
                                            "at its usual place");
    return false;
  }
  // (from here until the last message below was acted on, the place kept is the one read: the window's own of
  // this moment, neither maximised nor in full screen yet, is not written over it)
  s_screen.place.BeginRestore(*kept);
  SetWindowPos(window, nullptr, kept->window.left, kept->window.top, kept->window.Width(), kept->window.Height(),
               SWP_NOZORDER | SWP_NOACTIVATE);
  // maximised and full screen once the window is shown: the messages wait in its queue until then, and the last
  // of them says that both were acted on
  if (kept->maximized)
    PostMessageW(window, WM_SYSCOMMAND, SC_MAXIMIZE, 0);
  if (kept->fullscreen)
    PostMessageW(window, WM_COMMAND, WindowFrame::TOGGLE_FULLSCREEN, 0);
  PostMessageW(window, PLACE_RESTORED, 0, 0);
  return true;
}

void KeepPlace(HWND window)
{
  if (!s_screen.place.Keeps())
    return;
  // the window as it is now: its rectangle is its own place while it is neither maximised, minimised nor in full
  // screen; in full screen "maximised" is what it was before it went there
  place::Seen seen;
  RECT rect{};
  seen.fullscreen = s_screen.full;
  seen.plain = !s_screen.full && !IsIconic(window) && !IsZoomed(window) && GetWindowRect(window, &rect) != FALSE;
  seen.rect = {rect.left, rect.top, rect.right, rect.bottom};
  seen.maximized = s_screen.full ? (s_screen.windowed_style & WS_MAXIMIZE) != 0 : Maximized(window);
  s_screen.place.Note(seen);
  if (const std::optional<place::Kept> now = s_screen.place.ToWrite(); now && place::WriteKept(UserFolder(), *now))
    s_screen.place.Written(*now);
}

void PlaceChanged(HWND window, UINT message)
{
  if (message == PLACE_RESTORED)
    s_screen.place.Restored();
  // (while the window is dragged its place changes with every move of the mouse: kept when the drag ends)
  if (message == WM_ENTERSIZEMOVE)
    s_screen.place.Dragging(true);
  else if (message == WM_EXITSIZEMOVE)
    s_screen.place.Dragging(false);
  KeepPlace(window);
}
}  // namespace fzgx_port::frame

#endif
