// The game window's title strip: its look and where its parts are, as plain numbers with no window in them. The
// runner's adapter draws the strip and answers the system's questions about it from here (port/moderngekko/src,
// frame*.cpp); the tests check this file against no window at all.
// The look is the launcher's own title bar, value for value: the sizes of net/src/Launcher.App/Theme/Theme.axaml,
// the window buttons of Theme/Buttons.axaml (Button.chrome) and the bar of Views/MainWindow.axaml. A change there
// is made here too, by hand: the two programs share no file. The colours are the runner's palette,
// port/moderngekko/src/overlay.h (the one place for every colour the runner draws).
// Lengths are in the launcher's units, 1/96 inch, and become pixels at the window's dots per inch (Scaled).
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string_view>

namespace fzgx::strip
{
// Lengths.
constexpr int HEIGHT = 32;        // the bar, its line included
constexpr int LINE_WIDTH = 1;     // the line under it
constexpr int BUTTON_WIDTH = 46;  // Button.chrome
constexpr int DRAWING = 10;       // a button's line drawing, a square
constexpr int NAME_LEFT = 16;     // the name's margin
constexpr int NAME_SIZE = 16;     // the name, in the display face (leaning)
constexpr int SMALL_SIZE = 13;    // Size13: the small text (a test game's title, the frame rate)
constexpr int GAP = 12;           // between the name and the text after it, and before the buttons
constexpr int FRAME_RATE_WIDTH = 56;  // room for "999 FPS" in the small size
constexpr int WORDS_LEAST = 96;   // the name's room below which the frame rate gives way
constexpr int GRIP = 4;           // the bar's top edge that resizes the window (the system's own frame is as thick)
constexpr int PICTURE_LEAST_WIDTH = 320, PICTURE_LEAST_HEIGHT = 240;  // the smallest picture a window may be dragged to

// The font files, beside the runner in a folder Fonts: the launcher's own (net/src/Launcher.App/Assets/Fonts; the
// SIL Open Font Licence, OFL.txt, goes with them; port/build/stage-launcher.sh and package.py carry the three).
// Without them the strip is drawn in the system's font, and the overlays in the emulator's.
constexpr char DISPLAY_FONT[] = "SairaCondensed-ExtraBold.ttf";    // DisplayFont: the display role, leaning
constexpr char FIGURE_FONT[] = "SairaCondensed-SemiBold.ttf";      // TextBlock.figure: figures, a name
constexpr char TEXT_FONT[] = "SairaSemiCondensed-Regular.ttf";     // BodyFont: everything read

struct Box
{
  int left = 0, top = 0, right = 0, bottom = 0;
  bool Empty() const { return right <= left || bottom <= top; }
  bool Holds(int x, int y) const { return x >= left && x < right && y >= top && y < bottom; }
};

// What a point of the window's inside is.
enum class Part
{
  Outside,     // not in the window
  Picture,     // the game's picture, under the bar
  Bar,         // the bar itself: dragging it moves the window, a double click maximises
  Fullscreen,  // the four buttons, left to right
  Minimize,
  Maximize,
  Close,
  Top,         // the bar's top edge, which resizes; its two ends resize two ways
  TopLeft,
  TopRight,
};

// Where the frame rate begins that the runner appends to the window's title every second (" | 59.9 FPS", at the
// title's end), or npos when the title has none. A window whose strip does not show the frame rate does not show
// it in its title either: the taskbar and the window switcher read the title.
template <class Char>
constexpr std::size_t FrameRateInTitle(std::basic_string_view<Char> title)
{
  constexpr std::size_t none = std::basic_string_view<Char>::npos;
  const auto digit = [&](std::size_t i) { return title[i] >= Char('0') && title[i] <= Char('9'); };
  std::size_t at = title.size();
  for (const char letter : {'S', 'P', 'F', ' '})
  {
    if (at == 0 || title[--at] != Char(letter))
      return none;
  }
  const std::size_t number_end = at;
  while (at > 0 && (digit(at - 1) || title[at - 1] == Char('.')))
    --at;
  if (at == number_end || !digit(at) || !digit(number_end - 1))
    return none;
  for (const char letter : {' ', '|', ' '})
  {
    if (at == 0 || title[--at] != Char(letter))
      return none;
  }
  return at;
}

// Where the strip's parts are in a window whose inside is `width` pixels wide. In full screen there is no strip:
// `height` is 0, every box is empty, and the picture is the whole window.
struct Layout
{
  int width = 0;
  int height = 0;       // the bar with its line; the picture starts under it
  int line = 0;         // the line's thickness
  Box fullscreen, minimize, maximize, close;
  Box words;            // the name, and after it a test game's title: from the left margin to the frame rate
  Box frame_rate;       // the frame rate, set to the right; empty when it is not shown
};

// A length of the launcher's at `dpi` dots per inch, in whole pixels (at least one for a length that is not zero).
int Scaled(int length, int dpi);

// The window's first size on a screen of `dpi` dots per inch. The runner scales itself (its program file says so
// to Windows, runner.manifest), so a size is in the screen's own pixels; the size the settings name is in the
// launcher's units, 1/96 inch, as it always was. On an unscaled screen it is that size, pixel for pixel. On a
// scaled one the window is made as many pixels larger and so covers the same share of the screen as it did when
// Windows stretched it, but never more than the screen's free area (`most_width`, `most_height`; 0: no limit).
struct Size
{
  int width = 0, height = 0;
};
Size FirstSize(int width, int height, int dpi, int most_width, int most_height);

// The strip of a window `width` pixels wide. `frame_rate`: the player asked for it; in a window too narrow for
// the name and the frame rate together the frame rate gives way.
Layout Lay(int width, int dpi, bool frame_rate, bool fullscreen);

// What the point (x, y) of the window's inside is. `grip`: the height in pixels of the top edge that resizes; 0
// in a maximised window, which has none. The edge counts before a button under it, as in the system's own bar.
Part PartAt(const Layout& layout, int height, int x, int y, int grip);

// The smallest inside a window may be dragged to: the four buttons with a name's room, over the smallest picture.
int LeastWidth(int dpi);
int LeastHeight(int dpi);
}  // namespace fzgx::strip
