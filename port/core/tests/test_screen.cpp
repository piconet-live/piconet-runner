// Tests of the plain state behind the game's window (the runner's adapter has the system's and the emulator's side,
// which no test here reaches): what of the window's place is kept and when (fzgx/place.h, Tracker), the asking for
// the video backend's own full screen (fzgx/screen.h), and what each way of showing a picture needs of the emulator
// (fzgx/present.h, SetupOf).
// SPDX-License-Identifier: GPL-3.0-or-later

#include "harness.h"

#include "fzgx/place.h"
#include "fzgx/present.h"
#include "fzgx/screen.h"

void TestWhatOfTheWindowsPlaceIsKeptAndWhen()
{
  using namespace fzgx::place;
  const Rect first{100, 80, 1380, 832}, moved{300, 200, 1580, 952}, screen{0, 0, 2560, 1440}, over{-8, -8, 2568, 1408};
  const Rect small{10, 10, 110, 110}, least{10, 10, 346, 290};
  const auto K = [](const Rect& window, bool maximized, bool fullscreen) { return Kept{window, maximized, fullscreen}; };
  // a test game's window keeps nothing, whatever it does
  Tracker test(false);
  test.Note({first, true, false, false});
  CHECK(!test.ToWrite() && !test.Keeps());
  // a player's window: nothing until it was seen; then its place, once
  Tracker t(true);
  CHECK(!t.ToWrite());
  t.Note({first, true, false, false});
  CHECK(t.ToWrite() == K(first, false, false));
  // a write that failed is offered again; one that was made is not
  CHECK(t.ToWrite() == K(first, false, false));
  t.Written(*t.ToWrite());
  CHECK(!t.ToWrite());
  // dragged: nothing while the mouse moves it, the place when the drag ends
  t.Dragging(true);
  t.Note({moved, true, false, false});
  CHECK(!t.ToWrite());
  t.Dragging(false);
  CHECK(t.ToWrite() == K(moved, false, false));
  t.Written(*t.ToWrite());
  // maximised: its own place stays the one before, with the state; the same in full screen from maximised (the
  // window is then the size of the screen and not plain), and back
  t.Note({over, false, true, false});
  CHECK(t.ToWrite() == K(moved, true, false));
  t.Written(*t.ToWrite());
  t.Note({screen, false, true, true});
  CHECK(t.ToWrite() == K(moved, true, true));
  t.Written(*t.ToWrite());
  t.Note({over, false, true, false});
  t.Note({moved, true, false, false});
  CHECK(t.ToWrite() == K(moved, false, false));
  // minimised: nothing changes (it comes back as it was)
  t.Written(*t.ToWrite());
  t.Note({Rect{-32000, -32000, -31840, -31972}, false, false, false});
  CHECK(!t.ToWrite());

  // put back where it was, maximised and in full screen: the window is moved first, and maximised and taken to
  // full screen later. Until that is done nothing is written: the place of the moment, neither maximised nor in
  // full screen, must not replace the one just read (it did, and the window opened plain at the next start)
  const Kept was{moved, true, true};
  Tracker back(true);
  back.BeginRestore(was);
  CHECK(back.Restoring());
  back.Note({moved, true, false, false});   // moved to its place
  CHECK(!back.ToWrite());
  back.Note({over, false, true, false});    // maximised
  CHECK(!back.ToWrite());
  back.Note({screen, false, true, true});   // full screen
  CHECK(!back.ToWrite());
  back.Restored();
  CHECK(!back.Restoring() && !back.ToWrite());   // as it was kept: nothing to write
  // and from there on as ever: full screen left, the place with one state less
  back.Note({over, false, true, false});
  CHECK(back.ToWrite() == K(moved, true, false));
  // a restore of a plain window ends with nothing to write too; a restore whose place the system changed (a window
  // made larger to its least size) writes the place it got, once the restore is over
  Tracker plain(true);
  plain.BeginRestore({first, false, false});
  plain.Note({first, true, false, false});
  plain.Restored();
  CHECK(!plain.ToWrite());
  Tracker grown(true);
  grown.BeginRestore(K(small, false, false));
  grown.Note({least, true, false, false});
  CHECK(!grown.ToWrite());
  grown.Restored();
  CHECK(grown.ToWrite() == K(least, false, false));
}

