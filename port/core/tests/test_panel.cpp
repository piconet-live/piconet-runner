// The panel before a race (fzgx/panel.h): what it shows from a start to the go, its lines, and a session that holds
// at the start gate.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "harness.h"
#include "stat_packs.h"

#include "fzgx/panel.h"

namespace
{
using namespace fzgx::panel;

// The host is told on the game's thread when the panel changes.
class PanelHost : public TestHost
{
public:
  void PanelChanged() override { ++told; }
  void LoadPace(bool lifted) override { pace.push_back(lifted); }
  int told = 0;
  std::vector<bool> pace;  // every LoadPace call, in order
};
}  // namespace

// LOADING from the start until the game's mode holds a course; then nothing, and never LOADING again. At the gate
// WAITING once the wait is a quarter of a second old; down at the go.
void TestWhatThePanelShows()
{
  BEGIN_TEST();
  Board board;
  CHECK(board.Now() == Shows::Nothing);
  Begin(board);
  CHECK(board.Now() == Shows::Loading);
  // the logos (no mode), the title, the race's or the replay's mode while it loads (no phase yet, or one before
  // the intro), another mode with a stale phase: still loading
  CHECK(!Frame(board, 0xFFFF, 0) && !Frame(board, 10, 0) && !Frame(board, 2, 0) && !Frame(board, 0xC, 0));
  CHECK(!Frame(board, 2, 0x26) && !Frame(board, 2, 0xFFFF) && !Frame(board, 10, 0x27) && !Frame(board, 2, 0x2A));
  CHECK(board.Now() == Shows::Loading);
  // the course intro: down, said once
  CHECK(Frame(board, 2, 0x27) && board.Now() == Shows::Nothing);
  CHECK(!Frame(board, 2, 0x27) && !Frame(board, 2, 0) && board.Now() == Shows::Nothing);
  // a later phase of the race counts too; the game's replay mode's word (0xC) is no race (a replay plays as one)
  Begin(board);
  CHECK(!Frame(board, 0xC, 0x27) && board.Now() == Shows::Loading);
  Begin(board);
  Begin(board);
  CHECK(Frame(board, 2, 0x29) && board.Now() == Shows::Nothing);
  // the gate: nothing for a short wait
  CHECK(!AtGate(board, 0, 0, 0) && !AtGate(board, WAIT_SHOWN_MS - 1, 0, 0) && board.Now() == Shows::Nothing);
  CHECK(AtGate(board, WAIT_SHOWN_MS, 0, 0) && board.Now() == Shows::Waiting);
  CHECK(!AtGate(board, 900, 0, 0));
  // a count that changes is a change
  CHECK(AtGate(board, 1000, 3, 5) && board.ready == 3 && board.of == 5);
  CHECK(!AtGate(board, 1100, 3, 5) && AtGate(board, 1200, 4, 5));
  CHECK(Down(board) && board.Now() == Shows::Nothing && !Down(board));
  // the course's frames after the go bring nothing back
  CHECK(!Frame(board, 2, 0x27) && board.Now() == Shows::Nothing);
}

