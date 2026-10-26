// A race's summary: what the session counts while a race runs, and the one line it becomes when the game stops
// (summary.cpp; the line's format: protocol/formats.md, section 6). Counters in memory only: nothing here
// opens a file or allocates during a race.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "internal.h"

#include <array>
#include <chrono>
#include <string>

namespace fzgx::net
{
// Values tallied for their median and their worst: a count for each of 1,024 whole multiples of `unit` from `low`
// on (a value outside lands in the first or the last, so the median is exact to a unit within the range), and the
// least and the most exactly.
class Spread
{
public:
  Spread(double low, double unit) : m_low(low), m_unit(unit) {}
  void Add(double value);
  u32 Count() const { return m_count; }
  double Median() const;
  double Least() const { return m_least; }
  double Most() const { return m_most; }

private:
  double m_low, m_unit;
  std::array<u32, 1024> m_counts{};
  u32 m_count = 0;
  double m_least = 0, m_most = 0;
};

// What is counted in each of a race's three phases.
struct PhaseTally
{
  u32 frames = 0;                 // displayed frames
  double seconds = 0;
  Spread ping{0, 1};              // the round trip of each ping, ms
  Spread rate{0, 1};              // displayed frames in each whole second
  Spread behind{-20, 0.1};        // steps behind the schedule at each displayed frame (below 0: ahead)
  u32 late = 0;                   // frames the relay closed with another input for this seat than the game's own
  Spread frame{0, 0.1};           // ms from a displayed frame's start to the next one's (the hook's clock)
  u32 slow = 0;                   // of them, more than SLOW_MS over a frame's length: the frame's work did not fit
  Spread shown{0, 0.1};           // ms between the pictures the host handed to the screen (Host::PictureIntervals)
  u32 uneven = 0;                 // of them, more than UNEVEN_MS off a frame's length
};

// A displayed frame that came this much later than a frame's length after the one before is counted as slow, and a
// picture shown this much off a frame's length after the one before as uneven (2 ms: an eighth of a frame; the
// hook's own clock is even to 0.2 ms on a PC that holds the pace, the pictures to 0.7).
constexpr double SLOW_MS = 2, UNEVEN_MS = 2;

struct RaceSummary
{
  enum Phase { Countdown, Race, After };
  using Time = std::chrono::steady_clock::time_point;

  // A displayed frame: the phase it is in, its start, how far behind its schedule the game is, the step it begins
  // at and the step its own input is for.
  void Frame(Phase phase, Time now, double behind, u32 step, u32 input_step);
  void Ping(double ms);
  // The host handed a picture to the screen `ms` after the one before (counted in the phase the game is in).
  void Shown(double ms);
  void Rollback(u32 depth) { depths.Add(depth); }
  // The relay closed frame `input_step` with another input for this seat than the game's.
  void Late(u32 input_step);
  bool Began() const { return begun; }

  std::array<PhaseTally, 3> phases;
  Spread depths{0, 1};            // the race's rollbacks: steps gone back
  Phase phase = Countdown;
  bool begun = false;             // a frame was seen: the relay's go was taken
  u32 last_step = 0;
  // the first input step stamped in the race, and after it: an input belongs to the phase it was stamped in
  u32 race_inputs_from = 0xFFFFFFFF, after_inputs_from = 0xFFFFFFFF;
  Time last_frame{}, second_began{};
  u32 second_frames = 0;
};

struct Session;
// The game's own input for step `t` was stamped (`inserted`: for the first time), or the relay's official row for
// step `t` arrived: counts the frame as late when both are here and the row holds another input for this seat.
void NoteOwnInput(Session& s, u32 t, bool inserted);
void NoteOfficialRow(Session& s, u32 t);
// The session's line (no line end), and how the race ended, as the line says it.
std::string SummaryLine(const Session& s, const std::string& utc);
// When the game stops: the line appended to the player's file, for a race that began. The second: the same, and
// whatever the first throws ends in it, as one line of the log (a build without exceptions has nothing to end).
void WriteSummary(const Session& s);
void WriteSummaryWhateverHappens(const Session& s);

// files_summary.cpp: appends `line` to the text file at `path` and keeps the file to its last `keep` lines. False,
// and why (a sentence that names no path): the file is there and could not be read, or the new text could not be
// written; the file then stands as it was, and nothing is left beside it.
bool AppendBounded(const std::string& path, const std::string& line, std::size_t keep, std::string* why);
}  // namespace fzgx::net
