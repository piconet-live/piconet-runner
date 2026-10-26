// A game that stands still in a race (fzgx/freeze.h): when a report is due, the tallies for the summary line, the
// lines' words, and the test setting that makes a game stand ("debug.standStill").
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fzgx/freeze.h"

#include "net_internal.h"

#include <algorithm>
#include <format>
#include <thread>

namespace fzgx::freeze
{
void Pulse::Reset()
{
  frames = 0;
  step = 0;
  racing = false;
  waits_for_relay = false;
  countdown = true;
  freezes = 0;
  waits = 0;
  longest_ms = 0;
  standing_ms = 0;
  standing = false;
  stopping = false;
  stood_at_stop = false;
}

Pulse& ThePulse()
{
  static Pulse pulse;
  return pulse;
}

double ReportAt(u32 report, bool countdown)
{
  if (countdown)
    return report == 0 ? COUNTDOWN_SECONDS : ReportAt(report + 1);
  return report < 5 ? FIRST_SECONDS * (1u << report) : 32.0 + 60.0 * (report - 4);
}

// A freeze's length up to now, into the pulse (a wait for the relay is no freeze and is not tallied).
void Watch::Tally(Pulse& pulse, Time now) const
{
  if (m_wait)
    return;
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_since).count();
  const u32 stood = static_cast<u32>(std::clamp<long long>(ms, 0, 0xFFFFFFFFll));
  pulse.standing_ms = stood;
  pulse.longest_ms = std::max(pulse.longest_ms.load(), stood);
}

Said Watch::Look(Pulse& pulse, Time now)
{
  const bool reported = m_watching && m_reports != 0;
  const double stood = m_watching ? std::chrono::duration<double>(now - m_since).count() : 0;
  if (pulse.stopping)
  {
    // (a freeze the stop ended keeps its length up to here; "standing" stays as the stop found it)
    if (reported)
      Tally(pulse, now);
    m_watching = false;
    return {};
  }
  const u32 frames = pulse.frames;
  const bool racing = pulse.racing;
  if (!racing || !m_watching || frames != m_frames)
  {
    // a frame began, or the race is over for this game (lost to the relay, its recording ended): what was
    // reported is over either way, and nothing stands
    Said said;
    if (reported)
    {
      Tally(pulse, now);
      pulse.standing = false;
      pulse.standing_ms = 0;
      said = {racing ? Said::What::MovesAgain : Said::What::Left, stood, m_reports, m_wait};
    }
    m_watching = racing;
    m_frames = frames;
    m_since = now;
    m_reports = 0;
    m_wait = false;
    return said;
  }
  if (reported)
    Tally(pulse, now);
  if (m_reports >= MOST_REPORTS || stood < ReportAt(m_reports, pulse.countdown))
    return {};
  if (m_reports == 0)
  {
    // what it is, is settled here: the core's own wait for the relay's inputs, or a game that stands
    m_wait = pulse.waits_for_relay;
    if (m_wait)
      ++pulse.waits;
    else
    {
      ++pulse.freezes;
      pulse.standing = true;
      Tally(pulse, now);
    }
  }
  return {Said::What::Stands, stood, ++m_reports, m_wait};
}

bool Stop(Pulse& pulse)
{
  if (pulse.stopping.exchange(true))
    return false;
  pulse.stood_at_stop = pulse.standing.load();
  return true;
}

std::string StandsLine(const Said& said, const Pulse& pulse)
{
  if (said.wait)
    return std::format("fzgx: wait: no game frame for {:.1f} s: the core waits for the relay's inputs (report {} of this "
                       "wait; game frame {}, race step {}; it gives up after 15 s)",
                       said.seconds, said.report, pulse.frames.load(), pulse.step.load());
  return std::format("fzgx: freeze: no game frame for {:.1f} s (report {} of this freeze; game frame {}, race step {}; {}; {})",
                     said.seconds, said.report, pulse.frames.load(), pulse.step.load(),
                     pulse.countdown ? "in the countdown" : "in the race",
                     pulse.waits_for_relay ? "the core is waiting for the relay's inputs" :
                                             "the core is not waiting for the relay");
}

std::string MovesLine(const Said& said)
{
  const char* kind = said.wait ? "wait" : "freeze";
  const char* verb = said.wait ? "waited" : "stood";
  if (said.what == Said::What::Left)
    return std::format("fzgx: {}: the race is over for this game (lost, or its recording ended); it had {} for {:.1f} s "
                       "({} report{})", kind, verb, said.seconds, said.report, said.report == 1 ? "" : "s");
  return std::format("fzgx: {}: the game moves again; it {} for {:.1f} s ({} report{})", kind, verb, said.seconds,
                     said.report, said.report == 1 ? "" : "s");
}

std::string StopLine(const std::string& why, bool first, bool stood, u32 stood_ms)
{
  std::string line = std::string("fzgx: stop") + (first ? ": " : " (asked again): ") + why;
  if (first && stood)
    line += std::format("; the game stood still when it was asked (for {:.1f} s by then)", stood_ms / 1000.0);
  return line;
}

std::string SummaryWords(const Pulse& pulse)
{
  const u32 freezes = pulse.freezes;
  return std::format("freezes={} stood={} stuck={} waits={}", freezes,
                     freezes == 0 ? std::string("-") : std::format("{:.1f}", pulse.longest_ms / 1000.0),
                     pulse.stood_at_stop ? "yes" : "no", pulse.waits.load());
}
}  // namespace fzgx::freeze

namespace fzgx::net
{
// Tests ("debug.standStill"): before the displayed frame that begins at one of the named steps the game's thread
// sleeps, once for each. Nothing of the game is read or written: the step then runs as it would have.
void StandStill(Session& s, u32 step)
{
  const Debug& debug = TheOptions().debug;
  while (s.stands_done < debug.stand_steps.size() && step >= debug.stand_steps[s.stands_done])
  {
    const u32 tenths = debug.stand_tenths[s.stands_done++];
    Warn("fzgx: TEST: standing still for {:.1f} s before step {} (debug.standStill)", tenths / 10.0, step);
    std::this_thread::sleep_for(std::chrono::milliseconds(tenths * 100));
  }
}
}  // namespace fzgx::net
