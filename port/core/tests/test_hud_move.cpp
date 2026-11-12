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
  // FACES moves like any part, and it alone can also be hidden (the whole board; a move given with hide is moot)
  l = FromText(R"({"FACES": {"dx": 40, "angle": 15}})", &why);
  CHECK(why.empty() && l.any && !l.faces_hidden && l.parts[FACES].given);
  l = FromText(R"({"FACES": {"hide": true, "dx": 40}})", &why);
  CHECK(why.empty() && l.any && l.faces_hidden && !l.parts[FACES].given);
  l = FromText(R"({"RANK": {"hide": true}})", &why);
  CHECK(!why.empty() && !l.any);
  // a mode's places: for its part in place of the layout's (turn and mirror too); LEADER_GAP's go to BANNERS; not given: none
  {
    Layout layout = FromText(R"({"RANK": {"dx": 5, "angle": 30}, "LAPS": {"dy": 3}})", &why);
    ModePlaces places{};
    places[modes::PART_RANK] = {true, 24, 412, 90};
    places[modes::PART_LEADER_GAP] = {true, 320, 130, 0};
    Move e = Effective(layout, places, RANK);
    CHECK(e.given && e.to_point && Near(e.tx, 24) && Near(e.ty, 412) && Near(e.angle, 90));
    e = Effective(layout, places, 13);
    CHECK(e.given && !e.to_point && Near(e.dy, 3));
    e = Effective(layout, places, BANNERS);
    CHECK(e.given && e.to_point && Near(e.tx, 320) && Near(e.ty, 130));
    CHECK(!Effective(layout, places, LEADER_GAP).given && !Effective(layout, places, ENERGY).given);
    // a BANNERS place of its own wins over LEADER_GAP's; the game's own signs are placed by their own names (0.6.1)
    places[modes::PART_BANNERS] = {true, 320, 400, 180};
    e = Effective(layout, places, BANNERS);
    CHECK(e.given && e.to_point && Near(e.tx, 320) && Near(e.ty, 400) && Near(e.angle, 180));
    places[modes::PART_CHECK] = {true, 320, 100, 0};
    places[modes::PART_SKULLS] = {true, 100, 100, 45};
    e = Effective(layout, places, CHECK);
    CHECK(e.given && e.to_point && Near(e.tx, 320) && Near(e.ty, 100));
    e = Effective(layout, places, SKULLS);
    CHECK(e.given && e.to_point && Near(e.tx, 100) && Near(e.angle, 45));
    CHECK(!Effective(layout, places, WRONG_WAY).given && !Effective(layout, places, FINISH).given && !Effective(layout, places, BOOST).given);
    places[modes::PART_RANK] = {};
    e = Effective(layout, places, RANK);
    CHECK(e.given && !e.to_point && Near(e.dx, 5) && Near(e.angle, 30));
    // the mode's part p is the classic HUD's part p + 1; TEXT, the last one a mode names, then BOOST and STORY_OTHER
    CHECK(PARTS[modes::PART_DISTANCE + 1] == "DISTANCE" && PARTS[modes::PART_FACES + 1] == "FACES" && PARTS[modes::PART_SPARE + 1] == "SPARE" &&
          PARTS[modes::PART_SKULLS + 1] == "SKULLS" && PARTS[modes::PART_WRONG_WAY + 1] == "WRONG_WAY" && PARTS[modes::PART_BANNERS + 1] == "BANNERS" &&
          PARTS[modes::PART_FINISH + 1] == "FINISH" && PARTS[modes::PART_CHECK + 1] == "CHECK" && PARTS[modes::PART_SCORE + 1] == "SCORE" &&
          PARTS[modes::PART_TEXT + 1] == "TEXT" && PARTS[SCORE] == "SCORE" && PARTS[TEXT] == "TEXT" && PARTS[BOOST] == "BOOST" &&
          PARTS[STORY_OTHER] == "STORY_OTHER");
  }
  // a place: the part's centre to the point on the screen, in 4:3 and in Wide Screen alike. SPARE's box 24,432-88,456
  // (centre 56, 444) in the left group (anchor 96): placed at (320, 444) its two ends land 32 units either side of
  // 320 in 4:3, and 24 (squeezed by 0.75 about the place, which is now the anchor) in Wide Screen: centred both ways,
  // where a dx from its left group would squeeze about 96 and land off centre
  {
    Move m;
    m.given = true, m.to_point = true, m.tx = 320, m.ty = 444;
    const Packet left{24, 432, 0, 5 | 0x08000000, 0, 0, 1, 1, 96}, right{88, 432, 0, 5 | 0x08000000, 0, 0, 1, 1, 96};
    const Packet l4 = Apply(m, 56, 444, false, left), r4 = Apply(m, 56, 444, false, right);
    CHECK(Near(l4.x, 288) && Near(r4.x, 352) && Near(l4.y, 432) && Near(l4.anchor, 320) && Near(ScreenX(l4, false), 288) && Near(ScreenX(r4, false), 352));
    const Packet lw = Apply(m, 56, 444, true, left), rw = Apply(m, 56, 444, true, right);
    CHECK(Near(ScreenX(lw, true), 296) && Near(ScreenX(rw, true), 344));
    // the game's own left group, for comparison: 96 + 0.75 * (x - 96)
    CHECK(Near(ScreenX(left, true), 42) && Near(ScreenX(right, true), 90) && Near(ScreenX(left, false), 24));
    // a packet the game does not squeeze (no flag): no anchor given, its place as it is
    const Packet plain{24, 432, 0, 5, 0, 0, 1, 1, 0};
    CHECK(Near(Apply(m, 56, 444, true, plain).anchor, 0) && Near(ScreenX(Apply(m, 56, 444, true, plain), true), 288));
    // turned a quarter clockwise about the centre: the left end goes up, the quads turn with it
    m.angle = 90;
    const Packet lt = Apply(m, 56, 444, false, left);
    CHECK(Near(lt.x, 320 + 12) && Near(lt.y, 444 - 32) && lt.rot == -16384);
  }
}
