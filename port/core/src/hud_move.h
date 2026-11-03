// The classic HUD's moves: each part of the game's own race HUD moved, turned by any angle and mirrored, as one piece
// about one pivot, from a layout file (a test setting for now: the launch file's "debug.classicHudLayout" names it). The image's sites
// (port/image/sites_hudmove.py) tell the host which part is drawing while the game queues its 2D packets, and at the
// queue's drawing (fn_1_4FD64) the host rewrites the queued packets of every moved part. Drawing only: the packets
// are the frame's own, written by the drawing in the same frame; nothing a step reads changes, and with no layout the
// window's word is 0 and no site calls the host.
//
// The layout, JSON: {"PART": {"dx": 40, "dy": -12.5, "angle": 15, "flip": "x", "pivot": [320, 240]}, ..., "log": true}
//   dx, dy   the move, in 640x480 units of the screen (floats; in Wide Screen a squeezed part's dx is divided by the
//            squeeze, so it moves dx units of the picture's width too)
//   angle    degrees, clockwise on the screen, about the pivot
//   flip     "x" (mirrored left to right), "y" (upside down), "xy", or left out; about the pivot, before the turn
//   pivot    [x, y] in the game's 640x480 units before Wide Screen's squeeze; left out: the centre of the part's box
//            (PIVOTS below, measured; a part not measured: the box of everything it has drawn in this start)
//   "log"    true: the parts' boxes in the log every 600 drawn queues (to measure PIVOTS)
// A part's name is one of PARTS. BOOST (the energy meter's lightning, a sprite of its own) takes ENERGY's move and
// pivot unless the layout names it.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <string>
#include <string_view>

#include "fzgx/guest.h"

namespace fzgx::hud_move
{
// The parts, by the number the image's sites pass (sites_hudmove.py PARTS: the same order)
inline constexpr std::array<std::string_view, 26> PARTS = {
    "NONE",      "TIMER",     "DISTANCE", "DIGIT",   "WARNING", "SUCCESS",   "FAILED", "TIME_OVER",  "SPEED_GAUGE",
    "OBJECTS",   "LEADER_GAP", "GHOST_GAP", "RANK",  "LAPS",    "SPEED",     "ENERGY", "LAP_TIMES",  "RACE_TIME",
    "FACES",     "SPARE",     "SKULLS",   "BOOST",   "WRONG_WAY", "BANNERS", "FINISH", "STORY_OTHER"};
constexpr u32 NONE = 0, ENERGY = 15, BOOST = 21, WRONG_WAY = 22;
constexpr u32 PUSH = 1, POP = 2, SET = 3, SPRITE = 4, FLUSH = 5;  // the host call's operations (r3)

struct Move
{
  bool given = false;
  float dx = 0, dy = 0, angle = 0;
  bool flip_x = false, flip_y = false;
  bool has_pivot = false;
  float px = 0, py = 0;
};

struct Layout
{
  std::array<Move, PARTS.size()> parts{};
  bool any = false;
  bool log = false;
};

// A layout from its text; `why` says what is wrong (and nothing is moved) when it is refused.
Layout FromText(const std::string& text, std::string* why);
// The start's layout: read once from the file "debug.classicHudLayout" names (none: nothing moves).
const Layout& TheLayout();

// What one packet becomes: the pure arithmetic, for the tests. `x`, `y`, `rot` (the packet's +0x28, 65536 a turn),
// `flags` (+0x30), the texture corners `u0`, `u1`, `v0`, `v1`; `squeezed`: Wide Screen squeezes this packet.
struct Packet
{
  float x, y;
  s16 rot;
  u32 flags;
  float u0, v0, u1, v1;
};
Packet Apply(const Move& m, float px, float py, bool squeezed, const Packet& p);

// At the start (Init): the window's word the image's sites test, from the layout.
void Prepare(ModData& data);
// Once a drawn frame (the drawing's ask): the window's word the image's sites test.
void Frame(Guest& g);
// The image's host call (memmap.HOST_HUD): `op` one of PUSH, POP, SET, SPRITE, FLUSH, `arg` the part (SPRITE: the
// sprite's address).
void Call(Guest& g, u32 op, u32 arg);
}  // namespace fzgx::hud_move
