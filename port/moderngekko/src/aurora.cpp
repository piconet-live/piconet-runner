// Aurora as the presenter (the launch file's "renderer", or its test key "debug.renderer": "aurora"): the emulator's side is VideoCommon's
// AuroraPresent, which hands Aurora (GXRuntime's aurora-present library, beside the runner) a copy of the command
// stream the GPU thread reads and shows Aurora's pictures in place of the emulator's own. Presentation only: the
// emulator's GPU still runs every command, so the race is the same whatever Aurora does. Here: its settings from the
// launch file, its data folder made inside the runner's own (the user folder's Aurora, never the profile's), and its
// line at the stop.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "adapter_internal.h"

#include <filesystem>
#include <system_error>

#include "Common/Config/Config.h"
#include "Core/Config/MainSettings.h"
#include "VideoCommon/AuroraPresent.h"

#include <cstdlib>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fzgx_port
{
namespace
{
std::filesystem::path RunnerFolder()
{
#ifdef _WIN32
  wchar_t program[MAX_PATH * 4] = {};
  GetModuleFileNameW(nullptr, program, static_cast<DWORD>(std::size(program) - 1));
  return std::filesystem::path(program).parent_path();
#else
  std::error_code e;
  return std::filesystem::read_symlink("/proc/self/exe", e).parent_path();
#endif
}

std::string Utf8(const std::filesystem::path& p)
{
  const std::u8string s = p.u8string();
  return std::string(s.begin(), s.end());
}

// One of Aurora's switches, which the library reads for itself once it starts
void SetSwitch(const char* name, const std::string& value)
{
#ifdef _WIN32
  _putenv_s(name, value.c_str());
#else
  setenv(name, value.c_str(), 1);
#endif
}

// The player's settings in the launch file. A key left out leaves its switch alone, so a test's own switches stay.
void SetSwitches(const fzgx::Options& o)
{
  if (o.aurora_edges >= 4)  // (1 or 4 are the only counts every graphics card has)
    SetSwitch("AURORA_MSAA", "4");
  if (o.aurora_textures > 1)
  {
    SetSwitch("AURORA_ANISO", std::to_string(o.aurora_textures));
    SetSwitch("AURORA_ANISO_GX", "0");
    SetSwitch("AURORA_ANISO_ALL", "1");
  }
  if (!o.aurora_colourblind.empty() && o.aurora_colourblind != "off")
    SetSwitch("AURORA_COLOURBLIND", o.aurora_colourblind);
  if (o.aurora_high_refresh != 0)
    SetSwitch("AURORA_RUNAHEAD", std::to_string(o.aurora_high_refresh));
}

u32 AskedScale(const fzgx::Options& o)
{
  if (o.debug.aurora_scale != 0)
    return o.debug.aurora_scale;
  return o.aurora_size == "2" ? 2 : o.aurora_size == "3" ? 3 : o.aurora_size == "4" ? 4 : 0;
}
}  // namespace

void StartAurora()
{
  const fzgx::Options& o = s_state.options;
  const fzgx::Debug& d = o.debug;
  if (d.renderer != "aurora" && o.picture_renderer != "aurora")
    return;
  // The player's key means the emulator's video is off; the test key leaves it to the start's --graphics
  if (o.picture_renderer == "aurora")
    Config::SetCurrent(Config::MAIN_GFX_BACKEND, std::string("Null"));
  SetSwitches(o);
  // Aurora's caches (pipelines, Dawn's) in the runner's own data folder, made before Aurora starts: Aurora does not
  // make a missing one and runs on with no caches at all
  const std::filesystem::path folder = s_state.user_folder / "Aurora";
  std::error_code e;
  std::filesystem::create_directories(folder, e);
  if (e || !std::filesystem::is_directory(folder))
  {
    s_core_host.Log(fzgx::LogLevel::Error, "fzgx: aurora: its data folder " + Utf8(folder) +
                                               " cannot be made (" + e.message() + "): the emulator's own pictures");
    return;
  }
  VideoCommon::AuroraPresent::Settings s;
  s.on = true;
  s.library = Utf8(RunnerFolder() / "aurora-present.dll");
  s.data_dir = Utf8(folder);
  s.shot_prefix = d.aurora_log;
  s.shots.assign(d.aurora_shots.begin(), d.aurora_shots.end());
  s.log_path = d.aurora_log;
  // Into the window (the player's key) the picture is the window's own pixels and follows it, unless a scale is asked
  // for. The test key's Aurora, beside the emulator's video, draws at the emulator's internal resolution unless asked.
  const u32 asked_scale = AskedScale(o);
  s.scale = asked_scale;
  const bool window_size = asked_scale == 0 && o.picture_renderer == "aurora";
  if (window_size)
    SetSwitch("AURORA_NATIVE", "1");
  VideoCommon::AuroraPresent::Configure(s);
  s_core_host.Log(fzgx::LogLevel::Notice,
                  std::string("fzgx: aurora: ") +
                      (VideoCommon::AuroraPresent::On() ? "Aurora draws the pictures shown (presentation only)" :
                                                          "the library is missing: the emulator's own pictures") +
                      ", data folder " + Utf8(folder) +
                      (window_size ? ", picture size: the window's pixels" :
                       asked_scale != 0 ? ", picture size: " + std::to_string(asked_scale) + " times the game's" :
                                          ", picture size: the emulator's"));
}

void StopAurora()
{
  const std::string line = VideoCommon::AuroraPresent::Report();
  VideoCommon::AuroraPresent::Configure({});
  if (!line.empty())
    s_core_host.Log(fzgx::LogLevel::Notice, "fzgx: " + line);
}
}  // namespace fzgx_port
