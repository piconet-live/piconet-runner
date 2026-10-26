// Hits a rollback reveals (src/hits.h): which hits and lunges a displayed frame shows, from what each step left.
// The hook's order is the game's: before every step the state the step before left is observed, and at a displayed
// frame's first step the frame before's last step is the one it showed (Present).
// SPDX-License-Identifier: GPL-3.0-or-later

#include "harness.h"

#include "hits.h"

#include <map>

namespace
{
using fzgx::net::HitShow;

// A race as the hook sees it: each step's machine contact and side attack, as the true race has them and as a
// prediction had them.
struct Race
{
  HitShow show;
  std::map<u32, std::pair<u32, u32>> truth;      // step: contact, attack
  std::map<u32, std::pair<u32, u32>> predicted;  // the same as the predicted steps left them (else the truth)
  u32 at = 1;                                    // the next step to run
  HitShow::Shown total;

  // What step `step` left as it last ran: the prediction's, if it last ran on one and the prediction says.
  std::pair<u32, u32> State(u32 step) const
  {
    const auto& m = m_guessed.count(step) && predicted.count(step) ? predicted : truth;
    const auto it = m.find(step);
    return it == m.end() ? std::pair<u32, u32>{0, 0} : it->second;
  }

  // A displayed frame: on its first hook the frame before's last step is observed and presented; then, with a
  // rollback of `back` steps, the steps from at - back run again on the truth, and `fresh` new steps run (on a
  // prediction with `guess`); a hook before each step but the first observes the step before it.
  HitShow::Shown Frame(u32 back = 0, u32 fresh = 1, bool guess = false)
  {
    HitShow::Shown shown;
    if (at > 1)
    {
      show.Observe(at - 1, State(at - 1).first, State(at - 1).second);
      shown = show.Present(at - 1);
      total.hits |= shown.hits;
      total.lunges |= shown.lunges;
    }
    for (u32 u = at - back; u < at + fresh; ++u)
    {
      if (u > at - back)
        show.Observe(u - 1, State(u - 1).first, State(u - 1).second);
      if (u < at || !guess)
        m_guessed.erase(u);
      else
        m_guessed[u] = true;
    }
    at += fresh;
    return shown;
  }

private:
  std::map<u32, bool> m_guessed;
};
}  // namespace

void TestHitsAFrameShowedAreNotShownAgain()
{
  Race r;
  r.truth[5] = {1u << 3, 0};
  r.truth[6] = {1u << 3, 0};
  r.truth[20] = {0, 1u << 7};
  for (int f = 0; f < 40; ++f)
    r.Frame();
  CHECK(r.total.hits == 0 && r.total.lunges == 0);
  // catching up (two new steps a frame) runs no step again: nothing is shown either
  Race c;
  c.truth[7] = {1u << 2, 0};
  for (int f = 0; f < 20; ++f)
    c.Frame(0, 2);
  CHECK(c.total.hits == 0);
}

void TestARollbackShowsTheHitItRevealed()
{
  // steps 10 to 19 run on a prediction without the contact; the truth has seat 4 hit at step 15 (and 16)
  Race r;
  r.truth[15] = {1u << 4, 0};
  r.truth[16] = {1u << 4, 0};
  r.predicted[15] = {0, 0};
  r.predicted[16] = {0, 0};
  for (int f = 0; f < 9; ++f)
    r.Frame();
  for (int f = 0; f < 10; ++f)
    r.Frame(0, 1, true);
  CHECK(r.at == 20 && r.total.hits == 0);
  r.Frame(10, 1);  // the rollback: 10 to 19 run again with the truth, and 20
  const HitShow::Shown shown = r.Frame();  // the frame after it says what the rollback revealed
  CHECK(shown.hits == 1u << 4);
  CHECK(shown.hit_age == 5);  // seen at 20: hit at 15
  // run again by a later rollback, the same hit is not shown twice
  r.Frame(8, 1);
  CHECK(r.Frame().hits == 0);
}

void TestAHitThePredictionShowedIsNotShownAgain()
{
  // the prediction had seat 2's contact one step later; the player saw it then
  Race r;
  r.truth[15] = {1u << 2, 0};
  r.predicted[16] = {1u << 2, 0};
  for (int f = 0; f < 9; ++f)
    r.Frame();
  for (int f = 0; f < 10; ++f)
    r.Frame(0, 1, true);
  r.Frame(10, 1);
  CHECK(r.Frame().hits == 0);
}

