// A game that stands still in a race says so (fzgx/freeze.h): when the watching thread speaks and how often, what
// the summary line then says, a stop asked for while the game stands, and the test setting that makes a game stand.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "harness.h"
#include "net_internal.h"

#include "fzgx/freeze.h"

namespace
{
using fzgx::freeze::Said;
using What = fzgx::freeze::Said::What;

fzgx::freeze::Watch::Time At(double seconds)
{
  return fzgx::freeze::Watch::Time{} + std::chrono::milliseconds(static_cast<long long>(seconds * 1000) + 1000000);
}
}  // namespace

// Nothing is said outside a race or of a game whose frames come. A game that stands in a race is reported after
// 2, 4, 8, 16 and 32 s, then once a minute, ten times at most; when a frame comes, one line says how long it stood.
void TestAGameThatStandsStillIsReported()
{
  using namespace fzgx::freeze;
  Pulse p;
  Watch w;
  // no race: never, however long no frame comes
  for (double t = 0; t < 100; t += 0.25)
    CHECK(w.Look(p, At(t)).what == What::Nothing);
  CHECK(p.freezes == 0 && SummaryWords(p) == "freezes=0 stood=- stuck=no waits=0");
  // a race whose frames come
  p.racing = true;
  p.countdown = false;
  for (double t = 100; t <= 110; t += 0.25)
  {
    ++p.frames;
    CHECK(w.Look(p, At(t)).what == What::Nothing);
  }
  // it stands from 110 on: the reports and their times
  std::vector<double> at;
  for (double t = 110.25; t < 110 + 400; t += 0.25)
    if (const Said said = w.Look(p, At(t)); said.what == What::Stands)
    {
      at.push_back(said.seconds);
      CHECK(said.report == at.size());
    }
    else
      CHECK(said.what == What::Nothing);
  CHECK(at == std::vector<double>({2, 4, 8, 16, 32, 92, 152, 212, 272, 332}));
  CHECK(p.freezes == 1 && p.standing && p.longest_ms >= 399000);
  CHECK(ReportAt(0) == FIRST_SECONDS);
  // a frame: one line, and the watch starts over
  ++p.frames;
  const Said moved = w.Look(p, At(510));
  CHECK(moved.what == What::MovesAgain && moved.seconds == 400 && !p.standing && p.longest_ms == 400000);
  CHECK(MovesLine(moved) == "fzgx: freeze: the game moves again; it stood for 400.0 s (10 reports)");
  CHECK(w.Look(p, At(510.25)).what == What::Nothing);
  // a second, shorter one: counted, and the longest stays
  CHECK(w.Look(p, At(512.5)).what == What::Stands);
  p.step = 77;
  p.waits_for_relay = true;
  const std::string line = StandsLine({What::Stands, 2.25, 1}, p);
  CHECK(line.find("fzgx: freeze: no game frame for 2.2 s (report 1 of this freeze; game frame ") == 0 ||
        line.find("fzgx: freeze: no game frame for 2.3 s (report 1 of this freeze; game frame ") == 0);
  CHECK(line.find("race step 77; in the race; the core is waiting for the relay's inputs)") != std::string::npos);
  CHECK(p.freezes == 2 && SummaryWords(p) == "freezes=2 stood=400.0 stuck=no waits=0");
  // under 2 s nothing is counted
  ++p.frames;
  CHECK(w.Look(p, At(513)).what == What::MovesAgain);
  CHECK(w.Look(p, At(514.9)).what == What::Nothing && p.freezes == 2);
}

// A stop asked for while the game stands: the summary says so, the freeze's length runs up to the stop, and nothing
// more is reported (a shutdown runs no frame either). A stop of a game that moves leaves "stuck=no waits=0". Only the
// first stop counts.
void TestAStopWhileTheGameStands()
{
  using namespace fzgx::freeze;
  {
    Pulse p;
    Watch w;
    p.racing = true;
    p.countdown = false;
    w.Look(p, At(0));
    CHECK(w.Look(p, At(2)).what == What::Stands && w.Look(p, At(4)).what == What::Stands);
    CHECK(Stop(p) && p.stood_at_stop && !Stop(p));
    for (double t = 6.5; t < 60; t += 0.25)
      CHECK(w.Look(p, At(t)).what == What::Nothing);
    CHECK(SummaryWords(p) == "freezes=1 stood=6.5 stuck=yes waits=0");
    p.Reset();
    CHECK(SummaryWords(p) == "freezes=0 stood=- stuck=no waits=0" && !p.stopping);
  }
  {
    Pulse p;
    Watch w;
    p.racing = true;
    p.countdown = false;
    w.Look(p, At(0));
    CHECK(w.Look(p, At(2)).what == What::Stands);
    ++p.frames;
    CHECK(w.Look(p, At(3)).what == What::MovesAgain);
    CHECK(Stop(p) && !p.stood_at_stop && SummaryWords(p) == "freezes=1 stood=3.0 stuck=no waits=0");
  }
}

