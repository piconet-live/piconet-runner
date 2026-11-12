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
// FACES (the top-6 faces board) moves like the others (its 3D heads are copied pictures queued with the rings; their
// circle mask is an undrawn packet that stays put, see Flush). It alone can also be hidden: {"FACES": {"hide": true}}
// takes the whole board off (rings, numbers and the 3D heads, whose task is skipped by the image's mode_faces site).
// No other part takes "hide".
// A part's name is one of PARTS. BOOST (the energy meter's lightning, a sprite of its own) takes ENERGY's move and
// pivot unless the layout names it.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <string>
#include <string_view>

#include "fzgx/guest.h"
#include "mode_words.g.h"

namespace fzgx::hud_move
{
// The parts, by the number the image's sites pass (sites_hudmove.py PARTS: the same order): NONE, then the protocol's
// `part` choice in its order (modes::PART_* p is part p + 1; TEXT the last one a mode names), then the two of the HUD
// no mode names: BOOST (the energy meter's lightning, which goes with ENERGY) and STORY_OTHER (the story's other blocks,
// which no race draws)
inline constexpr std::array<std::string_view, 29> PARTS = {
    "NONE",      "TIMER",     "DISTANCE", "DIGIT",   "WARNING", "SUCCESS",   "FAILED", "TIME_OVER",  "SPEED_GAUGE",
    "OBJECTS",   "LEADER_GAP", "GHOST_GAP", "RANK",  "LAPS",    "SPEED",     "ENERGY", "LAP_TIMES",  "RACE_TIME",
    "FACES",     "SPARE",     "SKULLS",   "WRONG_WAY", "BANNERS", "FINISH",  "CHECK",  "SCORE",      "TEXT",       "BOOST",
    "STORY_OTHER"};
constexpr u32 NONE = 0, LEADER_GAP = 10, RANK = 12, ENERGY = 15, RACE_TIME = 17, FACES = 18, SPARE = 19, SKULLS = 20, WRONG_WAY = 21,
              BANNERS = 22, FINISH = 23, CHECK = 24, SCORE = 25, TEXT = 26, BOOST = 27, STORY_OTHER = 28;
static_assert(PARTS[LEADER_GAP] == "LEADER_GAP" && PARTS[RANK] == "RANK" && PARTS[ENERGY] == "ENERGY" && PARTS[RACE_TIME] == "RACE_TIME" &&
              PARTS[FACES] == "FACES" && PARTS[SPARE] == "SPARE" && PARTS[SKULLS] == "SKULLS" && PARTS[WRONG_WAY] == "WRONG_WAY" &&
              PARTS[BANNERS] == "BANNERS" && PARTS[FINISH] == "FINISH" && PARTS[CHECK] == "CHECK" && PARTS[SCORE] == "SCORE" &&
              PARTS[TEXT] == "TEXT" && PARTS[BOOST] == "BOOST" && PARTS[STORY_OTHER] == "STORY_OTHER" &&
              PARTS[modes::PART_COUNT] == "TEXT" && PARTS[modes::PART_SPARE + 1] == "SPARE" && PARTS[modes::PART_CHECK + 1] == "CHECK" &&
              PARTS[modes::PART_SCORE + 1] == "SCORE" && PARTS[modes::PART_TEXT + 1] == "TEXT" && PARTS.size() == modes::PART_COUNT + 3);
constexpr u32 PUSH = 1, POP = 2, SET = 3, SPRITE = 4, FLUSH = 5;  // the host call's operations (r3)

struct Move
{
  bool given = false;
  float dx = 0, dy = 0, angle = 0;
  bool flip_x = false, flip_y = false;
  bool has_pivot = false;
  bool hide = false;  // FACES only
  float px = 0, py = 0;
  // a mode's place (hud_move s, PART, x, y, angle): the part's centre to (tx, ty) of the screen, in place of dx and dy;
  // each squeezed packet gets tx as its Wide Screen anchor, so the part stays centred on tx in Wide Screen too
  bool to_point = false;
  float tx = 0, ty = 0;
};

struct Layout
{
  std::array<Move, PARTS.size()> parts{};
  bool any = false;
  bool log = false;
  bool faces_hidden = false;
};

// A layout from its text; `why` says what is wrong (and nothing is moved) when it is refused.
Layout FromText(const std::string& text, std::string* why);
// The start's layout: read once from the file "debug.classicHudLayout" names (none: nothing moves).
const Layout& TheLayout();

// What one packet becomes: the pure arithmetic, for the tests. `x`, `y`, `rot` (the packet's +0x28, 65536 a turn),
// `flags` (+0x30), the texture corners `u0`, `u1`, `v0`, `v1`, `anchor` (+0x34, the Wide Screen anchor's x: the
// game's fn_1_4EDAC squeezes a squeezed packet's x by 0.75 about it); `squeezed`: Wide Screen squeezes this packet.
struct Packet
{
  float x, y;
  s16 rot;
  u32 flags;
  float u0, v0, u1, v1;
  float anchor = 0;
};
Packet Apply(const Move& m, float px, float py, bool squeezed, const Packet& p);
// Where the game then draws x: the squeeze about the anchor in Wide Screen, as fn_1_4EDAC (for the tests).
float ScreenX(const Packet& p, bool widescreen);

// A game mode's places (hud_move s, PART, x, y, angle: protocol modes.toml), for the racer the picture follows, by
// modes::PART_*: the part's centre to (x, y) of the 640 x 480 screen, turned `angle` degrees clockwise (`given`: the
// part is placed). Set once a drawn frame before Frame (core.cpp, from mode_hud's ViewedMoves). For a part the mode
// places, its place takes the place of the layout's move for that part (the layout's turn and mirror too);
// LEADER_GAP, drawn as a banner, places BANNERS too (a BANNERS place of its own wins).
struct Place
{
  bool given = false;
  s16 x = 0, y = 0, angle = 0;
};
using ModePlaces = std::array<Place, modes::PART_COUNT>;
void SetMode(const ModePlaces& places);
// The move a part gets: the mode's place when it places the part, else the layout's move. Pure, for the tests.
Move Effective(const Layout& layout, const ModePlaces& places, std::size_t part);

// At the start (Init): the window's word the image's sites test, from the layout.
void Prepare(ModData& data);
// Once a drawn frame (the drawing's ask): the window's word the image's sites test.
void Frame(Guest& g);
// The image's host call (memmap.HOST_HUD): `op` one of PUSH, POP, SET, SPRITE, FLUSH, `arg` the part (SPRITE: the
// sprite's address).
void Call(Guest& g, u32 op, u32 arg);
}  // namespace fzgx::hud_move
