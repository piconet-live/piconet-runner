// The three calls from the runner, the core's host on ModernGekko's emulator core (fzgx/host.h), and every file the
// adapter opens itself: the log file, the game folder's executable and the game's files, the native module. No other
// file of the adapter opens one (scripts/structure_check.py --explain 11).
// SPDX-License-Identifier: GPL-3.0-or-later

#include "adapter.h"
#include "adapter_internal.h"
#include "lanes.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <fstream>
#include <future>
#include <iterator>
#include <string>
#include <vector>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "Common/Config/Config.h"
#include "Core/Config/MainSettings.h"
#include "Core/HW/EXI/EXI.h"
#include "Core/HW/EXI/EXI_Device.h"
#include "Core/HW/EXI/EXI_DeviceMemoryCard.h"
#include "Core/HW/Memmap.h"
#include "Core/Host.h"
#include "Core/HostHooks.h"
#include "Core/PowerPC/JitInterface.h"
#include "Core/PowerPC/PowerPC.h"
#include "Core/PowerPC/StaticRecomp/StaticRecompModuleSource.h"
#include "Core/System.h"

#include "fzgx/loglines.h"

#ifdef FZGX_PORT_XXHASH
#include <xxhash.h>
#endif

namespace fzgx_port
{
State s_state;

namespace
{
// What the core asks of the program that runs the game (fzgx/host.h), from ModernGekko's emulator core.
class RuntimeHost final : public fzgx::Host
{
public:
  fzgx::u8* Ram() override { return Core::System::GetInstance().GetMemory().GetRAM(); }
  fzgx::u32 RamSize() override { return Core::System::GetInstance().GetMemory().GetRamSizeReal(); }
  // the locked data cache as the emulator holds it (0xE0000000, 256 KiB of which the game can reach 16)
  fzgx::u8* Scratch() override { return Core::System::GetInstance().GetMemory().GetL1Cache(); }
  fzgx::u32 ScratchSize() override { return Core::System::GetInstance().GetMemory().GetL1CacheSize(); }

  // (the core's own line, before this call, says which of its reasons it was)
  void RequestStop() override
  {
    NoteStop("the game asked for it itself (its own line above says why)", fzgx::board::StopReason::Game);
    Host_Message(HostMessageID::WMUserStop);
  }

  // (while the load's speed limit is lifted: the speed to go back to, which is what a session keeps as its own)
  float EmulationSpeed() override { return PacedSpeed(); }
  float CpuClock() override
  {
    return Config::Get(Config::MAIN_OVERCLOCK_ENABLE) ? Config::Get(Config::MAIN_OVERCLOCK) : 1.0f;
  }
  void SetPace(float emulation_speed, float cpu_clock) override
  {
    LiftLoadPace(false);  // (a pace of the race's own ends the load's, should it still stand)
    Config::SetCurrent(Config::MAIN_EMULATION_SPEED, emulation_speed);
    Config::SetCurrent(Config::MAIN_OVERCLOCK_ENABLE, cpu_clock != 1.0f);
    Config::SetCurrent(Config::MAIN_OVERCLOCK, cpu_clock);
  }

  // The undo log's guard may write-protect main memory's own mapping (port/core/src/page_guard.h) when every write into
  // it goes through that mapping, in user mode: under the native core with no fast memory arena (host.cpp,
  // ApplyGameSettings), the module's stores, the interpreter's and the fallback JIT's (the page mappings), and the
  // emulator's own copies into it (the disc's reads, DMA, the DSP, the memory card, the core's own) are all the
  // processor's writes through GetRAM(). Nothing here reads a file or a socket straight into it.
  bool RamGuardable(std::string* why) override
  {
    auto& system = Core::System::GetInstance();
    if (Config::Get(Config::MAIN_CPU_CORE) != PowerPC::CPUCore::StaticRecomp)
      *why = "the emulator's own compiler runs the game, through the fast memory's mappings";
    else if (system.GetMemory().GetPhysicalBase() != nullptr)
      *why = "the fast memory arena is mapped, and the fallback compiler writes through it";
    else
      return true;
    return false;
  }

#ifdef FZGX_PORT_XXHASH
  // The undo log's page hash (never sent anywhere; two hosts may use different ones).
  fzgx::u64 PageHash(const fzgx::u8* data, std::size_t size) override { return XXH3_64bits(data, size); }
#endif

  // A replay's header names what recorded it. The module's SHA-1 is worked out beside the boot (150 MB), only in a
  // start that records, and is long done when the race starts.
  void Recorder(std::string* engine, std::string* module_sha1) override;

