// The advanced spectator controls (fzgx/spectator.h): the pad's mapping, the knocked-out racer's restriction, the
// leader, the closest battle, the racer before and after in race order, the follow modes, the camera kinds and the
// course's rule.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "harness.h"

#include "fzgx/spectator.h"

#include <string>

using namespace fzgx::spectator;

namespace
{
// a race of `n` racers, racer i at rank (n - 1 - i) (racer n-1 leads), 100 units apart
Racers Field(u32 n)
{
  Racers r{};
  for (u32 i = 0; i < n; ++i)
  {
    r[i].present = true;
    r[i].drawn = true;
    r[i].rank = n - 1 - i;
    r[i].distance = 1000.0f + 100.0f * static_cast<float>(i);
  }
  return r;
}

Pad Press(u16 buttons)
{
  return Pad{buttons, 0};
}

// one frame with `buttons` down after a frame with nothing down
Frame Tap(Controls& c, const Racers& r, u16 buttons)
{
  const Frame f = c.Next(Press(buttons), r);
  c.Next(Pad{}, r);
  return f;
}
}  // namespace

void TestTheSpectatorsPad()
{
  BEGIN_TEST();
  const Pad none;
  // presses count when they begin
  Asked a = Map(Role::Spectator, Press(PAD_RIGHT), none);
  CHECK(a.racer == 1 && a.kind == 0);
  CHECK(Map(Role::Spectator, Press(PAD_RIGHT), Press(PAD_RIGHT)).racer == 0);
  CHECK(Map(Role::Spectator, Press(PAD_LEFT), none).racer == -1);
  CHECK(Map(Role::Spectator, Press(PAD_DOWN), none).kind == 1);
  CHECK(Map(Role::Spectator, Press(PAD_UP), none).kind == -1);
  CHECK(Map(Role::Spectator, Press(PAD_X), none).leader);
  CHECK(Map(Role::Spectator, Press(PAD_Y), none).battle);
  CHECK(Map(Role::Spectator, Press(PAD_Z), none).overlay);
  CHECK(Map(Role::Spectator, Pad{0, 60}, none).variant == 1);
  CHECK(Map(Role::Spectator, Pad{0, 60}, Pad{0, 60}).variant == 0);
  CHECK(Map(Role::Spectator, Pad{0, -60}, none).variant == -1);
  CHECK(Map(Role::Spectator, Pad{0, 20}, none).variant == 0);
  // L and R count while held, both at once nothing
  CHECK(Map(Role::Replay, Press(PAD_L), Press(PAD_L)).scrub == -1);
  CHECK(Map(Role::Replay, Press(PAD_R), Press(PAD_R)).scrub == 1);
  CHECK(Map(Role::Replay, Press(PAD_L | PAD_R), none).scrub == 0);
  CHECK(Map(Role::Spectator, Press(PAD_A), none).live);
  CHECK(Map(Role::Replay, Press(PAD_START), none).pause);
  CHECK(!Map(Role::Replay, Press(PAD_START), Press(PAD_START)).pause);
}

void TestAKnockedOutRacerNeverScrubs()
{
  BEGIN_TEST();
  CHECK(MayScrub(Role::Spectator) && MayScrub(Role::Replay) && !MayScrub(Role::KnockedOut));
  const Pad none;
  const u16 all = PAD_L | PAD_R | PAD_A | PAD_START;
  for (u16 b : {PAD_L, PAD_R, PAD_A, PAD_START, all})
  {
    const Asked a = Map(Role::KnockedOut, Press(b), none);
    CHECK(a.scrub == 0 && !a.live && !a.pause);
  }
  // but follows and changes the camera as anyone
  CHECK(Map(Role::KnockedOut, Press(PAD_RIGHT), none).racer == 1);
  CHECK(Map(Role::KnockedOut, Press(PAD_DOWN), none).kind == 1);
  CHECK(Map(Role::KnockedOut, Press(PAD_X), none).leader);
  CHECK(Map(Role::KnockedOut, Press(PAD_Z), none).overlay);
  Controls c(Role::KnockedOut, 8, 3);
  CHECK(!c.MayJump());
  const Racers r = Field(6);
  const Frame f = c.Next(Press(all), r);
  CHECK(f.scrub == 0 && !f.live && !f.pause);
  CHECK(c.Click(1, r));
  CHECK(c.Next(Pad{}, r).follow == 1);
  CHECK(Controls(Role::Replay, 8, 0).MayJump() && Controls(Role::Spectator, 8, 0).MayJump());
}

