// The F-Zero GX port's adapter: what ties the host-side core (port/core, fzgx::) to ModernGekko's runner. The runner
// hands it the arguments it does not know, and calls it at four places (forks/moderngekko/2-port). A start needs a
// launch file (--config FILE): without one the runner is told to refuse it (RefusedStart). One start takes no launch
// file and is the adapter's all the same: `--controllers USER_FOLDER`, the controller helper, which starts no game
// (controllers.h); the program ends inside the argument that names it, so that start is never asked for a launch file.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>

struct StaticRecompModuleSource;

namespace fzgx_port
{
// The runner's argument loop: true when `argument` is one of the adapter's two for a game (--config FILE, the launch
// file with every setting of this start; --fzgx-log FILE); `value` reads its value. `--controllers USER_FOLDER` does
// not come back: the controller helper runs and the program ends there. An --fzgx-* option that has gone ends the
// program with status 2.
bool TakeArgument(const std::string& argument, const std::function<const char*()>& value);
// The adapter's arguments, for the runner's usage text.
const char* Usage();

// A launch file was named: the adapter is in use, and this start's settings are that file's alone.
bool LaunchFileGiven();

// Once the arguments are read: the sentence a start with no launch file is refused with (the runner prints it and
// ends with status 2), or nothing when one was named. A start without one would take its test settings from the
// environment and its emulator settings from whatever the user folder holds, with nothing the adapter decides.
std::optional<std::string> RefusedStart();

// What the runner's own options gave, as far as a launch file has a say in it.
struct RunnerOptions
{
  std::filesystem::path game_root;    // --game
  std::filesystem::path user_folder;  // --user-dir
  bool interpreter = false;           // --allow-interpreter
  bool mods_named = false;            // --mods DIR
  bool automation = false;            // --automation-dir DIR
  bool load_state = false;            // --load-state FILE
};

// After the arguments are read, before the runtime is created: reads the launch file, then its race's match or its
// replay, and the bundle, and hands the emulator's side the test settings the file's "debug" part names (the
// emulator reads none of them from the environment in a start with a launch file). Says what is wrong when the
// launch file is refused, when it does not allow an option the runner was given, or when its race cannot be set up
// (the runner then exits). The core's log lines go to the runner's standard error and to
// <user folder>/Logs/fzgx.log (started anew at each start): a game started by the launcher has no standard error
// anyone keeps.
std::optional<std::string> Prepare(const RunnerOptions& asked);

// After Prepare: whether the runner searches its mod folders (those named with --mods, and Mods beside the runner
// and in the user folder when --no-mods is not given).
bool SearchesMods();

// In Runtime::Create, after the runner's own settings and before the module source goes to the CPU core: the
// emulator settings the game and the race need, the per-frame tick, and the network hook as a host call chained in
// front of whatever `source` already holds (the mod loader's).
void BeforeBoot(StaticRecompModuleSource& source);

// After the emulated system has shut down: closes the session and finishes a recording.
void AfterStop();
}  // namespace fzgx_port
