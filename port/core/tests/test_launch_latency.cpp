// Tests of how a picture is shown and of a race's input delay as the launch file carries them (files_launch.cpp).
// "present": "console" unless the file says "field" or "copy", the two
// ways kept for comparison, which the log calls test settings. A race's input delay is its player's setting: 0, 1 or
// 2, and a file that asks for more is refused. "verticalSync": a player's setting, off unless the file says so.
// Neither is a "debug" setting (a launcher's settings file names them, on a server that takes no game with a debug
// setting).
// SPDX-License-Identifier: GPL-3.0-or-later

#include "harness.h"

namespace
{
const char* const MATCH = R"("match": {"matchId": "m7", "seat": 1, "token": "1f2e3d4c5b6a7988",
                 "relay": {"host": "", "port": 47811},
                 "setup": {"course": 8, "laps": 3, "seats": [{"seat": 0, "machine": 5, "playerId": "p1"}]}})";

bool Says(const std::string& text, const std::string& part)
{
  Options o;
  const std::string problem = ParseLaunchFile(text, &o).value_or("");
  if (problem.find(part) == std::string::npos)
    std::printf("refused with: \"%s\", expected: %s\n", problem.c_str(), part.c_str());
  return problem.find(part) != std::string::npos;
}
}  // namespace

void TestTheLatencyTestSettings()
{
  const std::string race = std::string(R"x({"about": "a race", "bundle": "b", "match": {)x") + MATCH +
                           R"(, "relayHost": "127.0.0.1", "delay": )";
  Options o;
  // as players have it: a console's moment, the delay of 2, no test setting; the log says both in plain lines
  CHECK(!ParseLaunchFile(race + "2}}", &o) && o.present == "console" && o.delay == PLAYERS_INPUT_DELAY && !o.vertical_sync);
  CHECK(LatencySettingsOn(o).empty() && DebugSettingsOn(o.debug).empty());
  std::vector<std::string> said = PlayersSettingsSaid(o);
  CHECK(said.size() == 2 && said[0].find("presentation is \"console\"") == 0 &&
        said[0].find("58 half-lines after the field's start") != std::string::npos &&
        said[1].find("input delay is 2 (the player's setting") != std::string::npos);
  CHECK(!ParseLaunchFile(R"({"about":"Piconet Launcher: offline play","showFrameRate":false,"present":"console"})", &o));
  CHECK(o.present == "console" && LatencySettingsOn(o).empty());
  // a plain start has no race and no delay to speak of
  said = PlayersSettingsSaid(o);
  CHECK(said.size() == 1 && said[0].find("presentation is \"console\"") == 0);
  // the two ways kept for comparison, in any start (GameWindowTests.cs pins the launcher's text): each a test
  // setting, by name
  for (const char* mode : {"field", "copy"})
  {
    CHECK(!ParseLaunchFile(std::string(R"({"about":"Piconet Launcher: offline play","bundle":"b","windowName":"Piconet","showFrameRate":false,"present":")") +
                           mode + "\"}", &o));
    CHECK(o.present == mode && DebugSettingsOn(o.debug).empty() && PlayersSettingsSaid(o).empty());
    const std::vector<std::string> on = LatencySettingsOn(o);
    CHECK(on.size() == 1 && on[0].find(std::string("presentation is \"") + mode + "\"") == 0 &&
          on[0].find("\"present\"") != std::string::npos);
  }
  CHECK(Says(R"({"about": "x", "present": true})", "\"present\""));
  CHECK(Says(R"({"about": "x", "present": "early"})", "\"present\""));
  CHECK(Says(race + R"(2}, "debug": {"present": "console"}})", "\"debug.present\" is not a setting of a launch file"));
  // a race's own delay: 0, 1 or 2, said as the player's setting and never as a test setting
  for (const u32 delay : {0u, 1u, 2u})
  {
    CHECK(!ParseLaunchFile(race + std::to_string(delay) + "}}", &o) && o.delay == delay);
    CHECK(LatencySettingsOn(o).empty());
    said = PlayersSettingsSaid(o);
    CHECK(said.size() == 2 && said[1].find("input delay is " + std::to_string(delay) + " (the player's setting") != std::string::npos);
  }
  // never above 2, whoever wrote the file: refused by the key's name, and the game does not start
  for (const char* more : {"3", "60", "61", "-1", "1.5", "\"2\""})
    CHECK(Says(race + more + "}}", "\"match.delay\""));
  CHECK(!ParseLaunchFile(race + "0}, \"present\": \"field\"}", &o) && LatencySettingsOn(o).size() == 1 &&
        PlayersSettingsSaid(o).size() == 1);
  // full screen: the borderless window unless the file says "exclusive"; no other word
  CHECK(!ParseLaunchFile(R"({"about": "x"})", &o) && o.full_screen == "borderless");
  CHECK(!ParseLaunchFile(R"({"about": "x", "fullScreen": "exclusive"})", &o) && o.full_screen == "exclusive");
  CHECK(!ParseLaunchFile(R"({"about": "x", "fullScreen": "borderless"})", &o) && o.full_screen == "borderless");
  CHECK(Says(R"({"about": "x", "fullScreen": "Exclusive"})", "\"fullScreen\" is not one of borderless, exclusive"));
  CHECK(Says(R"({"about": "x", "fullScreen": true})", "\"fullScreen\""));
  // a file's name for the log, without the folders it is in (a player's profile among them)
  CHECK(FileNameAlone("C:\\Users\\Somebody Made Up\\AppData\\Local\\fzgx-launcher\\recomp-user\\launch.json") == "launch.json");
  CHECK(FileNameAlone("/home/somebody/data/recomp-user/launch.json") == "launch.json" && FileNameAlone("launch.json") == "launch.json" &&
        FileNameAlone("D:\\copy/data\\x.json") == "x.json" && FileNameAlone("").empty() && FileNameAlone("folder\\").empty());
  // vertical sync: true or false, in any start
  CHECK(!ParseLaunchFile(R"({"about": "x", "verticalSync": true})", &o) && o.vertical_sync);
  CHECK(!ParseLaunchFile(R"({"about": "x", "verticalSync": false})", &o) && !o.vertical_sync);
  CHECK(Says(R"({"about": "x", "verticalSync": "on"})", "\"verticalSync\""));
  CHECK(Says(race + R"(2}, "debug": {"verticalSync": true}})", "\"debug.verticalSync\" is not a setting of a launch file"));
}

void TestTheLanesSettings();  // test_launch_lanes.cpp
void TestTrueWidescreen();    // test_launch_widescreen.cpp

// The launch file's keys a player's start may hold beside the race's: presentation, delay, lanes (0), true widescreen.
void TestTheLaunchFilesPlayersKeys()
{
  TestTheLatencyTestSettings();
  TestTheLanesSettings();
  TestTrueWidescreen();
}