  // The runner keeps no log file: the lines go to its standard error, which the launcher and the tests keep.
  void Log(fzgx::LogLevel level, const std::string& text) override
  {
    const char* mark = level == fzgx::LogLevel::Error ? "error: " : level == fzgx::LogLevel::Warning ? "warning: " : "";
    std::fprintf(stderr, "%s%s\n", mark, text.c_str());
    std::fflush(stderr);
    if (s_state.log)
    {
      std::fprintf(s_state.log, "%s%s\n", mark, text.c_str());
      std::fflush(s_state.log);
    }
  }

  // The core wrote words of the game's code (the native form's patch list): the CPU core's checks of the native
  // module's pieces there start over, and the fallback's blocks go. On the CPU thread (a host call, the frame hook).
  void CodeChanged(fzgx::u32 address, fzgx::u32 size) override
  {
    Core::System::GetInstance().GetJitInterface().InvalidateICache(address, size, true);
  }

  // The player's saves: slot A's memory card when it is a folder of GCI files, which is the emulator's stock
  // setting and what a user folder of the launcher's has (<user folder>/GC/USA/Card A). The emulator's own answer,
  // the one its memory card device takes (a path set in its settings, else the user folder's by the disc's
  // region), so the core reads the file the game itself would load. A card kept as one image, or no card: none.
  // Asked once, at a race's first frame.
  std::string CardFolder() override
  {
    using ExpansionInterface::Slot;
    if (Config::Get(Config::GetInfoForEXIDevice(Slot::A)) != ExpansionInterface::EXIDeviceType::MemoryCardFolder)
      return {};
    std::string folder = ExpansionInterface::CEXIMemoryCard::GetGCIFolderPath(
                             Slot::A, ExpansionInterface::AllowMovieFolder::No, Core::System::GetInstance().GetMovie())
                             .first;
    while (!folder.empty() && (folder.back() == '/' || folder.back() == '\\'))
      folder.pop_back();
    return folder;
  }

  // The course's load with no speed limit and no sound, and back (host.cpp).
  void LoadPace(bool lifted) override { LiftLoadPace(lifted); }

  // The panel before a race changed at the start gate (panel.cpp): a present of its own, queued.
  void PanelChanged() override { PresentPanel(); }

  // The replay scrubber (replay_bar.cpp, replay_host.cpp): its keys, a scrub on the bar, what the bar shows; its
  // undo log's packer; the process's memory for a playback's last lines.
  fzgx::u32 ReplayKeys() override { return TakeReplayKeys(); }
  fzgx::u32 ReplayScrub() override { return TakeReplayScrub(); }
  void ReplayBar(const fzgx::ScrubBar& bar) override { ShowReplayBar(bar); }
  // The spectator overlay (spectator_overlay.cpp).
  void ShowSpectator(const fzgx::SpectatorShow& show) override { ShowSpectatorOverlay(show); }
  fzgx::SpectatorClick SpectatorClicks() override { return TakeSpectatorClicks(); }
  void ReplayTimeline(const fzgx::Timeline& line) override { ShowReplayTimeline(line); }
  // Our course map (minimap.cpp).
  void MinimapCourseLoaded(const fzgx::MinimapCourse& course) override { fzgx_port::MinimapCourseLoaded(course); }
  void ShowMinimap(const fzgx::MinimapShow& show) override { fzgx_port::ShowMinimap(show); }
  bool Pack(const fzgx::u8* data, std::size_t size, const fzgx::u8* prefix, std::size_t prefix_size,
            std::vector<fzgx::u8>* out) override
  {
    return PackReplayLog(data, size, prefix, prefix_size, out);
  }
  bool Unpack(const fzgx::u8* data, std::size_t size, const fzgx::u8* prefix, std::size_t prefix_size, fzgx::u8* out,
              std::size_t out_size) override
  {
    return UnpackReplayLog(data, size, prefix, prefix_size, out, out_size);
  }
  bool ProcessMemory(fzgx::u64 out[4]) override { return ProcessMemoryNow(out); }
  bool ThreadClocks(fzgx::GameClocks* out) override { return GameThreadClocks(out); }

  // The times between the pictures handed to the screen since the core last asked (pictures.cpp).
  fzgx::u32 PictureIntervals(fzgx::u32* out, fzgx::u32 most) override { return TakePictureIntervals(out, most); }

  // The user folder the runner was started with (--user-dir): the launcher's recomp-user in the player's data
  // folder, where the runner's own log is too.
  std::string UserFolder() override
  {
    const std::u8string folder = s_state.user_folder.u8string();
    return std::string(folder.begin(), folder.end());
  }