// The summary line carries the three words in its head, after `steps` (protocol/formats.md, section 6): a
// race without a freeze reads "freezes=0 stood=- stuck=no waits=0"; one closed while it stood does not read as a race the
// player left.
void TestTheSummaryLineSaysAFreeze()
{
  using namespace fzgx;
  freeze::Pulse& p = freeze::ThePulse();
  p.Reset();
  const auto session = std::make_unique<net::Session>();
  CHECK(net::SummaryLine(*session, "T").find(" end=closed steps=0 freezes=0 stood=- stuck=no waits=0 | countdown ") != std::string::npos);
  freeze::Watch w;
  p.racing = true;
  p.countdown = false;
  w.Look(p, At(0));
  w.Look(p, At(2));
  freeze::Stop(p);
  w.Look(p, At(5.5));
  CHECK(net::SummaryLine(*session, "T").find(" end=closed steps=0 freezes=1 stood=5.5 stuck=yes waits=0 | countdown ") != std::string::npos);
  p.Reset();
}

// "debug.standStill": read, named among the test settings that are on, refused when its two lists do not fit; and
// the game's thread then sleeps once before each named step.
void TestTheSettingThatMakesAGameStand()
{
  using namespace fzgx;
  const std::string head = R"({"about": "a test", "debug": )";
  Options o;
  CHECK(!ParseLaunchFile(head + R"({"standStill": {"steps": [120, 1800], "tenths": [26, 250]}}})", &o));
  CHECK(o.debug.stand_steps == std::vector<u32>({120, 1800}) && o.debug.stand_tenths == std::vector<u32>({26, 250}));
  const std::vector<std::string> on = DebugSettingsOn(o.debug);
  CHECK(std::find(on.begin(), on.end(), "debug.standStill") != on.end());
  CHECK(DebugSettingsOn(Options().debug).empty());
  const auto says = [&head](const std::string& debug, const std::string& part) {
    Options refused;
    const std::optional<std::string> problem = ParseLaunchFile(head + debug + "}", &refused);
    return problem && problem->find(part) != std::string::npos;
  };
  CHECK(says(R"({"standStill": {"steps": [120]}})", "\"debug.standStill\" needs"));
  CHECK(says(R"({"standStill": {"steps": [120, 60], "tenths": [1, 1]}})", "\"debug.standStill\" needs"));
  CHECK(says(R"({"standStill": {"steps": [120], "tenths": [1201]}})", "\"debug.standStill.tenths\""));
  CHECK(says(R"({"standStill": {"steps": [120], "tenths": [1], "for": 2}})", "\"debug.standStill.for\" is not"));
  CHECK(says(R"({"standStill": 5})", "\"debug.standStill\" is not an object"));
}

// The race ends for this game while a freeze is being reported (lost to the relay, a recording over): nothing
// stands any more, one line says so, and a stop asked for later finds a game that does not stand. (It found the
// old freeze: a race lost a minute before its game was closed read "stuck=yes".)
void TestARaceThatEndsWhileTheGameStands()
{
  using namespace fzgx::freeze;
  Pulse p;
  Watch w;
  p.racing = true;
  p.countdown = false;
  w.Look(p, At(0));
  CHECK(w.Look(p, At(2)).what == What::Stands && w.Look(p, At(4)).what == What::Stands && p.standing);
  CHECK(p.standing_ms == 4000);
  p.racing = false;
  const Said left = w.Look(p, At(4.5));
  CHECK(left.what == What::Left && left.seconds == 4.5 && left.report == 2 && !left.wait);
  CHECK(!p.standing && p.standing_ms == 0 && p.longest_ms == 4500);
  CHECK(MovesLine(left) == "fzgx: freeze: the race is over for this game (lost, or its recording ended); it had stood "
                           "for 4.5 s (2 reports)");
  // frames come or not: nothing more is said, and the stop finds no freeze
  for (double t = 5; t < 200; t += 0.25)
    CHECK(w.Look(p, At(t)).what == What::Nothing);
  CHECK(Stop(p) && !p.stood_at_stop && SummaryWords(p) == "freezes=1 stood=4.5 stuck=no waits=0");
  // the stop's line: how long the freeze of that moment had lasted, not the longest of the race
  CHECK(StopLine("why", true, true, 4700) == "fzgx: stop: why; the game stood still when it was asked (for 4.7 s by then)");
  CHECK(StopLine("why", true, false, 0) == "fzgx: stop: why");
  CHECK(StopLine("why", false, true, 4700) == "fzgx: stop (asked again): why");
}

