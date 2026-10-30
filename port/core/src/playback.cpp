// The playback of a replay file, as the race: the session made from it, its steps against the recorded checksums,
// its camera; and what a race or a playback says of itself when it ends (the summaries).
// SPDX-License-Identifier: GPL-3.0-or-later

#include "net_internal.h"
#include "out_test.h"
#include "replay_file.g.h"

#include <algorithm>
#include <memory>
#include <string>
#include <tuple>
#include <utility>

namespace fzgx::net
{
// A replay played as the race keeps the game's own camera: the automatic spectating (netframe.py +248, the TV cameras
// 180 frames after the watched racer is out) is held off; instead, 120 steps after the racer the viewer chose is
// out by the game's own verdict (out_test.h, as the image's spectator counts it: never under Restore, a breakdown
// only at rest, never finished) the camera moves to the leader still racing, as the online spectate starts on the
// leader, and from that racer on the same way. Counted in simulated steps, so a paused replay switches nothing; a
// seek back before the chosen racer went out watches it again. A racer chosen when already out stays watched. On
// every change of racer the game camera's retired state is cleared, so it follows the new one.
// Presentation only (the stand-in seat, the camera record). `step`: the step the frame's picture is of, about.
void SpectateReplay(Guest& g, Session& s, u32 step)
{
  if (!s.as_race)
    return;
  g.Write32(CONTROL + 248, 0);
  const u32 module = g.Read32(MODULE_POINTER);
  const u32 machines = g.Read32(module + MACHINES);
  const u32 count = std::min<u32>(g.Read8(module + MACHINE_COUNT), 30);
  const u32 seat = g.Read32(SEAT_SLOT + 4);
  if (machines == 0 || seat >= count)
    return;
  const auto out = [&](u32 i) { return out_test::Out(g, module, machines + i * game::MACHINE_SIZE); };
  // the step a racer went out: the scrubber's front edge saw it exactly; without the scrubber, the step it was first
  // seen out (forgotten when it is seen in again)
  const auto out_since = [&](u32 i) {
    if (s.scrub.forked || s.scrub.over)
      return s.scrub.ends.out[i] <= step ? s.scrub.ends.out[i] : 0xFFFFFFFF;
    if (!out(i))
      return s.seen_out[i] = 0xFFFFFFFF;
    if (s.seen_out[i] == 0xFFFFFFFF || s.seen_out[i] > step)
      s.seen_out[i] = step;
    return s.seen_out[i];
  };
  const auto gone = [&](u32 i) {
    const u32 since = out_since(i);
    return since != 0xFFFFFFFF && step >= since + 120;
  };
  if (static_cast<int>(seat) != s.spectated)
  {
    // a change of racer that is not this routine's: the viewer's choice (the recorder's at the start; back to the
    // chosen racer is this routine's too)
    if (seat != s.auto_seat && static_cast<int>(seat) != s.chosen)
    {
      s.chosen = static_cast<int>(seat);
      s.chosen_was_in = !out(seat);
      s.auto_seat = 0xFFFFFFFF;
    }
    s.spectated = static_cast<int>(seat);
    // the game camera leaves the state it took for the racer before: 5, retired (position frozen, aimed at the
    // wreck), set by fn_1_AA54 when the followed machine is out and never cleared, and the explosion's shake
    //
    const u32 camera = g.Read32(module + BSS + 0xF6C);
    if (camera != 0 && !out(seat))
    {
      g.Write32(camera + 0xA8, 0);
      g.Write32(camera + 0x10C, 0);
    }
    return;
  }
  if (s.chosen < 0 || static_cast<u32>(s.chosen) >= count)
    return;
  // a course whose scenery follows the camera's machine (spectator.h RuleFor: 41) keeps the recorder's racer
  if (s.spectator.controls && !s.spectator.controls->rule().follow_others)
    return;
  u32 want = static_cast<u32>(s.chosen);
  if (s.chosen_was_in && gone(want))
  {
    if (s.auto_seat < count && !gone(s.auto_seat))
      want = s.auto_seat;
    else
    {
      u32 best = 0x100;
      for (u32 i = 0; i < count; ++i)
      {
        const u32 progress = g.Read32(machines + i * game::MACHINE_SIZE + game::MACHINE_PROGRESS);
        if (progress == 0 || out(i))
          continue;
        const u32 rank = g.Read8(progress + game::PROGRESS_RANK);
        if (rank < best)
          std::tie(best, want) = std::pair{rank, i};
      }
    }
  }
  if (want == seat)
    return;
  s.auto_seat = want == static_cast<u32>(s.chosen) ? 0xFFFFFFFF : want;
  g.Write32(SEAT_SLOT + 4, want);
}

// Playback: the steps that became final against the recorded checksums.
void VerifyReplay(Guest& g, Session& s)
{
  for (; s.verified < s.final_steps && s.verified < s.replay_checksums.size(); ++s.verified)
  {
    const u32 value = Rollback::KeptChecksum(g, s.verified);
    if (RecordedChecksumMatches(s.replay_checksums, s.replay_compact, s.verified, value))
      continue;
    if (s.desyncs == 0)
    {
      Warn("fzgx: the replay differs from the recording first at step {} ({:08x}, recorded {:08x})",
                   s.verified, value, s.replay_checksums[s.verified]);
    }
    ++s.desyncs;
  }
}

// Playback: the results the race came to, read where a recording reads them (frame.cpp), against the file's. A
// difference is said like a step that differs, and counted apart (the summary); a file without results is said too.
void CheckResults(Guest& g, Session& s)
{
  if (s.results_checked)
    return;
  s.results_checked = true;
  if (s.replay_results.empty())
  {
    Notice("fzgx: playback: results: none in the recording to compare");
    return;
  }
  const std::string raced = ResultsOfTheRace(g);
  if (SameResults(s.replay_results, raced))
  {
    Notice("fzgx: playback: results: the recording's, before step {}", Rollback::Step(g));
    return;
  }
  s.results_differ = true;
  if (!s.results_judged)
  {
    Notice("fzgx: playback: results: other than the recording's before step {}, which is from before 0.4.0 and is not "
           "judged by them: recorded {}, raced {}", Rollback::Step(g), s.replay_results.substr(0, 2000),
           raced.substr(0, 2000));
    return;
  }
  Warn("fzgx: playback: results: DIFFER from the recording's before step {}: recorded {}, raced {}", Rollback::Step(g),
       s.replay_results.substr(0, 2000), raced.substr(0, 2000));
}

// The results screen's pictures (the image's results_preload and results_load): whether
// the three packed files were read into memory before the start gate, and how many of the results phase's loads
// were served from them instead of the disc. Said when the race phase ends, when the player leaves the results and
// at a playback's end.
void ReportResultsPictures(Guest& g, const char* when)
{
  const u32 state = g.Read32(layout::PRELOAD);
  u32 files = 0;
  for (u32 i = 0; i < 3; ++i)
    files += g.Read32(layout::PRELOAD + 8 + i * 8) != 0 ? 1 : 0;
  Notice("fzgx: results pictures ({}): {} of 3 files in memory ({}; the main heap's largest free block was {} bytes), "
         "{} loads served from memory",
         when, files, state == 2 ? "read before the start gate" : state == 3 ? "not read: no room or no file" :
         state == 1 ? "the read did not end" : "never tried", g.Read32(layout::PRELOAD + 32),
         g.Read32(layout::PRELOAD + 28));
}

// The mines that went off and came back (the image's sites_mines.py: its block's counters, put back by the undo log
// with the race's memory, so the official race's), when the race's rule brought them back.
namespace
{
void MinesSummary(Guest& g)
{
  if (const u32 mines = g.Read32(layout::MINES + 4); mines != 0)
    Notice("fzgx: mines: {} went off, {} came back (the rule: {} steps)", g.Read32(mines + 4), g.Read32(mines + 8),
           g.Read32(layout::RACE_MINE_REGEN_AT));
}
}  // namespace

// What the player sees and hears through rollbacks, from the mod's own counters, once, when the race phase ends:
// the sound commands of the whole run and how many the mute dropped (mute.py: control block +36 and +32; a command
// from a step simulated before is dropped), and the rivals' smoothing (smooth.py: its frames, and the rivals drawn
// more than 3 units from where they were expected, with the smoothing and as the race's own positions would have
// been drawn).
void ReportPresentation(Guest& g, Session& s)
{
  if (s.presentation_reported)
    return;
  s.presentation_reported = true;
  constexpr u32 SMOOTH_GLOBALS = layout::SMOOTH + 0x600;
  Notice("fzgx: through the race: {} rollbacks (deepest {}), {} steps re-simulated; sound commands {}, of them "
         "dropped as a re-simulated step's {}; smoothing: {} frames, rivals drawn jumping {} (without smoothing {})",
         s.rollbacks, s.max_depth, s.resim_steps, g.Read32(CONTROL + 36), g.Read32(CONTROL + 32),
         g.Read32(SMOOTH_GLOBALS + 8), g.Read32(SMOOTH_GLOBALS + 12), g.Read32(SMOOTH_GLOBALS + 20));
  HitsSummary(s);
  MinesSummary(g);
  ReportResultsPictures(g, "the race phase ends");
}

// Playback: what the check of the recording came to, once, when the playback ends.
void PlaybackSummary(Guest& g, Session& s)
{
  AuditSummary(s);
  ScrubSummary(s);
  SpectatorSummary(s);
  TimelineSummary(s);
  Notice("fzgx: playback: {} of {} steps compared with the recording, {} differed; rollbacks {} ({} forced), "
         "deepest {}, {} steps re-simulated; sound commands {}, dropped as a re-simulated step's {}",
         s.verified, s.replay_checksums.size(), s.desyncs, s.rollbacks, s.forced, s.max_depth, s.resim_steps,
         g.Read32(CONTROL + 36), g.Read32(CONTROL + 32));
  Notice("fzgx: playback: results {}", s.results_differ && !s.results_judged ? "other, not judged (a recording from before 0.4.0)"
                                       : s.results_differ ? "DIFFER from the recording's"
                                       : !s.results_checked ? "not reached"
                                       : s.replay_results.empty() ? "not in the recording" : "the recording's");
  MinesSummary(g);
  ReportResultsPictures(g, "the playback ends");
}

// A playback session from a replay file (replay.h): its official frames are the file's.
bool StartPlayback(Guest& g, const std::string& path)
{
  Recording recording;
  if (!ReadRecording(path, &recording))
    return false;
  auto s = std::make_unique<Session>();
  s->my_seat = recording.seat;
  s->my_index = recording.index;
  s->seats = std::move(recording.seats);
  s->official = std::move(recording.rows);
  s->replay_checksums = std::move(recording.checksums);
  s->replay_compact = recording.compact;
  s->replay_results = std::move(recording.results);
  // Results taken as a recording takes them now are those of version 7 on (0.4.0); before, some were read from a
  // state that was not the race's end (a recording of 4 October holds every lap 0 and times of hours), so a
  // difference there is said and not counted
  s->results_judged = recording.version >= replay::VERSION_GRID;
  s->playback = true;
  s->phase = Phase::Joined;
  s->go = true;
  s->delay = g.Read32(SEAT_SLOT + 44);
  g.Write32(SESSION, 1);
  g.Write32(GATE_ON, 1);
  OpenWorkLog(*s);
  OpenHitLog(*s);
  OpenTrace(*s);
  if (TheOptions().debug.late_steps != 0)
    Notice("fzgx: TEST: the recording's inputs become known {} steps late", TheOptions().debug.late_steps);
  Notice("fzgx: playing back {}: {} steps, {} player seats, seen from seat {}", path,
                 s->official.size(), s->seats.size(), s->my_seat);
  s_session = std::move(s);
  return true;
}
}  // namespace fzgx::net
