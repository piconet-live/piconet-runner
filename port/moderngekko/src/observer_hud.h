// The observer HUD over a race watched (like an operator deck; the design canvas
// "Spectator observer HUD", boards Main, Live and KO): what spectator_overlay.cpp draws from what the core told it
// (fzgx/host.h SpectatorShow; the core's observer_frame.cpp). Three views, Z or the mode button round:
//   - Observer: the timing tower at the left (the race's lap and the racers still racing over a row a racer, in race
//     order: place, the racer's colour, name, gap to the leader; the racer followed lit), the camera groups at the
//     top right (Chase, TV, Auto, Fixed; the chosen group's numbered shots; Leader and Battle), the mode button under
//     them, the racer card at the bottom middle (place on the racer's colour, name and machine, energy, speed, the
//     lap and its time) and our course map in the bottom right corner (minimap.cpp, moved there from the game's map
//     corner), clear of the replay bar and its lanes when they are up;
//   - Game HUD: the game's own HUD for the racer followed, our course map where the game's was, the mode button;
//   - Clean: the picture alone, but a "Show HUD" button at the top right, nearly clear until the pointer is on it.
// A racer knocked out of a live race sees whom they watch and, at the top middle, that they are out (the KO board).
// One layout serves the drawing and the mouse (Lay), from the game's picture and what is shown. Plain labels, the
// launcher's palette and type (overlay.h: the headings and the place figure in the display face, leaning; the rest
// read in Saira Semi Condensed, figures on one pitch), soft-cornered panels on translucent grounds, square buttons,
// every change eased (overlay.h's motion). Nothing here reads or writes the game.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <cstdint>

#include <imgui.h>

#include "fzgx/host.h"
#include "overlay.h"

namespace fzgx_port::observer_hud
{
constexpr float PANEL = 0.78f, CARD = 0.84f, MAP = 0.6f, MODE = 0.6f;  // the grounds' opacity
constexpr float GHOST = 0.12f;  // the clean picture's Show HUD button, until the pointer is on it

// The camera groups and the shots in each (fzgx/spectator.h Kind).
constexpr int GROUPS = 4;
struct Group
{
  const char* label;
  int count;
  std::array<int, 4> kinds;  // its shots' kinds, by number: first person is Chase's 4, though it is the last kind
};
constexpr std::array<Group, GROUPS> GROUP{{{"Chase", 4, {0, 1, 2, 9}},
                                           {"TV", 1, {3}},
                                           {"Auto", 1, {4}},
                                           {"Fixed", 4, {5, 6, 7, 8}}}};
int GroupOf(fzgx::u32 kind);

// Where everything is, in the display's pixels, for one picture and what is shown.
struct Layout
{
  float s = 1;                         // 1 at 720 lines
  overlay::Box picture;
  // the tower: its head, then a row a racer; `row` the show's row each drawn row is (the racer followed takes the
  // last place when the tower is cut short and it would fall below)
  overlay::Box tower, head;
  std::array<overlay::Box, 30> rows{};
  std::array<fzgx::u32, 30> row{};
  fzgx::u32 drawn = 0;
  // the camera groups, the chosen group's shots, Leader, Battle; the mode button; the clean picture's button
  std::array<overlay::Box, GROUPS> groups{};
  std::array<overlay::Box, 4> shots{};
  int shot_count = 0, group = 0;
  overlay::Box leader, battle, mode, ghost;
  // the racer card and its parts (place, name, energy, speed, lap; `lap` empty when the picture is too narrow)
  overlay::Box card, place, name, energy, speed, lap;
  // the course map's ground and the square the map is drawn in (its side the game corner's: minimap_draw Place)
  overlay::Box map, square;
  // a racer knocked out: that they are out, at the top
  overlay::Box out;
};
// `clear`: the height at the picture's bottom the replay bar and its lanes take now (0 when they are not up);
// `map_side`: the course map's side.
Layout Lay(const fzgx::SpectatorShow& show, const overlay::Box& picture, float clear, float map_side);

// How much of each part shows now (0 to 1, eased): the observer HUD, the mode button (every view but the clean
// picture), the clean picture's button, and that button under the pointer.
struct Fades
{
  float observer = 0, mode = 0, clean = 0, ghost_hover = 0;
};

// Drawn on `list` (the course map's ground before the map, the rest after: DrawGround, then Draw), the pointer at
// (hx, hy) for the hover marks.
void DrawGround(ImDrawList* list, const Layout& l, float observer);
void Draw(ImDrawList* list, const fzgx::SpectatorShow& show, const Layout& l, const Fades& a, float hx, float hy);
// A press at (x, y): true when it is the HUD's, with what it asks in `click`.
bool Hit(const fzgx::SpectatorShow& show, const Layout& l, float x, float y, fzgx::SpectatorClick* click);
}  // namespace fzgx_port::observer_hud

namespace fzgx_port
{
// spectator_overlay.cpp: the course map's square (minimap_draw Place's, in the game's map corner) moved to where the
// observer HUD shows it, as far as the HUD has come in (the drawing thread's, as DrawOverlays left it).
overlay::Box MovedMapSquare(const overlay::Box& square);
}  // namespace fzgx_port
