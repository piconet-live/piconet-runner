// What the relay session's files share (relay_wire.cpp, session.cpp, step.cpp, frame.cpp, schedule.cpp, playback.cpp,
// frame_report.cpp, spectate.cpp, instruments.cpp): the mod's words, the session's state, and what the nine call of
// each other.
// The network layer of the F-Zero GX online mod, lifted from the Dolphin fork (dolphin-fzgx Core/FzgxNet.cpp at
// 5bdc7c640e). Guest memory is read and written through fzgx::Guest; the mod's memory is fzgx::ModData.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "fzgx/freeze.h"
#include "hits.h"
#include "internal.h"
#include "relay_packets.g.h"  // the relay's packets (protocol/relay.toml): relay::Encode, relay::Decode*
#include "replay.h"
#include "rollback.h"
#include "scrub.h"
#include "spectator_state.h"
#include "timeline_state.h"
#include "summary.h"
#include "udp.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <deque>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace fzgx::net
{
// The session's clock: the steady clock, read through the host (Host::Now), which the tests replace.
struct Clock
{
  using duration = std::chrono::steady_clock::duration;
  using rep = duration::rep;
  using period = duration::period;
  using time_point = std::chrono::steady_clock::time_point;
  static time_point now() { return TheHost().Now(); }
};

// The mod's memory (the image generators' memmap.py, netframe.py and standin.py, in port/image/routines).
constexpr u32 GO = CONTROL + 40;
constexpr u32 READY = CONTROL + 44;
constexpr u32 GATE_ON = CONTROL + 48;
constexpr u32 SESSION = CONTROL + 116;
constexpr u32 RUNNING = CONTROL + 120;
// The mod's request to leave the game (the player is done with the results): 1 stops the emulation once, as the
// window's stop button does (the launcher starts the game with -b, so Dolphin then exits).
constexpr u32 EXIT_REQUEST = CONTROL + 0xA30;
// The image presses A on port 0 for one frame when this is 1 (netframe.py +0xA34), then sets it back to 0.
constexpr u32 PRESS_A = layout::PRESS_A_AT;
// How long every racer's results page stands before a racer's game leaves it by itself, as with the player's A (the
// owner, 28 October), and how long after that A the game stops anyway (session.cpp LeaveTheResultsInTime).
constexpr std::chrono::seconds RESULTS_LEAVE{30};
constexpr std::chrono::seconds RESULTS_LEAVE_GRACE{3};
// How long the race's own results page (the first, results state 0) stands before a racer's game passes it by itself
// with an A, to every racer's page, whose RESULTS_LEAVE then runs (the owner, 28 October: "First page gets a timer of
// 15s"; session.cpp PassTheFirstResultsPageInTime). Pressed again every RESULTS_LEAVE_GRACE while the page stays.
constexpr std::chrono::seconds RESULTS_FIRST_PAGE{15};
// The image's count of displayed race frames the local seat has been out (knocked out or retired; not finished),
// after which, at 180, it watches the leader (frame_spectate.py `spectate`); it stays at 180 from there.
constexpr u32 OUT_FRAMES = CONTROL + 248;
constexpr u32 FINAL_STEPS = CONTROL + 232;  // steps below this were simulated with official inputs: final
// The race's end, held until it is official (frame.cpp; the image: standin_race.py `over`).
constexpr u32 END_OFFICIAL = layout::END_OFFICIAL;  // ours: every step below this ran, or runs now, on official inputs
constexpr u32 END_ASKED = layout::END_ASKED;        // the image's: the race looks over on steps not yet official
constexpr u32 END_OVER = layout::END_OVER;          // the image's: its race-over test said "over"
// Tests: not 0, the image keeps its three logs (the spectator's, the smoothing's, the generator words'). Set in a
// game whose launch file asks for a dump (Debug::dump_step), the only way the logs are read; no other game's image
// writes them (session.cpp).
constexpr u32 IMAGE_LOGS = layout::IMAGE_LOGS;
constexpr u32 FORCE_ROLLBACK = CONTROL + 292;  // the image's test word (N << 16 | D); the core does not read it: forced
                                               // rollbacks come from the launch file only (Debug::force_every)
// Memory never rolled back, set by the mod: presentation that must run on through a rollback (the sound system's: put
// back, the race music jumped back and skipped; the face board's: put back, its heap went out of step with the models
// it had loaded). +0x700 the count of ranges (at most 32), then start, end pairs; read when rollback starts.
constexpr u32 DESYNCS = CONTROL + 128;
constexpr u32 FAILED = CONTROL + 132;
constexpr u32 ROLLBACKS = CONTROL + 136;
constexpr u32 MAX_DEPTH = CONTROL + 140;
constexpr u32 RESIM_STEPS = CONTROL + 144;
constexpr u32 RTT_TENTHS = CONTROL + 148;
constexpr u32 CATCH_UPS = CONTROL + 152;
constexpr u32 WATCHED_PAGES = CONTROL + 156;
constexpr u32 LATE_PAGES = CONTROL + 160;
constexpr u32 DISPLAYED_FRAMES = CONTROL + 164;
constexpr u32 VERIFY_CHECKSUMS = CONTROL + 216;  // tests: the mod computes its checksum too; differences counted
constexpr u32 CHECKSUM_DIFFERENCES = CONTROL + 220;
constexpr u32 TIME_FRAME = CONTROL + 200;
constexpr u32 RESIM_TICKS = CONTROL + 28;          // the mod: time base ticks of the last frame's extra steps
constexpr u32 TICKS_PER_EXTRA_STEP = CONTROL + 224;  // average over the frames since the last report
constexpr u32 TIME_TRACK = CONTROL + 204;
constexpr u32 TIME_RESTORE = CONTROL + 208;
constexpr u32 BLOCKS_PER_FRAME = CONTROL + 212;
constexpr u32 HANDBACK = layout::HANDBACK;  // ours, for the picture: steps left until the hand-back (rejoin.cpp)
// The racer the results' cameras follow (follow.py; spectator_frame.cpp ResultsCamera): ours, not 0 in a spectator's
// game (its seat is the one it watches); the image's pick, which we log and clear.
constexpr u32 RESULTS_KEEP = layout::RESULTS_KEEP;
constexpr u32 RESULTS_CAMERA = layout::RESULTS_CAMERA;
constexpr u32 SEAT_SLOT = table::SLOT;  // the seat routines' data slot 'FZSI': +44 the input delay
constexpr u32 PHASE = BSS + 0x3C2A;    // MD_GAME phase (s16): 0x29 = RACE
constexpr u32 RACE_CLOCK = BSS + 0x50A4;  // the race clock: one more after every step from the race phase's first
                                       // on; what the image's race-end rule reads as "now" (raceend.py)
constexpr u32 PAD_RECORD = BSS + 0xA88;  // port 0's 6-byte control record (the input layer's)
constexpr int RECORD = 6;
constexpr u32 MAX_FRAMES = 20;
constexpr u32 MAX_AHEAD = 24;  // steps simulated beyond the last official frame before waiting
constexpr double FRAME_MS = 1000.0 / 60;
constexpr int LEAD = 1;         // steps of margin for the local inputs to reach the relay before it closes their frames
constexpr u32 MAX_CATCH_UP = 1;  // new steps a displayed frame may add to catch up
constexpr u32 CATCH_UP_DEPTH = 6;  // ... and only in a frame whose rollback is no deeper than this, at the input delay
                                   // of 2; one step more for each frame of delay less (schedule.cpp, CatchUpOrHold)
// The time sync (NormalStep): a displayed frame that came this much later or sooner than a frame's length after the
// one before is a long or a short one; and the most the host's pacing is trusted to make up by itself, in steps.
constexpr double PACE_SLACK_MS = 1.0;
constexpr double MAX_OWED_STEPS = 1.5;
// The account of what the host's pacing owes (PaceOwed): the most it holds (the emulator itself gives up what it
// lost beyond 100 ms); the long frames a host that holds the pace may have close together; and the frames in a row
// without a long one after which it is taken to hold the pace again.
constexpr double PACE_OWED_MAX_MS = 100.0;
constexpr u32 PACE_BURST_FRAMES = 3;
constexpr u32 PACE_CALM_FRAMES = 4;
// The lead a host's long frames need (NormalStep, the hold). Right after long frames that took D steps over their
// length, the game is D steps later with its inputs; they still reach the relay in time when the game was more
// than D - HOLD_SPARE_STEPS ahead (the lead of 1, the relay's close window of 2, less the half step of a frame's
// rounding). The worst D of late is kept, and fades by a step a second.
constexpr double HOLD_SPARE_STEPS = 2.5;
constexpr double PACE_WORST_FADE_MS = FRAME_MS / 60;
constexpr u32 LOCKSTEP_SLACK = 2;        // official frames past the next step that lockstep leaves waiting
// A relay that closes a frame once every seat's input is there (Online.Server ServerOptions.EarlyClose) can send a
// game in lockstep official frames up to its own input delay past its next step: a delay above LOCKSTEP_SLACK would
// set off lockstep's catch-up at CATCH_UP_SPEED by that alone, in every countdown. The delay is 2 at most (the
// project's rule); raising it needs this looked at first.
static_assert(PLAYERS_INPUT_DELAY <= LOCKSTEP_SLACK, "an input delay above LOCKSTEP_SLACK: see the comment above");
// Official frames are kept up to this many past the steps this game has run (five minutes): a relay closes frames on
// its clock, so a game that loads or stalls is behind, but never by this much, and the relay sends again from the
// game's own count. Frames further ahead are not kept: named without end they filled the memory (finding M2).
constexpr u32 MAX_BEHIND = 18000;
// Rejoin: a game coming back runs up to RETURN_CATCH_UP steps a displayed frame on
// official rows while it is more than CAUGHT_UP steps behind the newest official frame (the relay's own count is
// 60 too); a running game that has had no official frame for REJOIN_AFTER asks for its seat again (Join), and
// waits up to REJOIN_WAIT for frames before it leaves its race.
constexpr u32 CAUGHT_UP = 60;
constexpr u32 RETURN_CATCH_UP = 30;
constexpr auto REJOIN_AFTER = std::chrono::milliseconds(1000);
constexpr auto REJOIN_WAIT = std::chrono::seconds(120);
constexpr u32 NEVER = 0xFFFFFFFF;
constexpr float CATCH_UP_SPEED = 3.0f;   // the emulation speed while lockstep catches up,
constexpr float CATCH_UP_CLOCK = 2.0f;   // at most this CPU clock override (lockstep has no re-simulation to run; at
                                         // 6x the host could not run 3x speed)
// A game whose racer is out or has finished, watching behind live: spectate.cpp says what each of these does.
constexpr u32 SPECTATE_SLACK = 9;          // 150 ms
constexpr u32 SPECTATE_SLACK_MOST = 18;    // 300 ms
constexpr u32 SPECTATE_CUT = 180;
constexpr float SPECTATE_SLOWEST = 0.99f;
constexpr float SPECTATE_FASTEST = 1.02f;
constexpr float SPECTATE_CATCH_UP_MOST = 1.25f;
constexpr double SPECTATE_FAR = 30;
constexpr u32 SPECTATE_DRIFT_CHECK = 240;
constexpr u32 SPECTATE_GAP = 30;
constexpr float SPECTATE_DRIFT_STEP = 0.005f;
constexpr float SPECTATE_DRIFT_FLOOR = 0.95f;
constexpr u32 SPECTATE_INTO_LOCKSTEP = 2;  // official frames in hand past a race's frame's step from which the
                                           // next frame is lockstep's

// How a game watches the race (spectate.cpp).
enum class Watch : u8
{
  Racing,    // its racer races: prediction and rollback, or lockstep outside the race phase
  Drifting,  // its racer is out or has finished: the race's frames, slower, until no step it runs is predicted
  Behind,    // lockstep on official frames only, SPECTATE_SLACK behind the newest
};

enum class Phase
{
  Joining,
  Joined,
  Running,
  Failed,
};

using Row = std::vector<u8>;  // the player seats' records for one step

// One displayed race frame's work, for the rollback log (tests: Debug::rollback_log) and its timing table. Times
// in microseconds.
struct FrameWork
{
  long long at_us = 0;   // the steady clock when the frame's first hook call began
  u32 step = 0;          // the step the frame began at (before any rollback)
  u32 depth = 0;         // steps rolled back (0: none)
  bool forced = false;   // by the test switch
  u32 catch_up = 0;
  bool held = false;     // the frame ran no new step: it fell back one step to the schedule
  u32 extra = 0;         // steps asked of the frame routine after the first
  int ahead = 0;         // the step less the official frames in hand
  double hook_us = 0;    // inside the hook, all calls of the frame (the undo log and the restore included)
  double first_us = 0;   // the game's code between the first hook call and the second: the frame's first step
  double resim_us = 0;   // the game's code between later hook calls: re-simulated steps,
  u32 resim_n = 0;       // and how many (the frame's last step ends with no hook call and is not timed)
  double sched = 0;      // steps ahead of the schedule the time sync aims at (NormalStep; meant: -1 to +2)
  double owed = 0;       // steps the host's pacing is expected to make up by itself (the frames before were long)
  Watch watch = Watch::Racing;  // how the game watched the race in this frame (spectate.cpp)
  float speed = 1.0f;    // the emulation speed the frame set (1: the host's own)
  double buffer = 0;     // official frames in hand past the frame's step (below 0: it ran ahead of them)
  // tests: who the cameras follow as the frame begins, the camera's place and that racer's drawn and true places
  // (the picture of the frame before; spectate.cpp WatchedPlaces)
  int watched = -1;
  std::array<float, 9> places{};
};

// freeze.cpp: tests ("debug.standStill"): the game's thread sleeps before the displayed frame of a named step.
struct Session;
void StandStill(Session& s, u32 step);

struct Session
{
  Rollback rollback;  // the undo log: reached through rollback.h only (with the replay scrubber: the play log)
  Rollback front;     // the replay scrubber: the front edge's log (scrub.cpp)
  Udp socket;
  Endpoint relay;  // the relay's address and port
  u64 token = 0;
  u64 key = 0;  // the seat's key (RelayKey of the token): every packet the relay sends this game opens with it
  Phase phase = Phase::Joining;
  bool go = false;
  // The relay's race-start barrier (sent in "go"; the relay's ServerOptions.StartBarrierFrame): official frames before
  // it close on the relay's clock while the games load the countdown (its first steps after "go", about 1.5 s); frames
  // from it on wait until every game has come through, so a game behind before it is not behind the race and does not
  // speed up.
  u32 start_barrier = 0;
  bool foreign_logged = false;  // a packet of another relay protocol version was logged
  bool refused_logged = false;  // a packet naming seats or frames that are no race's was logged
  bool key_logged = false;      // a packet from the relay's address without this seat's key was logged
  int my_seat = -1, my_index = -1;
  std::vector<int> seats;  // the race's player seats, in order
  u32 player_seats = 0;    // the same as the Joined's mask (bit i = seat i): every official packet must name it
  u32 delay = 2;
  u32 lag_ms = 0;  // test: an added round trip, half on what is sent and half on what is received
  u32 jitter_ms = 0;  // test: each packet, either way, is held up to this much longer (so packets overtake each other)
  u32 jitter_state = 0x9E3779B9;
  // packets held by the test lag, each with the time it is let through
  std::vector<std::pair<Clock::time_point, std::vector<u8>>> lagged;
  std::vector<std::pair<Clock::time_point, std::vector<u8>>> outgoing;
  std::vector<Row> official;  // per step
  std::map<u32, Row> pending;
  std::map<u32, std::array<u8, RECORD>> my_inputs;
  Clock::time_point last_join{};
  Clock::time_point last_ready{};
  // the start gate's wait, for the panel (fzgx/panel.h): when it began, and when the host was last told to draw
  Clock::time_point gate_since{}, panel_told{};
  Clock::time_point last_input{};
  u32 desyncs = 0;
  // rejoin and the cookie
  u64 cookie = 0;         // the relay's cookie of this game's address (its Joined): every later packet carries it (H9)
  u32 own_from = 0;       // the first step whose local record is the player's; NEVER while the seat is away
  bool returning = false; // the relay said the seat was away: catching up on official frames from this game's step
  bool watcher = false;   // a spectator's token (relay_wire.cpp): no seat, no input, watching behind live
  bool held = false;      // LOADING stays up until the game has caught up (a game started again, Options::rejoin)
  u32 target = 0;         // the newest official frame heard of: where catching up ends
  u32 hand_back = 0;      // the step the seat is the player's again from (SeatBack); 0: none announced
  Clock::time_point last_official{}, last_rejoin{}, last_ack{}, last_over{};
  u32 overs_sent = 0;     // Over packets sent (a few, the race's end said once on official steps)
  Clock::time_point blackout_until{};  // tests (Debug::blackout_step): nothing goes to or comes from the relay until then

  // replay (protocol/formats.md section 1): the launcher starts the file (Core.FzgxReplay) with its
  // header and frame preamble, and the session appends every final step: the official row and the state checksum. A
  // playback session (Core.FzgxPlay) has no relay: its official frames are the file's, run in lockstep, and each
  // step's checksum is compared with the recorded one (a difference counts as a desync)
  Recorder recorder;  // replay.h: reads the race through the rollback interface, nothing of this session
  std::string replay_aside;  // a game started again: the file it records into until the relay gives it the race
                             // (then moved over the crashed game's replay; ReplayToWrite, rejoin.cpp)
  bool playback = false;
  bool practice = false;  // a solo practice with rewind, no relay: its frames are practice_session.cpp's
  std::vector<u32> replay_checksums;
  u32 verified = 0;
  bool replay_ended = false;
  bool as_race = false;  // a replay's playback (every playback plays as the race itself)
  int spectated = -1;        // as_race: the racer watched (the stand-in seat), as last seen
  int chosen = -1;           // ... the racer the viewer chose (the recorder's at first; the D-pad, [ and ]),
  bool chosen_was_in = false;  // which was still racing when chosen,
  u32 auto_seat = 0xFFFFFFFF;  // and the racer watched in its place while it is out (none: the chosen one)
  std::array<u32, 30> seen_out = [] {  // without the scrubber: the step each racer was first seen out (NONE: in)
    std::array<u32, 30> none;
    none.fill(0xFFFFFFFF);
    return none;
  }();

  // A game whose racer is out or has finished (spectate.cpp)
  Watch watch = Watch::Racing;
  u32 finished_frames = 0;    // displayed race frames in a row that found the local machine finished
  u32 next_input = 0;         // watching: the next frame the local seat's input is sent for (0: none yet)
  double buffer_avg = 0;      // watching: official frames in hand past the step, smoothed over frames
  bool buffer_seen = false;
  bool buffer_full = false;   // the slack was reached once
  u32 slack = SPECTATE_SLACK;
  float watch_speed = 1.0f;   // the emulation speed watching has set (1: the host's own)
  float drift_speed = SPECTATE_SLOWEST;  // the speed it falls behind at (lowered where the relay's clock is slow)
  u32 drift_frames = 0;       // frames of the drift's window so far, the sum of their frames in hand, and the mean of
  double drift_sum = 0;       // the window before (none yet: NaN)
  double drift_before = std::numeric_limits<double>::quiet_NaN();
  u32 drift_short = 0;        // windows in a row that gained too little
  u32 drift_have = 0, drift_still = 0;  // the official frames in hand last frame, frames since that count moved
  bool drift_gap = false;     // the window had half a second with no official frame coming: it is not counted
  bool behind_next = false;   // a drifting frame ran only official steps: the next frame confirms the seat is out
  u32 watch_frames = 0, watch_waits = 0;  // lockstep frames watched behind, and those that had to wait
  float watch_slowest = 1.0f, watch_fastest = 1.0f;

  bool exit_sent = false, leavable_said = false;  // leavable_said: SayResultsLeavable logged its step
  Clock::time_point leavable_at{};  // when every racer's results page was first seen (LeaveTheResultsInTime)
  bool leave_pressed = false;       // the A of the results' own leave was pressed for the player
  bool first_page_seen = false;     // the race's own results page was seen standing (PassTheFirstResultsPageInTime)
  Clock::time_point first_page_at{};  // when it was first seen, then when the A of its pass was last pressed
  u32 first_page_presses = 0;       // the As pressed for the player on that page
  u32 stands_done = 0;  // tests ("debug.standStill"): how many of its stands were made (freeze.cpp)
  bool speeding = false;     // lockstep catching up at CATCH_UP_SPEED
  float normal_speed = 1.0f;  // the emulation speed and CPU clock override to go back to
  float normal_clock = 1.0f;
  bool settled = false;  // the race's predicted steps were checked as the race ended (NormalStep)
  bool race_back = false;  // said once: the race phase came back after that check (NormalStep)
  bool autopilot = false;  // tests: the local seat's input is "the AI drives"
  std::map<u32, Row> used;   // the inputs each step since the last checked one used
  u32 checked = 0;           // steps below this used official inputs (or are being re-simulated with them)
  u32 final_steps = 0;       // steps below this are final (simulated with official inputs)
  u32 checksums_sent = 0;
  u32 rollbacks = 0;
  u32 max_depth = 0;
  u32 resim_steps = 0;
  u32 normal_steps = 0;
  RaceSummary summary;  // what the race's one line in the player's file is made of (summary.h)

  // time sync: the relay's race clock is the time since go here plus relay_offset_ms, from the relay's clock in its
  // pongs (the median of the latest few: the relay's clock and this one can run at different rates, by 3.7% between
  // WSL and Windows); until the first such pong, go arrived half a round trip after the relay sent it
  Clock::time_point go_local{};
  std::deque<double> rtt_samples;
  double rtt_ms = 0;
  std::deque<double> relay_offsets;
  double relay_offset_ms = 0;
  Clock::time_point last_ping{};
  u32 frontier = 0;  // steps below this have been simulated at least once
  std::array<u8, RECORD> pad{};
  u32 catch_ups = 0;
  u32 holds = 0;  // displayed frames that ran no new step, to fall back to the schedule
  // the host's pacing as the race's frames show it: when the last race frame began, and the time lost in long
  // frames that the host has not made up in short ones yet (NormalStep)
  Clock::time_point pace_last{};
  double pace_owed_ms = 0;
  u32 pace_long = 0;  // long frames since the host last held the pace (over PACE_BURST_FRAMES: it is too slow for it)
  u32 pace_calm = 0;  // frames in a row, up to now, that were not long
  double pace_burst_ms = 0;  // what the long frames since the host last held the pace took over a frame's length,
                             // less what the short frames between them made up
  double pace_worst_ms = 0;  // the most of that lately (it fades by a step a second)

  // timing (microseconds, sums over the frames since the last report)
  Clock::time_point last_frame{};
  double frame_us = 0, restore_us = 0, track_us = 0;
  // (late_pages: pages found changed by a periodic full scan, outside the watched ones)
  u32 blocks_logged = 0, watched_pages = 0, late_pages = 0;
  u32 timed_frames = 0;
  u32 checksum_differences = 0;  // with the verify switch: steps where the mod's checksum and this one differed
  u32 last_extra = 0;  // extra steps asked of the previous frame
  double total_track_us = 0, total_restore_us = 0;  // the same over the whole race, for the log
  u32 total_frames = 0;
  double extra_ticks = 0, extra_steps = 0;  // the mod's time base ticks in frames' extra steps since the last report,
                                            // and how many such steps: their average goes to the mod (NormalStep)

  // tests: forced rollbacks (Debug::force_every, force_depths), the per-frame rollback log, and
  // the state altered on purpose
  u32 force_next = 0;   // the next entry of the depths
  u32 forced = 0;       // rollbacks the switch made
  FrameWork work;       // the displayed frame being run
  bool work_open = false;
  double work_track_us = 0, work_restore_us = 0;  // the undo log's work in the frame that just ended
  u32 work_blocks = 0;
  Clock::time_point hook_left{};  // when the hook last returned to the game
  u32 hooks_in_frame = 0;
  std::unique_ptr<File> work_log;
  std::string work_lines;
  bool altered = false;
  // the audit of a playback's forced rollbacks: the machines' blocks and progress records after each step's first
  // run, against the same step run again after a restore (every one of them, not only the last)
  std::map<u32, std::vector<u8>> audit_first;
  u32 audits = 0;
  std::map<u32, u32> audit_block, audit_progress;  // word offset: how many machines x rollbacks it differed in
  bool presentation_reported = false;
  // tests (Debug::late_steps): the rows of a playback known so far; the trace of every checksum kept; the dumps
  u32 known_rows = 0;
  bool all_known = false;  // the lateness is over (the race settled, or the game's own replay ending is near)
  std::unique_ptr<File> trace, trace_states;
  std::string trace_lines;
  u32 dumps_before = 0, dumps_after = 0;
  ScrubState scrub;  // the replay scrubber (scrub.cpp)
  SpectatorState spectator;  // the advanced spectator controls (spectator_frame.cpp)
  TimelineState timeline;    // the racer timeline (timeline.cpp)
  // Hits a rollback reveals, shown (hits.cpp): the decision, each rival's frames of fast smoothing left, the sound
  // queue's write place last seen, the counts for the summary, and the tests' log (Debug::hit_log)
  HitShow hits;
  std::array<u8, 32> lunge_frames{};
  int queue_at = -1;
  u32 hits_shown = 0, hit_sounds = 0, hit_rumbles = 0, lunges_shown = 0;
  double hits_us = 0;
  u32 hits_calls = 0;
  std::unique_ptr<File> hit_log;
  bool slowed = false;  // Debug::slow_from: the speed is lowered, and the speed to go back to
  float slowed_from = 0;
};

// Tests: the forced rollbacks asked for. Every `every` displayed race frames one rollback, its depth the next of
// `depths` in turn. From the launcher's options (the port), or the mod's word (the fork's practice tools wrote it:
// every << 16 | depth).
struct Forcing
{
  u32 every = 0;
  std::vector<u32> depths;
  u32 deepest = 0;
};

// The packets of a held queue whose time has come, in the order of their times (with jitter a later packet can
// overtake an earlier one, as on a real network), each handed to `pass`.
template <typename Pass>
void Release(std::vector<std::pair<Clock::time_point, std::vector<u8>>>& held, Clock::time_point now, Pass&& pass)
{
  if (held.empty())
    return;
  std::stable_sort(held.begin(), held.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
  std::size_t due = 0;
  while (due < held.size() && held[due].first <= now)
    ++due;
  for (std::size_t i = 0; i < due; ++i)
    pass(held[i].second);
  held.erase(held.begin(), held.begin() + static_cast<std::ptrdiff_t>(due));
}

// session.cpp: the one session, and that it could not be started (said once).
extern std::unique_ptr<Session> s_session;
extern bool s_config_failed;
bool WaitFor(Session& s, u32 count);
void Fail(Guest& g, Session& s, u32 step);

// relay_wire.cpp
// The seat's key from its token (protocol/relay.toml; the client audit's M1): the first 8 bytes, big-endian, of
// SHA-1("fzgx relay key" + the token as 8 big-endian bytes), as Online.Protocol RelayKeys.Of.
u64 RelayKey(u64 token);
void Send(Session& s, const std::vector<u8>& packet);  // a packet as relay::Encode writes it
void Drain(Session& s);
void SendInputs(Session& s);
void Ping(Session& s);

// step.cpp
u32 Have(const Session& s);
Row RowFor(const Session& s, u32 t);
void WriteRow(Guest& g, const Session& s, u32 t, const Row& row);
void ChecksumPrevious(Guest& g, Session& s, bool extra);
void ExtraStep(Guest& g, Session& s);
void CloseFrameWork(Session& s, bool flush);

// frame.cpp
void NormalStep(Guest& g, Session& s);

// frame_report.cpp: a race frame's accounts (NormalStep's)
void TakeLastFramesWork(Guest& g, Session& s, u32 step, Clock::time_point frame_now);
void ReportFrameTimes(Guest& g, Session& s);
void ReportCounters(Guest& g, Session& s, u32 step);
void TakePictureTimes(Session& s);

// schedule.cpp
// The steps a race's frame runs, as NormalStep's steps decide them one after another.
struct FrameSteps
{
  u32 known = 0;      // steps below this become final in this frame
  u32 run_from = 0;   // the first step the frame runs: its own, or the one a rollback went back to
  u32 catch_up = 0;   // new steps after its own, to catch up with the schedule
  bool held = false;  // no new step: the frame falls back one step to the schedule
};
double StepsAhead(const Session& s, u32 step);
double ScheduledStep(const Session& s);
FrameSteps CatchUpOrHold(Guest& g, Session& s, u32 step, bool settle, bool check, Clock::time_point frame_now,
                         double ahead, FrameSteps steps);

// spectate.cpp
void FollowTheLocalRacer(Guest& g, Session& s, u32 module);
bool SendWatchersInput(Session& s, u32 step);
bool SettleTheWatching(Session& s, bool race_phase, bool end_over);
void BeforeTheWatchersWait(Session& s, u32 step);
void AfterTheWatchersStep(Session& s, u32 step);
void AfterADriftingFrame(Session& s, u32 step, const FrameSteps& steps);
bool TallyTheWatchersFrame(Session& s, bool race_phase, Clock::time_point now, u32 step);
void WatchSummary(const Session& s);

// instruments.cpp (tests)
bool Blackout(const Session& s);
void StartBlackout(Session& s, u32 step);
Clock::duration Held(Session& s, u32 fixed_ms);
void FlushOutgoing(Session& s);
void Trace(Guest& g, Session& s, u32 step, u32 value, bool extra);
void Dump(Guest& g, const std::string& name);
void KeepImageLogs(Guest& g);
void AlterState(Guest& g, Session& s);
Forcing ForcedRollbacks(const Guest& g);
void Audit(Guest& g, Session& s, bool extra);
// hits.cpp: before every step of a race, what the step before left; the hits and lunges a rollback revealed, shown
void ShowHits(Guest& g, Session& s, bool extra);
void HitsSummary(const Session& s);
void OpenHitLog(Session& s);
void AuditSummary(const Session& s);
void OpenWorkLog(Session& s);
void OpenTrace(Session& s);
}  // namespace fzgx::net

// The session's kinds besides a live race: playback, rejoin, practice.
#include "net_modes.h"