void TestTheLeaderAndTheClosestBattle()
{
  BEGIN_TEST();
  Racers r = Field(5);  // racer 4 leads
  CHECK(Leader(r) == 4);
  r[4].out = true;      // out: the leader still racing
  CHECK(Leader(r) == 3);
  r[3].finished = true;
  CHECK(Leader(r) == 2);
  for (u32 i = 0; i < 5; ++i)
    r[i].out = true;
  CHECK(Leader(r) == 4);  // nobody races: the leader of those that may be picked (every wreck still drawn)
  r[4].drawn = false;     // the leader's wreck exploded: the best-placed wreck still drawn (3 finished, but out too)
  CHECK(Leader(r) == 3);
  for (u32 i = 0; i < 5; ++i)
    r[i].drawn = false;
  CHECK(Leader(r) == NOBODY);  // every body gone: nobody to pick
  CHECK(Leader(Racers{}) == NOBODY);

  r = Field(5);
  CHECK(ClosestBattle(r) == 0 || ClosestBattle(r) != NOBODY);  // all 100 apart: some pair
  r[2].distance = 1290;  // racer 2 is 10 behind racer 3: that pair, the one behind
  CHECK(ClosestBattle(r) == 2);
  r[3].out = true;       // out: 2 and 4 are now next to each other, 110 apart; 0 and 1 100 apart
  CHECK(ClosestBattle(r) == 0);
  // the gap is between neighbours in race order, not any two racers
  Racers q = Field(3);
  q[0].distance = 1000; q[1].distance = 1500; q[2].distance = 1001;  // ranks say 2, 1, 0 though 2 is near 0
  CHECK(ClosestBattle(q) == 1);  // pairs (2,1): 499, (1,0): 500: the one behind of the first
  Racers one = Field(1);
  CHECK(ClosestBattle(one) == NOBODY);
}

void TestTheRacerBeforeAndAfter()
{
  BEGIN_TEST();
  Racers r = Field(4);  // order: 3, 2, 1, 0
  CHECK(ByRank(r, 3, 1) == 2);
  CHECK(ByRank(r, 0, 1) == 3);   // round
  CHECK(ByRank(r, 3, -1) == 0);
  CHECK(ByRank(r, 1, -1) == 2);
  r[2].out = true;               // a wreck still drawn (broken down or retired at rest) may be picked
  CHECK(ByRank(r, 3, 1) == 2);
  CHECK(ByRank(r, 1, -1) == 2);
  r[2].drawn = false;            // its body gone (exploded, fallen): passed over both ways
  CHECK(ByRank(r, 3, 1) == 1);
  CHECK(ByRank(r, 1, -1) == 3);
  CHECK(ByRank(r, 2, 1) == 1);   // the racer watched just went: on from its place
  CHECK(ByRank(r, 2, -1) == 3);
  r[1].finished = true;          // a finished racer may be picked
  CHECK(ByRank(r, 3, 1) == 1);
  r[3].out = true;               // the leader out, its wreck still drawn: first in order still
  CHECK(ByRank(r, 25, 1) == 3);  // not in the race: the first that may be picked
  r[3].drawn = false;
  CHECK(ByRank(r, 25, 1) == 1);
  CHECK(ByRank(r, 0, 1) == 1 && ByRank(r, 1, 1) == 0);  // round, past the two bodies gone
  r[0].out = true, r[0].drawn = false, r[1].out = true, r[1].drawn = false;
  CHECK(ByRank(r, 2, 1) == NOBODY);  // nobody may be picked: the controls keep the racer they have
  CHECK(ByRank(r, 25, 1) == NOBODY);
  r[1].drawn = true;
  CHECK(ByRank(r, 1, 1) == 1);   // the only one: itself
  r = Field(4);
  r[2].present = false;          // not in the race: skipped
  CHECK(ByRank(r, 3, 1) == 1);
  CHECK(ByRank(r, 25, 1) == 3);  // not in the race: the leader
  CHECK(ByRank(Racers{}, 0, 1) == NOBODY);
}

