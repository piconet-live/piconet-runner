// Tests of the start that opens the game's own controller options (options_screen.cpp; a launch file's
// "controllerSetup"): the watch's states against a game that is four words of a mode machine and one flag, moved
// here as the game moves them. No game: the test host's memory.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "harness.h"

namespace
{
constexpr u16 NONE = game::MODE_NONE, TITLE = game::MODE_TITLE, OPTION = game::MODE_OPTION;
constexpr u16 DEMO = game::SUBMODE_TITLE_DEMO, MENU = game::SUBMODE_OPTION_MENU, CONTROLLER = game::SUBMODE_OPTION_CONTROLLER;
constexpr u16 SAVE = game::SUBMODE_OPTION_SAVE;
// the title's other screens, and Options' under the controller menu
constexpr u16 LOGOS = 0x9E, CARD = 0x9A, CALIBRATION = 0x50, RUMBLE = 0x51, SELECT = 1;

// The game's mode machine as the watch sees it: where it is, and what is asked for.
struct Game
{
  explicit Game(TestHost& host) : g(host, Data())
  {
    LinkMainRel(g);
    Is(NONE, NONE);
    Asked(NONE, NONE);
  }
  void Is(u16 mode, u16 sub)
  {
    g.Write16(game::GAME_MODE_AT, mode);
    g.Write16(game::GAME_SUBMODE_AT, sub);
  }
  void Asked(u16 mode, u16 sub)
  {
    g.Write16(game::MODE_REQUEST_AT, mode);
    g.Write16(game::SUBMODE_REQUEST_AT, sub);
  }
  u16 AskedMode() const { return g.Read16(game::MODE_REQUEST_AT); }
  u16 AskedSub() const { return g.Read16(game::SUBMODE_REQUEST_AT); }
  // A picture: the watch runs once. Then what it left asked for.
  std::pair<u16, u16> Picture()
  {
    ControllerSetupTick();
    return {AskedMode(), AskedSub()};
  }
  Guest g;
};

const std::pair<u16, u16> NOTHING{NONE, NONE};

Options Setup()
{
  Options o;
  o.about = "a test of the controller options' start";
  o.controller_setup = true;
  return o;
}

bool Says(const std::string& text, const std::string& part)
{
  return text.find(part) != std::string::npos;
}

// A boot up to the controller menu, as the game goes: the watch has asked for Options and for the menu.
void Open(Game& game)
{
  game.g.Write8(game::SAVE_DEFAULTS_SET, 1);
  game.Is(TITLE, DEMO);
  game.Picture();
  game.Asked(NONE, NONE);   // the mode machine takes the request
  game.Is(OPTION, MENU);
  game.Picture();
  game.Asked(NONE, NONE);
  game.Is(OPTION, CONTROLLER);
  game.Picture();
}

// TestTheControllerSetupStart's steps.

// The title: nothing is asked for before it stands after its memory card sequence with nothing else asked, and
// then Options, once.
void TheTitle(TestHost& host, Game& game)
{
  // main.rel not linked, another module at the head of the list, the logos, the memory card sequence (the title has
  // set the save's defaults by then: the flag alone is no sign that the card was read)
  game.g.Write8(game::SAVE_DEFAULTS_SET, 1);
  game.Is(TITLE, DEMO);
  game.g.Write32(net::MODULE_POINTER, 0);
  CHECK(game.Picture() == NOTHING);
  LinkMainRel(game.g);
  game.g.Write32(MODULE, 0x12);
  CHECK(game.Picture() == NOTHING);
  LinkMainRel(game.g);
  game.Is(NONE, NONE);
  CHECK(game.Picture() == NOTHING);
  game.Is(TITLE, LOGOS);
  CHECK(game.Picture() == NOTHING);
  game.Is(TITLE, CARD);   // a card prompt stands here for as long as the player takes
  for (int i = 0; i < 100; ++i)
    CHECK(game.Picture() == NOTHING);
  // the card sequence asks for the demonstration: not yet (the title is still in the sequence's screen)
  game.Asked(NONE, DEMO);
  CHECK(game.Picture() == std::make_pair(NONE, DEMO));
  // the demonstration with the save's defaults not set: the title's exit would set them, over the player's save
  game.Asked(NONE, NONE);
  game.Is(TITLE, DEMO);
  game.g.Write8(game::SAVE_DEFAULTS_SET, 0);
  CHECK(game.Picture() == NOTHING);
  // and with something asked for already (the title's own request)
  game.g.Write8(game::SAVE_DEFAULTS_SET, 1);
  game.Asked(SELECT, NONE);
  CHECK(game.Picture() == std::make_pair(SELECT, NONE));
  game.Asked(NONE, static_cast<u16>(0x9B));
  CHECK(game.Picture() == std::make_pair(NONE, static_cast<u16>(0x9B)));
  CHECK(host.notices.empty() && host.stops == 0);
  // the title stands: Options is asked for, the mode alone (its module's start asks for its own first menu)
  game.Asked(NONE, NONE);
  CHECK(game.Picture() == std::make_pair(OPTION, NONE));
  CHECK(Says(host.notices, "fzgx: controller setup: the title stands; asked the game for its Options\n"));
  // the title loads its demonstration for seconds before its next frame: the request stands, and is not made twice
  host.notices.clear();
  for (int i = 0; i < 300; ++i)
    CHECK(game.Picture() == std::make_pair(OPTION, NONE));
  CHECK(host.notices.empty() && host.stops == 0);
}

// Options: the controller menu is asked for once the first menu stands, and the visit is open when it does.
void TheMenus(TestHost& host, Game& game)
{
  // the title is left, Options loads (its mode with no screen yet), its start asks for its first menu
  game.Asked(NONE, NONE);
  game.Is(TITLE, NONE);
  CHECK(game.Picture() == NOTHING);
  game.Is(NONE, NONE);
  CHECK(game.Picture() == NOTHING);
  game.Is(OPTION, NONE);
  CHECK(game.Picture() == NOTHING);
  game.Asked(NONE, MENU);
  CHECK(game.Picture() == std::make_pair(NONE, MENU));
  // the first menu stands: the controller menu is asked for, once
  game.Asked(NONE, NONE);
  game.Is(OPTION, MENU);
  CHECK(game.Picture() == std::make_pair(NONE, CONTROLLER));
  CHECK(game.Picture() == std::make_pair(NONE, CONTROLLER));
  game.Asked(NONE, NONE);
  CHECK(game.Picture() == NOTHING);   // taken, the first menu still on the screen for the frame: not asked again
  game.Is(OPTION, NONE);
  CHECK(game.Picture() == NOTHING);
  CHECK(host.notices.empty());
  game.Is(OPTION, CONTROLLER);
  CHECK(game.Picture() == NOTHING);
  CHECK(Says(host.notices, "fzgx: controller setup: the game's controller options are open\n") && host.stops == 0);
}

// The visit: the screens under the controller menu and the game's own save are no leaving; B is.
void TheVisit(TestHost& host, Game& game)
{
  host.notices.clear();
  const auto screen = [&](u16 sub) {
    game.Asked(NONE, sub);
    game.Picture();
    game.Asked(NONE, NONE);
    game.Is(OPTION, NONE);
    game.Picture();
    game.Is(OPTION, sub);
    for (int i = 0; i < 50; ++i)
      game.Picture();
  };
  screen(CALIBRATION);
  screen(CONTROLLER);   // left without a change: straight back
  screen(RUMBLE);
  screen(SAVE);         // changed and confirmed: the game's save screen, then the menu
  screen(CONTROLLER);
  screen(CALIBRATION);
  screen(SAVE);
  screen(CONTROLLER);
  CHECK(host.stops == 0 && host.notices.empty() && game.Picture() == NOTHING);
  // B: the menu asks for Options' first menu. The game is stopped, once, and nothing is asked of it any more
  game.Asked(NONE, MENU);
  CHECK(game.Picture() == std::make_pair(NONE, MENU));
  CHECK(host.stops == 1);
  CHECK(Says(host.notices, "fzgx: controller setup: the player left the controller options; the game saved 2 times in the "
                           "visit; stopping\n"));
  game.Asked(NONE, NONE);
  game.Is(OPTION, MENU);
  for (int i = 0; i < 10; ++i)
    CHECK(game.Picture() == NOTHING);
  game.Is(TITLE, DEMO);   // whatever the game shows while it stops
  CHECK(game.Picture() == NOTHING && host.stops == 1);
}

// Other ways out, each a new start: the request for the first menu missed (the watch runs at any point of a frame),
// B in the first menu's own first frame, and the game in another mode.
void OtherWaysOut(TestHost& host)
{
  {
    Init(host, Setup());
    host = TestHost();
    Game game(host);
    Open(game);
    CHECK(host.stops == 0);
    game.Is(OPTION, MENU);   // the request was made and taken between two pictures
    CHECK(game.Picture() == NOTHING && host.stops == 1);
    CHECK(Says(host.notices, "the player left the controller options; the game saved 0 times in the visit; stopping\n"));
  }
  {
    Init(host, Setup());
    host = TestHost();
    Game game(host);
    game.g.Write8(game::SAVE_DEFAULTS_SET, 1);
    game.Is(TITLE, DEMO);
    game.Picture();
    game.Asked(NONE, NONE);
    game.Is(OPTION, MENU);
    CHECK(game.Picture() == std::make_pair(NONE, CONTROLLER));
    // B was down in that frame: Options leaves for the menu before it (its own request for the mode)
    game.Asked(SELECT, NONE);
    CHECK(game.Picture() == std::make_pair(SELECT, NONE) && host.stops == 1);
    CHECK(Says(host.notices, "fzgx: controller setup: the game left its Options; the game saved 0 times in the visit; stopping\n"));
  }
  {
    Init(host, Setup());
    host = TestHost();
    Game game(host);
    Open(game);
    game.Is(SELECT, NONE);
    CHECK(game.Picture() == NOTHING && host.stops == 1 && Says(host.notices, "the game left its Options"));
    game.Picture();
    CHECK(host.stops == 1);
  }
}

std::string Refused(const std::string& text)
{
  Options o;
  return ParseLaunchFile(text, &o).value_or("");
}
}  // namespace

