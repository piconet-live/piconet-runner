// The pipeline build (`--build-pipelines`, with the game's own --game, --user-dir and --config): no game, only Aurora's
// compiles, so that no race ever compiles a shader. The launcher starts it in the background, with no console and no
// window, when the look's cache is not ready (its first start once the disc is set up, ray tracing turned on, and whenever
// the cache's stamp changes: Aurora, the graphics driver, the look or the mods), and blocks every start of the game until
// it is done (Launcher.Core PipelineBuild).
//
// The launch file is the offline game's, as the launcher writes it for that start: its Aurora settings (the look: ray
// tracing or not; the picture's edges) set Aurora's switches exactly as a game's start sets them (aurora.cpp), so the keys
// are a game's, and its "overlay" names the mods' files (every one the offline game would take: a race takes fewer, never
// others). Every file of the extracted disc that might hold models (as the emulator's disc monitor tells them,
// VideoCommon FileMonitor.cpp) and every mod file goes to Aurora's planner as the disc walk's (one form of every shader),
// on every core at below normal priority; what is left is Dawn's cache in <user folder>/Aurora (aurora_present.h,
// aurora_present_build_start). Aurora's device is on a window that is never shown and can take no focus.
//
// Standard output: one JSON object a line, for the launcher (other lines may come before them: the runner's own):
//   {"progress": 0.42}                              about every half second, 0 to 1, never going back
//   {"done": true, "shaders": N, "seconds": S}      the build is done; exit status 0
//   {"failed": "why"}                               exit status 1 (Aurora could not start, no Aurora, a refused file)
// Standard error, Aurora's lines and the runner's: <user folder>/Logs/pipelines.log (the build before kept as pipelines.1.log).
// The build ends when its standard input closes (the launcher went away), with status 3: what was compiled stays.
// Exit status 4: a fatal error of Aurora's.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "adapter.h"
#include "adapter_internal.h"

#include "fzgx/loglines.h"
#include "fzgx/mod_files.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif

