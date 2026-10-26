// The game window's frame, between its three files: frame.cpp is the window's side (what the system asks of a window
// whose title bar is ours: its size, what a point of it is, the buttons under the mouse), frame_paint.cpp draws
// the strip, and frame_screen.cpp has full screen and the window's kept place. Where the strip's parts are, and its colours and sizes, is the core's (fzgx/strip.h). Windows only.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#ifdef _WIN32

#include <string>

#include <windows.h>

#include "fzgx/core.h"
#include "fzgx/strip.h"
#include "overlay.h"  // the strip's colours

namespace fzgx_port::frame
{
// What the strip shows at this moment.
struct Shown
{
  fzgx::strip::Layout layout;  // where its parts are; height 0: full screen, nothing is drawn
  int dpi = 96;                // the window's dots per inch
  bool maximized = false;      // the maximise button is then "restore"
  fzgx::strip::Part over = fzgx::strip::Part::Outside;  // the button under the pointer, or none
  fzgx::strip::Part down = fzgx::strip::Part::Outside;  // the button the mouse went down on, until it is let go
  std::wstring name;           // the launch file's "windowName"; drawn only when there is no picture of it
  std::wstring title;          // a test game's window title, after the name; a player's strip has none
  int frame_rate = -1;         // pictures a second; -1: not shown
};

// Starts the drawing library and reads the font files beside the runner (Fonts\: fzgx/strip.h names them), once.
// `name`: the launch file's "windowNamePicture", the name as the launcher draws it (in the game's lettering, from
// the player's disc); with one the strip shows it in place of the name's text.
void LoadLook(const fzgx::WindowPicture& name);
// Draws the strip at the top of the window's inside. While a button's light eases, a timer of the window's
// (FadeTick) paints the strip again at the screen's pace.
void Paint(HWND window, HDC dc, const Shown& shown);
// A tick of the window's timers: true when it was the strip's own (the strip, `strip_height` high, is painted again).
bool FadeTick(HWND window, UINT_PTR timer, int strip_height);

// frame_screen.cpp, on the window's thread.
// Full screen: the window without a frame, the size of its monitor, and back as it was; the video backend is told
// (screen.cpp) and the place is kept.
bool InFullscreen();
void SetFullscreen(HWND window, bool on);
// A message that is one of the ways into full screen or out of it (F11, Alt+Enter, the window's command, Escape in
// full screen): acted on, and true. Anything else: false, and the window's own handling goes on.
bool FullscreenKey(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
// The window's kept place (fzgx/place.h). KeepsPlace: before the window is made; a player's window keeps its place,
// a test game's does not. RestorePlace: the window exists and is not yet shown: put where it last was, when that is
// on a monitor of now (true); it is maximised or goes to full screen once it is shown. PlaceChanged: after the
// window's position or state changed, and at the start and end of a drag. KeepPlace: written now, when it differs
// from what is kept.
// PLACE_RESTORED: the window's own message that ends a restore (the frame hands it to PlaceChanged).
constexpr UINT PLACE_RESTORED = WM_APP + 0x65A;
// PuttingBack: from RestorePlace until that message: the window's rectangle is the kept one, in the pixels of the
// monitor it was kept on, and is not to be scaled again when the system says the window's dots per inch changed
// (it was made on the main monitor and moved to its own).
void KeepsPlace(bool keeps);
bool PuttingBack();
bool RestorePlace(HWND window, int strip_height);
void PlaceChanged(HWND window, UINT message);
void KeepPlace(HWND window);
}  // namespace fzgx_port::frame

#endif
