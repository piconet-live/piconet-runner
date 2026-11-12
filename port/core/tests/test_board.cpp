// The launcher's status board as the core keeps it (fzgx/board.h, board.cpp, board_race.cpp): taken at Init from
// the launch file's "board", refused when it is not of this version, the heartbeat and the phase, the mailbox's
// notice and stop, the stop's reason, the freeze watch's events, the race's snapshot from the game's memory, and the
// session's phases. The board is a file made as the launcher makes it (View::Make), mapped by the core.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "harness.h"

#include "board_net.h"
#include "fzgx/board.h"
#include "mode.h"
#include "mode_hud.h"
#include "net_internal.h"

namespace
{
// A board file as the launcher makes it: SIZE bytes of zeros, the header's first words.
std::string MakeBoard(const std::string& dir, u32 version = board::VERSION, u32 size = board::SIZE)
{
  std::vector<u8> bytes(size + 64);
  auto* at = reinterpret_cast<u8*>((reinterpret_cast<std::uintptr_t>(bytes.data()) + 63) & ~std::uintptr_t{63});
  std::fill(at, at + size, u8{0});
  board::View v(at);
  v.Make(1234);
  v.SetHeaderVersion(version);
  const std::string path = dir + "/game.board";
  std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(at), size);
  return path;
}

// The board's file as it is now (read back from the disk: the core's mapping writes through to it).
struct Seen
{
  explicit Seen(const std::string& path)
  {
    std::ifstream file(path, std::ios::binary);
    file.read(reinterpret_cast<char*>(at()), board::SIZE);
  }
  u8* at() { return reinterpret_cast<u8*>((reinterpret_cast<std::uintptr_t>(bytes.data()) + 63) & ~std::uintptr_t{63}); }
  board::View view() { return board::View(at()); }
  board::Status status()
  {
    board::Status st;
    CHECK(view().ReadStatus(&st));
    return st;
  }
  std::vector<board::Event> events()
  {
    std::vector<board::Event> all;
    for (u64 n = 0; n < view().EventsHead(); ++n)
      if (board::Event e; view().ReadEvents(n, &e) == board::Read::Ok)
        all.push_back(e);
    return all;
  }
  std::vector<u8> bytes = std::vector<u8>(board::SIZE + 64);
};

// The launcher's mailbox, written into the file as the launcher writes it.
void Mail(const std::string& path, u32 stop, u32 notice, const std::string& text)
{
  Seen s(path);
  board::Mailbox m;
  m.stop_serial = stop;
  m.notice_serial = notice;
  m.notice_length = static_cast<u32>(text.size());
  std::copy(text.begin(), text.end(), m.notice_text.begin());
  s.view().WriteMailbox(m);
  std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
  file.seekp(board::at::MAILBOX);
  file.write(reinterpret_cast<const char*>(s.at() + board::at::MAILBOX), 4 + 12 + 128);
}
}  // namespace

void TestTheStatusBoardOfAPlainStart()
{
  const std::string dir = Temp();
  const std::string path = MakeBoard(dir);
  TestHost host;
  Options o;
  o.board = path;
  Init(host, o);
  CHECK(board::Taken() && host.notices.find("fzgx: board: taken (game.board, version 6)") != std::string::npos);
  {
    Seen s(path);
    CHECK(s.view().HeaderRunnerVersion() == board::VERSION && s.view().HeaderRunnerPid() == board::ProcessId() &&
          s.view().HeaderRunnerSize() == board::SIZE && s.view().HeaderLauncherPid() == 1234);
    CHECK(s.status().phase == board::Phase::Starting && s.view().PulseHeartbeat() == 0);
  }
  // a plain start beats every picture
  for (int i = 0; i < 3; ++i)
    board::Field();
  board::Look();
  board::Look();
  {
    Seen s(path);
    CHECK(s.view().PulseHeartbeat() == 3 && s.view().PulseWatchBeat() == 2 && s.status().phase == board::Phase::Offline);
  }
  // the freeze watch's reports
  board::Stands(2000, 1);
  board::Moves(2600);
  // a notice: into the log, taken once
  Mail(path, 0, 1, "Ready in\n10 s");
  board::Field();
  board::Field();
  CHECK(host.notices.find("fzgx: the launcher says: Ready in 10 s\n") != std::string::npos);
  CHECK(host.stops == 0);
  // the stop: asked once, its reason kept against a later one
  Mail(path, 1, 1, "Ready in 10 s");
  board::Field();
  board::Field();
  board::Stopped(board::StopReason::Escape);
  CHECK(host.stops == 1 && board::LauncherStopped());
  CHECK(host.notices.find("fzgx: the launcher asked the game to stop (its status board's mailbox); stopping") != std::string::npos);
  {
    Seen s(path);
    const board::Status st = s.status();
    CHECK(st.phase == board::Phase::Stopping && st.stop_seen == 1 && st.notice_seen == 1);
    CHECK(s.view().PulseStopReason() == board::StopReason::Launcher);
    std::vector<std::pair<board::EventKind, u32>> kinds;
    for (const board::Event& e : s.events())
      kinds.emplace_back(e.kind, e.a);
    const std::vector<std::pair<board::EventKind, u32>> want = {
        {board::EventKind::Phase, static_cast<u32>(board::Phase::Starting)},
        {board::EventKind::Phase, static_cast<u32>(board::Phase::Offline)},
        {board::EventKind::Stands, 2000},
        {board::EventKind::Moves, 2600},
        {board::EventKind::Notice, 1},
        {board::EventKind::Stop, static_cast<u32>(board::StopReason::Launcher)},
        {board::EventKind::Phase, static_cast<u32>(board::Phase::Stopping)},
    };
    CHECK(kinds == want);
  }
  Shutdown();
  board::Close();
  CHECK(!board::Taken());
}

