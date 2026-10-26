// When a picture goes to the screen with the launch file's "present": "console" (the runner's adapter,
// port/moderngekko/src/pictures.cpp, has the emulator's side): whether a picture whose first line's time on the
// host's clock is `left` from now is held for that time or presented at once. Plain numbers with no emulator in them, so that
// the three ways and the limit are tested without one.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string_view>

namespace fzgx::present
{
// A picture is never held longer than this for its moment, in microseconds: the game's copy comes 1.9 ms before the
// first line's time on a frame that held its pace, so a time further off is no first line's.
constexpr long long MOST_WAIT_US = 4000;

enum class Moment
{
  Wait,   // the time is still to come, and near: the picture is held for it
  After,  // the copy came at or after the time (a long frame): shown at once
  Far,    // the time is further off than MOST_WAIT_US: not a first line's time, shown at once
};

// `left_us`: the picture's intended time less now.
// With pictures between two steps (fzgx/hfr.h) a step's own picture is held for its moment as at 60 Hz (never
// longer), and the pictures after it for their fraction of a field after that moment: they are held up to a field
// and the limit. A field of the console's, in microseconds.
constexpr long long FIELD_US = 16683;
constexpr long long MOST_WAIT_DRAWN_US = FIELD_US + MOST_WAIT_US;

constexpr Moment At(long long left_us, long long most_us = MOST_WAIT_US)
{
  return left_us <= 0 ? Moment::After : left_us > most_us ? Moment::Far : Moment::Wait;
}

// The way a start shows its pictures: the launch file's "present", but for a start whose picture is drawn on the
// game's own thread (the tests' "debug.singleCore"): there no GPU thread's loop presents a held picture, and a wait
// would hold the game's thread before its pad read, so "console" is "field" in such a start. Nothing else changes a start's way.
constexpr std::string_view WayOf(std::string_view asked, bool single_core)
{
  return single_core && asked == "console" ? std::string_view("field") : asked;
}

// What a way needs of the emulator: its own "Immediately Present XFB", which hands a picture to the screen from the
// game's copy and not from the emulated video output ("console" and "copy"; without it no picture comes by the
// copy, and "console" would hold nothing and show as "field"), and that the picture is held until its first line's
// time ("console" alone). A held picture is presented by the GPU thread's loop once the time has come, between its
// passes over the game's commands or from its idle wait, which new commands end at once: no thread waits for the
// time. 0.2.2's race m23: a wait on the GPU thread let the game's switch of its two command buffers race with the
// GPU's lag, which then read garbage ("FIFO: Unknown Opcode"), inside the pass and after it alike.
struct Setup
{
  bool immediate_xfb = false;
  bool holds = false;
  constexpr bool operator==(const Setup&) const = default;
};
constexpr Setup SetupOf(std::string_view way)
{
  return {way == "console" || way == "copy", way == "console"};
}
}  // namespace fzgx::present