void TestAWatcherPicksNoRacerWhoseBodyIsGone()
{
  BEGIN_TEST();
  // the D-pad and a click land only on a racer racing, finished, or out with its wreck still drawn
  Racers r = Field(5);  // order: 4, 3, 2, 1, 0
  r[3].out = true;                     // exploded: gone
  r[2].out = true, r[2].drawn = true;  // broken down at rest: its wreck lies on the track
  r[1].finished = true;
  Controls c(Role::Replay, 8, 4);
  CHECK(c.Next(Pad{}, r).follow == 4);
  r[3].drawn = false;
  CHECK(Tap(c, r, PAD_RIGHT).follow == 2);  // past 3, onto the wreck still drawn
  CHECK(Tap(c, r, PAD_RIGHT).follow == 1);  // finished
  CHECK(Tap(c, r, PAD_LEFT).follow == 2);
  CHECK(Tap(c, r, PAD_LEFT).follow == 4);
  CHECK(!c.Click(3, r));                    // refused (counted by the frame as refused)
  CHECK(c.Next(Pad{}, r).follow == 4);
  CHECK(c.Click(2, r) && c.Next(Pad{}, r).follow == 2);
  CHECK(c.Click(1, r) && c.Next(Pad{}, r).follow == 1);
  // the wreck watched is hit and goes: the controls stay on it (the pictures' own rules move on: playback.cpp
  // SpectateReplay, the image's ss_watched), and the D-pad goes on from its place
  CHECK(c.Click(2, r) && c.Next(Pad{}, r).follow == 2);
  r[2].drawn = false;
  CHECK(c.Next(Pad{}, r).follow == 2);
  CHECK(Tap(c, r, PAD_RIGHT).follow == 1);
  CHECK(!c.Click(2, r));
  // X with nobody racing: the best-placed racer that may be picked
  Racers all = Field(3);
  for (auto& x : all)
    x.out = x.present;
  all[2].drawn = false;
  Controls k(Role::KnockedOut, 8, 0);
  CHECK(Tap(k, all, PAD_X).follow == 1);
}

void TestTheFollowModes()
{
  BEGIN_TEST();
  Racers r = Field(6);  // racer 5 leads
  Controls c(Role::Replay, 8, 0);
  CHECK(c.Next(Pad{}, r).follow == 0);
  Frame f = Tap(c, r, PAD_RIGHT);  // racer 0 is last: round to the leader
  CHECK(f.follow == 5 && f.follow_changed && c.how() == Follow::Chosen);
  f = Tap(c, r, PAD_LEFT);
  CHECK(f.follow == 0);
  // X: the leader, and the next leader when it changes
  f = Tap(c, r, PAD_X);
  CHECK(f.follow == 5 && c.how() == Follow::Leader);
  r[5].rank = 1; r[4].rank = 0;
  f = c.Next(Pad{}, r);
  CHECK(f.follow == 4 && f.follow_changed);
  CHECK(!c.Next(Pad{}, r).follow_changed);
  // a click is a chosen racer
  CHECK(c.Click(2, r));
  f = c.Next(Pad{}, r);
  CHECK(f.follow == 2 && c.how() == Follow::Chosen);
  r[4].rank = 1; r[5].rank = 0;
  CHECK(c.Next(Pad{}, r).follow == 2);
  CHECK(!c.Click(30, r));
  // Y: the closest battle, held BATTLE_HOLD frames
  r = Field(6);
  r[1].distance = 1195;  // racer 1 is 5 behind racer 2
  f = Tap(c, r, PAD_Y);
  CHECK(f.follow == 1 && c.how() == Follow::Battle);
  r[1].distance = 1100;
  r[3].distance = 1395;  // now 3 is 5 behind 4
  for (u32 i = 0; i + 3 < BATTLE_HOLD; ++i)
    CHECK(c.Next(Pad{}, r).follow == 1);
  bool moved = false;
  for (u32 i = 0; i < 4; ++i)
    moved = moved || c.Next(Pad{}, r).follow == 3;
  CHECK(moved);
  // the battle's racer goes out: another battle at once
  r[3].out = true;
  r[0].distance = 1099;
  CHECK(c.Next(Pad{}, r).follow == 0);
}

