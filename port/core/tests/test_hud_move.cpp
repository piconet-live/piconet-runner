// Tests of the classic HUD's moves (hud_move.h): a packet moved, turned about the part's pivot and mirrored as the
// game's quad builder (fn_1_4EDAC) then draws it, and the layout file's refusals.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "harness.h"

#include <cmath>

#include "hud_move.h"

using namespace fzgx::hud_move;

namespace
{
bool Near(float a, float b)
{
  return std::abs(a - b) < 0.01f;
}
}  // namespace

void TestTheClassicHudMoves()
{
  const Packet p{100, 50, 0, 5, 0.0f, 0.0f, 1.0f, 1.0f};  // the template's flags 5: left, top
  Move m;
  m.given = true;
  // a move alone: the position, nothing else; in Wide Screen a squeezed packet's dx is divided by the squeeze
  m.dx = 30, m.dy = -10.5f;
  Packet k = Apply(m, 0, 0, false, p);
  CHECK(Near(k.x, 130) && Near(k.y, 39.5f) && k.rot == 0 && k.flags == 5 && k.u0 == 0 && k.u1 == 1);
  k = Apply(m, 0, 0, true, p);
  CHECK(Near(k.x, 140));
  // a quarter turn clockwise about (100, 100): a point above the pivot goes to its right; the quad turns with it
  m = Move{};
  m.given = true, m.angle = 90;
  k = Apply(m, 100, 100, false, p);
  CHECK(Near(k.x, 150) && Near(k.y, 100) && k.rot == -16384);
  // a mirror left to right about x 120: the position mirrored, the box's side swapped, the texture's corners too
  m = Move{};
  m.given = true, m.flip_x = true;
  k = Apply(m, 120, 0, false, p);
  CHECK(Near(k.x, 140) && Near(k.y, 50) && (k.flags & 3) == 3 && ((k.flags >> 2) & 3) == 1 && k.u0 == 1 && k.u1 == 0);
  // mirrored, the packet's own turn changes sign; upside down mirrors the other axis
  Packet turned = p;
  turned.rot = 1000;
  k = Apply(m, 120, 0, false, turned);
  CHECK(k.rot == -1000);
  m.flip_x = false, m.flip_y = true;
  k = Apply(m, 0, 40, false, p);
  CHECK(Near(k.y, 30) && (k.flags & 3) == 1 && ((k.flags >> 2) & 3) == 3 && k.v0 == 1 && k.v1 == 0);
  // the file: parts by name, floats kept; an unknown part or member refuses the whole file
  std::string why;
  Layout l = FromText(R"({"ENERGY": {"dx": 12.25, "angle": 15, "flip": "xy", "pivot": [500, 40]}, "log": true})", &why);
  CHECK(why.empty() && l.any && l.log && l.parts[ENERGY].given && Near(l.parts[ENERGY].dx, 12.25f) &&
        l.parts[ENERGY].flip_x && l.parts[ENERGY].flip_y && l.parts[ENERGY].has_pivot && Near(l.parts[ENERGY].px, 500));
  l = FromText(R"({"ENERGIE": {"dx": 1}})", &why);
  CHECK(!why.empty() && !l.any);
  l = FromText(R"({"RANK": {"scale": 2}})", &why);
  CHECK(!why.empty() && !l.any);
  // FACES can only be hidden: a move of it is ignored (the board stays), hide takes the whole board off; no other part hides
  l = FromText(R"({"FACES": {"dx": 40, "angle": 15}})", &why);
  CHECK(why.empty() && !l.any && !l.faces_hidden && l.faces_moved_ignored && !l.parts[FACES].given);
  l = FromText(R"({"FACES": {"hide": true, "dx": 40}})", &why);
  CHECK(why.empty() && l.any && l.faces_hidden && !l.parts[FACES].given);
  l = FromText(R"({"RANK": {"hide": true}})", &why);
  CHECK(!why.empty() && !l.any);
}