void TestABoardThatIsNotThisVersionsIsLeft()
{
  const std::string dir = Temp();
  TestHost host;
  Options o;
  // another version: refused, said, and the game's own version left for the launcher to read
  o.board = MakeBoard(dir, board::VERSION + 1);
  Init(host, o);
  CHECK(!board::Taken() && host.log.find("fzgx: board: game.board refused: the launcher's layout is another version (its "
                                         "version 7, this game's 6") != std::string::npos);
  CHECK(Seen(o.board).view().HeaderRunnerVersion() == board::VERSION);
  board::Field();
  board::Stopped(board::StopReason::Escape);
  CHECK(Seen(o.board).view().PulseHeartbeat() == 0 && Seen(o.board).view().PulseStopReason() == board::StopReason::None);
  // too small
  host.log.clear();
  o.board = MakeBoard(dir, board::VERSION, board::SIZE / 2);
  Init(host, o);
  CHECK(!board::Taken() && host.log.find("it is too small") != std::string::npos);
  // no file
  host.log.clear();
  o.board = dir + "/none.board";
  Init(host, o);
  CHECK(!board::Taken() && host.log.find("fzgx: board: none.board cannot be used (the file cannot be opened") != std::string::npos);
  // no board named: nothing said, nothing done
  host.log.clear();
  host.notices.clear();
  Init(host, Options{});
  board::Field();
  CHECK(!board::Taken() && host.log.find("board") == std::string::npos && host.notices.find("board") == std::string::npos);
  Shutdown();
}