// A start held to the race (a replay with its scrubber): LOADING through the course intro and the race's phases,
// down only when the race begins, and once; RaceBegins does nothing in any other start. The meter: the load in the
// first HELD_LOAD thousandths, then a thousandth a field while the course runs unseen, never full, never back.
void TestThePanelHeldToTheRace()
{
  BEGIN_TEST();
  Board board;
  Begin(board, true);
  CHECK(board.Now() == Shows::Loading && board.until_race);
  CHECK(!Frame(board, 2, 0x27) && !Frame(board, 0xC, 0x27) && !Frame(board, 2, 0x28) && !Frame(board, 2, 0x29));
  CHECK(board.Now() == Shows::Loading);
  // a failed session's Down takes it down whatever the start
  CHECK(Down(board) && board.Now() == Shows::Nothing);
  Begin(board, true);
  CHECK(RaceBegins(board) && board.Now() == Shows::Nothing && !RaceBegins(board));
  CHECK(!Frame(board, 2, 0x27) && board.Now() == Shows::Nothing);
  // any other start: RaceBegins never acts; Reset forgets the hold
  Begin(board);
  CHECK(!board.until_race && !RaceBegins(board) && board.Now() == Shows::Loading);
  Begin(board, true);
  board.Reset();
  CHECK(!board.until_race);
  CHECK(CourseShown(2, 0x27) && !CourseShown(0xC, 0x29) && !CourseShown(2, 0x26) && !CourseShown(10, 0x27) &&
        !CourseShown(2, 0x2A));
  Progress p;
  CHECK(p.Held(0, false) == 0 && p.Held(500, false) == HELD_LOAD / 2 && p.Held(FULL, false) == HELD_LOAD);
  CHECK(p.Held(0, false) == HELD_LOAD);  // never back
  u32 last = 0;
  for (int k = 0; k < 1000; ++k)
    last = p.Held(0, true);
  CHECK(p.Held(0, true) == BEFORE_THE_INTRO && last == BEFORE_THE_INTRO);
  Progress q;
  CHECK(q.Held(990, false) == 594 && q.Held(0, true) == HELD_LOAD + 1 && q.Held(0, true) == HELD_LOAD + 2);
}

// A game started again into its running race: LOADING stays up through the course and
// the countdown, its meter goes on from the load's share by the steps run again, and it comes down once the game is
// live (caught up, or the race over); in any other start neither acts.
void TestThePanelHeldUntilLive()
{
  BEGIN_TEST();
  Board board;
  Begin(board, false, true);
  CHECK(board.Now() == Shows::Loading && board.until_live && !board.until_race);
  CHECK(!Frame(board, 2, 0x27) && !Frame(board, 2, 0x29) && board.Now() == Shows::Loading);
  CHECK(CatchingUp(board, 0, 1000) && board.progress == HELD_LOAD);
  CHECK(CatchingUp(board, 500, 1000) && board.progress == HELD_LOAD + (FULL - HELD_LOAD) / 2);
  CHECK(!CatchingUp(board, 100, 1000) && board.progress == HELD_LOAD + (FULL - HELD_LOAD) / 2);  // never back
  CHECK(!CatchingUp(board, 1, 0));                                                                 // no target yet
  CHECK(Live(board) && board.Now() == Shows::Nothing && !board.until_live && !Live(board));
  Begin(board);
  CHECK(!CatchingUp(board, 5, 10) && !Live(board) && board.Now() == Shows::Loading);
  Begin(board, false, true);
  board.Reset();
  CHECK(!board.until_live);
}

void TestThePanelsLines()
{
  BEGIN_TEST();
  CHECK(Lines(Shows::Nothing, 3, 5).empty());
  CHECK(Lines(Shows::Loading, 3, 5) == std::vector<std::string>{"LOADING"});
  CHECK(Lines(Shows::Waiting, 0, 0) == std::vector<std::string>{"WAITING FOR PLAYERS"});
  CHECK((Lines(Shows::Waiting, 3, 5) == std::vector<std::string>{"WAITING FOR PLAYERS", "3/5"}));
  // a practice's countdown: its figure alone, which changes the board
  CHECK(Lines(Shows::Countdown, 2, 0) == std::vector<std::string>{"2"});
  Board board;
  CHECK(Counting(board, 3) && !Counting(board, 3) && Counting(board, 2) && board.Now() == Shows::Countdown);
  CHECK(board.figure.load() == 2 && Down(board) && board.Now() == Shows::Nothing);
  CHECK(Lines(Shows::Waiting, 30, 30)[1] == "30/30" && Lines(Shows::Waiting, 0, 2)[1] == "0/2");
  CHECK(Lines(Shows::Waiting, 9, 5)[1] == "5/5");   // never more ready than there are
}

