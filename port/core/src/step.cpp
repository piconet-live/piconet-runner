// A step of the race: the inputs it runs with (the official ones, or a prediction), the checksum of the one before,
// what is done before a step run again (ExtraStep), and a frame's line of the rollback log. What a displayed frame
// does before its own step is frame.cpp's (NormalStep).
// SPDX-License-Identifier: GPL-3.0-or-later

#include "net_internal.h"

#include "host_timers.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <utility>

namespace fzgx::net
{
// The inputs of step t: the official ones, or a prediction (the local seat's own, the others' last official ones).
// The official rows the session has: all that came from the relay; in a playback every row of the file, or, with the
// test switch Options::late_steps, those it was let know so far (NormalStep).
u32 Have(const Session& s)
{
  if (s.playback && TheOptions().debug.late_steps != 0 && !s.all_known)
    return std::min<u32>(s.known_rows, static_cast<u32>(s.official.size()));
  return static_cast<u32>(s.official.size());
}

Row RowFor(const Session& s, u32 t)
{
  const u32 have = Have(s);
  if (t < have)
    return s.official[t];
  // a playback with late rows for some seats only (Debug::late_seats): every other seat's row is known
  if (const std::vector<u32>& late = TheOptions().debug.late_seats; s.playback && !late.empty() && t < s.official.size())
  {
    Row row = s.official[t];
    for (std::size_t j = 0; j < s.seats.size(); ++j)
    {
      if (std::find(late.begin(), late.end(), static_cast<u32>(s.seats[j])) == late.end())
        continue;
      if (have == 0)
        std::fill_n(row.begin() + j * RECORD, RECORD, u8{0});
      else
        std::copy_n(s.official[have - 1].begin() + j * RECORD, RECORD, row.begin() + j * RECORD);
      if (const s32 a = static_cast<s32>(TheOptions().debug.late_noise); a != 0)
      {
        // a stick that moved since the last known row: the steering byte moved by a step-dependent amount
        const s32 move = static_cast<s32>(((t + 1) * 2654435761u >> 24) % static_cast<u32>(2 * a + 1)) - a;
        const s32 steer = static_cast<std::int8_t>(row[j * RECORD + 1]) + move;
        row[j * RECORD + 1] = static_cast<u8>(static_cast<std::int8_t>(std::clamp(steer, -100, 100)));
      }
    }
    return row;
  }
  Row row = have == 0 ? Row(s.seats.size() * RECORD, 0) : s.official[have - 1];
  std::array<u8, RECORD> mine{};
  if (const auto it = s.my_inputs.find(t); it != s.my_inputs.end())
    mine = it->second;
  // a playback with late rows: the recorder knew its own inputs when it ran the step, as a live game does
  if (s.playback && s.my_index >= 0 && t < s.official.size())
    std::copy_n(s.official[t].begin() + s.my_index * RECORD, RECORD, mine.begin());
  if (s.my_index >= 0)
    std::copy(mine.begin(), mine.end(), row.begin() + s.my_index * RECORD);
  return row;
}

void WriteRow(Guest& g, const Session& s, u32 t, const Row& row)
{
  Rollback::UseInputs(g, t, s.seats, row.data());
}

// Before step t: the checksum of step t - 1 into the ring (with the verify switch on, compared first with the one
// the mod's frame routine stored there after that step).
void ChecksumPrevious(Guest& g, Session& s, bool extra)
{
  const u32 step = g.Read32(STEP);
  if (step == 0)
    return;
  u32 value;
  {
    FZGX_TIME(Checksum);
    value = Rollback::Checksum(g);
  }
  Trace(g, s, step - 1, value, extra);
  if (g.Read32(VERIFY_CHECKSUMS) != 0 && Rollback::KeptChecksum(g, step - 1) != value)
  {
    ++s.checksum_differences;
    g.Write32(CHECKSUM_DIFFERENCES, s.checksum_differences);
  }
  Rollback::KeepChecksum(g, step - 1, value);
}

// Before a re-simulated step: its inputs, and the previous step's undo entry.
void ExtraStep(Guest& g, Session& s)
{
  if (s.scrub.forked)  // (the replay scrubber: the steps between a frame's hooks are its own)
  {
    FZGX_TIME(ScrubExtra);
    return ScrubExtra(g, s);
  }
  FZGX_TIME(ExtraStep);
  const u32 t = g.Read32(STEP);
  if (t >= s.frontier)
  {
    // a new step (catching up): the pad of this displayed frame is the local seat's input
    NoteOwnInput(s, t + s.delay, s.my_inputs.try_emplace(t + s.delay, PadFor(s, t + s.delay)).second);
    s.frontier = t + 1;
  }
  s.rollback.SaveStep(g, t, false);
  Row row = RowFor(s, t);
  WriteRow(g, s, t, row);
  s.used[t] = std::move(row);
  ++s.resim_steps;
}

// The rollback log (tests): the displayed frame that just ended becomes one line; written out now and then and when
// the session closes, never from inside a frame's steps.
void CloseFrameWork(Session& s, bool flush)
{
  if (s.work_open && s.work_log)
  {
    const FrameWork& w = s.work;
    s.work_lines += std::format("{},{},{},{},{},{},{},{:.1f},{:.1f},{},{:.1f},{:.1f},{:.1f},{},{:.2f},{:.2f},{}",
                                w.at_us, w.step, w.depth, w.forced ? 1 : 0, w.catch_up, w.extra, w.ahead,
                                s.work_track_us, s.work_restore_us, s.work_blocks, w.hook_us, w.first_us, w.resim_us,
                                w.resim_n, w.sched, w.owed, w.held ? 1 : 0);
    // how the game watched (spectate.cpp), and the watched racer's places
    s.work_lines += std::format(",{},{:.3f},{:.2f},{}", static_cast<int>(w.watch), w.speed, w.buffer, w.watched);
    for (const float v : w.places)
      s.work_lines += std::format(",{:.4f}", v);
    s.work_lines += '\n';
  }
  s.work_open = false;
  if (s.work_log && !s.work_lines.empty() && (flush || s.work_lines.size() > 60000))
  {
    s.work_log->WriteBytes(s.work_lines.data(), s.work_lines.size());
    s.work_log->Flush();
    s.work_lines.clear();
  }
}
}  // namespace fzgx::net
