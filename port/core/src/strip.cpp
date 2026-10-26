// The game window's title strip as numbers (fzgx/strip.h): where its parts are at a width and a scaling, and what
// a point of the window is. No window, no drawing: the adapter does both from what this file answers.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fzgx/strip.h"

#include <algorithm>
#include <initializer_list>

namespace fzgx::strip
{
int Scaled(int length, int dpi)
{
  if (length <= 0)
    return 0;
  return std::max(1, (length * std::max(dpi, 1) + 48) / 96);
}

Size FirstSize(int width, int height, int dpi, int most_width, int most_height)
{
  if (dpi <= 96)
    return {width, height};
  Size size{Scaled(width, dpi), Scaled(height, dpi)};
  if (most_width > 0)
    size.width = std::min(size.width, std::max(most_width, width));
  if (most_height > 0)
    size.height = std::min(size.height, std::max(most_height, height));
  return size;
}

Layout Lay(int width, int dpi, bool frame_rate, bool fullscreen)
{
  Layout l;
  l.width = std::max(width, 0);
  if (fullscreen)
    return l;
  l.height = Scaled(HEIGHT, dpi);
  l.line = Scaled(LINE_WIDTH, dpi);
  const int face = l.height - l.line;  // a button stands on the line, as the launcher's do
  const int button = Scaled(BUTTON_WIDTH, dpi);
  // the buttons from the right edge; in a window narrower than the four, the left ones are cut off at its edge
  int right = l.width;
  for (Box* box : {&l.close, &l.maximize, &l.minimize, &l.fullscreen})
  {
    *box = {std::max(right - button, 0), 0, std::max(right, 0), face};
    right -= button;
  }
  const int gap = Scaled(GAP, dpi);
  const int left = Scaled(NAME_LEFT, dpi);
  int words_right = l.fullscreen.left - gap;
  const int rate = Scaled(FRAME_RATE_WIDTH, dpi);
  if (frame_rate && words_right - rate - gap - left >= Scaled(WORDS_LEAST, dpi))
  {
    l.frame_rate = {words_right - rate, 0, words_right, face};
    words_right = l.frame_rate.left - gap;
  }
  if (words_right > left)
    l.words = {left, 0, words_right, face};
  return l;
}

Part PartAt(const Layout& layout, int height, int x, int y, int grip)
{
  if (x < 0 || y < 0 || x >= layout.width || y >= height)
    return Part::Outside;
  if (y >= layout.height)
    return Part::Picture;
  if (y < grip)
  {
    if (x < grip)
      return Part::TopLeft;
    return x >= layout.width - grip ? Part::TopRight : Part::Top;
  }
  if (layout.close.Holds(x, y))
    return Part::Close;
  if (layout.maximize.Holds(x, y))
    return Part::Maximize;
  if (layout.minimize.Holds(x, y))
    return Part::Minimize;
  if (layout.fullscreen.Holds(x, y))
    return Part::Fullscreen;
  return Part::Bar;
}

int LeastWidth(int dpi)
{
  const int bar = Scaled(NAME_LEFT, dpi) + Scaled(WORDS_LEAST, dpi) + Scaled(GAP, dpi) + 4 * Scaled(BUTTON_WIDTH, dpi);
  return std::max(bar, Scaled(PICTURE_LEAST_WIDTH, dpi));
}

int LeastHeight(int dpi)
{
  return Scaled(HEIGHT, dpi) + Scaled(PICTURE_LEAST_HEIGHT, dpi);
}
}  // namespace fzgx::strip
