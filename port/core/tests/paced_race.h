// A paced race, for the tests of when steps run (test_pace.cpp, where RunAPacedRace is) and of the line a race
// leaves (test_summary.cpp): what its host and relay do, and what came of it.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "harness.h"
#include "net_internal.h"

#include <map>

constexpr double FRAME_MS = 1000.0 / 60;
constexpr u32 FROM = 10;      // the race phase begins at this step
constexpr u32 SETTLED = 120;  // displayed frames of the race after which its start is over

// What the host and the relay of one race do.
struct Pace
{
  u32 race_frames = 1500;     // displayed frames of the race phase
  // What the race's frames take, in ms, one after the other and round again. 0, and when empty: 5 ms, and 1.5 ms
  // for each step after the frame's first (a host with time to spare).
  std::vector<double> work;
  u32 stands_at = 0;          // at this frame of the race the relay's clock stands for stands_steps steps
  u32 stands_steps = 0;
  u32 race_to = 1000000;      // the race phase ends with this step (the frames after it are the results')
  bool summary = false;       // the host names a folder for the race's summary line (Raced::summary)
  bool summary_throws = false;  // and throws when it is asked for it
  bool record = false;        // the race is recorded (Raced::results: the recording's results, if it holds any)
  u32 delay = 2;              // this game's input delay in steps (the player's setting: 0, 1 or 2)
  u32 other_changes_every = 7;  // the other seat's input changes every so many steps (1: every prediction is wrong)
  // From frame together_from to together_to the game is told of the frames closed only rows_together at a time: a burst.
  u32 rows_together = 0, together_from = 0, together_to = 0;
  u32 relay_back_steps = 0;   // an official frame reaches the game this many steps after the relay closed it (the
                              // way back of a round trip: the game's rollbacks are as much deeper)
  // The host hands a picture to the screen in every displayed frame of the race, this many microseconds after the
  // one before, and every `picture_late_every`th one `picture_late_us` later. 0: the host shows none.
  u32 picture_us = 0, picture_late_every = 0, picture_late_us = 0;
  // The local seat is out from this displayed frame of the race on (0: never): the image's count of frames out
  // (net::OUT_FRAMES, control block +248) goes up by one a frame from there, to 180, as frame_spectate.py's does.
  u32 out_at = 0;
  u32 out_until = 0;           // from this frame of the race on the count is 0 again: a knock-out taken back (0: never)
  bool undo_at_behind = false;  // the count is 0 again from the frame the game would confirm the seat out on
                                // before going into lockstep: a knock-out taken back in the frame before
  u32 finish_goal = 0;         // FakeRace::finish_goal (the local seat has finished from the race's first step)
  u32 stall_at = 0, stall_ms = 0;
  // Seat 0 knocked out by seat 1's input in this step (FakeRace::knock_out_at; 0: not), and the image's count of
  // frames out kept as frame_spectate.py keeps it, after each frame's steps from the machine's flags
  u32 knock_out_by_input_at = 0;  // the race's frame stall_at takes this long (a hitch of the host)
  bool follow_speed = false;   // the host keeps the emulation speed the game sets (Host::SetPace) in the race
  double relay_rate = 1.0;     // the relay's clock runs this fast against the host's
};

// What came of it.
struct Raced
{
  bool ran = false;                      // to its last frame, nothing logged as an error
  std::vector<double> sched, held, step;  // the rollback log's columns, a value for every displayed race frame
  std::vector<double> depth, catch_up;   // (the same: the frame's rollback in steps, the steps it added to catch up)
  std::vector<double> watch, speed, buffer;  // (the same: how the game watched, its speed, official frames in hand)
  u32 other_checksums = 0;               // final steps whose checksum is not the official race's
  u32 final_steps = 0;
  // the local seat's inputs as the relay saw them, for the frames first sent after the race's start was over:
  std::vector<double> margin;            // frames left until the relay closed the frame, when its input came
  u32 late = 0;                          // inputs that came for a frame already closed
  u32 sent_twice = 0;                    // frames the game sent two different inputs for
  u32 not_stamped = 0;                   // closed frames whose input is not the one the game last sent for them
  std::string summary;                   // the file of summary lines the game left (Pace::summary)
  std::string results;                   // the results the recording was closed with (Pace::record)
  std::string warnings;                  // what the session logged as a warning or an error when it closed
  std::string notices;                   // and as a notice
  std::vector<std::array<u8, 6>> official_mine;  // the local seat's record in each official frame, by frame
  std::map<u32, std::array<u8, 6>> pads;  // the pad of the displayed race frame that began at each step
  u32 next_input_end = 0;                 // the session's Session::next_input as the race stopped
};

// The image's count of frames the local seat has been out (Pace::out_at), at a frame of the race from out_at on: 0
// once taken back.
inline u32 OutCount(const Pace& pace, u32 race_frames, bool undone)
{
  if (undone || (pace.out_until != 0 && race_frames >= pace.out_until))
    return 0;
  return std::min<u32>(race_frames - pace.out_at + 1, 180);
}

// Before a displayed frame (RunAPacedRace), as the image would have left it: the count of frames out by the frame
// number (Pace::out_at), or after the frame before's steps by the machine's flags (knock_out_by_input_at); and the
// frame's pad kept by its step.
inline void BeforeTheFrame(FakeRace& race, const Pace& pace, Raced& out, u32 step, const std::array<u8, 6>& pad,
                           bool racing, u32 race_frames, bool& undone)
{
  undone = undone || (pace.undo_at_behind && net::s_session && net::s_session->behind_next);
  if (racing && pace.out_at != 0 && race_frames >= pace.out_at)
    race.g.Write32(layout::CONTROL + 248, OutCount(pace, race_frames, undone));
  if (racing && pace.knock_out_by_input_at != 0)
  {
    const u32 out_frames = race.g.Read32(layout::CONTROL + 248);
    const bool retired = (race.g.Read32(race.machines) & 0x08000000) != 0;
    race.g.Write32(layout::CONTROL + 248, retired ? std::min<u32>(out_frames + 1, 180) : 0);
  }
  if (racing)
    out.pads.try_emplace(step, pad);
}

// The race run (test_pace.cpp says what its host and relay are), and a share of sorted values.
Raced RunAPacedRace(const Pace& pace);
double Percentile(std::vector<double> values, double p);
