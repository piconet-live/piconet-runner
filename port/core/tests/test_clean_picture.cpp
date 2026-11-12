// The clean picture (clean_picture.h, a game mode's `hud clean`): the protocol's clean vector sets the window's flag
// HUD_CLEAN; with the flag, a frame's part writes the names' first bytes and the mode tags 0 and every viewport's CHECK
// gap 0.0 (none); without it, it writes nothing.
// SPDX-License-Identifier: GPL-3.0-or-later

#include <bit>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>

#include "harness.h"
#include "clean_picture.h"
#include "files.h"
#include "mode.h"
#include "mode_hud.h"
#include "parts.h"
#include "rollback.h"

namespace
{
namespace hud = mode::hud;

// what the drawing would show: a rival behind in every viewport, every seat named and tagged, the rank markers on
void Shown(Guest& g)
{
  LinkMainRel(g);
  for (u32 vp = 0; vp < 4; ++vp)
    g.Write32(game::CHECK_GAP_AT + vp * 4, std::bit_cast<u32>(-10.0f));
  for (u32 seat = 0; seat < 30; ++seat)
  {
    g.Write8(boot::NAMES + seat * boot::NAME_SIZE, 'A');
    g.Write8(boot::NAMES + seat * boot::NAME_SIZE + 1, 'B');
    g.Write8(layout::MODE_HUD + hud::WINDOW_TAGS + seat, 3);
  }
  g.Write32(layout::MODE_HUD + hud::WINDOW_RANK, 0);
}

std::optional<mode::Package> Vector(const char* name)
{
  std::ifstream file(ProtocolFile("protocol/tests/vectors/modes") / name);
  std::stringstream text;
  text << file.rdbuf();
  picojson::value v;
  picojson::parse(v, text.str());
  std::string why;
  return mode::Read(v, &why);
}
}  // namespace

void TestTheCleanPicture()
{
  BEGIN_TEST();
  // the word: the clean vector's package says it, and its window flags have HUD_CLEAN; a mode without it has not
  const std::optional<mode::Package> clean = Vector("clean.mode.json");
  CHECK(clean.has_value() && clean->hud_clean && (hud::WindowFlags(*clean) & hud::HUD_CLEAN) != 0);
  const std::optional<mode::Package> same = Vector("gap_same.mode.json");
  CHECK(same.has_value() && !same->hud_clean && (hud::WindowFlags(*same) & hud::HUD_CLEAN) == 0);
  // distance_label off: its package says it and the window has NO_DISTANCE_LABEL (the image's label site); none without
  const std::optional<mode::Package> unlabelled = Vector("distance_label.mode.json");
  CHECK(unlabelled.has_value() && unlabelled->distance_label_off && (hud::WindowFlags(*unlabelled) & hud::NO_DISTANCE_LABEL) != 0);
  CHECK(same.has_value() && !same->distance_label_off && (hud::WindowFlags(*same) & hud::NO_DISTANCE_LABEL) == 0);

  TestHost host;
  ModData data;
  Guest g(host, data);
  Init(host, Options{});
  // off (a mode's other flags, no HUD_CLEAN): nothing is written
  Shown(g);
  g.Write32(layout::MODE_HUD + hud::WINDOW_FLAGS, hud::SAME_SPOT | hud::GHOST_TASK);
  clean_picture::Frame(g);
  CHECK(g.Read32(game::CHECK_GAP_AT) == std::bit_cast<u32>(-10.0f) && g.Read8(boot::NAMES) == 'A' &&
        g.Read8(layout::MODE_HUD + hud::WINDOW_TAGS) == 3 && g.Read32(layout::MODE_HUD + hud::WINDOW_RANK) == 0);
  // on: no names and no tags (the rest of a name stays), every viewport's gap none; the rank word is the mode's own
  g.Write32(layout::MODE_HUD + hud::WINDOW_FLAGS, hud::WindowFlags(*clean));
  clean_picture::Frame(g);
  bool cleared = true;
  for (u32 seat = 0; seat < 30; ++seat)
    cleared = cleared && g.Read8(boot::NAMES + seat * boot::NAME_SIZE) == 0 &&
              g.Read8(boot::NAMES + seat * boot::NAME_SIZE + 1) == 'B' &&
              g.Read8(layout::MODE_HUD + hud::WINDOW_TAGS + seat) == 0;
  CHECK(cleared);
  for (u32 vp = 0; vp < 4; ++vp)
    CHECK(g.Read32(game::CHECK_GAP_AT + vp * 4) == 0);
  CHECK(g.Read32(layout::MODE_HUD + hud::WINDOW_RANK) == 0);
  // main.rel not linked yet: the window's bytes still, the game's memory left alone
  Shown(g);
  g.Write32(layout::MODE_HUD + hud::WINDOW_FLAGS, hud::HUD_CLEAN);
  g.Write32(net::MODULE_POINTER, 0);
  clean_picture::Frame(g);
  CHECK(g.Read8(boot::NAMES) == 0 && g.Read8(layout::MODE_HUD + hud::WINDOW_TAGS) == 0 &&
        g.Read32(game::CHECK_GAP_AT) == std::bit_cast<u32>(-10.0f));
  Shutdown();
}