void TestTheAskingForTheBackendsOwnFullScreen()
{
  using namespace fzgx::screen;
  // in a window, or with the borderless kind: nothing is asked, ever
  Ask a;
  CHECK(Before(a, false, false) == Tell::Nothing && !After(a, false) && !a.asked && !a.refused);
  // wanted: asked once; the present acts on it; held: nothing more is asked while it is held
  CHECK(Before(a, true, false) == Tell::Ask && a.asked);
  CHECK(!After(a, true) && a.asked && !a.refused);
  CHECK(Before(a, true, true) == Tell::Nothing && !After(a, true));
  // no longer wanted (the window left full screen, or another window got the focus): given back, once
  CHECK(Before(a, false, true) == Tell::GiveBack && !a.asked && !a.refused);
  CHECK(Before(a, false, false) == Tell::Nothing);
  // wanted again (the focus came back): asked again
  CHECK(Before(a, true, false) == Tell::Ask);
  CHECK(!After(a, true));
  // the driver takes it away while it is wanted: refused; not asked again, present after present
  CHECK(After(a, false) && a.refused && !a.asked);
  CHECK(Before(a, true, false) == Tell::Nothing && !After(a, false));
  CHECK(Before(a, true, false) == Tell::Nothing);
  // until it is no longer wanted, and wanted again
  CHECK(Before(a, false, false) == Tell::Nothing && !a.refused);
  CHECK(Before(a, true, false) == Tell::Ask);

  // a driver that refuses from the first: asked once, refused after that present, the borderless window stays
  Ask b;
  CHECK(Before(b, true, false) == Tell::Ask);
  CHECK(After(b, false) && b.refused);
  for (int present = 0; present < 5; ++present)
    CHECK(Before(b, true, false) == Tell::Nothing && !After(b, false));
  // asked, and left full screen before the present acted on it: the backend is told to give back all the same
  Ask c;
  CHECK(Before(c, true, false) == Tell::Ask);
  CHECK(Before(c, false, false) == Tell::GiveBack && !c.asked);
  // held although nobody asked (a state left over): given back when it is not wanted
  Ask d;
  CHECK(Before(d, false, true) == Tell::GiveBack);
  // the two kinds a launch file names
  CHECK(std::string(BORDERLESS) == "borderless" && std::string(EXCLUSIVE) == "exclusive");
}

void TestWhatAWayOfShowingAPictureNeedsOfTheEmulator()
{
  using namespace fzgx::present;
  const auto S = [](bool immediate_xfb, bool holds) { return Setup{immediate_xfb, holds}; };
  // "console" and "copy" hand a picture over from the game's copy: the emulator's own "Immediately Present XFB".
  // Without it no picture comes by the copy, and "console" would show as "field" while the log said "console".
  // "console" alone holds the picture for its first line's time (the emulator's GPU loop presents it; nothing waits)
  CHECK(SetupOf("console") == S(true, true));
  CHECK(SetupOf("copy") == S(true, false));
  CHECK(SetupOf("field") == S(false, false));
  // a single-core start's "console" is "field": neither
  CHECK(SetupOf(WayOf("console", true)) == S(false, false));
  CHECK(SetupOf(WayOf("copy", true)) == S(true, false));
  // a way that holds pictures always has them by the copy (else nothing would be held, and the log would lie)
  for (const char* way : {"console", "copy", "field", "unknown"})
    CHECK(!SetupOf(way).holds || SetupOf(way).immediate_xfb);
}
