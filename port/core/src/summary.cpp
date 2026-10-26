// A race's summary (summary.h): the tallies' arithmetic, which frames count as late, and the line the game appends
// to the player's file when it stops. Why: after "it felt laggy" there was nothing to read; the relay's own log
// counts every late input, also the countdown's, which are late by design.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "net_internal.h"

#include "files.h"
#include "fzgx/present.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <format>

namespace fzgx::net
{
namespace
{
// The file is the player's to send: the last RACES_KEPT races.
constexpr std::size_t RACES_KEPT = 300;
constexpr const char* FILE_NAME = "race-summary.txt";
constexpr const char* PHASE_NAMES[3] = {"countdown", "race", "after"};

// "median/worst" of a tally with `digits` after the point; "-" when nothing was tallied.
std::string Pair(const Spread& spread, double worst, int digits)
{
  if (spread.Count() == 0)
    return "-";
  return std::format("{:.{}f}/{:.{}f}", spread.Median(), digits, worst, digits);
}

// The race as its match names it: the match's id (letters, digits, - and _ only: it is the server's text), the
// course, the racers. A session without a match (the core's own tests): "-".
std::string RaceOfMatch()
{
  const boot::Match* match = boot::Get();
  if (!match)
    return "match=- course=- racers=-";
  std::string id = boot::Text(match->json, "matchId");
  id.resize(std::min<std::size_t>(id.size(), 64));
  const bool plain = !id.empty() && std::all_of(id.begin(), id.end(), [](char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-' || c == '_';
  });
  std::size_t racers = 0;
  if (const picojson::object* setup = boot::Object(match->json, "setup"))
    if (const auto seats = setup->find("seats"); seats != setup->end() && seats->second.is<picojson::array>())
      racers = seats->second.get<picojson::array>().size();
  return std::format("match={} course={} racers={}", plain ? id : "-", paint::Be32(match->race.data() + 4), racers);
}

const char* EndOf(const Session& s)
{
  if (s.desyncs != 0)
    return "desync";  // the relay found this game's state different from the others'
  if (ExitCode() == EXIT_RACE_OVER)
    return "over";    // started again into a race the relay said was over: nothing to rejoin
  if (s.phase == Phase::Failed)
    return "lost";    // no official inputs for 15 s: the relay was gone
  return s.settled ? "finished" : "closed";  // the race ran to its end here; or the game was closed before it
}
}  // namespace

void Spread::Add(double value)
{
  const double at = std::round((value - m_low) / m_unit);
  ++m_counts[static_cast<std::size_t>(std::clamp(at, 0.0, static_cast<double>(m_counts.size() - 1)))];
  m_least = m_count == 0 ? value : std::min(m_least, value);
  m_most = m_count == 0 ? value : std::max(m_most, value);
  ++m_count;
}

double Spread::Median() const
{
  u32 below = 0;
  for (std::size_t i = 0; i < m_counts.size(); ++i)
  {
    below += m_counts[i];
    if (below * 2 >= m_count && below != 0)
      return std::clamp(m_low + static_cast<double>(i) * m_unit, m_least, m_most);
  }
  return 0;
}

void RaceSummary::Frame(Phase now_in, Time now, double behind, u32 step, u32 input_step)
{
  // (after the race there is no way back: the frames of a race phase that came back are lockstep's, as the rest)
  if (!begun || now_in > phase)
  {
    if (now_in >= Race && race_inputs_from == 0xFFFFFFFF)
      race_inputs_from = input_step;
    if (now_in == After)
      after_inputs_from = input_step;
    phase = now_in;
    second_began = now;  // a second belongs to one phase: the one cut by the change is not counted
    second_frames = 0;
  }
  PhaseTally& tally = phases[phase];
  if (begun)
  {
    const double ms = std::chrono::duration<double, std::milli>(now - last_frame).count();
    tally.seconds += ms / 1000;
    tally.frame.Add(ms);
    if (ms > FRAME_MS + SLOW_MS)
      ++tally.slow;
    const double second_ms = std::chrono::duration<double, std::milli>(now - second_began).count();
    if (second_ms >= 1000)
    {
      tally.rate.Add(second_frames * 1000.0 / second_ms);
      second_began = now;
      second_frames = 0;
    }
  }
  begun = true;
  ++tally.frames;
  ++second_frames;
  tally.behind.Add(behind);
  last_frame = now;
  last_step = step;
}

void RaceSummary::Ping(double ms)
{
  if (begun)
    phases[phase].ping.Add(ms);
}

void RaceSummary::Shown(double ms)
{
  if (!begun)
    return;
  phases[phase].shown.Add(ms);
  if (std::abs(ms - FRAME_MS) > UNEVEN_MS)
    ++phases[phase].uneven;
}

void RaceSummary::Late(u32 input_step)
{
  ++phases[input_step < race_inputs_from ? Countdown : input_step < after_inputs_from ? Race : After].late;
}

namespace
{
bool OwnInputReplaced(const Session& s, u32 t)
{
  const auto mine = s.my_inputs.find(t);
  return !s.playback && s.my_index >= 0 && t < s.official.size() && mine != s.my_inputs.end() &&
         !std::equal(mine->second.begin(), mine->second.end(), s.official[t].begin() + s.my_index * RECORD);
}
}  // namespace

void NoteOwnInput(Session& s, u32 t, bool inserted)
{
  if (inserted && OwnInputReplaced(s, t))
    s.summary.Late(t);
}

void NoteOfficialRow(Session& s, u32 t)
{
  if (OwnInputReplaced(s, t))
    s.summary.Late(t);
}

std::string SummaryLine(const Session& s, const std::string& utc)
{
  const RaceSummary& r = s.summary;
  std::string line = std::format("1 {} {} players={} seat={} delay={} present={} end={} steps={}", utc, RaceOfMatch(),
                                 s.seats.size(), s.my_seat, s.delay, present::WayOf(TheOptions().present, TheOptions().debug.single_core),
                                 EndOf(s), r.last_step);
  line += " " + freeze::SummaryWords(freeze::ThePulse());
  for (int i = 0; i < 3; ++i)
  {
    const PhaseTally& p = r.phases[i];
    line += std::format(" | {} s={:.1f} frames={} ping={} fps={} behind={} late={}", PHASE_NAMES[i], p.seconds, p.frames,
                        Pair(p.ping, p.ping.Most(), 0), Pair(p.rate, p.rate.Least(), 0),
                        Pair(p.behind, p.behind.Most(), 1), p.late);
    line += std::format(" frame={} slow={} shown={} uneven={}", Pair(p.frame, p.frame.Most(), 1), p.slow,
                        Pair(p.shown, p.shown.Most(), 1), p.uneven);
    if (i == RaceSummary::Race)
      line += std::format(" rollbacks={} depth={} catchups={} held={}", r.depths.Count(),
                          Pair(r.depths, r.depths.Most(), 0), s.catch_ups, s.holds);
  }
  return line;
}

void WriteSummary(const Session& s)
{
  if (s.playback || !s.summary.Began())
    return;
  const std::time_t now = std::time(nullptr);
  std::array<char, 32> utc{};
  std::strftime(utc.data(), utc.size(), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&now));
  const std::string line = SummaryLine(s, utc.data());
  Notice("fzgx: race summary: {}", line);
  const std::string folder = TheHost().UserFolder();
  if (folder.empty())
    return;
  std::string why;
  // (the file's name only: the folder's path holds the name of the player's account)
  if (!AppendBounded(folder + "/" + FILE_NAME, line, RACES_KEPT, &why))
    Warn("fzgx: the race's summary was not written to {}: {}", FILE_NAME, why);
}

void WriteSummaryWhateverHappens(const Session& s)
{
#ifdef __cpp_exceptions
  try
  {
    WriteSummary(s);
  }
  catch (...)
  {
    // (no word of the exception's own: a file system's names the path)
    try
    {
      Warn("fzgx: the race's summary was not written to {}: an error while it was made", FILE_NAME);
    }
    catch (...)
    {
    }
  }
#else
  WriteSummary(s);
#endif
}
}  // namespace fzgx::net
