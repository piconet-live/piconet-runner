// What a start's launch file says, for the log and the runner (files.h): the file's name alone, the settings of
// one kind that are on, and the runner's own options a launch file refuses. The reader itself: files_launch.cpp.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "files.h"

#include <optional>
#include <string>
#include <vector>

namespace fzgx
{
std::string FileNameAlone(const std::string& path)
{
  const std::size_t last = path.find_last_of("/\\");
  return last == std::string::npos ? path : path.substr(last + 1);
}

std::vector<std::string> LatencySettingsOn(const Options& o)
{
  std::vector<std::string> on;
  if (o.present == "field")
    on.push_back("presentation is \"field\" (the launch file's \"present\"): a picture goes to the screen when the "
                 "emulated video output reaches its first line, some milliseconds after a console puts it out, as up "
                 "to 0.2.0");
  else if (o.present == "copy")
    on.push_back("presentation is \"copy\" (the launch file's \"present\"): a picture goes to the screen when the game "
                 "copies it out of its frame buffer, sooner than a console puts it out");
  return on;
}

std::vector<std::string> PlayersSettingsSaid(const Options& o)
{
  std::vector<std::string> said;
  if (o.present == "console" && o.debug.single_core)
    said.push_back("presentation is \"field\" in this start, not \"console\": with \"debug.singleCore\" the picture "
                   "is drawn on the game's own thread, where a wait for a console's moment would hold the game "
                   "before its pad read");
  else if (o.present == "console")
    said.push_back("presentation is \"console\": a picture goes to the screen at the time of its first line on the "
                   "host's clock, 58 half-lines after the field's start, as a console puts it out");
  if (!o.rumble)
    said.push_back("the controllers do not rumble in this start (the player's setting; the launch file's \"rumble\")");
  if (!o.match.empty())
    said.push_back("this game's input delay is " + std::to_string(o.delay) + " (the player's setting, 0 to " +
                   std::to_string(PLAYERS_INPUT_DELAY) + "; the launch file's \"match.delay\")");
  return said;
}

std::optional<std::string> RefusedRunnerOption(const Debug& debug, const RunnerAsked& asked)
{
  const auto refused = [](const char* option, const char* key, const char* why) {
    return std::string(option) + " is refused: this start has a launch file, and its \"debug\" part does not say \"" +
           key + "\": true (" + why + ")";
  };
  if (asked.interpreter && !debug.jit)
    return refused("--allow-interpreter", "jit", "a game runs on the native module; the emulator's own compiler is the tests' reference");
  if (asked.mods && !debug.mods)
    return refused("--mods", "mods", "a mod is code the game calls");
  if (asked.automation && !debug.automation)
    return refused("--automation-dir", "automation", "its commands can press the controller's buttons");
  if (asked.load_state && !debug.load_state)
    return refused("--load-state", "loadState", "a saved state is the game's memory as somebody made it");
  return std::nullopt;
}

std::vector<std::string> DebugSettingsOn(const Debug& d)
{
  std::vector<std::string> on;
  const auto add = [&on](bool is_on, const char* key) {
    if (is_on)
      on.push_back(std::string("debug.") + key);
  };
  add(d.lag_ms != 0, "testLagMs");
  add(d.jitter_ms != 0, "jitterMs");
  add(d.autopilot, "autopilot");
  add(d.blackout_step != 0, "blackoutStep");
  add(d.force_every != 0, "forceRollback");
  add(!d.rollback_log.empty(), "rollbackLog");
  add(d.alter_step != 0, "alterStep");
  add(d.handback_at != 0, "handbackAt");
  add(d.clock != 0, "clock");
  add(d.force_patches, "forcePatches");
  add(d.single_core, "singleCore");
  add(d.drawn_frames != 0, "drawnFrames");
  add(d.redraw_replays != 0, "redraw");
  add(d.late_steps != 0, "lateSteps");
  add(!d.late_seats.empty(), "lateSeats");
  add(d.late_noise != 0, "lateNoise");
  add(!d.partial_mode.empty() || !d.partial_log.empty() || !d.partial_guard, "partialRollback");
  add(d.skip_draws != 0, "skipDraws");
  add(d.buffer_share != 40, "bufferShare");
  add(d.buffer_steps != 0, "bufferSteps");
  add(d.replay_memory != 512, "replayMemory");
  add(d.replay_near != 1024, "replayNear");
  add(!d.replay_script.empty(), "replayScript");
  add(d.replay_end_quits, "replayEndQuits");
  add(!d.trace_file.empty(), "trace");
  add(d.trace_to != 0, "traceStates");
  add(d.dump_step != 0, "dump");
  add(d.keep_scratch, "keepScratch");
  add(!d.undo_log.empty(), "undoLog");
  add(!d.undo_trace.empty(), "undoTrace");
  add(!d.stand_steps.empty(), "standStill");
  add(d.full_screen_without_focus, "fullScreenWithoutFocus");
  add(!d.relay.empty(), "relay (the core's own tests)");
  add(d.jit, "jit");
  add(d.renderer != "dolphin", "renderer");
  add(!d.aurora_shots.empty(), "auroraShots");
  add(!d.aurora_log.empty(), "auroraLog");
  add(d.cheats, "cheats");
  add(d.mods, "mods");
  add(d.automation, "automation");
  add(d.load_state, "loadState");
  add(d.user_settings, "userSettings");
  for (const EmulatorSetting& setting : d.emulator)
    add(true, setting.key.c_str());
  return on;
}
}  // namespace fzgx
