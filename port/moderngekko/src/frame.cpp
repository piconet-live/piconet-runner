// The game window's frame is ours: no title bar and no menu of the system's, but a thin strip in the launcher's look
// (the name, four buttons: full screen, minimise, maximise, close) and the game's picture under it, in a child
// window that is the video backend's surface. The emulator's window stays the window (forks/recompcore/2-port,
// Common/WindowFrame.h): it keeps its style, so moving, resizing from the edges, snapping, maximising and
// minimising are the system's own, and it keeps its other keys and closing. Full screen, where the window has no
// frame at all, and the window's kept place are frame_screen.cpp's.
// This file answers what the system asks of such a window: how big its inside is (the whole window, less the
// system's invisible edges), what a point of it is, and what a click on a button does. Where the strip's parts are
// is the core's (fzgx/strip.h, tested without a window); frame_paint.cpp draws them.
// Nothing here touches the game: the picture's surface is a window of another size, as after any resize.
// With the launch file's "debug.windowControls" the window is the system's, with its menu bar: nothing is given.
// SPDX-License-Identifier: GPL-3.0-or-later

#ifdef _WIN32

#include "adapter_internal.h"
#include "frame.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <iterator>
#include <string_view>

#include <dwmapi.h>
#include <windowsx.h>

#include "Common/WindowFrame.h"
#include "Core/System.h"
#include "VideoCommon/PerformanceMetrics.h"
#include "VideoCommon/Present.h"