  // The game's files are an extracted tree (<game>/files), not a disc image.
  bool ReadGameFile(const std::string& path, std::vector<fzgx::u8>* out) override
  {
    std::ifstream file(s_state.game_root / "files" / std::filesystem::path(path), std::ios::binary | std::ios::ate);
    if (!file)
      return false;
    out->resize(static_cast<std::size_t>(file.tellg()));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(out->data()), static_cast<std::streamsize>(out->size()));
    return static_cast<bool>(file);
  }
};

void RuntimeHost::Recorder(std::string* engine, std::string* module_sha1)
{
  *engine = s_state.engine;
  if (s_state.module_sha1.valid())
    *module_sha1 = s_state.module_sha1.get();
}

RuntimeHost s_runtime_host;
}  // namespace

fzgx::Host& s_core_host = s_runtime_host;

namespace
{
// --fzgx-log FILE (switches.cpp): the runner's standard output and standard error go to FILE from here on, for a game started with
// no console (the launcher's): what the CPU core says at start and shutdown, which pieces of the game failed their
// check, the slow-second lines, a crash's last words. The runner writes the file itself, so the game neither needs
// the launcher alive nor can block on a pipe nobody reads. The 19 runs before are kept beside it (FILE's name
// with .1 to .19 before its extension, .1 the latest): the last 20 games' logs, so that the log of a race that went
// wrong is still there after an evening's races (with three, it was gone after three more). Older ones go, so
// nothing grows without end. A log that cannot be moved aside is one a game still holds (its emulator never
// finished shutting down): it is left as it is, with its freeze report, and this start writes a file beside it
// (fzgx/loglines.h, LogFileToWrite, tested in the core).
constexpr int LOGS_KEPT = 19;
}  // namespace

bool WriteOutputTo(const std::filesystem::path& asked)
{
  const std::filesystem::path file = fzgx::loglines::LogFileToWrite(asked, LOGS_KEPT);
  std::fflush(stdout);
#ifdef _WIN32
  if (!_wfreopen(file.c_str(), L"w", stderr))
    return false;
  // One file, one position: standard output becomes the same open file, for the C library and for the system.
  // Started without a console it has no descriptor yet and is given one first.
  if (_fileno(stdout) < 0 && !_wfreopen(L"NUL", L"w", stdout))
    return false;
  if (_dup2(_fileno(stderr), _fileno(stdout)) != 0)
    return false;
  const HANDLE handle = reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(stderr)));
  SetStdHandle(STD_ERROR_HANDLE, handle);
  SetStdHandle(STD_OUTPUT_HANDLE, handle);
#else
  if (!std::freopen(file.c_str(), "w", stderr) || dup2(fileno(stderr), fileno(stdout)) < 0)
    return false;
#endif
  std::setvbuf(stderr, nullptr, _IONBF, 0);
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  return true;
}

