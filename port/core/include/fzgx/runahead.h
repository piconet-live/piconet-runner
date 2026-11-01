// High refresh by a step run ahead (the switch AURORA_RUNAHEAD=<rate>, off by default): after the stock frame begin has
// copied step N's picture, the frame tick runs step N+1 ahead on the rollback machinery with the inputs it will have,
// notes the machines and the camera, undoes it, and draws the pictures between N and N+1 with the game's own draw code
// on blended state, each drawing undone. The real step N+1 then runs as if none of it had been: the undo log puts back
// what it covers and the data window is kept whole around each part.
//
// The core says what runs (Next); the adapter (run_ahead.cpp) runs it on the game's thread as guest calls that end in a
// host call. Nothing nests: between two calls the run loop goes on as ever, interrupts included.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>
#include <string>

#include "fzgx/host.h"

namespace fzgx::runahead
{
struct Call
{
  u32 pc = 0, r3 = 0;
};

// Reads the switch. `between`: told the fraction of the next XFB copy the game is about to make, a picture between.
// `ran`: told that step was run ahead (the per-machine rollback must not take it for the step's last run).
void Start(std::function<void(float fraction)> between, std::function<void(u32 step)> ran);
bool On();
// The frame tick, after the core's own: true when pictures between will be made this pass (Next gives the calls).
bool Begin();
// The next call; `returned` the last call's r3. False: this pass's work is done.
bool Next(u32 returned, Call* call);
// A step or a picture of this file is running: the host's per-step services leave it alone.
bool Inside();
// The image's store into DRAW_ASK (2: the frame's drawing has ended): what the real step and its drawing cost.
void TimeTheDrawing(u32 value);
// The test log AURORA_TEST_INPUT_LOG: when the frame's own step came to send its input, against the tick. `changed`: the
// pad record is not what was sent at the tick.
void NoteInputSent(u32 step, bool changed);
// The log's lines at the stop.
std::string Report();
void Stop();
}  // namespace fzgx::runahead
