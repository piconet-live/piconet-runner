// A game that stands still in a race says so in its log. The game's thread publishes a few plain values as it goes
// (Pulse: a count of game frames, the race's step, whether a race runs); a thread of the host's reads them a few
// times a second and decides when to speak (Watch). Nothing here reads or writes the game's memory, and a healthy
// frame pays two stores for it. What the host adds to a report (where its threads are) is the host's own.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <atomic>
#include <chrono>
#include <string>

#include "fzgx/host.h"

namespace fzgx::freeze
{
// What is published. One game per process, so there is one (ThePulse).
struct Pulse
{
  // written by the game's thread
  std::atomic<u32> frames{0};            // game frames begun (the frame tick): the heartbeat
  std::atomic<u32> step{0};              // the race's step at the last displayed frame
  std::atomic<bool> racing{false};       // from the relay's go until the session ends, fails or is over
  std::atomic<bool> waits_for_relay{false};  // the core itself holds the frame: it waits for official inputs
  std::atomic<bool> countdown{true};     // the race's countdown (lockstep, with the course's loads): until the first
                                         // frame that runs ahead; written when it changes
  // written by the watching thread
  std::atomic<u32> freezes{0};           // times the game stood for FIRST_SECONDS or longer (in the countdown:
                                         // COUNTDOWN_SECONDS) while the core did not wait for the relay
  std::atomic<u32> waits{0};             // times a frame was held that long by the core's wait for the relay
  std::atomic<u32> longest_ms{0};        // the longest of the freezes (a wait is not in it)
  std::atomic<u32> standing_ms{0};       // how long the freeze of now has lasted, at the last look
  std::atomic<bool> standing{false};     // it stands now: a freeze was reported and no frame came since
  // written by whoever asks the game to stop (Stop)
  std::atomic<bool> stopping{false};
  std::atomic<bool> stood_at_stop{false};  // it stood when the first stop was asked for

  void Reset();
};
Pulse& ThePulse();

// A game counts as standing when no game frame began for this long. In the countdown a healthy frame can take
// most of a second (the course's loads) and a slow disc more: there the first report waits longer.
constexpr double FIRST_SECONDS = 2.0, COUNTDOWN_SECONDS = 5.0;
// One freeze is reported at most this often: after 2, 4, 8, 16 and 32 s (in the countdown 5, 8, 16, 32), then
// once a minute.
constexpr u32 MOST_REPORTS = 10;
// How long the game has stood when its report number `report` (from 0) is due, in seconds.
double ReportAt(u32 report, bool countdown = false);

struct Said
{
  // Stands: a report is due. MovesAgain: a frame began after one. Left: the race ended for this game (lost, or its
  // recording over) while it was being reported; nothing stands any more.
  enum class What { Nothing, Stands, MovesAgain, Left };
  What what = What::Nothing;
  double seconds = 0;  // how long it has stood, or stood
  u32 report = 0;      // Stands: which report of this freeze, from 1; else how many were made
  bool wait = false;   // it is the core's own wait for the relay's inputs: counted apart, and no freeze
};

// The watching thread's side: looked at a few times a second, it says when a report is due and when a game that
// was reported moves again, and keeps the pulse's tallies. Only while a race runs and no stop was asked for.
class Watch
{
public:
  using Time = std::chrono::steady_clock::time_point;
  Said Look(Pulse& pulse, Time now);

private:
  void Tally(Pulse& pulse, Time now) const;

  bool m_watching = false;
  u32 m_frames = 0;
  Time m_since{};
  u32 m_reports = 0;
  bool m_wait = false;  // what is being reported is a wait for the relay (as its first report found it)
};

// A stop was asked for: the watch ends. True for the first one asked; the pulse then keeps whether the game stood.
bool Stop(Pulse& pulse);

// The report's first line ("fzgx: freeze: ...", or "fzgx: wait: ..." for the core's own wait, which has no other
// line), and the line of a game that moves again or has left its race.
std::string StandsLine(const Said& said, const Pulse& pulse);
std::string MovesLine(const Said& said);
// The line of a stop ("fzgx: stop: WHY"): `first` as Stop said; `stood_ms` how long the freeze of that moment had
// lasted (Pulse::standing_ms, taken before Stop), or nothing when the game did not stand.
std::string StopLine(const std::string& why, bool first, bool stood, u32 stood_ms);
// The words a race's summary line gets (protocol/formats.md, section 6):
// "freezes=1 stood=6.2 stuck=yes waits=0".
std::string SummaryWords(const Pulse& pulse);
}  // namespace fzgx::freeze
