// The high refresh rate: pictures between two of the game's (the launch file's "refreshRate"; protocol/formats.md).
//
// The simulation keeps the console's clock: 59.94 steps a second, each step's own picture shown as before, at its
// console moment as at 60 Hz (fzgx/present.h): no picture waits for the next step. After it the emulator's GPU
// thread draws the step's frame again (VideoCommon/FrameRedraw.h) with corrected matrices: machines and the chase
// camera where they will be a fraction of a field later, guessed forward from the step and the one before (as a
// headset guesses the head's pose: never waiting for the future); each picture is shown at its own time on the
// display's clock, and the next step's own picture replaces the guesses. The image's routine at fn_1_37AA8's draw half (port/image/sites_hfr.py) calls the host once a
// pass, after the pass's steps and before its drawing; this file answers it with the request for those pictures:
// the fractions, and for each the camera's correction and every machine's. Nothing of the game runs again and
// nothing in its memory is written: the race is the same as without, by construction.
//
// The rules of the pictures between:
// - the world's perspective draws follow the chase camera guessed forward (its eye on in a straight line, its turn
//   on by the shorter arc); machine i's draws (and what is drawn at its place: flame, shadow, effects) follow machine
//   i's pose guessed forward as well: its turn on, its place moved on by its move from the step before (not at
//   all when a wall or a machine hit it in the step: no guess into the wall; on the road less its part along the
//   machine's up vector, the road's normal: it stays on a banked or curved road); a turn of more than
//   MOST_TURN a step is not carried on (hfr_ahead.h); a machine or camera that jumped (a respawn, a camera cut, a
//   step that is not the next one) stops the pass's pictures between or keeps the machine where the step has it;
// - what is drawn over a machine moves with it: a flat draw over machine i that moved with it on the screen since the
//   frame before (its name tag, a quad a letter) is shifted on the screen as machine i's guessed place is, and its
//   rank icon (a billboard standing up from it) takes the camera's correction and machine i's move, not its turn
//   (the emulator's VideoCommon/FrameRedrawTags.cpp, Over and IconOver; at 60 Hz nothing is drawn between);
// - the HUD and every other flat draw, and the offscreen passes, stay as the step drew them (60 a second);
// - nothing is drawn between while the game is paused or its camera object runs itself, or with more than one
//   viewport.
// When the GPU thread's time is short its replays are left out (FrameRedraw's budget), never a step.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <chrono>
#include <functional>
#include <string>
#include <vector>

#include "fzgx/guest.h"

namespace fzgx
{
struct Options;

namespace hfr
{
// The image's host call (memmap.HOST_HFR): r3 the operation, r4 the frame. The image's routine has operations for
// frames between drawn by the game's own code (BEGIN to END); PLAN answers 0, so they are not called.
enum Op : u32
{
  PLAN = 0,      // r4 not 0: nothing may be drawn between this pass. The request is made. Answer: 0.
  BEGIN = 1,     // (not called)
  GPU_WAIT = 2,  // (not called)
  PRESENT = 3,   // (not called)
  END = 4,       // (not called)
  DONE = 5,      // the pass is done: its state is kept for the next pass's request
};

// The request of one pass: 4x4 matrices, row-major, column vectors, last row 0 0 0 1 (the emulator's order).
constexpr u32 SEATS = 30;
using Matrix = std::array<float, 16>;
struct InBetween
{
  float fraction = 0;  // of a field after the step's own picture
  Matrix camera{};     // the world's correction: the guessed view times the step's view's inverse
  std::array<Matrix, SEATS> machine{};  // machine i's: its guessed pose too
  // machine i's own move in the step's view (the camera's correction undone from machine i's): for what the game
  // puts in the view itself near a machine (its engine trail and flame), moved vertex by vertex
  std::array<Matrix, SEATS> motion{};
};
struct Request
{
  u32 machines = 0;  // a bit for each machine with a place
  std::array<std::array<float, 3>, SEATS> at{};  // machine i's place in the step's view
  std::vector<float> fractions;  // the pictures' planned times after the step's own, in fractions of a field
  // The corrections of a picture `fraction` of a field after the step's own (hfr_ahead.h). Asked on the GPU thread
  // just before each picture is drawn, with the fraction of the time it will be shown at; holds its own copy of the
  // step's state.
  std::function<InBetween(float fraction)> predict;
};

// The host's video side.
struct Video
{
  // a pass's request, made on the game's thread after its steps and before its drawing
  std::function<void(Request)> request;
  // the display's refresh rate, pictures a second; 0 when not known
  std::function<double()> display_hz;
};

using Clock = std::chrono::steady_clock;

// A start: the launch file's settings.
void Start(const Options& options, Video video);
// Whether this start draws frames between steps (only then is anything of this file used).
bool On();
// The image's host call.
u32 Call(Guest& g, u32 op, u32 frame, Clock::time_point now);
// The log's lines at the stop: requests made, passes with none, machines and cameras that jumped; how far the guesses
// missed.
std::string Report();

// The in-between fractions of one pass for `per_step` pictures a step (2 at 120 Hz, 4 at 240 Hz, 2.4 at 144 Hz):
// the display's refreshes between the last true picture (fraction 0) and the next (1). `phase` carries the first
// refresh's place after a true picture from pass to pass (0 to 1 / per_step).
std::vector<float> Fractions(double per_step, double* phase);
// The most pictures between two steps drawn: a display up to 12 pictures a step (720 Hz). A plan of more, or of more
// than the emulator's time a pass takes, is drawn evenly thinned (not its first few and a gap: with a lower limit a
// 360 Hz display's 5 a step were cut to their first 3, so each step's last two refreshes repeated the third
// picture, seen as "frame skipping" on 360 Hz).
constexpr u32 MOST_FRAMES = 11;
}  // namespace hfr

// The image's host call with the core's own host and data window.
u32 HfrCall(u32 op, u32 frame);
}  // namespace fzgx