void TestAnOldHitIsNotShown()
{
  // a rollback deeper than the window: a hit more than WINDOW steps before the frame's own step is let go
  Race r;
  r.truth[5] = {1u << 1, 0};
  r.truth[18] = {1u << 6, 0};
  r.predicted[5] = r.predicted[18] = {0, 0};
  for (int f = 0; f < 3; ++f)
    r.Frame();
  for (int f = 0; f < 17; ++f)
    r.Frame(0, 1, true);
  r.Frame(17, 1);
  const HitShow::Shown shown = r.Frame();
  CHECK(shown.hits == 1u << 6);
}

void TestARollbackShowsTheLungeItRevealed()
{
  // seat 9's side attack from step 30 to 35, unknown to the prediction until the rollback
  Race r;
  for (u32 t = 30; t <= 35; ++t)
  {
    r.truth[t] = {0, 1u << 9};
    r.predicted[t] = {0, 0};
  }
  for (int f = 0; f < 27; ++f)
    r.Frame();
  for (int f = 0; f < 8; ++f)
    r.Frame(0, 1, true);
  r.Frame(8, 1);
  const HitShow::Shown shown = r.Frame();
  CHECK(shown.lunges == 1u << 9 && shown.hits == 0);
  // the attack still going when the frame showed it: no lunge to show
  Race seen;
  for (u32 t = 30; t <= 45; ++t)
    seen.truth[t] = {0, 1u << 9};
  for (int f = 0; f < 31; ++f)
    seen.Frame();
  for (int f = 0; f < 4; ++f)
    seen.Frame(0, 1, true);
  seen.Frame(4, 1);
  CHECK(seen.Frame().lunges == 0);
}

void TestAHitInANewStepIsTheGamesOwn()
{
  // a rollback of 6 that also catches up 3 steps: the hit in a step new in that frame played its own sound (only a
  // step run before is muted), so it is not shown again; a side attacker in contact makes the hit the heavy one
  Race r;
  r.truth[14] = {1u << 5 | 1u << 8, 1u << 8};
  r.truth[21] = {1u << 3, 0};
  r.predicted[14] = {0, 0};
  for (int f = 0; f < 9; ++f)
    r.Frame();
  for (int f = 0; f < 10; ++f)
    r.Frame(0, 1, true);
  CHECK(r.at == 20);
  r.Frame(6, 3);  // 14 to 19 again, 20 to 22 new; 22 shown
  const HitShow::Shown shown = r.Frame();
  CHECK(shown.hits == (1u << 5 | 1u << 8));
  CHECK(shown.hard);
  CHECK(r.total.hits == (1u << 5 | 1u << 8));
}

void TestTheHitRumbleIsAskedOnce()
{
  // the local seat 2 hit at step 15 and 16, unknown to the prediction: the frame after the rollback rumbles, and no
  // later frame does, though later rollbacks run the hit again; a rival's hit alone (seat 6) rumbles nothing here
  Race r;
  r.truth[15] = r.truth[16] = {1u << 2 | 1u << 5, 0};
  r.truth[24] = {1u << 6 | 1u << 7, 0};
  r.predicted[15] = r.predicted[16] = r.predicted[24] = {0, 0};
  for (int f = 0; f < 9; ++f)
    r.Frame();
  for (int f = 0; f < 10; ++f)
    r.Frame(0, 1, true);
  int rumbles = 0, frames = 0;
  auto count = [&](const HitShow::Shown& shown) {
    rumbles += HitShow::RumblesFor(shown, 2) ? 1 : 0;
    ++frames;
  };
  r.Frame(10, 1);
  count(r.Frame());
  for (int f = 0; f < 6; ++f)
    count(r.Frame(4, 1, f % 2 == 0));
  count(r.Frame(5, 1));
  for (int f = 0; f < 4; ++f)
    count(r.Frame());
  CHECK(rumbles == 1 && frames == 12);
  CHECK(r.total.hits == (1u << 2 | 1u << 5 | 1u << 6 | 1u << 7));
  CHECK(!HitShow::RumblesFor(HitShow::Shown{1u << 6, 0, 1, false}, 2));
  CHECK(!HitShow::RumblesFor(HitShow::Shown{1u << 2, 0, 1, false}, 0xFFFFFFFF));  // no local seat (watching)
}
