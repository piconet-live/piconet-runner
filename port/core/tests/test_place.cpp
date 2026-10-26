// Tests of the game window's kept place (fzgx/place.h): the file's text there and back, texts that are no place, and
// whether a kept window is put back on made-up monitor layouts: one screen, two side by side with the second to the
// left (negative numbers) or above, a second screen that is gone, a taskbar that moved, a window dragged off the
// edge. And of the wait for a console's moment (fzgx/present.h): its three ways and its limit.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "harness.h"

#include "fzgx/place.h"
#include "fzgx/present.h"

void TestTheWindowsKeptPlace()
{
  using namespace fzgx::place;
  // there and back, every flag, a window on a monitor to the left of the main one
  for (const Kept& kept : {Kept{{100, 80, 1380, 832}, false, false}, Kept{{-2400, 40, -1120, 792}, true, false},
                           Kept{{0, 0, 64, 64}, false, true}, Kept{{-1048576, -1048576, -1048000, -1048000}, true, true}})
  {
    const std::optional<Kept> back = Read(Text(kept));
    CHECK(back && *back == kept);
  }
  CHECK(Text({{1, 2, 303, 404}, true, false}) == "fzgx-window 1 1 2 303 404 1 0\n");
  CHECK(Read("fzgx-window 1 1 2 303 404 1 0") && Read("fzgx-window 1 1 2 303 404 1 0\r\n"));
  // what is no place: nothing is put back from it
  for (const char* text : {"", "\n", "fzgx-window", "fzgx-window 1", "fzgx-window 1 1 2 303 404 1", "fzgx-window 1 1 2 303 404 1 0 0",
                           "fzgx-window 2 1 2 303 404 1 0", "window 1 1 2 303 404 1 0", "fzgx-window 1 1 2 303 404 2 0",
                           "fzgx-window 1 1 2 303 404 0 -1", "fzgx-window 1 a 2 303 404 0 0", "fzgx-window 1 1.5 2 303 404 0 0",
                           "fzgx-window 1  1 2 303 404 0 0", "fzgx-window 1 1 2 303 404 0 0 ", " fzgx-window 1 1 2 303 404 0 0",
                           "fzgx-window 1 1 2 60 404 0 0", "fzgx-window 1 1 2 303 60 0 0", "fzgx-window 1 303 2 1 404 0 0",
                           "fzgx-window 1 0 0 40000 404 0 0", "fzgx-window 1 0 0 303 40000 0 0",
                           "fzgx-window 1 2000000 0 2000300 404 0 0", "fzgx-window 1 99999999999 2 303 404 0 0",
                           "fzgx-window 1 1 2 303 404 0 0\nfzgx-window 1 1 2 303 404 0 0"})
  {
    const bool none = !Read(text);
    CHECK(none);
    if (!none)
      std::printf("  read as a place: \"%s\"\n", text);
  }
  CHECK(!Read(std::string(200, '1')));

  // the file in the game's user folder: none at first; written and read back; written over; a file that is no place
  // (a hand's edit, half a line, another program's) is none, and the next place replaces it; nothing is left beside
  const std::string folder = Temp();
  CHECK(!ReadKept(folder));
  const Kept first{{-2400, 40, -1120, 792}, true, false}, second{{10, 20, 1290, 772}, false, true};
  CHECK(WriteKept(folder, first) && ReadKept(folder) == first);
  CHECK(WriteKept(folder + "/", second) && ReadKept(folder) == second && ReadKept(folder + "/") == second);
  for (const std::string& text : {std::string("fzgx-window 1 10 20"), std::string(), std::string(5000, 'x'), std::string("{\"x\": 1}")})
  {
    WriteText(folder + "/" + FILE_NAME, text);
    CHECK(!ReadKept(folder));
  }
  CHECK(WriteKept(folder, first) && ReadKept(folder) == first);
  CHECK(!IsFile(folder + "/" + FILE_NAME + ".new"));
  // a folder that is not there: not written, and said so
  CHECK(!WriteKept(folder + "/none/there", first) && !ReadKept(folder + "/none/there"));

  // made-up monitor layouts: each monitor's work area (the screen less its taskbar)
  const Rect main{0, 0, 2560, 1400}, left{-2560, 0, 0, 1440}, above{0, -1080, 1920, -40}, small{0, 0, 1280, 680};
  const int strip = 32;
  const Rect window{300, 200, 1580, 952};
  // one screen: a window on it; hanging over its right edge or its bottom with the strip in reach; not when only
  // a sliver of the strip is left, when the strip is above the screen, or when the window is off it altogether
  CHECK(OnAScreen(window, {main}, strip));
  CHECK(OnAScreen({2300, 200, 3580, 952}, {main}, strip));
  CHECK(OnAScreen({300, 1300, 1580, 2052}, {main}, strip));
  CHECK(!OnAScreen({2500, 200, 3780, 952}, {main}, strip));
  CHECK(!OnAScreen({300, -10, 1580, 742}, {main}, strip));
  CHECK(!OnAScreen({300, 1390, 1580, 2142}, {main}, strip));
  CHECK(!OnAScreen({3000, 200, 4280, 952}, {main}, strip));
  CHECK(OnAScreen({-1100, 200, 180, 952}, {main}, strip) && !OnAScreen({-1200, 200, 80, 952}, {main}, strip));
  // a narrow window needs its whole strip, not more than it has
  CHECK(OnAScreen({100, 100, 200, 400}, {main}, strip) && !OnAScreen({-50, 100, 50, 400}, {main}, strip));
  // a second screen to the left: a window kept there is put back there, and one across both by the part on either
  const Rect on_left{-2000, 300, -720, 1052};
  CHECK(OnAScreen(on_left, {main, left}, strip) && OnAScreen(on_left, {left, main}, strip));
  CHECK(OnAScreen({-100, 300, 1180, 1052}, {main, left}, strip));
  // that screen gone: not put back (the usual place); the same for a screen above that is gone
  CHECK(!OnAScreen(on_left, {main}, strip));
  CHECK(OnAScreen({200, -900, 1480, -148}, {main, above}, strip) && !OnAScreen({200, -900, 1480, -148}, {main}, strip));
  // a smaller screen now (another resolution): a window that was further right or lower than it reaches is not
  CHECK(OnAScreen({100, 100, 1380, 852}, {small}, strip) && !OnAScreen({1300, 100, 2580, 852}, {small}, strip) &&
        !OnAScreen({100, 700, 1380, 1452}, {small}, strip));
  // the taskbar moved to the top (the work area starts lower): a window whose strip would be under it is not
  CHECK(!OnAScreen({300, 0, 1580, 752}, {Rect{0, 40, 2560, 1440}}, strip) && OnAScreen({300, 40, 1580, 792}, {Rect{0, 40, 2560, 1440}}, strip));
  // no screen, no window, no strip
  CHECK(!OnAScreen(window, {}, strip) && !OnAScreen({5, 5, 5, 5}, {main}, strip) && !OnAScreen(window, {main}, 0));
}

void TestThePicturesWaitForAConsolesMoment()
{
  using namespace fzgx::present;
  // the time still to come and near: the picture waits; to the microsecond at both ends
  CHECK(At(1) == Moment::Wait && At(1900) == Moment::Wait && At(MOST_WAIT_US) == Moment::Wait);
  // the copy came at the time or after it (a long frame): at once
  CHECK(At(0) == Moment::After && At(-1) == Moment::After && At(-16683) == Moment::After);
  // further off than the limit: no first line's time, at once; the limit is 4 ms
  CHECK(At(MOST_WAIT_US + 1) == Moment::Far && At(16683) == Moment::Far && At(1000000000000ll) == Moment::Far);
  CHECK(MOST_WAIT_US == 4000);
  // a start whose picture is drawn on the game's own thread does not wait: "console" is "field" there
  CHECK(std::string(WayOf("console", true)) == "field" && std::string(WayOf("console", false)) == "console");
  CHECK(std::string(WayOf("copy", true)) == "copy" && std::string(WayOf("field", true)) == "field" &&
        std::string(WayOf("copy", false)) == "copy" && std::string(WayOf("field", false)) == "field");
}