namespace
{
// The native form: the executable that is booted must hold the patch list's words of the executable (the branch to
// the "main.rel is linked" call among them), which port/build/native-game.sh writes into the game folder's copy.
// A game folder made for another image branches to where that image had its routines: the game then stops at its
// first frames with nothing said (as with a bundle rebuilt and the folder not). Checked from the
// file before the boot; an empty text when it fits.
std::string ExecutableProblem(const std::filesystem::path& game_root)
{
  std::ifstream file(game_root / "sys" / "main.dol", std::ios::binary);
  std::vector<char> dol((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  if (dol.size() < game::DOL_FILE_HEADER_SIZE)
    return "the game folder has no executable (sys/main.dol)";
  const auto word = [&dol](std::size_t at) {
    return static_cast<u32>(static_cast<u8>(dol[at])) << 24 | static_cast<u32>(static_cast<u8>(dol[at + 1])) << 16 |
           static_cast<u32>(static_cast<u8>(dol[at + 2])) << 8 | static_cast<u32>(static_cast<u8>(dol[at + 3]));
  };
  for (const fzgx::Patch& p : fzgx::Patches())
  {
    if (p.address >= game::MAIN_REL)
      continue;
    bool found = false;
    for (std::size_t section = 0; section < game::DOL_FILE_SECTIONS && !found; ++section)  // 7 text and 11 data
    {
      const u32 offset = word(game::DOL_FILE_OFFSETS + section * 4), start = word(game::DOL_FILE_ADDRESSES + section * 4),
                size = word(game::DOL_FILE_SIZES + section * 4);
      if (size < 4 || p.address < start || p.address > start + size - 4 || offset + (p.address - start) + 4 > dol.size())
        continue;
      found = true;
      if (const u32 holds = word(offset + (p.address - start)); holds != p.replacement)
      {
        char text[240];
        std::snprintf(text, sizeof(text),
                      "the game folder's executable is not the one this online part was built for: %08X (%s) holds "
                      "%08X, not %08X. Make the game folder again from this bundle (port/build/native-game.sh)",
                      p.address, p.what.c_str(), holds, p.replacement);
        return text;
      }
    }
    if (!found)
      return "the game folder's executable has no section for a patched address";
  }
  return {};
}
}  // namespace

std::optional<std::string> Prepare(const RunnerOptions& asked)
{
  if (!InUse())
    return std::nullopt;
  const std::filesystem::path& game_root = asked.game_root;
  const std::filesystem::path& user_folder = asked.user_folder;
  s_state.game_root = game_root;
  s_state.user_folder = user_folder;
  std::error_code ec;
  std::filesystem::create_directories(user_folder / "Logs", ec);
  // (the log of the start before stays as fzgx.1.log; one a game still holds is not emptied: a file beside it)
  const std::filesystem::path log = fzgx::loglines::LogFileToWrite(user_folder / "Logs" / "fzgx.log", 1);
#ifdef _WIN32
  s_state.log = _wfopen(log.c_str(), L"w");
#else
  s_state.log = std::fopen(log.c_str(), "w");
#endif
  // every setting of this start: the launch file, or the game does not start
  if (std::optional<std::string> problem = ReadSettings())
    return problem;
  GiveEmulatorSettings();
  GiveWindowFrame();
  JoinTaskbarGroup();
  const fzgx::Options& o = s_state.options;
  // what the runner's options asked for and only the launch file's "debug" part allows
  fzgx::RunnerAsked wanted;
  wanted.interpreter = asked.interpreter;
  wanted.mods = asked.mods_named;
  wanted.automation = asked.automation;
  wanted.load_state = asked.load_state;
  if (std::optional<std::string> refused = fzgx::RefusedRunnerOption(o.debug, wanted))
    return refused;
  const bool race = !o.match.empty() || !o.play_file.empty();
  fzgx::Init(s_core_host, o);
  ReportSettings();
  if (race && !fzgx::Active())
    return "the race could not be set up from " + (o.match.empty() ? o.play_file : o.launch_file);
  // A saved state brings its own code (tests: "debug.forcePatches" writes the list over it); a boot runs the file's.
  if (fzgx::NativeForm() && !o.debug.force_patches)
  {
    if (std::string problem = ExecutableProblem(game_root); !problem.empty())
      return problem;
  }
  return std::nullopt;
}

// Without the launch file's "debug.mods" no mod folder is searched: not one named with --mods (refused above), nor
// the two the runner searches by itself when --no-mods is not given.
bool SearchesMods() { return !InUse() || s_state.options.debug.mods; }

namespace
{
// The native form's work at every field, on the game's thread (BeforeBoot says what each is for).
void NativeField()
{
  StartWatches();
  fzgx::board::Field();  // the launcher's status board: a plain start's heartbeat, the phase, the mailbox
  fzgx::EnsurePatched();
  FollowScreenOption();
  fzgx::ControllerSetupTick();
  PanelField();  // the load's progress, and the panel's meter moving (panel.cpp)
  RumbleField();
}
}  // namespace

void BeforeBoot(StaticRecompModuleSource& source)
{
  if (!InUse())
    return;
  // The CPU core is the one the runner chose from this start's settings: the native module's, or the emulator's JIT
  // when the launch file's "debug.jit" asks for the reference. Set as the run's own value, which wins over a
  // settings file that names another core.
  Config::SetCurrent(Config::MAIN_CPU_CORE, Config::GetBase(Config::MAIN_CPU_CORE));
  // what a replay recorded in this start says of its recorder: the CPU core the runner chose before this call (the
  // launch file's "debug.jit" is the JIT of the gates' reference), asked of the emulator's settings
  const bool native_code = source.kind != StaticRecompModuleSource::Kind::None &&
                           Config::Get(Config::MAIN_CPU_CORE) == PowerPC::CPUCore::StaticRecomp;
  s_state.engine = native_code ? "native" : "jit";
  lanes::Module(source.kind == StaticRecompModuleSource::Kind::DynamicPath ? source.path : std::string());
  if (native_code && source.kind == StaticRecompModuleSource::Kind::DynamicPath && !s_state.options.replay_file.empty())
  {
    s_state.module_sha1 = std::async(std::launch::async, [path = std::filesystem::path(source.path)] {
      std::ifstream file(path, std::ios::binary);
      const std::vector<char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
      return bytes.empty() ? std::string() : fzgx::Sha1(reinterpret_cast<const fzgx::u8*>(bytes.data()), bytes.size());
    });
  }
  ApplyGameSettings();
  StartDrawnFrames();  // (before ApplyPresentation: the pictures' holds ask whether frames are drawn)
  ApplyPresentation();
  WatchPictures();
  HostHooks::Hooks& hooks = HostHooks::Get();
  KeepCopiesUnderRollback(hooks);
  HoldRumble(hooks);
  s_state.native = fzgx::NativeForm();
  const bool race = fzgx::Active();
  if (s_state.native)
  {
    // The native form, in every start: the game's patched code is in the native module, so the data window, the
    // host calls and the patch list are needed for a plain start too (its mode word is 0, and every patched site
    // then does what the stock game does). The game runs on its retail 24 MiB: no code handler, no codes, no
    // memory override.
    RegisterWindow(hooks);
    TakeHostCalls(source);
    if (race)
      ApplyRaceSettings();
    else
      ShowPicture(false);  // a plain start: the logos are 4:3 pictures; the game's own option takes over
    // Every picture: the patch list again if a saved state brought stock words back; the picture's shape; and in a
    // plain start whose launch file asks for the game's controller options, the watch that opens them and stops
    // the game when the player leaves them (a plain start has no frame tick: its mode word is 0, and the mode
    // machine's routine then makes no host call). The frame tick itself is the host call from the mode machine's
    // routine, once a game frame on the main thread.
    hooks.frame = [](const Core::CPUThreadGuard&) { NativeField(); };
    char text[200];
    std::snprintf(text, sizeof(text), "fzgx: adapter on: native form, host calls at %08X-%08X, data window %08X-%08X, %s",
                  HOST_CALLS, HOST_CALLS_END, table::WINDOW, table::WINDOW_END,
                  race ? "a race or a replay" : "a plain start");
    s_core_host.Log(fzgx::LogLevel::Notice, text);
    return;
  }
  if (!race)
  {
    // A plain start: the logos are 4:3 pictures; the game's own option takes over when the save is read.
    ShowPicture(false);
    hooks.frame = [](const Core::CPUThreadGuard&) {
      StartWatches();
      fzgx::board::Field();
      FollowScreenOption();
      fzgx::ControllerSetupTick();
      RumbleField();
    };
    return;
  }
  ApplyRaceSettings();
  if (const std::vector<fzgx::Code> codes = fzgx::Codes(); !codes.empty())
    ApplyReferenceConfiguration(codes);
  hooks.frame = [](const Core::CPUThreadGuard&) {
    StartWatches();
    fzgx::board::Field();
    fzgx::FrameTick();
    FollowScreenOption();
    PanelField();
    RumbleField();
  };
  // A run on the JIT alone has no host calls: there the emulator's own hook stands at the address.
  hooks.call_address = NET_HOOK;
  hooks.call = [](const Core::CPUThreadGuard&) { fzgx::NetSync(); };
  TakeHostCalls(source);
  char text[200];
  std::snprintf(text, sizeof(text), "fzgx: adapter on: network hook at %08X by host call (the emulator's hook on a JIT run), %s",
                NET_HOOK, hooks.gecko_codes ? "reference configuration (codes, image in memory above 24 MiB)" : "no codes");
  s_core_host.Log(fzgx::LogLevel::Notice, text);
}

void AfterStop()
{
  if (!InUse())
    return;
  EndFreezeWatch();
  LeaveEmulatorLines();
  ReportCopiesKept();
  ReportRumble();
  if (s_state.native)
    ReportWindow(true);
  StopDrawnFrames();
  StopWatchingPictures();
  lanes::Stop();
  LiftLoadPace(false);  // (a game closed while it loaded: the settings are the process's, put back all the same)
  fzgx::MemoryGone();   // (Core::Shutdown has let the game's memory go: the undo log's guard touches none of it)
  fzgx::Shutdown();
  HostHooks::Get() = {};
  s_state.picture_wide = -1;
  if (s_state.log)
    std::fclose(s_state.log);
  s_state.log = nullptr;
  // An exit code of the core's own (fzgx::EXIT_RACE_OVER: started again into a race that is over;
  // fzgx::EXIT_LEFT_RACING: stopped by no stop of the game's own while its seat raced), which the runner's main,
  // returning 0 after a stop, has no word for. The emulator is stopped and shut down, the logs and the replay
  // closed: what is left after this call is the runtime's flags and main's return.
  if (const int code = fzgx::ExitCode(); code != 0)
  {
    std::fflush(nullptr);
    std::_Exit(code);
  }
}
}  // namespace fzgx_port