// A start that goes straight into a replay: LOADING from Init, through the frames of the title and of the load,
// down at the first frame of the course intro. A plain start shows nothing. The load's speed limit and sound are
// lifted for exactly that stretch: asked once before the game starts, given back once at the intro's first frame;
// not in a plain start.
void TestThePanelFromAStartToTheCourse()
{
  BEGIN_TEST();
  PanelHost host;
  const std::string dir = Temp();
  stat_packs::WriteABundle(dir);
  WriteReplay(dir + "/race.fzrp", {0, 1});
  Options options;
  options.bundle_dir = dir;
  options.play_file = dir + "/race.fzrp";
  Init(host, options);
  CHECK(Active() && TheBoard().Now() == Shows::Loading);
  // (a host without the game's files: no meter, and why is said; the words need none)
  Meter none;
  std::string problem;
  LoadArtFromTheGame(&none, &problem);
  CHECK(!none.Loaded() && problem.find(METER_FILE) != std::string::npos);
  CHECK(host.pace.empty());
  BeforeTheGameStarts();
  CHECK(host.pace == std::vector<bool>{true});
  Guest g(host, Data());
  // main.rel not loaded yet (the logos): the words are not read
  g.Write16(game::GAME_MODE_AT, 2);
  g.Write16(game::PHASE_AT, 0x27);
  FrameTick();
  CHECK(TheBoard().Now() == Shows::Loading);
  LinkMainRel(g);
  g.Write16(game::GAME_MODE_AT, 10);   // the title
  g.Write16(game::PHASE_AT, 0);
  FrameTick();
  g.Write16(game::GAME_MODE_AT, 2);    // the race's mode, loading
  FrameTick();
  CHECK(TheBoard().Now() == Shows::Loading && host.pace == std::vector<bool>{true});
  g.Write16(game::PHASE_AT, 0x27);     // the course intro
  FrameTick();
  CHECK(TheBoard().Now() == Shows::Nothing && (host.pace == std::vector<bool>{true, false}));
  FrameTick();
  CHECK(host.pace.size() == 2);        // given back once
  Shutdown();
  // the same replay with its scrubber (every player's): held to the race. LOADING and the lifted pace stay through
  // the intro, whose countdown is cut to the game's own skip's 30 once (not when it is at or below), and through
  // the race's phases; nothing is given back at the intro
  options.replay_scrubber = true;
  host.pace.clear();
  Init(host, options);
  BeforeTheGameStarts();
  CHECK(TheBoard().Now() == Shows::Loading && TheBoard().until_race && (host.pace == std::vector<bool>{true}));
  LinkMainRel(g);
  g.Write16(game::GAME_MODE_AT, 2);  // the race's mode (a replay plays as the race), loading
  g.Write16(game::PHASE_AT, 0);
  g.Write32(game::INTRO_COUNTDOWN_AT, 350);
  FrameTick();
  CHECK(g.Read32(game::INTRO_COUNTDOWN_AT) == 350);   // (not the intro yet: untouched)
  CHECK(LoadField());                                   // the game draws nothing: the host presents the panel
  g.Write16(game::PHASE_AT, 0x27);                      // the course intro
  // not while the game's own skip is shut: a load queued, a fade, fn_1_EB200's states 0xE to 0x10
  g.Write32(game::LOAD_QUEUED_AT, 7);
  g.Write32(game::LOAD_DONE_AT, 6);
  FrameTick();
  g.Write32(game::LOAD_DONE_AT, 7);
  g.Write8(game::INTRO_FADE, 1);
  FrameTick();
  g.Write8(game::INTRO_FADE, 0);
  g.Write32(game::INTRO_SKIP_ON_AT, 1);
  g.Write32(game::INTRO_SKIP_STATE, 0xF);
  FrameTick();
  CHECK(g.Read32(game::INTRO_COUNTDOWN_AT) == 350);
  g.Write32(game::INTRO_SKIP_STATE, 0x11);
  FrameTick();
  CHECK(g.Read32(game::INTRO_COUNTDOWN_AT) == 30 && TheBoard().Now() == Shows::Loading);
  CHECK(host.notices.find("course intro is cut short unseen (320 fields") != std::string::npos);
  g.Write32(game::INTRO_COUNTDOWN_AT, 12);
  FrameTick();
  CHECK(g.Read32(game::INTRO_COUNTDOWN_AT) == 12);
  CHECK(!LoadField() && TheBoard().progress >= HELD_LOAD);  // the game draws: its own pictures carry the panel
  g.Write16(game::PHASE_AT, 0x29);
  FrameTick();
  CHECK(TheBoard().Now() == Shows::Loading && (host.pace == std::vector<bool>{true}));
  CHECK(RaceBegins(TheBoard()) && TheBoard().Now() == Shows::Nothing);
  Shutdown();
  options.replay_scrubber = false;
  host.pace.clear();
  // a plain start
  Init(host, Options{});
  BeforeTheGameStarts();
  CHECK(!Active() && TheBoard().Now() == Shows::Nothing && host.pace.empty());
  std::filesystem::remove_all(dir);
}