// The snapshot holds what the launcher's race page showed from the UDP feed before it: the same blocks, the same
// numbers.
void TestTheRaceOnTheBoard()
{
  const std::string dir = Temp();
  const std::string path = MakeBoard(dir);
  TestHost host;
  Options o;
  o.board = path;
  Init(host, o);
  Guest g(host, Data());
  LinkMainRel(g);
  constexpr u32 BLOCKS = 0x80800000, PROGRESS = 0x80900000;
  g.Write32(MODULE + net::MACHINES, BLOCKS);
  g.Write8(MODULE + net::MACHINE_COUNT, 3);
  for (u32 i = 0; i < 3; ++i)
  {
    const u32 base = BLOCKS + i * game::MACHINE_SIZE, m = PROGRESS + i * 0x200;
    g.Write32(base + game::MACHINE_PROGRESS, m);
    g.Write32(base + game::MACHINE_SPEED, std::bit_cast<u32>(300.5f + static_cast<float>(i)));  // speed
    g.Write32(base + game::MACHINE_ENERGY, std::bit_cast<u32>(80.0f));                           // energy
    g.Write16(base + game::MACHINE_MAX_ENERGY, 100);                                                  // maximum energy
    g.Write32(base + game::MACHINE_POSITION_X, std::bit_cast<u32>(-12.5f));                            // x
    g.Write8(m + game::PROGRESS_RANK, static_cast<u8>(i + 1));                                   // rank
    g.Write32(m + game::PROGRESS_LAPS, i);                                                       // laps
    g.Write8(m + game::PROGRESS_LAP_TIME + 8, 1), g.Write8(m + game::PROGRESS_LAP_TIME + 9, 2), g.Write16(m + game::PROGRESS_LAP_TIME + 10, 345);  // lap 1'02"345
    g.Write8(m + game::PROGRESS_BEST_LAP + 9, 59), g.Write16(m + game::PROGRESS_BEST_LAP + 10, 999);                   // best lap 59"999
    g.Write32(m + game::PROGRESS_DISTANCE, std::bit_cast<u32>(1000.0f * static_cast<float>(i)));      // distance
  }
  g.Write32(BLOCKS, game::MACHINE_FLAG_FINISHED);                // racer 0 finished
  g.Write32(BLOCKS + game::MACHINE_SIZE * 2, game::MACHINE_FLAG_TIME_OVER);// racer 2 out (its time over)
  g.Write32(BLOCKS + game::MACHINE_SIZE * 2 + game::MACHINE_SPEED, std::bit_cast<u32>(std::numeric_limits<float>::infinity()));
  g.Write8(BLOCKS + game::MACHINE_SIZE + game::MACHINE_ATTACK_KOS, 2); // racer 1: two knockouts in its attack
  board::Race(g, 600, 1);
  g.Write8(BLOCKS + game::MACHINE_SIZE + game::MACHINE_ATTACK_KOS, 3); // and a third
  board::Race(g, 601, 1);
  Seen s(path);
  board::Snapshot snap;
  CHECK(s.view().ReadSnapshot(&snap) && snap.serial == 2 && snap.frame == 601 && snap.local_seat == 1 && snap.count == 3);
  const board::Racer &a = snap.racers[0], &b = snap.racers[1], &c = snap.racers[2];
  CHECK(a.seat == 0 && a.state == board::RacerState::Finished && a.rank == 1 && a.lap == 0 && a.flags == 0);
  CHECK(b.seat == 1 && b.state == board::RacerState::Racing && b.lap == 1 && b.kos == 3 && b.flags == 1 &&
        b.best_lap_ms == 59999 && b.lap_ms == 62345 && b.speed == 301.5f && b.energy == 80.0f && b.max_energy == 100 &&
        b.x == -12.5f && b.distance == 1000.0f);
  CHECK(c.state == board::RacerState::Out && c.speed == 0.0f && c.lap == 2);  // (a speed that is no number: 0)
  // no mode in this start: no places (the launcher orders by times and distances) and no score
  CHECK(snap.score_kind == board::ScoreKind::None && snap.score_label_length == 0 && a.place == 0 && b.place == 0 && c.place == 0 &&
        (b.flags & board::RACER_FLAGS_HAS_SCORE) == 0);
  Shutdown();
  board::Close();
}

