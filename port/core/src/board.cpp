// The status board (fzgx/board.h, board_net.h): taken at Init, then written by the game's thread (the heartbeat,
// the phase, the race), by the host's watching thread (its own beat, a freeze's reports) and by whoever asks for a
// stop; the launcher's mailbox is read on the game's thread once a picture. Every write is a few stores into the
// mapping: nothing waits, and a launcher that is gone, slow or never reads changes nothing here.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "board_net.h"

#include <atomic>
#include <chrono>
#include <optional>
#include <string>

#include "fzgx/freeze.h"
#include "fzgx/panel.h"
#include "internal.h"

namespace fzgx::board
{
static_assert(static_cast<u32>(StopReason::Unasked) == 8, "fzgx/board.h's callers name the reasons the schema gives");

namespace
{
using Clock = std::chrono::steady_clock;

struct Board
{
  Mapping map;
  std::optional<View> view;               // a board this game took; set and cleared only before the threads exist
  Clock::time_point since;
  std::atomic<u64> events{0};             // event numbers handed out (several threads write events)
  std::atomic<u32> stop{0};               // the first stop's reason
  std::atomic<u32> step{0};               // the race's step, for the events
  std::atomic<bool> launcher_stop{false};
  u64 beat = 0;                           // the game's thread's
  u32 watch_beat = 0;                     // the watching thread's
  Status status;                          // as last written (the game's thread's)
  bool fields_beat = false;               // a plain start: every picture beats
};

Board& The()
{
  static Board board;
  return board;
}

u32 Ms()
{
  return static_cast<u32>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - The().since).count());
}

void Write(EventKind kind, u32 a, u32 b = 0)
{
  Board& s = The();
  if (!s.view)
    return;
  Event e;
  e.kind = kind;
  e.beat = static_cast<u32>(s.view->PulseHeartbeat());
  e.step = s.step.load(std::memory_order_relaxed);
  e.a = a;
  e.b = b;
  e.ms = Ms();
  s.view->WriteEvents(s.events.fetch_add(1, std::memory_order_relaxed), e);
}

// The status as it is now, written when it changed (the game's thread).
void Put(Phase phase, u32 flags, u32 step)
{
  Board& s = The();
  if (freeze::ThePulse().waits_for_relay.load(std::memory_order_relaxed))
    flags |= STATUS_FLAGS_WAITS_FOR_RELAY;
  if (s.status.phase == phase && s.status.flags == flags && s.status.step == step)
    return;
  const Phase before = s.status.phase;
  s.status.phase = phase;
  s.status.flags = flags;
  s.status.step = step;
  s.step.store(step, std::memory_order_relaxed);
  s.view->WriteStatus(s.status);
  if (phase != before)
    Write(EventKind::Phase, static_cast<u32>(phase), static_cast<u32>(before));
}

// Where the game is, from what the core knows.
void Update()
{
  Board& s = The();
  u32 flags = 0, step = s.status.step;
  Phase phase = Phase::Loading;
  if (s.stop.load(std::memory_order_relaxed) != 0)
    phase = Phase::Stopping;
  else if (s.fields_beat)
    phase = Phase::Offline;
  else if (const std::optional<Phase> session = net::SessionPhase(&step))
    phase = *session == Phase::Joining && panel::TheBoard().Now() == panel::Shows::Loading ? Phase::Loading : *session;
  const Options& o = TheOptions();
  flags |= (o.practice ? STATUS_FLAGS_PRACTICE : 0u) | (o.spectate ? STATUS_FLAGS_WATCHING : 0u) |
           (o.rejoin ? STATUS_FLAGS_REJOIN : 0u);
  Put(phase, flags, step);
}

