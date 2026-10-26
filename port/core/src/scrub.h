// The replay scrubber's state in a playback's session (net_internal.h includes this; scrub.cpp says what it is).
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <vector>

#include "fzgx/host.h"
#include "rollback.h"

namespace fzgx::net
{
constexpr double FIELD_TICKS = 40500000.0 / 59.94;  // the emulated time base's ticks in a field
constexpr u32 KEY_EVERY = 128;      // a whole copy of the front's moment every so many steps (scrub_move.cpp)
constexpr u32 KEY_KEPT = 4 * KEY_EVERY;  // over the budget, a key every so many steps from the race's first stays
constexpr u32 PLAY_LOG_STEPS = 600; // the play position's own log reaches this many steps back (ten seconds)
constexpr u32 MOST_LAPS = 16;       // lap marks the bar is given
constexpr u32 NONE_STEP = 0xFFFFFFFF;

// What the frames of one scrub (the bar pressed, dragged, released) did, or of all of them: for the log.
struct ScrubCount
{
  u32 scrubs = 0, frames = 0, pictures = 0, clamped = 0;  // frames while held; those that showed another step;
                                                          // those whose pointer was beyond what can be shown
  u32 by_key = 0, by_edge = 0, by_logs = 0, by_runs = 0;  // how the memory got to each picture's step
  double ms = 0, move_ms = 0, move_most_ms = 0;           // the scrub's time; the time moving the memory
  u32 from = 0, to = 0;
};

// The leader's laps, read at the front edge's steps (read only): the step at which the first racer completed each
// lap, for the bar's marks.
// What each racer's flags showed at the front edge's step boundaries (read only): the boundary (the step about to run)
// at which it was first finished (0x10000) and first out by the game's own verdict (out_test.h: the image's finish
// banner and spectating count the same). NONE_STEP: not (yet).
struct RacerEnds
{
  std::array<u32, 30> finish{}, out{};
  RacerEnds() { finish.fill(0xFFFFFFFF); out.fill(0xFFFFFFFF); }
};

struct LapMarks
{
  u32 laps = 0;                       // the most laps any racer has completed so far
  std::array<u32, MOST_LAPS> at{};    // the step of each, in order
  u32 count = 0;
};

// One decision of the front steps' budget (FrontStepFits): may one more front step be given to this frame?
struct FrontBudget
{
  u32 committed = 0;       // front steps the frame has run or is set to run (the frame routine is told one ahead)
  double since_ms = 0;     // the host's time since the frame's start
  double step_ms = 0;      // a front step's time on the host's clock, smoothed (0: not known)
  double own_ms = 0;       // the frame's own work: its play step and its drawing (0: not known)
  double step_ticks = 0;   // the same two in the emulated time base's ticks (0: not known)
  double own_ticks = 0;
  double share = 0.4;      // the upper bound: the share of a field front steps may take (Debug::buffer_share)
  u32 lead = 0xFFFFFFFF;   // steps the front edge is ahead of the play position
};
constexpr double WHOLE_SHARE = 0.9;  // of a field, for the whole frame: its own work and its front steps
constexpr u32 LEAST_LEAD = 60;       // below this lead of the front edge over the play position a frame's first front
                                     // step always runs (the play position never passes the front edge)
// scrub_budget.cpp: the budget's rule; the frame's own work measured (from the end of its front steps to the next
// frame's start, less the host's waits for the speed limit and the emulated processor's idle ticks)
bool FrontStepFits(const FrontBudget& b);
struct ScrubState;
void OwnWorkBegins(ScrubState& a);
void OwnWorkEnds(ScrubState& a, std::chrono::steady_clock::time_point now);

struct ScrubState
{
  bool forked = false;      // the two moments exist: from the race's first frame until the race has settled
  bool over = false;        // ... and it has
  Rollback::Moment other;   // the moment the memory does not hold now
  Rollback::Moment base;    // the memory as the race's first frame found it: what a key's missing pages are
  bool in_front = false;    // the memory holds the front edge's moment (between hooks of one frame only)
  bool play_next = false;   // the frame's next hook is the play step's
  u32 first = 0;            // the race's first step, where the two moments parted
  u32 front = 0;            // the front edge: every step below it is buffered
  bool front_done = false;  // the front edge stands where the race is over
  u32 end = 0xFFFFFFFF;     // with front_done: the replay's end. The play position never passes it, so its step
                            // (the one that makes the race over; a cut recording's last) is never run for a picture
  bool end_said = false;
  bool end_paused = false;  // the replay paused itself at its end (not: Space did)
  bool played_on = false;   // a check's playback, its script done, was set playing to its end
  u32 play_step = 0;        // the step this frame runs for its picture,
  bool play_silent = false; // without its sounds (a picture shown again),
  bool play_ran = false;    // and it did run (not: the frame was cut at the front's last step)
  bool cut = false;         // the frame before ran no play step: what it changed is put back
  bool paused = false;
  bool held = false;        // this frame shows the picture of the frame before again
  // a scrub (the bar pressed): the frame shows the step under the pointer
  bool scrubbing = false, clamped = false;
  ScrubCount one, all;      // the scrub under way, and all so far
  std::chrono::steady_clock::time_point scrub_at{};
  u32 wait_for = 0xFFFFFFFF;  // a jump beyond the front edge, taken when the front edge is past it
  u32 pure_at = 0xFFFFFFFF;   // the step at which the play position's memory was last the front's own
  // steps of the play position run again from a key before the frame's own (their log was dropped, over the
  // budget): the next, and how many more
  u32 catch_at = 0, catch_left = 0;
  LapMarks laps;
  RacerEnds ends;
  u32 banner_said = 0;      // racers whose finish banner was checked at the play position (bits; a line each)
  // the budget of a frame's front steps
  std::chrono::steady_clock::time_point frame_at{}, hook_left{};
  double step_ms = 0;       // a front step's time on the host's clock, smoothed (0: not known)
  double step_ticks = 0;    // ... and in the emulated time base's ticks
  u32 front_steps = 0;      // front steps in this frame so far
  double own_ms = 0;        // the frame's own work (FrontBudget), smoothed as step_ms (0: not known)
  double own_ticks = 0;     // ... and in the emulated time base's ticks (0: not known)
  bool own_open = false;    // the frame's own work is being measured: since own_at, and the host's clocks then
  std::chrono::steady_clock::time_point own_at{};
  GameClocks own_clocks;
  bool own_clocked = false; // ... which the host knew
  u32 frames_without = 0, long_with = 0;  // frames while buffering that ran no front step; frames that ran front
                                          // steps and took longer than a field and a tenth
  std::chrono::steady_clock::time_point race_at{};  // the race's first frame
  u32 frames = 0;           // displayed frames of the race so far
  // tests (Debug::replay_script): its entries (frame, what, n), the next, and a scrub of its own
  bool script_read = false;
  std::vector<std::array<u32, 3>> script;
  std::size_t script_next = 0;
  bool script_held = false;
  long long script_pointer = 0, script_speed = 0;
  // ... and its entries for the spectator controls (spectator_frame.cpp takes them at the next frame): pad buttons
  // down for one frame ('b'), a racer clicked ('w'), a camera kind's button ('c')
  u32 script_pad = 0, script_follow = 0xFFFFFFFF;
  int script_kind = -1;
  // measurements, for the lines at the end. By kind of frame (0 playing beside front steps, 1 playing alone, 2 the
  // picture standing beside front steps, 3 standing alone): frames, their time, their front steps, and those longer
  // than a field and a tenth
  std::array<u32, 4> kind_frames{}, kind_steps{}, kind_long{};
  std::array<double, 4> kind_ms{}, kind_longest_ms{};
  int kind = -1;            // the frame before's
  double swap_us = 0, swap_most_us = 0;   // exchanging the two moments, a frame (both ways)
  u32 swaps = 0;
  double log_us = 0;                      // keeping the front's log
  u32 front_total = 0;                    // front steps in all
  std::vector<u32> front_differed, play_differed;  // steps whose checksum differed from the recording's
  u32 play_checked = 0;
  std::size_t front_bytes = 0, play_bytes = 0, key_bytes = 0;  // the most the two logs held; the keys
  std::size_t replay_most = 0;                     // the most the replay held beyond the game's own
  u32 jumps_front = 0, jumps_play = 0, jumps_key = 0, jump_steps_most = 0;
  double jump_ms = 0, jump_most_ms = 0;
  u32 catch_steps = 0, catch_frames = 0, cuts = 0, cut_catches = 0;
  u32 over_budget = 0;      // frames that ended over the budget with nothing left that may go
  u32 frames_ahead = 0, frames_back = 0, frames_back_keyed = 0;  // frame steps (. and ,); back ones from a key
  double frame_back_ms = 0, frame_back_most_ms = 0;              // moving the memory for a frame back
  u32 racer_changes = 0;
  bool said = false;
};

struct Session;
// scrub.cpp (the replay scrubber). On in this session; the step below which the play log's entries are dropped;
// before a frame's first hook work (the step the frame before showed, against the recording); a race's frame,
// whole; the hook between two steps of such a frame; the race has settled; the bar outside the race; the bytes the
// scrubber holds beyond the game's own.
bool ScrubOn(const Session& s);
u32 ScrubKeeps(const Session& s);
void ScrubVerify(Guest& g, Session& s);
void ScrubFrame(Guest& g, Session& s, u32 step);
void ScrubExtra(Guest& g, Session& s);
void ScrubStop(Session& s);
// scrub_show.cpp: the racers at the front edge's boundary before step t (read only); the finish banner's words for
// the play position before step p; the bar of a race's frame; the bar outside the race.
void ReadRacers(Guest& g, Session& s, u32 t);
void FinishBanner(Guest& g, Session& s, u32 p);
void ShowBar(const Session& s);
void ScrubBarOutside(Session& s, u32 step);
// scrub_move.cpp: the step this frame runs for its picture, by the keys, the bar and a test's script, with the
// memory brought to before it.
u32 ScrubMove(Guest& g, Session& s, u32 play);
// scrub_report.cpp: the lines at the end; the test script read; the replay's bytes.
void ScrubSummary(Session& s);
void ScrubMemorySummary(Session& s);
void ReadScrubScript(ScrubState& a);
std::size_t ScrubBytes(const Session& s);
}  // namespace fzgx::net