// The race's mode on the snapshot (board.toml version 6): the standing's rows as the places once the mode orders the
// standing (live_places with a seat placed, or the race ended by the mode), each seat's points under the HUD's label
// when any seat shows a score, else each seat's metres when any shows a distance; nothing of a mode that shows none.
void TestTheModesPlacesAndScoresOnTheSnapshot()
{
  TestHost host;
  Guest g(host, Data());
  constexpr u32 BLOCK = 0x80A00000;
  using namespace mode::hud;
  board::Snapshot s;
  s.count = 3;
  for (u32 i = 0; i < 3; ++i)
  {
    s.racers[i].seat = static_cast<u8>(i);
    s.racers[i].rank = static_cast<u8>(2 - i);  // the standing's rows: seat 2 first
    s.racers[i].flags = board::RACER_FLAGS_HAS_BEST_LAP;
  }
  mode::Package p;
  // no mode, or no block: nothing
  board::ModeOnTheSnapshot(g, nullptr, BLOCK, s);
  board::ModeOnTheSnapshot(g, &p, 0, s);
  CHECK(s.score_kind == board::ScoreKind::None && s.racers[0].place == 0 && s.racers[2].place == 0);
  // a mode without live_places, no seat placed, no score shown: the same
  board::ModeOnTheSnapshot(g, &p, BLOCK, s);
  CHECK(s.score_kind == board::ScoreKind::None && s.racers[2].place == 0);
  // live_places, but no seat placed yet (ShowPlaces leaves the pass's order): no places
  p.live_places = true;
  board::ModeOnTheSnapshot(g, &p, BLOCK, s);
  CHECK(s.racers[2].place == 0);
  // a seat placed: every seat's place is its row, 1 first; a seat without a score has none, the rest theirs
  g.Write8(BLOCK + mode::PLACES + 2, 1);
  g.Write32(BLOCK + SCORES + 2 * SCORE_RECORD + SCORE_VALUE, 12 + 1);
  g.Write32(BLOCK + SCORES + 2 * SCORE_RECORD + SCORE_LABEL, 0);  // the default label
  g.Write32(BLOCK + SCORES + 0 * SCORE_RECORD + SCORE_VALUE, 0 + 1);
  g.Write8(BLOCK + STORY + 1 * STORY_RECORD + STORY_FLAGS, STORY_DISTANCE);  // (Crown's holder: metres beside the points)
  g.Write32(BLOCK + STORY + 1 * STORY_RECORD + STORY_METRES, std::bit_cast<u32>(250.0f));
  board::ModeOnTheSnapshot(g, &p, BLOCK, s);
  CHECK(s.racers[0].place == 3 && s.racers[1].place == 2 && s.racers[2].place == 1);
  CHECK(s.score_kind == board::ScoreKind::Points && s.score_label_length == 6 &&
        std::string(reinterpret_cast<const char*>(s.score_label.data()), 6) == "POINTS");
  CHECK((s.racers[2].flags & board::RACER_FLAGS_HAS_SCORE) != 0 && s.racers[2].score == 12.0f);
  CHECK((s.racers[0].flags & board::RACER_FLAGS_HAS_SCORE) != 0 && s.racers[0].score == 0.0f);
  CHECK((s.racers[1].flags & board::RACER_FLAGS_HAS_SCORE) == 0 && s.racers[1].score == 0.0f &&
        (s.racers[1].flags & board::RACER_FLAGS_HAS_BEST_LAP) != 0);
  // the mode's own label (the tag text the word names, from 1)
  p.tag_texts = {"CUTS"};
  g.Write32(BLOCK + SCORES + 2 * SCORE_RECORD + SCORE_LABEL, 1);
  board::ModeOnTheSnapshot(g, &p, BLOCK, s);
  CHECK(s.score_label_length == 4 && std::string(reinterpret_cast<const char*>(s.score_label.data()), 4) == "CUTS");
  // no score shown: the metres shown are the score (Overdrive), with no label; a seat not fed has none
  g.Write32(BLOCK + SCORES + 2 * SCORE_RECORD + SCORE_VALUE, 0);
  g.Write32(BLOCK + SCORES + 0 * SCORE_RECORD + SCORE_VALUE, 0);
  board::ModeOnTheSnapshot(g, &p, BLOCK, s);
  CHECK(s.score_kind == board::ScoreKind::Metres && s.score_label_length == 0 && s.score_label[0] == 0);
  CHECK((s.racers[1].flags & board::RACER_FLAGS_HAS_SCORE) != 0 && s.racers[1].score == 250.0f &&
        (s.racers[2].flags & board::RACER_FLAGS_HAS_SCORE) == 0 && s.racers[2].score == 0.0f);
  // the race ended by the mode: the places whether or not live_places (the end clause holds its order)
  p.live_places = false;
  g.Write8(BLOCK + mode::PLACES + 2, 0);
  board::ModeOnTheSnapshot(g, &p, BLOCK, s);
  CHECK(s.racers[2].place == 0);
  g.Write32(BLOCK + mode::END, 2);
  board::ModeOnTheSnapshot(g, &p, BLOCK, s);
  CHECK(s.racers[0].place == 3 && s.racers[2].place == 1);
  // a rank past the rows (a seat the standing does not name): no place
  s.racers[0].rank = 7;
  board::ModeOnTheSnapshot(g, &p, BLOCK, s);
  CHECK(s.racers[0].place == 0 && s.racers[1].place == 2);
}

// The local racer's camera view for the launcher: a Camera event at the first view read and at each change, only
// a view a player chooses (1 to 3), only while the racer races and this game does not spectate.
void TestTheCameraViewOnTheBoard()
{
  const std::string dir = Temp();
  const std::string path = MakeBoard(dir);
  TestHost host;
  Options o;
  o.board = path;
  Init(host, o);
  Guest g(host, Data());
  LinkMainRel(g);
  constexpr u32 RECORD = 0x80800000;
  // no record yet: nothing
  board::CameraView(g, true);
  g.Write32(MODULE + game::CHASE_RECORD, RECORD);
  const auto view = [&](u16 v, bool racing) {
    g.Write16(RECORD + game::CHASE_VIEW, v);
    board::CameraView(g, racing);
  };
  view(2, true);    // the start's: said, none before it
  view(2, true);    // unchanged: nothing
  view(3, true);    // the button: said
  view(5, true);    // the retired view: not a choice
  view(1, false);   // the racer finished or is out: not the player's
  g.Write32(table::SPECTATE_FRAMES_AT, 180);
  view(1, true);    // spectating: the spectator's controls write the view
  g.Write32(table::SPECTATE_FRAMES_AT, 0);
  view(1, true);    // the player's again
  std::vector<std::pair<u32, u32>> said;
  for (const board::Event& e : Seen(path).events())
    if (e.kind == board::EventKind::Camera)
      said.emplace_back(e.a, e.b);
  CHECK((said == std::vector<std::pair<u32, u32>>{{2, 0}, {3, 2}, {1, 3}}));
  Shutdown();
  board::Close();
}