void TestTheCameraKinds()
{
  BEGIN_TEST();
  CHECK(KINDS == 10 && ChaseView(Kind::FirstPerson) == 0 && ChaseHeight(Kind::FirstPerson) == 3 && ChaseHeight(Kind::Chase1) == 0 && ChaseHeight(Kind::Chase3) == 2 && IsChase(Kind::FirstPerson) && !IsFixed(Kind::FirstPerson) && ChaseView(Kind::Chase1) == 1 && ChaseView(Kind::Chase3) == 3 && ChaseView(Kind::Course) == 0);
  CHECK(FixedType(Kind::Fixed1) == 0 && FixedType(Kind::Fixed4) == 3 && IsFixed(Kind::Fixed2) && !IsFixed(Kind::Auto));
  const Racers r = Field(3);
  Controls c(Role::Spectator, 8, 0);
  Frame f = c.Next(Pad{}, r);
  CHECK(f.kind == Kind::Chase1 && !f.kind_chosen);  // the game's own camera until the watcher picks one
  f = Tap(c, r, PAD_DOWN);
  CHECK(f.kind == Kind::Chase2 && f.kind_changed && f.kind_chosen);
  {
    // first person is reached as the other views are: D-pad up from the first chase view, down from it back again
    Controls v(Role::Spectator, 8, 0);
    v.Next(Pad{}, r);
    CHECK(Tap(v, r, PAD_UP).kind == Kind::FirstPerson);
    CHECK(Tap(v, r, PAD_DOWN).kind == Kind::Chase1);
    Controls none(Role::Replay, 41, 0);  // a course that allows no camera keeps its own
    none.Next(Pad{}, r);
    CHECK(Tap(none, r, PAD_UP).kind == Kind::Chase1);
  }
  f = Tap(c, r, PAD_UP);
  CHECK(f.kind == Kind::Chase1);
  f = Tap(c, r, PAD_UP);
  CHECK(f.kind == Kind::FirstPerson);  // round, through first person
  f = Tap(c, r, PAD_UP);
  CHECK(f.kind == Kind::Fixed4);
  // the C-stick: a fixed shot's variant, round within its type's
  CHECK(f.variant == 0);
  f = c.Next(Pad{0, 60}, r);
  CHECK(f.variant == 1 && f.kind_changed);
  c.Next(Pad{}, r);
  f = c.Next(Pad{0, 60}, r);
  CHECK(f.variant == 0);  // type 3 has 2 variants
  c.Next(Pad{}, r);
  f = c.Next(Pad{0, -60}, r);
  CHECK(f.variant == 1);
  CHECK(c.ClickKind(Kind::Course));
  f = c.Next(Pad{}, r);
  CHECK(f.kind == Kind::Course && f.variant == 0);
  f = c.Next(Pad{0, 60}, r);
  CHECK(f.variant == 0 && !f.kind_changed);  // no variants but a fixed shot's
  // Z, and the overlay's mode button: Observer, the game's HUD, the clean picture, round
  f = c.Next(Pad{}, r);
  CHECK(f.view == View::Observer && f.overlay);
  f = c.Next(Pad{PAD_Z, 0}, r);
  CHECK(f.view == View::Game && f.overlay);
  f = c.Next(Pad{PAD_Z, 0}, r);
  CHECK(f.view == View::Game);  // held: one press
  c.Next(Pad{}, r);
  f = Tap(c, r, PAD_Z);
  CHECK(f.view == View::Clean && !f.overlay && c.view() == View::Clean);
  c.ClickOverlay();
  f = c.Next(Pad{}, r);
  CHECK(f.view == View::Observer && f.overlay);
  CHECK(NextView(View::Clean) == View::Observer && std::string(ViewName(View::Game)) == "Game HUD");
  // a knocked-out racer cycles the views too
  Controls ko(Role::KnockedOut, 8, 1);
  CHECK(Tap(ko, r, PAD_Z).view == View::Game);
}