// A frame held by the core's own wait for the relay's inputs is no freeze: it is said in a line of its own kind,
// counted apart, and neither "stood" nor "stuck" knows of it. And in the countdown, where a healthy frame can take
// most of a second, the first report waits 5 s: a stand of 3 s there is nothing.
void TestAWaitForTheRelayAndTheCountdownsThreshold()
{
  using namespace fzgx::freeze;
  {
    Pulse p;
    Watch w;
    p.racing = true;
    p.countdown = false;
    w.Look(p, At(0));
    p.waits_for_relay = true;
    const Said first = w.Look(p, At(2));
    CHECK(first.what == What::Stands && first.wait && p.waits == 1 && p.freezes == 0 && !p.standing);
    CHECK(StandsLine(first, p).find("fzgx: wait: no game frame for 2.0 s: the core waits for the relay's inputs (report 1 of "
                                    "this wait; ") == 0);
    CHECK(Stop(p) && !p.stood_at_stop && SummaryWords(p) == "freezes=0 stood=- stuck=no waits=1");
  }
  {
    Pulse p;
    Watch w;
    p.racing = true;
    p.countdown = false;
    w.Look(p, At(0));
    p.waits_for_relay = true;
    w.Look(p, At(2));
    // the relay answers: one line; or the wait gives up and the race is lost: one line of the other wording
    ++p.frames;
    const Said moved = w.Look(p, At(3));
    CHECK(moved.what == What::MovesAgain && moved.wait);
    CHECK(MovesLine(moved) == "fzgx: wait: the game moves again; it waited for 3.0 s (1 report)");
    CHECK(p.longest_ms == 0 && p.freezes == 0);
  }
  {
    Pulse p;
    Watch w;
    p.racing = true;  // (the countdown: as the go leaves it)
    w.Look(p, At(0));
    for (double t = 0.25; t < 5; t += 0.25)
      CHECK(w.Look(p, At(t)).what == What::Nothing);
    CHECK(p.freezes == 0);
    std::vector<double> at;
    for (double t = 5; t < 40; t += 0.25)
      if (const Said said = w.Look(p, At(t)); said.what == What::Stands)
        at.push_back(said.seconds);
    CHECK(at == std::vector<double>({5, 8, 16, 32}) && p.freezes == 1);
    CHECK(StandsLine({What::Stands, 5, 1, false}, p).find("; in the countdown; ") != std::string::npos);
    CHECK(ReportAt(0, true) == COUNTDOWN_SECONDS && ReportAt(1, true) == 8 && ReportAt(1) == 4);
  }
}

// A session that fails (no official inputs for 15 s) leaves its race: no restore follows, so its undo log ends
// there, and no other thread is told "a rollback keeps this memory" for the rest of the process.
void TestAFailedSessionEndsItsUndoLog()
{
  TestHost host;
  ModData data;
  Guest g(host, data);
  std::mt19937 rng(20261014);
  FakeGame(host, g, rng);
  Init(host, Options{});  // the failure's line goes to this test's host
  const auto session = std::make_unique<net::Session>();
  net::Session& s = *session;
  const u32 cells = net::STATE_END - 0x100000;
  CHECK(!fzgx::UnderRollback(cells, 0x280));
  s.rollback.Start(g);
  CHECK(s.rollback.Active() && fzgx::UnderRollback(cells, 0x280) && fzgx::UnderRollback(net::DOL_RNG, 4));
  net::Fail(g, s, 77);
  CHECK(s.phase == net::Phase::Failed && !s.rollback.Active());
  CHECK(!fzgx::UnderRollback(cells, 0x280) && !fzgx::UnderRollback(net::DOL_RNG, 4));
  CHECK(!freeze::ThePulse().racing && host.log.find("leaving the race") != std::string::npos);
  Shutdown();
}

