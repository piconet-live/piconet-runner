// Scoped timers of the core's own work on the game's thread, for a profiling build only: a runner built with
// FZGX_HOST_TIMERS (port/moderngekko/build.sh with HOST_TIMERS=1) sums each named part's time and calls, and writes a
// line for each when the session closes ("fzgx: host timer ..."). Every other build compiles FZGX_TIME to nothing:
// no player's runner reads a clock for them.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#ifdef FZGX_HOST_TIMERS

#include <chrono>

#include "fzgx/host.h"

namespace fzgx::timers
{
// The parts timed. A part may lie inside another (the undo log's inside a step's hook): each is summed alone.
enum Id : int
{
  Hook,            // the hook before a step, whole (session.cpp RunSession)
  Drain,           // the relay's socket read (relay_wire.cpp)
  Checksum,        // the state checksum of the step before
  FrameBegin,      // a displayed frame's first accounts: the pictures' times, the replay scrubber's check of the step
                   // shown, the last frame's work and times
  SendInput,       // the local input to the relay and the live feed
  CloseFinal,      // the final steps: count, checksums to the relay, the replay's rows
  RollbackBegin,   // BeginRollbackFrame: the undo log on, the frame's own save, the waits
  Predictions,     // CheckPredictions: the comparison of rows, and the restore
  RunSteps,        // RunSteps: the steps asked, the final ones dropped, the first row
  FrameEnd,        // after RunSteps: the drift, the counters to the image
  ExtraStep,       // a step run again's own hook part (ExtraStep), its undo entry included
  ScrubFrame,      // the replay scrubber's displayed frame in the race (ScrubFrame): the play log's entry, the move,
                   // the first front or play step's start, the budget kept
  ScrubExtra,      // the replay scrubber's own hook part of a step between a frame's hooks (ScrubExtra), its undo
                   // entries, packing and keys included
  UndoFull,        // the undo log: a comparison of every page (before a frame's own step and the one after)
  UndoPool,        // of it: the parts on the scan pool, this thread's part and the wait for the others
  UndoPoolWait,    // of that: the wait for the pool's other threads after this thread's part
  UndoGather,      // of it: the parts' blocks into the entry, the late pages watched
  UndoWatched,     // the undo log: a comparison of the watched pages only (the steps between; with the guard, every step
                   // but the learning ones, the written pages with them)
  UndoCheck,       // the undo log, Both (a test): every page compared again, and the entry checked against it
  UndoScratch,     // the undo log: the locked cache, every step
  UndoStore,       // the undo log: the entry into the map
  Restore,         // the undo log: memory back to an earlier step
  Final,           // the undo log: final steps' entries dropped
  Mode,            // the game mode's run at the image's mode point (mode.cpp)
  Count
};

struct Sum
{
  double us = 0;
  u64 calls = 0;
};

Sum& At(Id id);

class Scope
{
public:
  explicit Scope(Id id) : m_id(id), m_start(std::chrono::steady_clock::now()) {}
  ~Scope()
  {
    Sum& sum = At(m_id);
    sum.us += std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - m_start).count();
    ++sum.calls;
  }
  Scope(const Scope&) = delete;
  Scope& operator=(const Scope&) = delete;

private:
  Id m_id;
  std::chrono::steady_clock::time_point m_start;
};

// The pages of main memory and of the locked cache one step changed (its undo entry), by how it was compared: 0 a
// learning step, 1 before a frame's own step (the step before and what ran between the frames), 2 before the step
// after it (a frame's first step, with the presentation tasks), 3 before a step run again (the steps between).
void NotePages(int kind, u32 pages, u32 scratch_pages);

// A line for each part that was timed, per call and per displayed frame of the `frames` given; then all start over.
void Report(u64 frames, u64 steps);
}  // namespace fzgx::timers

#define FZGX_TIME_JOIN2(a, b) a##b
#define FZGX_TIME_JOIN(a, b) FZGX_TIME_JOIN2(a, b)
#define FZGX_TIME(id) const ::fzgx::timers::Scope FZGX_TIME_JOIN(fzgx_time_, __LINE__)(::fzgx::timers::id)
#define FZGX_TIME_REPORT(frames, steps) ::fzgx::timers::Report(frames, steps)
#define FZGX_NOTE_PAGES(kind, pages, scratch_pages) ::fzgx::timers::NotePages(kind, pages, scratch_pages)

#else

#define FZGX_TIME(id) static_cast<void>(0)
#define FZGX_TIME_REPORT(frames, steps) static_cast<void>(0)
#define FZGX_NOTE_PAGES(kind, pages, scratch_pages) static_cast<void>(0)

#endif
