// Aurora as the presenter (the launch file's "debug.renderer": "aurora"): the emulator's side is VideoCommon's
// AuroraPresent, which hands Aurora (GXRuntime's aurora-present library, beside the runner) a copy of the command
// stream the GPU thread reads and shows Aurora's pictures in place of the emulator's own. Presentation only: the
// emulator's GPU still runs every command, so the race is the same whatever Aurora does. Here: its settings from the
// launch file, its data folder made inside the runner's own (the user folder's Aurora, never the profile's), and its
// line at the stop.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "adapter_internal.h"

#include <filesystem>
#include <system_error>

#include "VideoCommon/AuroraPresent.h"

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
}  // namespace

void StartAurora()
{
  const fzgx::Debug& d = s_state.options.debug;
  if (d.renderer != "aurora")
    return;
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
  VideoCommon::AuroraPresent::Configure(s);
  s_core_host.Log(fzgx::LogLevel::Notice,
                  std::string("fzgx: aurora: ") +
                      (VideoCommon::AuroraPresent::On() ? "Aurora draws the pictures shown (presentation only)" :
                                                          "the library is missing: the emulator's own pictures") +
                      ", data folder " + Utf8(folder));
}

void StopAurora()
{
  const std::string line = VideoCommon::AuroraPresent::Report();
  VideoCommon::AuroraPresent::Configure({});
  if (!line.empty())
    s_core_host.Log(fzgx::LogLevel::Notice, "fzgx: " + line);
}
}  // namespace fzgx_port