// The launcher's words (the game's thread, once a picture): a notice to the log, a stop asked for.
void TakeMail()
{
  Board& s = The();
  Mailbox mail;
  if (!s.view->ReadMailbox(&mail, 4))
    return;  // (the launcher is writing it: the next picture reads it)
  bool changed = false;
  if (mail.notice_serial != s.status.notice_seen)
  {
    s.status.notice_seen = mail.notice_serial;
    changed = true;
    std::string text;
    for (u32 i = 0; i < std::min<u32>(mail.notice_length, static_cast<u32>(mail.notice_text.size())); ++i)
      text += mail.notice_text[i] >= 0x20 && mail.notice_text[i] != 0x7F ? static_cast<char>(mail.notice_text[i]) : ' ';
    Notice("fzgx: the launcher says: {}", text);
    Write(EventKind::Notice, mail.notice_serial);
  }
  if (mail.stop_serial != s.status.stop_seen)
  {
    s.status.stop_seen = mail.stop_serial;
    changed = true;
    if (!s.launcher_stop.exchange(true))
    {
      Notice("fzgx: the launcher asked the game to stop (its status board's mailbox); stopping");
      Stopped(StopReason::Launcher);
      TheHost().RequestStop();
    }
  }
  if (changed)
    s.view->WriteStatus(s.status);
}
}  // namespace

void Open(const std::string& path)
{
  Close();
  Board& s = The();
  if (path.empty())
    return;
  std::string error;
  if (!s.map.Open(path, &error))
  {
    Warn("fzgx: board: {} cannot be used ({}): the launcher sees nothing of this game but its end",
         FileNameAlone(path), error);
    return;
  }
  View view(s.map.At());
  const View::Taken taken = view.Take(ProcessId(), s.map.Size());
  if (taken != View::Taken::Ok)
  {
    Warn("fzgx: board: {} refused: {} (its version {}, this game's {}; its size {}, this game's {}): the launcher sees "
         "nothing of this game but its end",
         FileNameAlone(path),
         taken == View::Taken::NotABoard  ? "it is no status board" :
         taken == View::Taken::OtherVersion ? "the launcher's layout is another version" : "it is too small",
         view.HeaderVersion(), VERSION, view.HeaderSize(), SIZE);
    s.map.Close();
    return;
  }
  s.view = view;
  s.since = Clock::now();
  s.fields_beat = !Active();
  ResetRace();
  Put(Phase::Starting, 0, 0);
  Notice("fzgx: board: taken ({}, version {})", FileNameAlone(path), VERSION);
}

void Close()
{
  Board& s = The();
  s.view.reset();
  s.map.Close();
  s.events = 0;
  s.stop = 0;
  s.step = 0;
  s.launcher_stop = false;
  s.beat = 0;
  s.watch_beat = 0;
  s.status = {};
  s.fields_beat = false;
}

bool Taken()
{
  return The().view.has_value();
}

void Beat()
{
  Board& s = The();
  if (!s.view)
    return;
  s.view->SetPulseHeartbeat(++s.beat);
  Update();
}

void Field()
{
  Board& s = The();
  if (!s.view)
    return;
  // a plain start beats every picture; so does a race's while it loads (the frame tick stands in the course's load)
  if (s.fields_beat || panel::TheBoard().Now() == panel::Shows::Loading)
    s.view->SetPulseHeartbeat(++s.beat);
  Update();
  TakeMail();
}

void AtGate()
{
  Board& s = The();
  if (s.view && s.stop.load(std::memory_order_relaxed) == 0)
    Put(Phase::Gate, s.status.flags, s.status.step);
}

void Waits()
{
  Board& s = The();
  if (s.view)
    Put(s.status.phase, s.status.flags, s.status.step);  // (Put adds the flag while the pulse says it waits)
}

void Desync(u32 step)
{
  Write(EventKind::Desync, step);
}

void Look()
{
  Board& s = The();
  if (s.view)
    s.view->SetPulseWatchBeat(++s.watch_beat);
}

void Stopped(StopReason why)
{
  Board& s = The();
  u32 none = 0;
  if (!s.view || !s.stop.compare_exchange_strong(none, static_cast<u32>(why)))
    return;
  s.view->SetPulseStopReason(why);
  Write(EventKind::Stop, static_cast<u32>(why));
}

void Stands(u32 ms, u32 report)
{
  Write(EventKind::Stands, ms, report);
}

void Moves(u32 ms)
{
  Write(EventKind::Moves, ms);
}

void PutSnapshot(const Snapshot& snapshot)
{
  if (The().view)
    The().view->WriteSnapshot(snapshot);
}

bool LauncherStopped()
{
  return The().launcher_stop.load(std::memory_order_relaxed);
}
}  // namespace fzgx::board
