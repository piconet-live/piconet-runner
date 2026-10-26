// A displayed race frame's accounts (frame.cpp NormalStep): the frame before as a line of the rollback log and a
// part of the timing sums, the averages and counters to the mod, the pictures the host has shown. They read and
// report; nothing here changes what a step computes.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "net_internal.h"

#include <algorithm>
#include <array>
#include <chrono>

namespace fzgx::net
{
// The frame before becomes a line of the rollback log and a part of the timing sums; the new frame's line begins.
void TakeLastFramesWork(Guest& g, Session& s, u32 step, Clock::time_point frame_now)
{
  // the undo log's work of the frame before (its own step's and its re-simulated steps')
  {
    const Work work = s.rollback.TakeWork();
    s.track_us += work.track_us;
    s.restore_us += work.restore_us;
    s.blocks_logged += work.blocks;
    s.watched_pages = work.watched_pages;
    s.late_pages = work.late_pages;
    s.work_track_us = work.track_us;
    s.work_restore_us = work.restore_us;
    s.work_blocks = work.blocks;
  }
  CloseFrameWork(s, false);
  s.work = FrameWork{};
  s.work.at_us = std::chrono::duration_cast<std::chrono::microseconds>(frame_now.time_since_epoch()).count();
  s.work.step = step;
  s.work.ahead = static_cast<int>(step) - static_cast<int>(Have(s));
  if (s.last_frame != Clock::time_point{})
  {
    s.frame_us += std::chrono::duration<double, std::micro>(frame_now - s.last_frame).count();
    ++s.timed_frames;
  }
  s.last_frame = frame_now;
  if (s.last_extra != 0)
  {
    s.extra_ticks += g.Read32(RESIM_TICKS);
    s.extra_steps += s.last_extra;
  }
  s.last_extra = 0;
}

// Every 60 timed frames: their averages to the mod, their sums to the race's.
void ReportFrameTimes(Guest& g, Session& s)
{
  if (s.timed_frames >= 60)
  {
    if (s.extra_steps > 0)
      g.Write32(TICKS_PER_EXTRA_STEP, static_cast<u32>(s.extra_ticks / s.extra_steps));
    s.extra_ticks = s.extra_steps = 0;
    // per displayed frame, in microseconds: the whole frame, undo tracking, restoring; blocks logged per frame
    g.Write32(TIME_FRAME, static_cast<u32>(s.frame_us / s.timed_frames));
    g.Write32(TIME_TRACK, static_cast<u32>(s.track_us / s.timed_frames));
    g.Write32(TIME_RESTORE, static_cast<u32>(s.restore_us / s.timed_frames));
    g.Write32(BLOCKS_PER_FRAME, s.blocks_logged / s.timed_frames);
    s.total_track_us += s.track_us;
    s.total_restore_us += s.restore_us;
    s.total_frames += s.timed_frames;
    s.frame_us = s.track_us = s.restore_us = 0;
    s.timed_frames = 0;
    s.blocks_logged = 0;
  }
}

// The race's counters to the mod, and every ten seconds to the log.
void ReportCounters(Guest& g, Session& s, u32 step)
{
  g.Write32(ROLLBACKS, s.rollbacks);
  g.Write32(MAX_DEPTH, s.max_depth);
  g.Write32(RESIM_STEPS, s.resim_steps);
  g.Write32(RTT_TENTHS, static_cast<u32>(s.rtt_ms * 10));
  g.Write32(CATCH_UPS, s.catch_ups);
  g.Write32(WATCHED_PAGES, s.watched_pages);
  g.Write32(LATE_PAGES, s.late_pages);
  g.Write32(DISPLAYED_FRAMES, s.normal_steps);
  if (s.normal_steps % 600 == 0)
  {
    Notice("fzgx: step {}, official {}, rollbacks {}, deepest {}, re-simulated {}, round trip {:.1f} ms, "
                   "caught up {}, held {}, watched pages {}, late pages {}; per displayed frame of {}: undo log "
                   "{:.0f} us, restoring {:.0f} us",
                   step, s.official.size(), s.rollbacks, s.max_depth, s.resim_steps, s.rtt_ms, s.catch_ups,
                   s.holds, s.watched_pages, s.late_pages, s.total_frames,
                   s.total_frames ? s.total_track_us / s.total_frames : 0.0,
                   s.total_frames ? s.total_restore_us / s.total_frames : 0.0);
    Notice("fzgx: step {}: sound commands {}, dropped as a re-simulated step's {}; rivals drawn jumping {} (without "
           "smoothing {}) in {} frames",
           step, g.Read32(CONTROL + 36), g.Read32(CONTROL + 32), g.Read32(layout::SMOOTH + 0x600 + 12),
           g.Read32(layout::SMOOTH + 0x600 + 20), g.Read32(layout::SMOOTH + 0x600 + 8));
  }
}

// The pictures the host has shown since the last displayed frame, into the summary's tally: the time from each to
// the next. No clock is read here (the host stamped them), and what the time sync reads is not touched.
void TakePictureTimes(Session& s)
{
  std::array<u32, 16> us{};
  const u32 n = std::min<u32>(TheHost().PictureIntervals(us.data(), static_cast<u32>(us.size())), static_cast<u32>(us.size()));
  for (u32 i = 0; i < n; ++i)
    s.summary.Shown(us[i] / 1000.0);
}
}  // namespace fzgx::net
