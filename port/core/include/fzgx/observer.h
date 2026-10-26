// The observer HUD's numbers (the design canvas "Spectator observer HUD", boards Main, Live
// and KO): the timing tower's gaps to the leader, the race's lap and the racers still racing, and the texts the HUD
// draws them as. The decision alone, plain numbers with no game in it, so that the order, the gaps and the texts
// are tested without one (tests/test_observer.cpp); observer_frame.cpp reads the race once a displayed frame and
// asks it, and the runner draws what it is told (port/moderngekko/src/observer_hud.cpp).
//
// The gap is a time, as a broadcast's: how long ago the front of the race stood where the racer stands now. The
// front (the furthest distance any racer has covered, progress +0xF8, over all laps) is kept for every step shown
// (FrontClock); a racer's gap is the steps since the front first reached its distance. A racer a whole lap or more
// behind the front is lapped ("+1 LAP"); a finished racer's gap is its finish time less the winner's; one out says
// so. Where the clock does not reach back (a spectator who came in mid-race), the distance at the racer's own
// speed stands in.
//
// Picture only: nothing here is read by the race.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace fzgx::observer
{
using u8 = std::uint8_t;
using u32 = std::uint32_t;

constexpr u32 STEPS_PER_SECOND = 60;
constexpr float NEVER = -1.0f;

// The front of the race by step: for each step shown, the furthest distance covered by then (never less than the
// step before). Steps are recorded as they are shown, in any order (a scrubber moves back and on); the steps between
// two recorded ones far apart are filled in a straight line.
class FrontClock
{
public:
  void Clear();
  void Record(u32 step, float front);
  // How many steps before `step` the front first reached `distance` (a fraction between two steps by a straight
  // line); 0 when `distance` is at or beyond the front at `step`; NEVER when the clock does not reach back to then.
  float Behind(u32 step, float distance) const;
  // The front at `step`; NEVER before the first step recorded.
  float At(u32 step) const;
  bool Empty() const { return m_front.empty(); }

private:
  u32 m_first = 0;
  std::vector<float> m_front;
};

// A racer as the gaps need it.
struct Standing
{
  bool present = false, out = false, finished = false;
  u32 rank = 0;            // 0 the leader
  float distance = 0;      // over all laps
  float lap_length = 0;    // the course's lap (progress +0xFC); 0: not known
  float speed = 0;         // km/h
  u32 finish_ms = 0;       // its race time once finished
};

enum class GapKind : u8
{
  None,      // not present
  Leader,
  Time,      // behind the leader by `ms`
  Laps,      // a lap or more behind the front: `laps`
  Out,
  Finished,  // finished `ms` after the winner
};
struct Gap
{
  GapKind kind = GapKind::None;
  u32 ms = 0;
  u32 laps = 0;
};

// The gap of `racer`, at `step`, in a race whose front is `clock`; `winner_ms` the winner's finish time (0: nobody
// has finished).
Gap GapOf(const Standing& racer, const FrontClock& clock, u32 step, u32 winner_ms);
// The least finish time of those finished (0: none).
u32 WinnerTime(const Standing* racers, std::size_t count);
// Racers still racing: present, neither out nor finished.
u32 Racing(const Standing* racers, std::size_t count);
// The race's lap as the tower's head says it: the leader's lap on (laps completed plus one), at most the race's
// laps (0: a free run, no most).
u32 LapOf(u32 leader_laps_done, u32 laps);

// A machine's name by its id (0 to 40, as the match file and the race block hold it: the launcher's list,
// Online.Protocol GameMachines); "Custom" for a garage-built machine (41 on).
const char* MachineName(u32 id);

// The texts. "LEADER"; "+0.9", "+12.3", "+1:02.4"; "+1 LAP", "+2 LAPS"; "OUT"; nothing for None.
void GapText(const Gap& gap, char* out, std::size_t size);
// A time as the game's HUD writes it: 0'38"512.
void ClockText(u32 ms, char* out, std::size_t size);
}  // namespace fzgx::observer
