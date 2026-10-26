// The status board: the channel between the launcher and this game, a small file the launcher makes and both map
// (protocol/board.toml, generated into status_board.g.h; protocol/formats.md section 8). The game writes its
// heartbeat, where it is (the phase), the race as it runs, why it stops, and events that must not be lost; it reads
// the launcher's mailbox once a picture. Nothing here waits for the launcher, and nothing of the race reads it.
// The core opens it at Init from the launch file's "board" (none: every call here does nothing); the host calls the
// functions below. Several threads call them: each says which.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "status_board.g.h"

#include "fzgx/host.h"

namespace fzgx::board
{
// Every picture of every start, on the game's thread: the heartbeat of a plain start (a race's or replay's is the
// frame tick's), the phase, and the mailbox (a stop it asks for is asked here).
void Field();
// The host's watching thread, about four times a second: WatchBeat, which goes on while the game's thread stands.
void Look();
// A stop is asked for (any thread, before the stop): the first one's reason stays on the board.
void Stopped(StopReason why);
// The watching thread's freeze reports (fzgx/freeze.h): a freeze reported, and a game that moves again.
void Stands(u32 ms, u32 report);
void Moves(u32 ms);
// Whether this game took a board (the launcher's, of its own version). Tests.
bool Taken();
}  // namespace fzgx::board
