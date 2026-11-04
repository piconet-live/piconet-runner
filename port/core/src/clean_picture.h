// The clean picture (a game mode's `hud clean`, the window's flag HUD_CLEAN; for trailer footage): what is drawn over
// the race that no HUD part's `hide` reaches is taken off:
//   - the CHECK sign (the arrow at the screen's bottom edge that points at the nearest rival close behind,
//     fn_1_12907C after the rank markers): the Check task (fn_1_128F54, a flag-0 task of the frame's last step)
//     writes each viewport's gap to that rival, 0.0 for none, and the sign is not drawn for 0.0; the gaps are
//     written 0.0;
//   - the name tags over the other players' machines (routines/nametags.py, on in a session and in a replay's
//     playback) and a mode's own `tag` texts: a seat without a player name gets no ordinary tag, and a seat whose
//     window tag byte is 0 no mode tag; the names' first bytes and the tag bytes are written 0 (the results' rows
//     then show the pilots' names).
// Once a frame, after the frame's steps and the mode's HUD pass (which writes the tag bytes every frame), before the
// drawing (core.cpp's DRAW_ASK). Drawing only: the Check task writes a viewport's gap before its own compare reads it,
// and the sign is the gaps' only other reader (the decomp: fn_1_128F10 sets them up, fn_1_128F54 writes them,
// fn_1_12907C reads them; no step task reads them); the names and the tag bytes are the data window's, which no step
// reads, and the state checksum covers only the machines.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "fzgx/guest.h"

namespace fzgx::clean_picture
{
// The frame's part, when the race's mode says `hud clean` (the window's flag); nothing otherwise.
void Frame(Guest& g);
}  // namespace fzgx::clean_picture