void TestTheOverlaysFollowButtons()
{
  BEGIN_TEST();
  // the overlay's Leader and Battle buttons, as X and Y
  const Racers r = Field(5);  // racer 4 leads; 100 apart
  Racers close = r;
  close[1].distance = close[2].distance - 10;  // racers 1 and 2 the closest pair: 1 behind
  Controls c(Role::Replay, 8, 0);
  c.ClickLeader();
  Frame f = c.Next(Pad{}, r);
  CHECK(f.follow == 4 && f.follow_changed && c.how() == Follow::Leader);
  c.ClickBattle();
  f = c.Next(Pad{}, close);
  CHECK(f.follow == 1 && c.how() == Follow::Battle);
  // a click of a row ends either
  CHECK(c.Click(3, close));
  f = c.Next(Pad{}, close);
  CHECK(f.follow == 3 && c.how() == Follow::Chosen);
  // course 41: the leader is not followed there
  Controls only(Role::Replay, 41, 2);
  only.ClickLeader();
  CHECK(only.Next(Pad{}, r).follow == 2);
}

void TestACoursesRule()
{
  BEGIN_TEST();
  CHECK(RuleFor(8).follow_others && RuleFor(8).kinds == (1u << KINDS) - 1);
  for (u32 course : {37u, 38u})
    CHECK(RuleFor(course).follow_others);
  // course 41: its gates follow the camera's machine: the recorder's racer and the recording's camera only
  const CourseRule rule = RuleFor(41);
  CHECK(!rule.follow_others && rule.kinds == 0);
  const Racers r = Field(4);
  Controls c(Role::Replay, 41, 2);
  CHECK(!c.Click(1, r) && c.Click(2, r) && !c.ClickKind(Kind::Course));
  Frame f = Tap(c, r, PAD_RIGHT);
  CHECK(f.follow == 2 && !f.follow_changed);
  f = Tap(c, r, PAD_X);
  CHECK(f.follow == 2);
  f = Tap(c, r, PAD_DOWN);
  CHECK(!f.kind_chosen && !f.kind_changed);
}

void TestTheResultsCamerasPick()
{
  BEGIN_TEST();
  // memmap.RESULTS_CAMERA as follow.py's helper writes it: the top bit, why << 8, racer; 0 is nothing said
  CHECK(!ReadResultsPick(0).said && !ReadResultsPick(0x00000305).said);
  const ResultsPick winner = ReadResultsPick(1u << 31 | 0x305);
  CHECK(winner.said && winner.racer == 5 && winner.why == static_cast<u32>(ResultsWhy::Winner));
  const ResultsPick wreck = ReadResultsPick(1u << 31 | 0x21D);
  CHECK(wreck.said && wreck.racer == 29 && wreck.why == static_cast<u32>(ResultsWhy::Wreck));
  // the move during the results (follow.py results_watch): the winner, why 7
  const ResultsPick gone = ReadResultsPick(1u << 31 | 0x702);
  CHECK(gone.said && gone.racer == 2 && gone.why == static_cast<u32>(ResultsWhy::Gone));
  // follow.py's numbers: RC_OWN, RC_WRECK, RC_WINNER, RC_COURSE, RC_KEEP, RC_ALONE, RC_GONE = 1 to 7
  CHECK(static_cast<u32>(ResultsWhy::Own) == 1 && static_cast<u32>(ResultsWhy::Alone) == 6 &&
        static_cast<u32>(ResultsWhy::Gone) == 7);
  for (u32 why = 1; why <= 7; ++why)
    CHECK(std::string(ResultsWhyText(why)) != ResultsWhyText(0));
  CHECK(std::string(ResultsWhyText(8)) == ResultsWhyText(0));
}
