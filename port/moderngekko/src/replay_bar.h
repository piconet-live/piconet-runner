// The replay scrubber's bar (replay_bar.cpp) and the sound gate (replay_audio.cpp): what the two share. The bar is
// drawn over the game's picture (overlay.h); the gate ticks on its hidden child window's timer.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#ifdef _WIN32
#include <windows.h>

namespace fzgx_port
{
// replay_audio.cpp: a tick of the bar's timer, or of the sound gate's own while it fades (it sets one when a fade
// begins); true: it was the gate's own timer.
bool ReplaySoundTick(HWND window, WPARAM timer);
}  // namespace fzgx_port
#endif