void TestTheControllerSetupStart()
{
  BEGIN_TEST();
  TestHost host;
  Init(host, Setup());
  {
    Game game(host);
    TheTitle(host, game);
    TheMenus(host, game);
    TheVisit(host, game);
  }
  OtherWaysOut(host);
  // a start that did not ask for it: the title stands, and nothing is asked of the game, ever
  Init(host, Options{});
  host = TestHost();
  {
    Game game(host);
    game.g.Write8(game::SAVE_DEFAULTS_SET, 1);
    game.Is(TITLE, DEMO);
    for (int i = 0; i < 10; ++i)
      CHECK(game.Picture() == NOTHING);
    CHECK(host.stops == 0 && host.notices.empty());
  }
}

void TestTheControllerSetupKey()
{
  // a player's key: at the file's own level, and no test setting is on with it
  Options o;
  CHECK(!ParseLaunchFile(R"({"about": "the controller options", "bundle": "b", "controllerSetup": true})", &o));
  CHECK(o.controller_setup && DebugSettingsOn(o.debug).empty() && o.match.empty() && o.play_file.empty());
  Options plain;
  CHECK(!ParseLaunchFile(R"({"about": "offline play", "controllerSetup": false})", &plain) && !plain.controller_setup);
  CHECK(!ParseLaunchFile(R"({"about": "offline play"})", &plain) && !plain.controller_setup);
  // refused: another kind of value, the key among the test settings, and with a race or a playback
  CHECK(Refused(R"({"about": "a", "controllerSetup": 1})") == "\"controllerSetup\" is not true or false");
  CHECK(Refused(R"({"about": "a", "controllerSetup": "yes"})") == "\"controllerSetup\" is not true or false");
  CHECK(Refused(R"({"about": "a", "debug": {"controllerSetup": true}})") == "\"debug.controllerSetup\" is not a setting of a launch file");
  CHECK(Refused(R"({"about": "a", "bundle": "b", "controllerSetup": true, "replay": {"file": "r.fzrp"}})") ==
        "\"controllerSetup\" and \"replay\" are both there: the game's controller options open in a plain start");
  CHECK(Refused(R"({"about": "a", "bundle": "b", "controllerSetup": true, "match": {"match": {}}})") ==
        "\"controllerSetup\" and \"match\" are both there: the game's controller options open in a plain start");
  CHECK(Refused(R"({"about": "a", "bundle": "b", "controllerSetup": false, "replay": {"file": "r.fzrp"}})").empty());
}