namespace fzgx_port
{
namespace
{
// GXRuntime's gxruntime/aurora_present.h, as the library exports it (ABI 5; the emulator's AuroraPresent.cpp mirrors the rest)
constexpr std::uint32_t AURORA_PRESENT_ABI = 5;
struct AuroraBuildConfig
{
  std::uint32_t abi;
  const char* data_dir;
};
struct AuroraBuildProgress
{
  std::uint32_t state;  // 0 starting, 1 compiling, 2 done, 3 failed
  std::uint64_t bytes_read, compiled, waiting, shaders;
};

bool s_asked = false;

[[noreturn]] void Say(int status, const std::string& json)
{
  std::fprintf(stdout, "%s\n", json.c_str());
  std::fflush(nullptr);
  std::_Exit(status);
}

std::string Quoted(const std::string& text)
{
  std::string out = "\"";
  for (const char c : text)
  {
    if (c == '"' || c == '\\')
      out += '\\';
    out += static_cast<unsigned char>(c) < 0x20 ? ' ' : c;
  }
  return out + "\"";
}

[[noreturn]] void Failed(const std::string& why)
{
  std::fprintf(stderr, "fzgx: pipeline build: %s\n", why.c_str());
  Say(1, "{\"failed\": " + Quoted(why) + "}");
}

// What might hold models, by the file's first bytes (the emulator's disc monitor's test, FileMonitor.cpp MightHoldModels): LZ
// (its packed size says the file's size), a U8 archive, or a model file's table. The course files go by their name.
bool MightHoldModels(const std::vector<unsigned char>& head, std::uint64_t size)
{
  if (head.size() < 8)
    return false;
  const std::uint32_t le = head[0] | head[1] << 8 | head[2] << 16 | std::uint32_t(head[3]) << 24;
  const std::uint32_t be0 = std::uint32_t(head[0]) << 24 | head[1] << 16 | head[2] << 8 | head[3];
  const std::uint32_t be1 = std::uint32_t(head[4]) << 24 | head[5] << 16 | head[6] << 8 | head[7];
  if (std::uint64_t(le) + 8 == size || be0 == 0x55AA382D)
    return true;
  return be0 != 0 && be0 < 0x10000 && be1 >= 8 + std::uint64_t(be0) * 8 && be1 < size && be1 % 0x20 == 0;
}

struct Candidate
{
  std::string disc;               // the path under files/, with '/' (what Aurora is told)
  std::filesystem::path from;     // where its bytes are
  std::uint64_t size = 0;
};

bool ReadWhole(const std::filesystem::path& path, std::vector<unsigned char>* out, std::size_t most = SIZE_MAX)
{
  std::ifstream f(path, std::ios::binary);
  if (!f)
    return false;
  f.seekg(0, std::ios::end);
  const std::streamoff size = f.tellg();
  if (size < 0)
    return false;
  f.seekg(0);
  out->resize(static_cast<std::size_t>(std::min<std::uint64_t>(static_cast<std::uint64_t>(size), most)));
  return out->empty() || static_cast<bool>(f.read(reinterpret_cast<char*>(out->data()), static_cast<std::streamsize>(out->size())));
}

std::string Slashed(const std::filesystem::path& relative)
{
  const std::u8string s = relative.generic_u8string();
  return std::string(s.begin(), s.end());
}

// The disc's files that might hold models, then the mods' (each as the disc path it replaces: a mod's model is planned as
// its own file; the disc's own stays planned too, for the starts that do not take the mod's)
std::vector<Candidate> Files(const std::filesystem::path& game_root, const std::vector<fzgx::OverlayFile>& overlay)
{
  std::vector<Candidate> out;
  const std::filesystem::path files = game_root / "files";
  std::error_code e;
  std::vector<std::filesystem::path> found;
  for (std::filesystem::recursive_directory_iterator it(files, e), end; !e && it != end; it.increment(e))
    if (it->is_regular_file(e))
      found.push_back(it->path());
  std::sort(found.begin(), found.end());
  const auto add = [&out](std::string disc, const std::filesystem::path& from) {
    std::error_code e;
    const std::uint64_t size = std::filesystem::file_size(from, e);
    if (e || size < 16)
      return;
    std::vector<unsigned char> head;
    if (disc.find("COLI_COURSE") == std::string::npos && (!ReadWhole(from, &head, 8) || !MightHoldModels(head, size)))
      return;
    out.push_back({std::move(disc), from, size});
  };
  for (const std::filesystem::path& p : found)
    add(Slashed(p.lexically_relative(files)), p);
  const fzgx::mod_files::Plan plan = fzgx::mod_files::Choose(
      overlay, true, [&files](const std::string& disc) {
        std::error_code e;
        return std::filesystem::is_regular_file(files / std::filesystem::path(std::u8string(disc.begin(), disc.end())), e);
      },
      [](const std::string& from) {
        std::error_code e;
        return std::filesystem::is_regular_file(std::filesystem::path(std::u8string(from.begin(), from.end())), e);
      });
  for (const fzgx::OverlayFile& f : plan.files)
    add(f.disc, std::filesystem::path(std::u8string(f.from.begin(), f.from.end())));
  return out;
}

// Standard error into the user folder's log of the build: the runner's lines and Aurora's (the library writes to the
// process's standard error handle, so the handle is set too)
void ErrorsInto(const std::filesystem::path& asked)
{
  const std::filesystem::path file = fzgx::loglines::LogFileToWrite(asked, 1);
#ifdef _WIN32
  if (!_wfreopen(file.c_str(), L"w", stderr))
    return;
  SetStdHandle(STD_ERROR_HANDLE, reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(stderr))));
#else
  if (!std::freopen(file.c_str(), "w", stderr))
    return;
#endif
  std::setvbuf(stderr, nullptr, _IOLBF, 1 << 14);
}

template <typename F>
F Bound(void* library, const char* name)
{
#ifdef _WIN32
  return reinterpret_cast<F>(GetProcAddress(static_cast<HMODULE>(library), name));
#else
  return reinterpret_cast<F>(dlsym(library, name));
#endif
}
}  // namespace

void AskPipelineBuild()
{
  s_asked = true;
}

bool PipelineBuildAsked()
{
  return s_asked;
}

void RunPipelineBuild(const RunnerOptions& asked)
{
  const auto t0 = std::chrono::steady_clock::now();
  std::error_code e;
  std::filesystem::create_directories(asked.user_folder / "Logs", e);
  ErrorsInto(asked.user_folder / "Logs" / "pipelines.log");
#ifdef _WIN32
  SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);