// The local player's pad for the launcher's Inputs window (minimap_frame.cpp InputsFrame): the game's PADStatus from
// the course intro on while this game has a driver of its own, held 0 otherwise; written only when it changed.
void TestTheLocalPadOnTheBoard()
{
  const std::string dir = Temp();
  const std::string path = MakeBoard(dir);
  TestHost host;
  Options o;
  o.board = path;
  Init(host, o);
  Guest g(host, Data());
  const auto seen = [&] {
    board::LocalPad p;
    CHECK(Seen(path).view().ReadLocalPad(&p));
    return p;
  };
  CHECK(seen().serial == 0);
  net::InputsFrame(g);  // no race module: no pad, said once
  CHECK(seen().serial == 1 && seen().held == 0);
  LinkMainRel(g);
  constexpr u32 PAD = 0x1BA040 + 0x998;  // main.rel's .bss, PADStatus of port 0 (minimap_frame.cpp PAD_STATUS)
  g.Write16(MODULE + PAD, 0x0110);       // A and Z
  g.Write8(MODULE + PAD + 2, static_cast<u8>(-72)), g.Write8(MODULE + PAD + 3, 40);
  g.Write8(MODULE + PAD + 4, 59), g.Write8(MODULE + PAD + 5, static_cast<u8>(-3));
  g.Write8(MODULE + PAD + 6, 150), g.Write8(MODULE + PAD + 7, 12);
  g.Write16(MODULE + net::PHASE, 0x26);  // before the course intro: still none
  net::InputsFrame(g);
  CHECK(seen().serial == 1);
  g.Write16(MODULE + net::PHASE, 0x27);  // the course intro: the player's pad
  net::InputsFrame(g);
  board::LocalPad p = seen();
  CHECK(p.serial == 2 && p.held == 1 && p.buttons == 0x0110 && p.stick_x == -72 && p.stick_y == 40 && p.c_stick_x == 59 &&
        p.c_stick_y == -3 && p.l == 150 && p.r == 12);
  net::InputsFrame(g);  // the same pad: not written again
  CHECK(seen().serial == 2);
  g.Write16(MODULE + PAD, 0x1000);  // Start
  net::InputsFrame(g);
  CHECK(seen().serial == 3 && seen().buttons == 0x1000);
  g.Write32(net::SEAT_SLOT + 4, 0xFFFFFFFF);  // no seat of this game's own (a replay, a watched race): none
  net::InputsFrame(g);
  p = seen();
  CHECK(p.serial == 4 && p.held == 0 && p.buttons == 0 && p.stick_x == 0);
  Shutdown();
  board::Close();
  // without a board nothing is read or written
  net::InputsFrame(g);
  CHECK(seen().serial == 4);
}

// Where the session is, as the board's phase says it.
void TestTheSessionsPhasesOnTheBoard()
{
  TestHost host;
  Init(host, Options{});
  u32 step = 0;
  CHECK(!net::SessionPhase(&step));
  net::s_session = std::make_unique<net::Session>();
  net::Session& s = *net::s_session;
  const auto phase = [&] { return net::SessionPhase(&step).value_or(board::Phase::None); };
  CHECK(phase() == board::Phase::Joining);
  s.phase = net::Phase::Joined;
  CHECK(phase() == board::Phase::Loading);
  s.gate_since = net::Clock::now();
  CHECK(phase() == board::Phase::Gate);
  s.phase = net::Phase::Running;
  freeze::ThePulse().countdown = true;
  CHECK(phase() == board::Phase::Countdown);
  freeze::ThePulse().countdown = false;
  CHECK(phase() == board::Phase::Racing);
  s.watch = net::Watch::Drifting;
  CHECK(phase() == board::Phase::Finished);
  s.watch = net::Watch::Racing;
  s.settled = true;
  CHECK(phase() == board::Phase::Finished);
  s.settled = false;
  s.practice = true;
  CHECK(phase() == board::Phase::Racing);
  s.playback = true;
  CHECK(phase() == board::Phase::Playback);
  s.phase = net::Phase::Failed;
  CHECK(phase() == board::Phase::Left);
  net::s_session.reset();
  freeze::ThePulse().Reset();
  Shutdown();
}
