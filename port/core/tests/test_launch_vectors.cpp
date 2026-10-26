// Tests of the launch file's reader (files_launch.cpp) on the files the launcher writes: the shared samples of
// protocol/tests/vectors/launch (the description: protocol/launch.toml), and a few written by hand.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "harness.h"
#include "parts.h"
#include "picojson.h"

#include <filesystem>
#include <fstream>
#include <iterator>

namespace
{
const char* const MATCH = R"("match": {"matchId": "m7", "seat": 1, "token": "1f2e3d4c5b6a7988", "futureMember": 1,
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

// The launch files the launcher writes for the games it starts, the shared samples (protocol/tests/vectors/launch,
// which the launcher's LaunchVectorsTests write byte for byte from GameLaunchFile): the reader takes each, each
// member lands where it goes, and the test settings a launcher hands on arrive as the game's; and what it does
// with the files somebody wrote by hand.
void TestALaunchFileOfTheLaunchers()
{
  const std::filesystem::path folder = ProtocolFile("protocol/tests/vectors/launch");
  int files = 0, races = 0, playbacks = 0, windows = 0, with_debug = 0;
  for (const auto& entry : std::filesystem::directory_iterator(folder))
  {
    if (entry.path().extension() != ".json")
      continue;
    std::ifstream in(entry.path(), std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    picojson::value root;
    CHECK(picojson::parse(root, text).empty() && root.is<picojson::object>());
    const picojson::object& file = root.get<picojson::object>();
    const auto words = [](const picojson::object& o, const char* key) {
      return o.count(key) ? o.at(key).get<std::string>() : std::string();
    };
    Options o;
    const std::optional<std::string> problem = ParseLaunchFile(text, &o);
    if (problem)
      std::printf("%s: %s\n", entry.path().filename().string().c_str(), problem->c_str());
    CHECK(!problem);
    ++files;
    CHECK(o.about == words(file, "about") && o.bundle_dir == words(file, "bundle") && o.board == words(file, "board"));
    CHECK(o.window_name == words(file, "windowName"));
    CHECK(o.widescreen == (file.count("widescreen") ? file.at("widescreen").get<bool>() : true));
    CHECK(o.show_frame_rate == (file.count("showFrameRate") && file.at("showFrameRate").get<bool>()));
    CHECK(o.rumble == !file.count("rumble"));
    if (file.count("match"))
    {
      ++races;
      const picojson::object& match = file.at("match").get<picojson::object>();
      CHECK(!o.match.empty() && o.replay_file == words(match, "record") && o.rejoin == (match.count("rejoin") != 0));
    }
    if (file.count("replay"))
    {
      ++playbacks;
      const picojson::object& replay = file.at("replay").get<picojson::object>();
      CHECK(o.play_file == words(replay, "file") && o.other_sounds == words(replay, "otherSounds") && o.replay_scrubber);
    }
    if (file.count("taskbar"))
    {
      ++windows;
      CHECK(o.taskbar.id == words(file.at("taskbar").get<picojson::object>(), "id"));
      CHECK(o.window_name_picture.width * o.window_name_picture.height * 4 == o.window_name_picture.rgba.size());
    }
    // the test settings a launcher hands on are the only ones on
    const std::size_t debug = file.count("debug") ? file.at("debug").get<picojson::object>().size() : 0;
    CHECK(DebugSettingsOn(o.debug).size() == debug);
    with_debug += debug != 0;
  }
  CHECK(files >= 19 && races >= 5 && playbacks >= 4 && windows >= 2 && with_debug >= 4);

  // and a launch file somebody wrote: a key that has gone, the test settings of a launcher started with a launch
  // file of its own, a match file with the old test fields
  Options o;
  const std::string race = std::string(R"x({"about": "Piconet Launcher: a race (match m7)", "bundle": "b", "widescreen": true,
    "match": {)x") + MATCH + R"(, "relayHost": "127.0.0.1", "delay": 2, "overclock": 6, "otherSounds": "quieter",
              "record": "race.fzrp"})";
  CHECK(!ParseLaunchFile(race + "}", &o));
  CHECK(o.replay_file == "race.fzrp" && o.board.empty() && o.cpu_clock == 6.0f && DebugSettingsOn(o.debug).empty());
  // the live race went to a UDP listener until the status board (0.3.0): the key is gone, and refused as unknown
  CHECK(ParseLaunchFile(race.substr(0, race.size() - 1) + R"(, "live": "127.0.0.1:47000"}})", &o)
            .value_or("").find("match.live") != std::string::npos);
  // a launcher started with a launch file of its own: "autopilot", "testLagMs" and the members of its "game"
  CHECK(!ParseLaunchFile(race + R"(, "debug": {"rollbackLog":"rb.csv","jitterMs":8,"autopilot":true,"testLagMs":50}})", &o));
  CHECK(o.debug.autopilot && o.debug.lag_ms == 50 && o.debug.jitter_ms == 8 && o.debug.rollback_log == "rb.csv" &&
        DebugSettingsOn(o.debug).size() == 4);
  // a match file somebody wrote with the old test fields goes into the "match" part as it is, and is refused there
  CHECK(Says(std::string(R"({"about": "x", "bundle": "b", "match": {)") + MATCH + R"(, "autopilot": true}})",
             "\"match.autopilot\" is a test setting"));
}
