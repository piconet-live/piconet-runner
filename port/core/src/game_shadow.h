// The game's own drop shadow, left out at its source while Aurora's ray tracing lights the race (the rays shadow the
// machines themselves; the game's dark quad under each machine was a second shadow, and its 6x6 quad a disc on the road).
// The game: car_shadow_calc (fn_1_56D4C, a PRES-LAST step task, prio 22, one a machine; car.c fn_1_82724 makes them)
// places each machine's CarShadow record every frame and sets its flags' bit 0x4 ("placed this frame"); CarDisp
// (fn_1_84860, a draw task) then calls fn_1_5745C(&shadows[racer]), which returns at once unless that bit is set,
// else queues the quad (fn_1_571E8: the 64x64 silhouette, blend INVDSTALPHA/DSTALPHA). With rays on, once a frame after
// the frame's steps and before its drawing (core.cpp's DRAW_ASK, beside clean_picture), bit 0x4 of every machine's
// record is cleared: the quad is never queued, in a race, a replay's playback and a plain start alike.
// Drawing only. The records' readers are the drawing alone: the array pointer (main.rel .bss 0x6D844) is loaded by
// fn_1_82724 (it allocates the array, racers x 0x4C, and hands each record to its calc task and to the silhouette's
// picture task fn_1_568F8), fn_1_85688 (frees it) and CarDisp (the draw call) only, and the record reaches no other
// function (the decomp's main.rel assembly, every load of 0x6D844 through any symbol of that block); the calc task
// writes bit 0x4 again every frame before the drawing reads it, and the state checksum covers only the machines.
// Rays off: nothing is written.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "fzgx/guest.h"

namespace fzgx::game_shadow
{
// The frame's part: with rays on, no machine's drop shadow is drawn this frame; rays off, nothing.
void Frame(Guest& g, bool rays);
}  // namespace fzgx::game_shadow
