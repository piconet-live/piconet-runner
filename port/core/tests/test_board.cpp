// The launcher's status board as the core keeps it (fzgx/board.h, board.cpp, board_race.cpp): taken at Init from
// the launch file's "board", refused when it is not of this version, the heartbeat and the phase, the mailbox's
// notice and stop, the stop's reason, the freeze watch's events, the race's snapshot from the game's memory, and the
// session's phases. The board is a file made as the launcher makes it (View::Make), mapped by the core.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "harness.h"

#include "board_net.h"
#include "fzgx/board.h"
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
  CHECK(board::Taken() && host.notices.find("fzgx: board: taken (game.board, version 1)") != std::string::npos);
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
                                         "version 2, this game's 1") != std::string::npos);
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
    const u32 base = BLOCKS + i * 0x620, m = PROGRESS + i * 0x200;
    g.Write32(base + 0x49C, m);
    g.Write32(base + 0x17C, std::bit_cast<u32>(300.5f + static_cast<float>(i)));  // speed
    g.Write32(base + 0x184, std::bit_cast<u32>(80.0f));                           // energy
    g.Write16(base + 0x5DA, 100);                                                  // maximum energy
    g.Write32(base + 0x7C, std::bit_cast<u32>(-12.5f));                            // x
    g.Write8(m + 0x115, static_cast<u8>(i + 1));                                   // rank
    g.Write32(m + 0x118, i);                                                       // laps
    g.Write8(m + 0x160 + 8, 1), g.Write8(m + 0x160 + 9, 2), g.Write16(m + 0x160 + 10, 345);  // lap 1'02"345
    g.Write8(m + 0x1CC + 9, 59), g.Write16(m + 0x1CC + 10, 999);                   // best lap 59"999
    g.Write32(m + 0xF8, std::bit_cast<u32>(1000.0f * static_cast<float>(i)));      // distance
  }
  g.Write32(BLOCKS, 0x10000);                    // racer 0 finished
  g.Write32(BLOCKS + 0x620 * 2, 0x10000000);     // racer 2 out (its time over)
  g.Write32(BLOCKS + 0x620 * 2 + 0x17C, std::bit_cast<u32>(std::numeric_limits<float>::infinity()));
  g.Write8(BLOCKS + 0x620 + 0x212, 2);           // racer 1: two knockouts in its attack
  board::Race(g, 600, 1);
  g.Write8(BLOCKS + 0x620 + 0x212, 3);           // and a third
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
  Shutdown();
  board::Close();
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
