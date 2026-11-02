// Practice with rewind: a solo race in the game's own Practice mode, with a rolling buffer of
// the last minute. Start pauses; while paused the player scrubs back with the replay scrubber's bar or with the
// stick; Start again resumes after a 1 s countdown, and resuming from an earlier moment branches: the later buffer
// is dropped. One save, UnclePunch's way: D-pad right saves, D-pad left loads (with the countdown too). The save is a
// mark on a step inside the buffer: once the race has run a minute past it, it is gone.
//
// This is the decision alone, as plain numbers with no game in it, so that the buffer's reach, the save's expiry,
// the branch and the stick's curve are tested without one (tests/test_practice.cpp). The session that moves the
// game's memory by it (the scrubber's keys and logs) asks it, a displayed frame at a time, what that frame does.
//
// Steps are the session's (0 at the relay's go, or the practice's own go). "The moment before step t" is the memory
// after step t - 1 has run: what a key of step t holds, and what the picture at position t shows.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace fzgx::practice
{
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u8 = std::uint8_t;

constexpr u32 WINDOW = 3600;     // the rewind's reach in steps: one minute at 60 steps a second
constexpr u32 KEY_EVERY = 128;   // a whole copy of the race's memory every so many steps (the scrubber's)
constexpr u32 COUNTDOWN = 60;    // displayed frames between the resume and the race running on (1 s)

// The pad as the game reads it (PADStatus, main.rel .bss 0x998: the buttons, then the stick): the player's own
// controls as the launcher maps them onto the GameCube's.
constexpr u16 BUTTON_LEFT = 0x0001;   // D-pad left: load
constexpr u16 BUTTON_RIGHT = 0x0002;  // D-pad right: save
constexpr u16 BUTTON_START = 0x1000;  // pause, resume
struct Pad
{
  u16 buttons = 0;
  int stick_x = 0;  // as the game keeps it after PADClamp: its own dead zone of 15 taken off, about 56 at full tilt
};

// The stick's scrub speed: steps a displayed frame, signed (left goes back). Nothing inside the dead zone; past it a
// quarter of the race's speed, rising evenly to the race's own speed at half tilt (easy to hold at slow speeds),
// then faster and faster to eight times at full tilt. The stick is the game's clamped one (.bss 0x998, after
// PADClamp): the SDK's dead zone (15 of the raw value) is already gone, and the
// octagon's edge is about 56 to either side (57 seen in practice run pr-1, as 1.4x at "full tilt"
// with the raw value's range here), so a small dead zone of its own and the full speed at 54.
constexpr int STICK_DEAD = 3;
constexpr int STICK_FULL = 54;
constexpr double SLOWEST = 0.25, HALF_TILT = 1.0, FASTEST = 8.0;
double ScrubSpeed(int stick_x);

// What a practice's game mode gives it (the setup words rewind, pause, save_state, resume_countdown; mode.h). As they
// stand here: a practice with no game mode, today's words of practice.fzmode.
struct Words
{
  u32 reach = WINDOW;         // the rewind's reach in steps; 0: no way back
  bool pause = true;          // Start pauses
  bool save = true;           // D-pad right saves, D-pad left loads
  u32 countdown = COUNTDOWN;  // displayed frames between a resume or a load and the race running on
};

enum class Mode
{
  Racing,     // the race runs at the front
  Paused,     // the picture stands at the position; the stick and the bar move it
  Countdown,  // the second before the race runs on from the position
};

// What one displayed frame does.
struct Frame
{
  bool run = false;     // the race runs its next step, `step`; else the frame shows the moment before `step`
  u32 step = 0;
  bool branch = false;  // (with run) the buffer from `step` on was dropped: the race runs on from an earlier moment
  bool saved = false;   // the save was set in this frame
  bool loaded = false;  // the save was loaded in this frame (its countdown begins)
};

struct Counts
{
  u32 pauses = 0, branches = 0, saves = 0, loads = 0, saves_expired = 0, saves_dropped = 0;
};

class Rewind
{
public:
  // `first`: the race's first step, the earliest any rewind reaches and the step keys are counted from
  explicit Rewind(u32 first = 0, Words words = {}) : m_words(words), m_first(first), m_front(first), m_position(first) {}

  // A displayed frame, with the pad as it reads now: what the frame does.
  Frame Next(const Pad& pad);
  // The race ran the step `step` at the front (it must be the front: anything else is ignored and counted).
  void Ran(u32 step);
  // The replay scrubber's bar while paused: the picture moves to `step`, kept inside the buffer.
  void Seek(u32 step);

  Mode Now() const { return m_mode; }
  u32 First() const { return m_first; }
  u32 Front() const { return m_front; }  // every step below it has run
  u32 Oldest() const { return m_front - m_first > m_words.reach ? m_front - m_words.reach : m_first; }  // the earliest moment
  const Words& Said() const { return m_words; }
  u32 Position() const;                  // the moment the picture shows (the front while racing)
  std::optional<u32> Save() const { return m_save; }
  u32 CountdownLeft() const { return m_countdown; }
  const Counts& Count() const { return m_counts; }
  u32 Misses() const { return m_misses; }

  // Keys. A key is taken before a step that WantsKey; a move to the moment before `step` starts from KeyFor(step)
  // and runs the steps from there (their inputs: the session's log). Keys that went out of the buffer, or past a
  // branch, are handed out once by TakeDropped. Steps below FinalBelow can no longer change: the replay may write them.
  bool WantsKey(u32 step) const { return step >= m_first && (step - m_first) % KEY_EVERY == 0; }
  u32 KeyFor(u32 step) const { return step < m_first ? m_first : step - (step - m_first) % KEY_EVERY; }
  std::vector<u32> TakeDropped();
  u32 FinalBelow() const { return Oldest(); }

private:
  void Expire();
  void Branch(u32 at);
  void Load();
  bool Pressed(u16 button) const { return (m_buttons & button) != 0 && (m_before & button) == 0; }

  Words m_words;
  u32 m_first, m_front;
  double m_position;  // while paused or counting down: fractional, so slow scrubs move by quarters of a step
  Mode m_mode = Mode::Racing;
  u32 m_countdown = 0;
  std::optional<u32> m_save;
  u16 m_buttons = 0, m_before = 0;
  std::vector<u32> m_kept;     // keys taken (their steps, ascending), as far as this knows
  std::vector<u32> m_dropped;  // keys to let go
  Counts m_counts;
  u32 m_misses = 0;
};

// The run kept for the replay: one unbroken line of inputs from the race's first step, each step's row and the
// checksum after it. A step run again after a branch takes its place; a branch cuts the line where it went back to,
// so rewinds and loads leave nothing in it. Steps below the buffer's oldest moment can no longer change: they are
// handed to the recorder once, in order (TakeFinal); the rest when the session ends (TakeAll).
class Line
{
public:
  struct Step
  {
    std::vector<u8> row;
    u32 checksum = 0;
  };
  explicit Line(u32 first = 0) : m_first(first), m_written(first) {}
  // the step `step` ran with `row`: it extends the line, or replaces the step after a branch (false: a step past the
  // line's end, or one already handed to the recorder)
  bool Ran(u32 step, std::vector<u8> row, u32 checksum);
  void Cut(u32 at);  // the race goes on from the moment before `at`: steps from it on are gone
  u32 End() const { return m_first + static_cast<u32>(m_steps.size()); }
  u32 Written() const { return m_written; }
  const Step* At(u32 step) const;
  // the steps from Written() up to `below` (never past the line's end), each once
  std::vector<std::pair<u32, Step>> TakeFinal(u32 below);
  std::vector<std::pair<u32, Step>> TakeAll() { return TakeFinal(End()); }

private:
  u32 m_first, m_written;
  std::vector<Step> m_steps;  // from m_first; those below m_written are kept too (the session reads them back)
};
}  // namespace fzgx::practice