#endif
  if (std::optional<std::string> problem = ReadSettings())
    Failed(*problem);
  const fzgx::Options& o = s_state.options;
  if (o.picture_renderer != "aurora")
    Failed("the launch file's renderer is not Aurora: nothing to build");
  SetAuroraSwitches(o);
  // every core compiles (the process is below normal priority: the PC stays the player's)
  if (std::getenv("AURORA_PIPELINE_THREADS") == nullptr)
  {
    const unsigned cores = std::max(1u, std::thread::hardware_concurrency());
#ifdef _WIN32
    _putenv_s("AURORA_PIPELINE_THREADS", std::to_string(cores).c_str());
#else
    setenv("AURORA_PIPELINE_THREADS", std::to_string(cores).c_str(), 1);
#endif
  }
  const std::filesystem::path folder = asked.user_folder / "Aurora";
  std::filesystem::create_directories(folder, e);
  if (!std::filesystem::is_directory(folder))
    Failed("Aurora's data folder cannot be made");
  const std::vector<Candidate> files = Files(asked.game_root, o.overlay);
  if (files.empty())
    Failed("the game folder has no model files");
  std::uint64_t total = 0;
  for (const Candidate& c : files)
    total += c.size;
  std::fprintf(stderr, "fzgx: pipeline build: %zu files, %.0f MB, %zu of the mods' files\n", files.size(), total / 1e6,
               static_cast<std::size_t>(std::count_if(files.begin(), files.end(), [&asked](const Candidate& c) {
                 return c.from.native().rfind((asked.game_root / "files").native(), 0) != 0;
               })));

  const std::filesystem::path library_path = RunnerFolder() / "aurora-present.dll";
#ifdef _WIN32
  void* library = LoadLibraryExW(library_path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
#else
  void* library = dlopen(library_path.c_str(), RTLD_NOW);
#endif
  if (library == nullptr)
    Failed("aurora-present.dll is not beside the runner");
  using Start = int (*)(const AuroraBuildConfig*);
  using Progress = void (*)(AuroraBuildProgress*);
  using Stop = void (*)();
  using ModelFile = void (*)(const char*, const std::uint8_t*, std::uint64_t, int);
  const auto start = Bound<Start>(library, "aurora_present_build_start");
  const auto progress = Bound<Progress>(library, "aurora_present_build_progress");
  const auto stop = Bound<Stop>(library, "aurora_present_build_stop");
  const auto model_file = Bound<ModelFile>(library, "aurora_present_model_file");
  if (!start || !progress || !stop || !model_file)
    Failed("this aurora-present.dll has no pipeline build");
  const std::u8string dir8 = folder.u8string();
  const std::string dir(dir8.begin(), dir8.end());
  const AuroraBuildConfig config{AURORA_PRESENT_ABI, dir.c_str()};
  if (!start(&config))
    Failed("Aurora's pipeline build did not start (another Aurora version?)");

  // The launcher went away (its end of standard input closed): the build ends, what was compiled stays
  std::atomic<bool> orphaned{false};
  std::thread([&orphaned] {
    char buffer[256];
    while (std::fread(buffer, 1, sizeof buffer, stdin) > 0)
    {
    }
    orphaned = true;
  }).detach();

  // The files, in the disc's order, on a thread of their own: Aurora holds this one back while it has enough to do
  std::atomic<bool> fed{false};
  std::thread feeder([&] {
    std::vector<unsigned char> bytes;
    for (const Candidate& c : files)
    {
      if (orphaned)
        break;
      if (ReadWhole(c.from, &bytes))
        model_file(c.disc.c_str(), bytes.data(), bytes.size(), 1);
    }
    model_file("", nullptr, 0, 1);  // the walk's end
    fed = true;
  });

  double shown = 0;
  AuroraBuildProgress p{};
  for (;;)
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    progress(&p);
    if (p.state == 3)
    {
      feeder.detach();
      Failed("Aurora could not start (pipelines.log says why)");
    }
    if (orphaned)
    {
      std::fprintf(stderr, "fzgx: pipeline build: the launcher went away; stopping\n");
      feeder.detach();
      stop();
      std::fflush(nullptr);
      std::_Exit(3);
    }
    if (p.state == 2 && fed)
      break;
    // the files read by Aurora's plan, which follows the compiles (it takes a file only while few wait): 99% at most until done
    const double read = total != 0 ? static_cast<double>(std::min(p.bytes_read, total)) / static_cast<double>(total) : 0.0;
    const double now = std::min(0.99, read * 0.99);
    if (now > shown + 0.001)
    {
      shown = now;
      std::fprintf(stdout, "{\"progress\": %.3f}\n", shown);
      std::fflush(stdout);
    }
  }
  feeder.join();
  stop();
  const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  std::fprintf(stderr, "fzgx: pipeline build: done, %llu shaders, %llu pipelines, %.0f s\n", static_cast<unsigned long long>(p.shaders),
               static_cast<unsigned long long>(p.compiled), seconds);
  std::fprintf(stdout, "{\"progress\": 1}\n");
  Say(0, "{\"done\": true, \"shaders\": " + std::to_string(p.shaders) + ", \"seconds\": " + std::to_string(static_cast<long long>(seconds)) + "}");
}
}  // namespace fzgx_port
