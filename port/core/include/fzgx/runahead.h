// High refresh by a step run ahead (the switch AURORA_RUNAHEAD=<rate>, the launcher's "Up to N", off by default; the rate
// drawn is the largest multiple of 60 at or under it and the rate of the screen the window is on: PicturesASecond): after the stock frame begin has
// copied step N's picture, the frame tick runs step N+1 ahead on the rollback machinery with the inputs it will have,
// notes the machines and the camera, undoes it, and draws the pictures between N and N+1 with the game's own draw code
// on blended state, each drawing undone. The real step N+1 then runs as if none of it had been: the undo log puts back
// what it covers and the data window is kept whole around each part.
// AURORA_RUNAHEAD_SKIP (on by default with run-ahead; 0 turns it off, for tests and comparison): the step ahead leaves out
// the task lists whose work is undone unseen (the effects, BackGroundCalc, the face board, ...: runahead.cpp, SKIP_LISTS);
// AURORA_TEST_SKIP_COMPARE runs each step ahead both ways and compares what is read from it.
// AURORA_RUNAHEAD_AFTER (on by default with AURORA_REDRAW, which it needs; 0: the step ahead, for tests and comparison): no step ahead at all. The machines and the camera are taken after the
// frame's own true step, at the image's store DRAW_ASK = 1 (the frame's drawing is about to begin), and the request leaves
// there with the command stream's position the tick saw. Nothing new runs in the game: no undo, no muted run, the mode's
// rules and the per-machine rollback untouched. AURORA_TEST_AFTER_COMPARE (today's step ahead on): the same Take at
// DRAW_ASK = 1, compared bit for bit with the step ahead's and with the next tick's, the positions and times beside.
//
// The core says what runs (Next); the adapter (run_ahead.cpp) runs it on the game's thread as guest calls that end in a
// host call. Nothing nests: between two calls the run loop goes on as ever, interrupts included.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <functional>
#include <string>

#include "fzgx/host.h"

namespace fzgx::runahead
{
struct Call
{
  u32 pc = 0, r3 = 0;
};

// AURORA_REDRAW=1 beside AURORA_RUNAHEAD (off by default): no pictures by the game's draw code. The step ahead gives where the machines and
// the camera will be; the host's renderer draws the true frame's own draws again with these corrections. Matrices: row-major, column
// vectors, last row 0 0 0 1. The game's thread, inside the tick (or, with AURORA_RUNAHEAD_AFTER, at DRAW_ASK = 1); nothing here is
// game state.
constexpr u32 PICTURES_MOST = 7;
using Matrix = std::array<float, 16>;
struct RedrawPicture
{
  float fraction = 0;  // of a field after the true picture
  Matrix camera{};
  std::array<Matrix, 30> machine{};
};
struct RedrawRequest
{
  u64 position = 0;  // the command stream's written position at the frame tick (`written`): the true picture's copy is just before it
  u32 machines = 0;
  std::array<std::array<float, 3>, 30> at{};  // each machine's place in the true picture's view
  // that view (the game's 3x4, row-major): the renderer reads a draw's own place in the world with it, and what stands still there
  // stays the world's though a machine is near (the course is drawn from the world's origin, and Big Blue's start line lies on it)
  std::array<float, 12> view{};
  u32 pictures = 0;
  std::array<RedrawPicture, PICTURES_MOST> picture{};
};

// The screen the game's window is on: its refresh rate (0 when not known: no window yet, no system that tells it) and
// the system's name for it ("\\.\DISPLAY2", as the presenter's own pace log names it).
struct Screen
{
  double hz = 0;
  std::string name;
};
// The pictures a second drawn for the setting `asked` (the launcher's "Up to N": AURORA_RUNAHEAD) on a screen of
// `hz`: the largest multiple of 60 at or under both, so that every refresh shows a picture of its own and no picture
// is cut: 240 on 240 Hz is 240, on 200 Hz 180, on 165 or 144 Hz 120, on 100, 75 or 60 Hz 60 (the game's own, none
// between). A screen's rate within SCREEN_TOLERANCE under a multiple of 60 counts as that multiple (59.94 Hz is 60,
// 239.76 is 240, and the system's whole numbers 59, 119, 239 are theirs). `hz` 0: the setting alone, as before the
// screen was followed. Under 120: 60.
constexpr double SCREEN_TOLERANCE = 0.02;
u32 PicturesASecond(u32 asked, double hz);

// Reads the switch. `between`: told the fraction of the next XFB copy the game is about to make, a picture between.
// `ran`: told that step was run ahead (the per-machine rollback must not take it for the step's last run).
// `redraw`: told the pictures between, when AURORA_REDRAW is on. `written`: the command stream's written position now.
// `screen`: the screen the window is on now, asked at each pass's tick (the safe point: a pass's pictures are planned
// whole from its tick, and a change of the count between two passes is one request with fewer or more pictures,
// each at its even place in its field; nothing of a pass is changed under it). The rate follows the screen the window
// is on and the screen's rate as they change (PicturesASecond), each change logged once. None: the setting alone.
void Start(std::function<void(float fraction)> between, std::function<void(u32 step)> ran,
           std::function<void(const RedrawRequest&)> redraw, std::function<u64()> written,
           std::function<Screen()> screen = nullptr);
bool On();
// A plain start (offline play) may have pictures between: on, without a step ahead, with the renderer's redraw. The core
// then sets the image's PLAIN_DRAW (boot.cpp PlainField), and the frame tick and DRAW_ASK come as in a race.
bool PlainDraw();
// The frame tick, after the core's own: true when pictures between will be made this pass (Next gives the calls).
bool Begin();
// The next call; `returned` the last call's r3. False: this pass's work is done.
bool Next(u32 returned, Call* call);
// A step or a picture of this file is running: the host's per-step services leave it alone.
bool Inside();
// The image's store into DRAW_ASK: 1 the frame's drawing is about to begin (AURORA_RUNAHEAD_AFTER's request leaves here; the
// comparison's Take), 2 it has ended (what the real step and its drawing cost).
void TimeTheDrawing(u32 value);
// The test log AURORA_TEST_INPUT_LOG: when the frame's own step came to send its input, against the tick. `changed`: the
// pad record is not what was sent at the tick.
void NoteInputSent(u32 step, bool changed);
// The log's lines at the stop.
std::string Report();
void Stop();
}  // namespace fzgx::runahead