// A session joined to a relay that does not say go: the game holds at the gate, the panel comes up after a quarter
// of a second and the host is told, then twice a second; the go takes it down without telling the host (the game's
// next picture is the countdown's).
void TestThePanelAtTheStartGate()
{
  BEGIN_TEST();
  Udp relay;
  CHECK(relay.Open());
  Options options;
  options.debug.relay = "127.0.0.1:" + std::to_string(relay.LocalPort()) + ":1f2e3d4c5b6a7988";
  PanelHost host;
  host.time = std::chrono::steady_clock::now();
  Init(host, options);
  Guest g(host, Data());
  LinkMainRel(g);
  // (a session without a match is no start that goes straight into a race: no LOADING)
  CHECK(TheBoard().Now() == Shows::Nothing);
  std::optional<Endpoint> game;
  const auto hear = [&] {
    u8 buffer[512];
    std::size_t n = 0;
    Endpoint from;
    for (int tries = 0; tries < 200 && !relay.Receive(buffer, sizeof(buffer), &n, &from); ++tries)
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    game = from;
  };
  const auto pass = [&](int ms) {
    *host.time += std::chrono::milliseconds(ms);
    NetSync();
  };
  NetSync();   // joins
  hear();
  CHECK(game.has_value());
  const std::string joined = FromRelay('\x04') + std::string("\0\0\0\x01\0\0\0\0\x03", 9) + std::string(13, '\0');
  relay.Send(joined.data(), joined.size(), *game);
  for (int tries = 0; tries < 200 && host.notices.find("joined the race") == std::string::npos; ++tries)
    pass(1);
  CHECK(host.notices.find("joined the race as seat 0 of 2 player seats") != std::string::npos);
  // joined, and the game is not at the gate yet (the intro plays): nothing, however long
  pass(5000);
  CHECK(TheBoard().Now() == Shows::Nothing && host.told == 0);
  // at the gate
  g.Write32(layout::READY_AT, 1);
  pass(0);
  pass(200);
  CHECK(TheBoard().Now() == Shows::Nothing && host.told == 0);
  pass(60);
  CHECK(TheBoard().Now() == Shows::Waiting && host.told == 1 && TheBoard().of == 0);
  pass(100);
  CHECK(host.told == 1);
  pass(450);
  CHECK(host.told == 2);
  // the go
  const std::string go = FromRelay('\x06') + std::string("\0\0\0\x01\0\0\0\0", 8);
  relay.Send(go.data(), go.size(), *game);
  for (int tries = 0; tries < 200 && g.Read32(layout::GO_AT) == 0; ++tries)
    pass(1);
  CHECK(g.Read32(layout::GO_AT) == 1 && TheBoard().Now() == Shows::Nothing);
  CHECK(host.told <= 3);   // (the passes that waited for the packet are under half a second)
  Shutdown();
  Init(host, Options{});
}
