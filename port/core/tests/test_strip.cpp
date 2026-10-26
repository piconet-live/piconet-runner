// The game window's title strip as numbers (fzgx/strip.h): where its parts are, and what a point of the window is.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "harness.h"

#include "fzgx/strip.h"

using namespace fzgx::strip;

void TestTheStripsLayout()
{
  // the launcher's bar at the usual scaling: 32 high with its line, four buttons of 46 from the right edge
  const Layout l = Lay(1000, 96, false, false);
  CHECK(l.width == 1000 && l.height == 32 && l.line == 1);
  CHECK(l.close.left == 954 && l.close.right == 1000 && l.close.top == 0 && l.close.bottom == 31);
  CHECK(l.maximize.left == 908 && l.maximize.right == 954 && l.minimize.left == 862 && l.minimize.right == 908);
  CHECK(l.fullscreen.left == 816 && l.fullscreen.right == 862 && l.fullscreen.bottom == 31);
  // the name from its margin up to the buttons, less the gap; no frame rate unless asked for
  CHECK(l.words.left == 16 && l.words.right == 804 && l.frame_rate.Empty());
  const Layout rate = Lay(1000, 96, true, false);
  CHECK(rate.frame_rate.right == 804 && rate.frame_rate.left == 748 && rate.words.right == 736 && rate.words.left == 16);
  CHECK(rate.close.left == l.close.left && rate.fullscreen.left == l.fullscreen.left);

  // another scaling: every length grows with it
  const Layout big = Lay(1500, 144, true, false);
  CHECK(big.height == 48 && big.line == 2 && big.close.left == 1500 - 69 && big.close.bottom == 46);
  CHECK(big.fullscreen.left == 1500 - 4 * 69 && big.words.left == 24 && big.frame_rate.right == big.fullscreen.left - 18);
  CHECK(Scaled(0, 144) == 0 && Scaled(1, 96) == 1 && Scaled(1, 120) == 1 && Scaled(1, 144) == 2 && Scaled(46, 120) == 58);
  CHECK(Scaled(1, 48) == 1);  // a length that is not zero never becomes nothing

  // the scalings Windows offers, 125, 150 and 200 %: the bar, a button, the margin, the least window
  const Layout at120 = Lay(1250, 120, true, false), at192 = Lay(2000, 192, true, false);
  CHECK(at120.height == 40 && at120.line == 1 && at120.close.left == 1250 - 58 && at120.fullscreen.left == 1250 - 4 * 58);
  CHECK(at120.words.left == 20 && at120.frame_rate.right == at120.fullscreen.left - 15 && at120.frame_rate.left == at120.frame_rate.right - 70);
  CHECK(at192.height == 64 && at192.line == 2 && at192.close.left == 2000 - 92 && at192.fullscreen.left == 2000 - 4 * 92);
  CHECK(at192.words.left == 32 && at192.frame_rate.right == at192.fullscreen.left - 24 && at192.frame_rate.left == at192.frame_rate.right - 112);
  CHECK(LeastWidth(120) == 400 && LeastHeight(120) == 40 + 300 && LeastWidth(192) == 640 && LeastHeight(192) == 64 + 480);
  // the window's first size: the settings' own on an unscaled screen, whatever the screen's room; as many pixels
  // larger on a scaled one, and there never more than the screen's free area (nor less than the settings' size)
  const auto first = [](int dpi, int most_width, int most_height) { return FirstSize(1556, 934, dpi, most_width, most_height); };
  CHECK(first(96, 0, 0).width == 1556 && first(96, 0, 0).height == 934 && first(96, 1280, 680).width == 1556 && first(96, 1280, 680).height == 934);
  CHECK(first(120, 0, 0).width == 1945 && first(120, 0, 0).height == 1168 && first(144, 0, 0).width == 2334 && first(144, 0, 0).height == 1401);
  CHECK(first(192, 0, 0).width == 3112 && first(192, 0, 0).height == 1868);
  CHECK(first(144, 2560, 1392).width == 2334 && first(144, 2560, 1392).height == 1392);   // a 2560 x 1440 screen at 150 %
  CHECK(first(192, 3840, 2112).width == 3112 && first(192, 3840, 2112).height == 1868);   // a 3840 x 2160 screen at 200 %
  CHECK(first(120, 1536, 824).width == 1556 && first(120, 1536, 824).height == 934);      // a small screen: the settings' size
  CHECK(first(72, 0, 0).width == 1556);

  // a narrow window: the frame rate gives way to the name before the name loses its room
  const int least = LeastWidth(96);
  CHECK(least == 320 && LeastHeight(96) == 32 + 240 && LeastWidth(144) == 480 && LeastHeight(144) == 48 + 360);
  const Layout narrow = Lay(least, 96, true, false);
  CHECK(narrow.frame_rate.Empty() && narrow.words.left == 16 && narrow.words.right == 320 - 4 * 46 - 12);
  const Layout room = Lay(16 + 96 + 12 + 56 + 12 + 4 * 46, 96, true, false);
  CHECK(!room.frame_rate.Empty() && room.words.right - room.words.left == 96);
  CHECK(Lay(16 + 96 + 12 + 56 + 12 + 4 * 46 - 1, 96, true, false).frame_rate.Empty());
  // narrower than its buttons (the system can still make it so): nothing has a negative edge
  const Layout tiny = Lay(100, 96, true, false);
  CHECK(tiny.close.left == 54 && tiny.maximize.left == 8 && tiny.minimize.left == 0 && tiny.minimize.right == 8);
  CHECK(tiny.fullscreen.Empty() && tiny.words.Empty() && tiny.frame_rate.Empty());
  CHECK(Lay(-5, 96, true, false).width == 0);

  // full screen: no strip at all
  const Layout full = Lay(1920, 96, true, true);
  CHECK(full.width == 1920 && full.height == 0 && full.close.Empty() && full.fullscreen.Empty() && full.words.Empty() &&
        full.frame_rate.Empty());
}

