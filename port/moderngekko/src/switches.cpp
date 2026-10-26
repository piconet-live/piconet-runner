// The adapter's arguments and the settings of a start. The runner hands the adapter every argument it does not know;
// the adapter takes two for a game, `--controllers USER_FOLDER`, which starts none (the controller helper,
// controllers.h: the program ends in it), and `--replay-info FILE`, which starts none either: it prints what a replay
// file holds as one JSON object (fzgx/replay_info.h) and ends, 0 or, for a file that is no replay, 1. `--config FILE` names the launch file: every setting of this start (the
// race or the replay, the bundle, the test settings) is in it, read by the core's reader
// (port/core/src/files_launch.cpp) into the one settings struct, fzgx::Options, here and nowhere else.
// `--fzgx-log FILE` is no setting of a start: it says where the runner's own output goes from the first line on, the
// launch file's refusal included.
// The adapter reads no environment variable, and in a start with a launch file neither does the emulator's side for
// any test setting of its own (the pad script, the pokes, the probe, the logs, the CPU core's switches, the JIT):
// naming the launch file tells it so (RunSettings::RuleByLaunchFile, forks/recompcore/2-port), and what the file's
// "debug" part names is handed to it here. An option --fzgx-* that has gone is refused, and the game does not
// start: a script that still passes one must not get a game with that setting silently off. A start with no launch
// file at all is refused too (RefusedStart).
// SPDX-License-Identifier: GPL-3.0-or-later

#include "adapter.h"
#include "adapter_internal.h"
#include "controllers.h"
#include "fzgx/replay_info.h"

#include <cstdio>
#include <cstdlib>
#include <string>

#include "Common/RunSettings.h"

namespace fzgx_port
{
const char* Usage()
{
  return "       [--config <launch file: this start's settings, JSON (protocol/formats.md section 2)>]\n"
         "       [--fzgx-log <file for the runner's standard output and error; the 19 runs before are kept>]\n"
         "       [--controllers <user folder>: no game; the launcher's Controller page talks to it (controllers.h)]\n"
         "       [--replay-info <replay file>: no game; what the file holds, as JSON (fzgx/replay_info.h)]\n";
}

bool TakeArgument(const std::string& argument, const std::function<const char*()>& value)
{
  if (argument == "--config")
  {
    // The argument is in the system's code page (the runner's own options, --fzgx-log and the user folder are
    // read as that); the core opens a file by its UTF-8 path, as a launch file's own texts name theirs.
    const auto path = std::filesystem::path(value()).u8string();
    s_state.launch_file.assign(path.begin(), path.end());
    // From here on the emulator's side has the test settings this file gives it and no others: what the
    // environment holds under their names is not asked.
    RunSettings::RuleByLaunchFile();
    return true;
  }
  if (argument == "--fzgx-log")
  {
    WriteOutputTo(std::filesystem::path(value()));  // if the file cannot be made, output stays where it was
    return true;
  }
  if (argument == "--controllers")
    RunControllerHelper(std::filesystem::path(value()));  // no game is started: the program ends in there
  if (argument == "--replay-info")
  {
    // no game is started: the file's parts and its header, printed, and the program ends
    const auto path = std::filesystem::path(value()).u8string();
    std::string json, error;
    const bool read = fzgx::ReplayInfo(std::string(path.begin(), path.end()), &json, &error);
    std::fprintf(read ? stdout : stderr, "%s\n", read ? json.c_str() : error.c_str());
    std::fflush(nullptr);
    std::exit(read ? 0 : 1);
  }
  if (argument.rfind("--fzgx-", 0) == 0)
  {
    std::fprintf(stderr, "%s is no option of the runner any more: a start's settings are in its launch file (--config FILE)\n",
                 argument.c_str());
    std::exit(2);
  }
  return false;
}

bool InUse()
{
  return !s_state.launch_file.empty();
}

bool LaunchFileGiven()
{
  return InUse();
}

std::optional<std::string> RefusedStart()
{
  if (InUse())
    return std::nullopt;
  return "this runner starts a game only from a launch file: give --config FILE (the launcher writes one for every "
         "game it starts; protocol/formats.md section 2 says what a launch file holds)";
}

// Each test setting of the emulator's side that the launch file's "debug" part names, under the name the emulator
// knows it by (the core's table, files_launch.cpp); "jit" is the runtime's own name for its choice of CPU core.
void GiveEmulatorSettings()
{
  const fzgx::Debug& debug = s_state.options.debug;
  for (const fzgx::EmulatorSetting& setting : debug.emulator)
    RunSettings::Set(setting.name, setting.value);
  if (debug.jit)
    RunSettings::Set("MODERNGEKKO_STATICRECOMP", "0");
  // the user folder's settings file for the game (GameSettings/GFZE01.ini: settings that win over the main ones,
  // Gecko and Action Replay codes, every-frame memory patches): read only when "debug.cheats" says so. On the same
  // word: the patches and codes of the system folder's file for the game (Sys/GameSettings beside the runner), and
  // the user folder's map file (forks/recompcore/2-port: RunSettings::FilesMayPatch). Without it none patches the game.
  if (debug.cheats)
    RunSettings::Set("FZGX_USER_GAME_SETTINGS", "1");
}

std::optional<std::string> ReadSettings()
{
  return fzgx::ReadLaunchFile(s_state.launch_file, &s_state.options);
}

// For the log: which file this start's settings came from, and every test setting that is on (a player's start
// has none). The file by its name alone: a player's data folder is under their profile, whose path holds their
// name, and players send and post this log.
void ReportSettings()
{
  const fzgx::Options& o = s_state.options;
  const std::string file = fzgx::FileNameAlone(o.launch_file);
  std::string on;
  for (const std::string& key : fzgx::DebugSettingsOn(o.debug))
    on += (on.empty() ? "" : ", ") + key;
  if (on.empty())
    s_core_host.Log(fzgx::LogLevel::Notice, "fzgx: launch file " + file + " (" + o.about + "): no test setting is on");
  else
    s_core_host.Log(fzgx::LogLevel::Warning, "fzgx: TEST: launch file " + file + " (" + o.about + ") turns on: " + on);
  // how this start shows a picture and, for a race, the input delay its player chose: a line each
  for (const std::string& setting : fzgx::PlayersSettingsSaid(o))
    s_core_host.Log(fzgx::LogLevel::Notice, "fzgx: " + setting);
  // the two ways of showing a picture that are kept for comparison, which are no part of "debug" (a launcher's
  // settings file names them): each by name
  for (const std::string& setting : fzgx::LatencySettingsOn(o))
    s_core_host.Log(fzgx::LogLevel::Warning, "fzgx: TEST SETTING: " + setting);
  if (o.replay_scrubber)
    s_core_host.Log(fzgx::LogLevel::Notice, "fzgx: replay scrubber: a bar under the picture; with the game's window in "
                                            "front Space pauses and plays, Left and Right jump five seconds, Home goes "
                                            "to the race's start, End to the newest step buffered, . and , step one "
                                            "frame, [ and ] change the racer (the race view); the mouse held on the "
                                            "bar scrubs, a click jumps");
  if (o.controller_setup)
    s_core_host.Log(fzgx::LogLevel::Notice, "fzgx: controller setup: this start opens the game's own controller options once "
                                            "the title stands, and closes the game when the player leaves them");
}
}  // namespace fzgx_port