namespace fzgx_port
{
namespace
{
namespace strip = fzgx::strip;
using strip::Part;

constexpr wchar_t PICTURE_CLASS[] = L"FzgxPicture";
constexpr UINT_PTR FRAME_RATE_TIMER = 0x4652;  // the strip's frame rate is read twice a second
constexpr DWORD BORDER_COLOUR_ATTRIBUTE = 34;  // DWMWA_BORDER_COLOR (Windows 11; older systems refuse it, harmlessly)

struct Frame
{
  // The child window the game's picture is drawn on. Made and destroyed on the window's thread only (WM_CREATE,
  // WM_DESTROY); the emulator asks for it from its own threads too (Surface), which only read it.
  std::atomic<HWND> picture{nullptr};
  frame::Shown shown;
  bool show_title = false;       // a test game: its window title, which names the test, is in the strip
  bool show_frame_rate = false;  // the launch file's "showFrameRate"
  bool tracking = false;         // the system tells us when the pointer leaves the strip
};
Frame s_frame;

bool IsButton(Part part)
{
  return part == Part::Fullscreen || part == Part::Minimize || part == Part::Maximize || part == Part::Close;
}

LRESULT Default(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
  return IsWindowUnicode(window) ? DefWindowProcW(window, message, wparam, lparam) :
                                   DefWindowProcA(window, message, wparam, lparam);
}

// Full screen (frame_screen.cpp) takes the frame's styles off, and puts them back.
bool Fullscreen(HWND window)
{
  return (GetWindowLongPtrW(window, GWL_STYLE) & WS_THICKFRAME) == 0;
}

bool Maximized(HWND window)
{
  WINDOWPLACEMENT placement{};
  placement.length = sizeof(placement);
  return GetWindowPlacement(window, &placement) && placement.showCmd == SW_SHOWMAXIMIZED;
}

int Dpi(HWND window)
{
  const UINT dpi = GetDpiForWindow(window);
  return dpi != 0 ? static_cast<int>(dpi) : 96;
}

// The thickness of the system's resize edge, which a maximised window hangs over its screen by.
int EdgeX(int dpi)
{
  return GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
}
int EdgeY(int dpi)
{
  return GetSystemMetricsForDpi(SM_CYSIZEFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
}

void Redraw(HWND window)
{
  RECT inside{};
  GetClientRect(window, &inside);
  inside.bottom = s_frame.shown.layout.height;
  InvalidateRect(window, &inside, FALSE);
}

// The strip for the window as it is now, and the picture's window under it (the whole inside in full screen).
void Lay(HWND window)
{
  RECT inside{};
  GetClientRect(window, &inside);
  frame::Shown& shown = s_frame.shown;
  shown.dpi = Dpi(window);
  shown.maximized = Maximized(window);
  shown.layout = strip::Lay(inside.right, shown.dpi, s_frame.show_frame_rate, Fullscreen(window));
  if (shown.layout.height == 0)
    shown.over = shown.down = Part::Outside;
  // (the replay scrubber's bar takes the bottom of the inside, in full screen too)
  const int bar = std::min<int>(ReplayBarHeight(shown.dpi), std::max<int>(inside.bottom - shown.layout.height, 0));
  if (const HWND picture = s_frame.picture.load())
    SetWindowPos(picture, nullptr, 0, shown.layout.height, inside.right,
                 std::max<int>(inside.bottom - shown.layout.height - bar, 0), SWP_NOZORDER | SWP_NOACTIVATE);
  LayReplayBar(inside.bottom - bar, inside.right, bar);
  Redraw(window);
}

// The picture's window: a point of it is the game window's (so a click there reaches the window as before), and a
// new size is told to the video backend, as the game window's own was.
LRESULT CALLBACK PictureProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
  if (message == WM_NCHITTEST)
    return HTTRANSPARENT;
  if (message == WM_SIZE && g_presenter)
    g_presenter->ResizeSurface();
  return DefWindowProcW(window, message, wparam, lparam);
}

// On the window's thread only (OnCreate).
void MakePicture(HWND window)
{
  if (s_frame.picture.load())
    return;
  WNDCLASSEXW picture{};
  picture.cbSize = sizeof(picture);
  picture.lpfnWndProc = PictureProc;
  picture.hInstance = GetModuleHandleW(nullptr);
  picture.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  picture.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
  picture.lpszClassName = PICTURE_CLASS;
  RegisterClassExW(&picture);  // a second window of the program finds the class there
  s_frame.picture.store(CreateWindowExW(0, PICTURE_CLASS, L"", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window, nullptr,
                                        GetModuleHandleW(nullptr), nullptr));
}

// A new title. The runner appends the frame rate to it every second (" | 59.9 FPS"); the taskbar and the window
// switcher show the title, so a window whose strip does not show the frame rate gets its title without it.
template <class Char>
LRESULT SetTitle(HWND window, WPARAM wparam, LPARAM lparam)
{
  const std::basic_string_view<Char> title(reinterpret_cast<const Char*>(lparam));
  const std::size_t at = strip::FrameRateInTitle(title);
  if (at == title.npos)
    return Default(window, WM_SETTEXT, wparam, lparam);
  const std::basic_string<Char> cut(title.substr(0, at));
  return Default(window, WM_SETTEXT, wparam, reinterpret_cast<LPARAM>(cut.c_str()));
}

// The window was made with the size the settings name, which is in 1/96 inch. The runner scales itself
// (runner.manifest), so on a scaled screen that many pixels would be a smaller window than before: it is made as
// many pixels larger (strip::FirstSize) and kept inside its screen's free area. On an unscaled screen nothing moves.
void ScaleFirstSize(HWND window)
{
  const int dpi = Dpi(window);
  RECT now{};
  MONITORINFO screen{};
  screen.cbSize = sizeof(screen);
  if (dpi == 96 || !GetWindowRect(window, &now) ||
      !GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &screen))
    return;
  const RECT& area = screen.rcWork;
  const strip::Size size = strip::FirstSize(now.right - now.left, now.bottom - now.top, dpi, area.right - area.left,
                                            area.bottom - area.top);
  const int left = std::max<int>(area.left, std::min<int>(now.left, area.right - size.width));
  const int top = std::max<int>(area.top, std::min<int>(now.top, area.bottom - size.height));
  SetWindowPos(window, nullptr, left, top, size.width, size.height, SWP_NOZORDER | SWP_NOACTIVATE);
}

// The window exists and is not yet shown: its first size on a scaled screen, its picture's window, the border in
// the strip's line colour, the fonts, the app's icon and the launcher's taskbar button (taskbar.cpp), and the
// frame rate's clock.
void OnCreate(HWND window)
{
  // where it last was (a player's window), or its first size on a scaled screen
  if (!frame::RestorePlace(window, strip::Scaled(strip::HEIGHT, Dpi(window))))
    ScaleFirstSize(window);
  DressWindow(window);
  frame::LoadLook(s_state.options.window_name_picture);
  MakePicture(window);
  MakeReplayBar(window);
  const COLORREF border = RGB((overlay::RAISED >> 16) & 0xFF, (overlay::RAISED >> 8) & 0xFF, overlay::RAISED & 0xFF);
  DwmSetWindowAttribute(window, BORDER_COLOUR_ATTRIBUTE, &border, sizeof(border));
  if (s_frame.show_frame_rate)
    SetTimer(window, FRAME_RATE_TIMER, 500, nullptr);
  // the system measures the frame anew, with our answer for certain (OnCalcSize), and the strip is laid then
  SetWindowPos(window, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
  Lay(window);
}

// The window's inside is the whole window at the top (our strip is the title bar) and the system's at the sides
// and the bottom, where its resize edges are and stay invisible. A maximised window hangs over its screen by the
// edge on every side: the top is brought in by as much, so the strip starts at the screen's first row.
bool OnCalcSize(HWND window, WPARAM wparam, LPARAM lparam, LRESULT* result)
{
  if (Fullscreen(window))
    return false;
  // the window's rectangle comes in and its inside goes out: the first rectangle of the three when the system
  // asks with them (wparam true), the only one when it asks with one (as it does once, when the window is made)
  RECT* inside = wparam != FALSE ? &reinterpret_cast<NCCALCSIZE_PARAMS*>(lparam)->rgrc[0] : reinterpret_cast<RECT*>(lparam);
  const LONG top = inside->top;
  Default(window, WM_NCCALCSIZE, wparam, lparam);
  inside->top = top + (Maximized(window) ? EdgeY(Dpi(window)) : 0);
  *result = 0;
  return true;
}

Part PartAtScreen(HWND window, LPARAM lparam)
{
  POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
  ScreenToClient(window, &point);
  RECT inside{};
  GetClientRect(window, &inside);
  const frame::Shown& shown = s_frame.shown;
  const int grip = shown.maximized ? 0 : GetSystemMetricsForDpi(SM_CYSIZEFRAME, shown.dpi);
  return strip::PartAt(shown.layout, inside.bottom, point.x, point.y, grip);
}

// What a point is, in the system's words. The sides and the bottom are the system's own answer. The full-screen
// button has no word of the system's: it is "border", which the system does nothing with by itself.
bool OnHitTest(HWND window, LPARAM lparam, LRESULT* result)
{
  if (Fullscreen(window))
    return false;
  *result = Default(window, WM_NCHITTEST, 0, lparam);
  if (*result != HTCLIENT)
    return true;
  switch (PartAtScreen(window, lparam))
  {
  case Part::Top: *result = HTTOP; break;
  case Part::TopLeft: *result = HTTOPLEFT; break;
  case Part::TopRight: *result = HTTOPRIGHT; break;
  case Part::Bar: *result = HTCAPTION; break;
  case Part::Minimize: *result = HTMINBUTTON; break;
  case Part::Maximize: *result = HTMAXBUTTON; break;  // the system's own snap choices open over it
  case Part::Close: *result = HTCLOSE; break;
  case Part::Fullscreen: *result = HTBORDER; break;
  default: *result = HTCLIENT; break;
  }
  return true;
}

void Press(HWND window, Part button)
{
  switch (button)
  {
  case Part::Minimize: PostMessageW(window, WM_SYSCOMMAND, SC_MINIMIZE, 0); break;
  case Part::Maximize: PostMessageW(window, WM_SYSCOMMAND, Maximized(window) ? SC_RESTORE : SC_MAXIMIZE, 0); break;
  case Part::Close: NoteStripClose(), PostMessageW(window, WM_CLOSE, 0, 0); break;
  case Part::Fullscreen: PostMessageW(window, WM_COMMAND, WindowFrame::TOGGLE_FULLSCREEN, 0); break;
  default: break;
  }
}

// The mouse over the strip. The buttons are ours from the press to the release (the system would draw its own old
// buttons there); the rest of the strip is the system's: it drags, maximises on a double click, opens its menu.
bool OnMouse(HWND window, UINT message, LPARAM lparam, LRESULT* result)
{
  frame::Shown& shown = s_frame.shown;
  if (message == WM_NCMOUSELEAVE)
  {
    s_frame.tracking = false;
    if (shown.over != Part::Outside || shown.down != Part::Outside)
      Redraw(window);
    shown.over = shown.down = Part::Outside;
    return false;
  }
  const Part at = PartAtScreen(window, lparam);
  const Part button = IsButton(at) && !Fullscreen(window) ? at : Part::Outside;
  const Part was_over = shown.over, was_down = shown.down;
  shown.over = button;
  if (message == WM_NCLBUTTONDOWN || message == WM_NCLBUTTONDBLCLK)
    shown.down = button;
  else if (message == WM_NCLBUTTONUP)
    shown.down = Part::Outside;
  if (shown.over != was_over || shown.down != was_down)
    Redraw(window);
  // told when the pointer leaves the strip, from the first move over a button on (a press that came with no move
  // before it, as a program posts one, asks for nothing: with the pointer elsewhere the leave would come at once)
  if (message == WM_NCMOUSEMOVE && button != Part::Outside && !s_frame.tracking)
  {
    TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE | TME_NONCLIENT, window, 0};
    s_frame.tracking = TrackMouseEvent(&track) != FALSE;
  }
  if (message == WM_NCLBUTTONUP && button != Part::Outside && button == was_down)
    Press(window, button);
  // a button, or the release of a press that began on one: not the system's to act on
  if (button == Part::Outside && !(message == WM_NCLBUTTONUP && was_down != Part::Outside))
    return false;
  *result = 0;
  return true;
}

void OnPaint(HWND window)
{
  PAINTSTRUCT paint{};
  const HDC dc = BeginPaint(window, &paint);
  frame::Shown& shown = s_frame.shown;
  shown.title.clear();
  if (s_frame.show_title)
  {
    wchar_t title[256] = {};
    GetWindowTextW(window, title, static_cast<int>(std::size(title)));
    shown.title = title;
  }
  frame::Paint(window, dc, shown);
  EndPaint(window, &paint);
}

// The smallest window: the strip's buttons with room for a name, over the smallest picture.
void OnLeastSize(HWND window, LPARAM lparam)
{
  if (Fullscreen(window))
    return;
  const int dpi = Dpi(window);
  auto* sizes = reinterpret_cast<MINMAXINFO*>(lparam);
  sizes->ptMinTrackSize.x = strip::LeastWidth(dpi) + 2 * EdgeX(dpi);
  sizes->ptMinTrackSize.y = strip::LeastHeight(dpi) + EdgeY(dpi);
}

void OnFrameRateTimer(HWND window)
{
  const double measured = Core::System::GetInstance().GetPerfMetrics().GetFPS();
  const int frame_rate = std::isfinite(measured) && measured > 0 ? static_cast<int>(std::lround(measured)) : 0;
  if (frame_rate == s_frame.shown.frame_rate)
    return;
  s_frame.shown.frame_rate = frame_rate;
  Redraw(window);
}

// The window is on a monitor of another scale now, and the system offers the rectangle that keeps its size there.
// Not taken for a window being put back at its kept place, which already has the rectangle it had on that monitor:
// the offer, the one before scaled to the new monitor, would make it larger or smaller at every start.
void OnDpiChanged(HWND window, LPARAM lparam)
{
  if (frame::PuttingBack())
    return;
  const RECT* wanted = reinterpret_cast<const RECT*>(lparam);
  SetWindowPos(window, nullptr, wanted->left, wanted->top, wanted->right - wanted->left, wanted->bottom - wanted->top,
               SWP_NOZORDER | SWP_NOACTIVATE);
}

// Every message of the game's window, before the emulator's own handling (WindowFrame::Host::message).
bool Message(void* handle, unsigned message, std::uintptr_t wparam, std::intptr_t lparam, std::intptr_t* answer)
{
  const HWND window = static_cast<HWND>(handle);
  LRESULT result = 0;
  bool ours = false;
  switch (message)
  {
  case WM_NCCREATE:
    // the system's sunken edge around the inside goes; the strip is not drawn over the picture's window
    SetWindowLongPtrW(window, GWL_EXSTYLE, GetWindowLongPtrW(window, GWL_EXSTYLE) & ~WS_EX_CLIENTEDGE);
    SetWindowLongPtrW(window, GWL_STYLE, GetWindowLongPtrW(window, GWL_STYLE) | WS_CLIPCHILDREN);
    break;
  case WM_CREATE: OnCreate(window); break;
  case WM_CLOSE: frame::KeepPlace(window); break;  // (the emulator's own handling then stops the game)
  case WM_KEYDOWN:
  case WM_KEYUP:
  case WM_SYSKEYDOWN:
  case WM_COMMAND:
    ours = ReplayKeyMessage(message, wparam, lparam) || frame::FullscreenKey(window, message, wparam, lparam);
    break;
  case WM_MOUSEMOVE: case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_MOUSELEAVE: case WM_CAPTURECHANGED:
    ours = SpectatorMouse(window, message, lparam); break;  // (the overlays over the picture)
  case WM_ENTERSIZEMOVE:
  case WM_EXITSIZEMOVE:
    frame::PlaceChanged(window, message);
    break;
  case frame::PLACE_RESTORED:
    frame::PlaceChanged(window, message);
    ours = true;
    break;
  case WM_DESTROY:
    frame::KeepPlace(window);
    KillTimer(window, FRAME_RATE_TIMER);
    UndressWindow(window);
    s_frame.picture.store(nullptr);
    break;
  case WM_NCCALCSIZE: ours = OnCalcSize(window, wparam, lparam, &result); break;
  case WM_NCHITTEST: ours = OnHitTest(window, lparam, &result); break;
  case WM_NCMOUSEMOVE:
  case WM_NCMOUSELEAVE:
  case WM_NCLBUTTONDOWN:
  case WM_NCLBUTTONDBLCLK:
  case WM_NCLBUTTONUP:
    ours = OnMouse(window, message, lparam, &result);
    break;
  case WM_NCACTIVATE:
    // the system would draw its own title bar's new state over ours: -1 tells it not to draw
    result = Default(window, message, wparam, -1);
    ours = true;
    break;
  case WM_SETTEXT:
    // the same for a new title (the runner writes one a second); a test game's title is in the strip
    if (s_frame.show_frame_rate || lparam == 0)
      result = Default(window, message, wparam, lparam);
    else
      result = IsWindowUnicode(window) ? SetTitle<wchar_t>(window, wparam, lparam) : SetTitle<char>(window, wparam, lparam);
    Redraw(window);
    ours = true;
    break;
  case WM_WINDOWPOSCHANGED:
  {
    // the system's own handling tells the window its new size; then the strip is laid again, also when only the
    // frame changed (going to full screen and back does not always change the size). A minimised window keeps
    // what it had: its picture's window is not made empty for the time it is away.
    const UINT flags = reinterpret_cast<const WINDOWPOS*>(lparam)->flags;
    result = Default(window, message, wparam, lparam);
    if (((flags & SWP_NOSIZE) == 0 || (flags & SWP_FRAMECHANGED) != 0) && !IsIconic(window))
      Lay(window);
    frame::PlaceChanged(window, message);
    ours = true;
    break;
  }
  case WM_DPICHANGED:
    OnDpiChanged(window, lparam);
    ours = true;
    break;
  case WM_GETMINMAXINFO: OnLeastSize(window, lparam); break;
  case WM_ERASEBKGND:
    result = 1;  // the strip is drawn whole, and the rest of the inside is the picture's window
    ours = true;
    break;
  case WM_PAINT:
    OnPaint(window);
    ours = true;
    break;
  case WM_TIMER:
    if (wparam == FRAME_RATE_TIMER)
      OnFrameRateTimer(window);
    ours = wparam == FRAME_RATE_TIMER || frame::FadeTick(window, wparam, s_frame.shown.layout.height);
    break;
  default: break;
  }
  *answer = result;
  return ours;
}

// Asked from the emulator's threads as well as the window's (at boot, and with every new title): only what the
// window's thread made is handed out, and no window is ever made here. None: the picture is drawn on the game's
// window itself (the emulator's own fallback).
void* Surface(void*)
{
  return s_frame.picture.load();
}

std::wstring Wide(const std::string& utf8)
{
  const int length = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
  std::wstring wide(static_cast<std::size_t>(std::max(length, 0)), L'\0');
  if (length > 0)
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), length);
  return wide;
}
}  // namespace

void GiveWindowFrame()
{
  const fzgx::Options& options = s_state.options;
  const std::vector<std::string> tests = fzgx::DebugSettingsOn(options.debug);
  // the developer's window: the system's own frame, with the menu bar the window's test controls are in
  if (std::find(tests.begin(), tests.end(), "debug.windowControls") != tests.end())
    return;
  s_frame.shown.name = Wide(options.window_name);
  frame::KeepsPlace(tests.empty());
  s_frame.show_title = !tests.empty();
  s_frame.show_frame_rate = options.show_frame_rate;
  s_frame.shown.frame_rate = options.show_frame_rate ? 0 : -1;
  WindowFrame::Host& host = WindowFrame::Get();
  host.message = WatchStops(&Message);  // (freeze_watch.cpp: a close says what asked for it)
  host.surface = &Surface;
}
}  // namespace fzgx_port

#else

namespace fzgx_port
{
void GiveWindowFrame()
{
}
}  // namespace fzgx_port

#endif