// The frame rate the runner appends to the window's title: found at the title's end only, in either kind of text.
void TestTheFrameRateInATitle()
{
  using std::string_view;
  CHECK(FrameRateInTitle(string_view("F-Zero GX | 60.0 FPS")) == 9 && FrameRateInTitle(string_view("F-Zero GX | 7 FPS")) == 9);
  CHECK(FrameRateInTitle(std::wstring_view(L"Taskbar test | 59.9 FPS")) == 12);
  CHECK(FrameRateInTitle(string_view(" | 0.0 FPS")) == 0 && FrameRateInTitle(string_view("a | 120.25 FPS")) == 1);
  // a title without one, and what only looks like one
  for (const char* title : {"F-Zero GX", "", "FPS", " | FPS", "F-Zero GX | . FPS", "F-Zero GX | 60.0 FPS ", "F-Zero GX 60.0 FPS",
                            "F-Zero GX | 60. FPS", "F-Zero GX | .5 FPS", "F-Zero GX |60.0 FPS", "F-Zero GX | 60.0 fps", "60.0 FPS"})
    CHECK(FrameRateInTitle(string_view(title)) == string_view::npos);
  // a test's label that ends like a frame rate keeps what stands before the last one
  CHECK(FrameRateInTitle(string_view("run | 3 FPS | 58.1 FPS")) == 11);
}

void TestWhatAPointOfTheWindowIs()
{
  const Layout l = Lay(1000, 96, true, false);
  // the buttons, left to right, each from its first pixel to its last
  CHECK(PartAt(l, 600, 816, 10, 4) == Part::Fullscreen && PartAt(l, 600, 861, 30, 4) == Part::Fullscreen);
  CHECK(PartAt(l, 600, 862, 10, 4) == Part::Minimize && PartAt(l, 600, 908, 10, 4) == Part::Maximize);
  CHECK(PartAt(l, 600, 954, 10, 4) == Part::Close && PartAt(l, 600, 999, 30, 4) == Part::Close);
  // the bar: the name, the frame rate, the gap before the buttons, and the line under them all
  CHECK(PartAt(l, 600, 20, 10, 4) == Part::Bar && PartAt(l, 600, 760, 10, 4) == Part::Bar && PartAt(l, 600, 815, 10, 4) == Part::Bar);
  CHECK(PartAt(l, 600, 970, 31, 4) == Part::Bar);
  // the top edge resizes, over a button too; its ends resize two ways
  CHECK(PartAt(l, 600, 500, 0, 4) == Part::Top && PartAt(l, 600, 500, 3, 4) == Part::Top && PartAt(l, 600, 500, 4, 4) == Part::Bar);
  CHECK(PartAt(l, 600, 970, 3, 4) == Part::Top && PartAt(l, 600, 970, 4, 4) == Part::Close);
  CHECK(PartAt(l, 600, 0, 0, 4) == Part::TopLeft && PartAt(l, 600, 3, 3, 4) == Part::TopLeft && PartAt(l, 600, 4, 3, 4) == Part::Top);
  CHECK(PartAt(l, 600, 999, 0, 4) == Part::TopRight && PartAt(l, 600, 996, 3, 4) == Part::TopRight && PartAt(l, 600, 995, 3, 4) == Part::Top);
  // a maximised window has no edge that resizes: the corner's top pixel is the close button
  CHECK(PartAt(l, 600, 999, 0, 0) == Part::Close && PartAt(l, 600, 500, 0, 0) == Part::Bar && PartAt(l, 600, 0, 0, 0) == Part::Bar);
  // under the bar: the picture; past the window: nothing
  CHECK(PartAt(l, 600, 500, 32, 4) == Part::Picture && PartAt(l, 600, 999, 599, 4) == Part::Picture);
  CHECK(PartAt(l, 600, 500, 600, 4) == Part::Outside && PartAt(l, 600, 1000, 10, 4) == Part::Outside);
  CHECK(PartAt(l, 600, -1, 10, 4) == Part::Outside && PartAt(l, 600, 10, -1, 4) == Part::Outside);
  // full screen: the picture is the whole window, its top row included
  const Layout full = Lay(1920, 96, true, true);
  CHECK(PartAt(full, 1080, 1919, 0, 0) == Part::Picture && PartAt(full, 1080, 0, 0, 0) == Part::Picture);
  // another scaling: the parts are where the layout put them
  const Layout big = Lay(1500, 144, false, false);
  CHECK(PartAt(big, 900, 1499, 10, 6) == Part::Close && PartAt(big, 900, 1500 - 70, 10, 6) == Part::Maximize);
  CHECK(PartAt(big, 900, 700, 47, 6) == Part::Bar && PartAt(big, 900, 700, 48, 6) == Part::Picture);
}
