// The emulator's settings that decide what the game computes or how fast it runs, set by the runner.
// The emulator reads its settings from the user folder (Config/Dolphin.ini, Config/GFX.ini) and from the system
// folder's file for the game (Sys/GameSettings), and a player can edit all three. In a start with a launch file each
// setting below is set as the run's own value, which wins over every file: the value the game is built and tested
// with, which is the emulator's stock value unless a line says otherwise. Only the launch file's
// "debug.userSettings" leaves them to the files (the tests' user folders: no speed limit, a fixed clock).
// Not here, and the player's: the picture (resolution, the backend, enhancements, a borderless full screen in place
// of the backend's own: GFX.ini's BorderlessFullscreen), the sound (volume, mute, the
// backend, the mixer's latency and gap filling), the controllers (Config/GCPadNew.ini, which device is in each port
// and the adapter's rumble: [Core] SIDevice0 to 3, AdapterRumble0 to 3), the memory cards, the window. One thing of
// the controllers is not the player's in a race: the kind of device in port 1 (PinRaceController).
// Set elsewhere, for every start with a launch file: the CPU core (adapter.cpp), the picture's thread and cheats
// (host.cpp), and for a race the disc speed, the clock factor and the real-time clock (ApplyRaceSettings).
// SPDX-License-Identifier: GPL-3.0-or-later

#include "adapter_internal.h"

#include <string>
#include <vector>

#include "Common/Config/Config.h"
#include "Core/Config/GraphicsSettings.h"
#include "Core/Config/MainSettings.h"
#include "Core/Config/StaticRecompSettings.h"
#include "Core/HW/SI/SI_Device.h"

namespace fzgx_port
{
namespace
{
// The settings whose value in the user folder's files is another than the run's: named in the log, so that a player
// who changed one can read why nothing came of it.
std::vector<std::string> s_not_used;

// The run's value of a setting. `in_files` says which value of the files is no news: the run's own (the launcher
// writes it into the user folder, so anything else there is somebody's change), or also the emulator's stock value
// (a setting the emulator's own file for the game makes, which no user folder holds).
// ThePlayersOwn: a setting the player chose in the launcher, which reaches the game by its launch file and is
// written into no file of the user folder: whatever a file says of it is no news, and nothing is said.
enum class InFiles { TheRunsValue, OrTheStockValue, ThePlayersOwn };

template <typename T, typename V>
void Set(const Config::Info<T>& setting, const V& value, InFiles in_files = InFiles::TheRunsValue)
{
  const T wanted(value);
  const T had = Config::GetBase(setting);
  if (had != wanted && in_files != InFiles::ThePlayersOwn &&
      !(in_files == InFiles::OrTheStockValue && had == setting.GetDefaultValue()))
    s_not_used.push_back(setting.GetLocation().section + "." + setting.GetLocation().key);
  Config::SetCurrent(setting, wanted);
}

// The emulator's stock value.
template <typename T>
void Stock(const Config::Info<T>& setting)
{
  Set(setting, setting.GetDefaultValue());
}

// How fast the game runs: the speed limit, the emulated CPU's and the picture's clocks, the waits counted as idle
// time, how the emulator makes up for time it lost, and how the CPU's and the picture's threads keep step.
void PinPace()
{
  Set(Config::MAIN_EMULATION_SPEED, 1.0f);  // full speed, no more; a race's catch-up changes it for a while (SetPace)
  // the clock factor the launch file's race names (6 unless it says otherwise: re-simulation needs the headroom, and
  // every check of the native module ran at 6)
  const float clock = fzgx::Boot().cpu_clock;
  Set(Config::MAIN_OVERCLOCK_ENABLE, clock != 1.0f);
  Set(Config::MAIN_OVERCLOCK, clock);
  Stock(Config::MAIN_VI_OVERCLOCK_ENABLE);
  Stock(Config::MAIN_VI_OVERCLOCK);
  // the operating system's idle loop, which native code spins in unless it is told (the emulator's own compiler
  // skips it by itself); the game's three waits are given beside it (host.cpp)
  Set(Config::MAIN_STATICRECOMP_IDLE_PC, u32{game::IDLE_OS});
  Stock(Config::MAIN_STATICRECOMP_IDLE_PCS);
  Stock(Config::MAIN_TIMING_VARIANCE);
  Stock(Config::MAIN_CORRECT_TIME_DRIFT);
  Stock(Config::MAIN_SYNC_ON_SKIP_IDLE);
  Stock(Config::MAIN_SYNC_GPU);
  Stock(Config::MAIN_SYNC_GPU_MAX_DISTANCE);
  Stock(Config::MAIN_SYNC_GPU_MIN_DISTANCE);
  Stock(Config::MAIN_SYNC_GPU_OVERCLOCK);
  // an emulator that is behind by more than MaxFallback gives the lost time up, and with VISkip on it then leaves
  // out the picture's interrupt, which the game counts its fields by: a field the game never sees
  Stock(Config::MAIN_MAX_FALLBACK);
  Stock(Config::GFX_HACK_VI_SKIP);
  Stock(Config::MAIN_FAST_DISC_SPEED);  // a race sets it on after this
}

// What the emulated CPU computes: its floating point, its caches, its memory, and the parts of the emulator's own
// compiler that can be switched off one by one (it runs what the native module does not cover).
void PinCpu()
{
  // the emulator's own setting for the game: the result flags of floating-point work
  Set(Config::MAIN_FPRF, true, InFiles::OrTheStockValue);
  Stock(Config::MAIN_ACCURATE_NANS);
  Stock(Config::MAIN_ACCURATE_FMADDS);
  Stock(Config::MAIN_FLOAT_EXCEPTIONS);
  Stock(Config::MAIN_DIVIDE_BY_ZERO_EXCEPTIONS);
  Stock(Config::MAIN_DISABLE_ICACHE);
  Stock(Config::MAIN_ACCURATE_CPU_CACHE);
  Stock(Config::MAIN_LOW_DCBZ_HACK);
  Stock(Config::MAIN_MMU);
  Stock(Config::MAIN_FASTMEM);
  Stock(Config::MAIN_FASTMEM_ARENA);
  Stock(Config::MAIN_PAGE_TABLE_FASTMEM);
  Stock(Config::MAIN_LARGE_ENTRY_POINTS_MAP);
  Stock(Config::MAIN_JIT_FOLLOW_BRANCH);
  Stock(Config::MAIN_RAM_OVERRIDE_ENABLE);  // the reference configuration sets its own after this
  Stock(Config::MAIN_MEM1_SIZE);
  Stock(Config::MAIN_MEM2_SIZE);
  Stock(Config::MAIN_ARAM_EXPANSION_SIZE);
  Stock(Config::MAIN_DEBUG_JIT_OFF);
  Stock(Config::MAIN_DEBUG_JIT_LOAD_STORE_OFF);
  Stock(Config::MAIN_DEBUG_JIT_LOAD_STORE_LXZ_OFF);
  Stock(Config::MAIN_DEBUG_JIT_LOAD_STORE_LWZ_OFF);
  Stock(Config::MAIN_DEBUG_JIT_LOAD_STORE_LBZX_OFF);
  Stock(Config::MAIN_DEBUG_JIT_LOAD_STORE_FLOATING_OFF);
  Stock(Config::MAIN_DEBUG_JIT_LOAD_STORE_PAIRED_OFF);
  Stock(Config::MAIN_DEBUG_JIT_FLOATING_POINT_OFF);
  Stock(Config::MAIN_DEBUG_JIT_INTEGER_OFF);
  Stock(Config::MAIN_DEBUG_JIT_PAIRED_OFF);
  Stock(Config::MAIN_DEBUG_JIT_SYSTEM_REGISTERS_OFF);
  Stock(Config::MAIN_DEBUG_JIT_BRANCH_OFF);
  Stock(Config::MAIN_DEBUG_JIT_REGISTER_CACHE_OFF);
}

// The console around the CPU, and the doors a debugger comes in by.
void PinConsole()
{
  Stock(Config::MAIN_DSP_HLE);
  Stock(Config::MAIN_DSP_JIT);
  Stock(Config::MAIN_HSP_DEVICE);  // nothing in the high-speed port (an expansion of the sound memory would be one)
  Stock(Config::MAIN_SKIP_IPL);
  Stock(Config::MAIN_GC_LANGUAGE);
  Stock(Config::MAIN_OVERRIDE_REGION_SETTINGS);
  Stock(Config::MAIN_GPU_DETERMINISM_MODE);
  // a debugger at a network port or a socket reads and writes the game's memory and holds it still
  Stock(Config::MAIN_GDB_PORT);
  Stock(Config::MAIN_GDB_SOCKET);
  Stock(Config::MAIN_ENABLE_DEBUGGING);
  Stock(Config::MAIN_PAUSE_ON_PANIC);
  Stock(Config::MAIN_PAUSE_ON_FOCUS_LOST);
  // an alert of the emulator's is a line of its log and no message box: a box holds the game until somebody answers
  // it, while the race goes on for everybody else (the launcher writes the same into a new user folder)
  Set(Config::MAIN_USE_PANIC_HANDLERS, false);
}

// The picture's settings that reach back into what the game reads: copies of the frame buffer into its memory, its
// reads of the frame buffer and of the bounding box, the video mode it is given. How the picture looks is not here.
void PinPictureReadBack()
{
  Stock(Config::GFX_HACK_EFB_ACCESS_ENABLE);
  Stock(Config::GFX_HACK_EFB_DEFER_INVALIDATION);
  Set(Config::GFX_HACK_EFB_ACCESS_TILE_SIZE, 0, InFiles::OrTheStockValue);  // the emulator's own setting for the game
  Stock(Config::GFX_HACK_BBOX_ENABLE);
  Stock(Config::GFX_HACK_FORCE_PROGRESSIVE);
  Stock(Config::GFX_HACK_SKIP_EFB_COPY_TO_RAM);
  Stock(Config::GFX_HACK_SKIP_XFB_COPY_TO_RAM);
  Stock(Config::GFX_HACK_DISABLE_COPY_TO_VRAM);
  Stock(Config::GFX_HACK_DEFER_EFB_COPIES);
  Stock(Config::GFX_HACK_IMMEDIATE_XFB);
  Stock(Config::GFX_HACK_EFB_EMULATE_FORMAT_CHANGES);
  Stock(Config::GFX_SAFE_TEXTURE_CACHE_COLOR_SAMPLES);
  Stock(Config::GFX_PERF_QUERIES_ENABLE);
}

// The one setting of how the picture looks that the game's own look needs: Arbitrary Mipmap Detection. The game
// gives some textures mipmap levels that are not smaller copies of the first but other pictures, for effects that
// change with distance (the recovery strips turn purple and blue farther away, as on a console). With the option
// off the emulator makes the levels from the first and the effect is lost; on, it reads the game's own levels. Its
// threshold stays the emulator's stock value. Picture only: nothing the game reads back.
void PinPictureLook()
{
  // a file's off is the stock value, which no user folder changes: no news, never named in the log
  Set(Config::GFX_ENHANCE_ARBITRARY_MIPMAP_DETECTION, true, InFiles::OrTheStockValue);
}

// When a picture is handed to the screen, as far as a file of the user folder could change it: when the game's
// thread waits and when the picture's does (RushFramePresentation, SmoothEarlyPresentation), how exactly both wait
// (PrecisionFrameTiming: the wait for a console's moment is the emulator's precise one, and "nothing waits longer
// than its limit" holds with it), which pictures go out and when (EarlyXFBOutput off puts a picture out at the end
// of its field, a field later than a console; CapImmediateXFB, SkipDuplicateXFBs). "Immediately Present XFB" is with
// the read-back settings above and is set after this as the launch file's "present" needs it (pictures.cpp).
// Vertical sync is the player's own setting, and reaches the game as the launch file's "verticalSync": the file's
// value is the run's, whatever GFX.ini says, and the log's "not used" line never names it (the launcher writes it
// into no GFX.ini, so the two differ whenever the player switched it on).
void PinPresentation()
{
  Stock(Config::MAIN_RUSH_FRAME_PRESENTATION);
  Stock(Config::MAIN_SMOOTH_EARLY_PRESENTATION);
  Stock(Config::MAIN_PRECISION_FRAME_TIMING);
  Stock(Config::GFX_HACK_EARLY_XFB_OUTPUT);
  Stock(Config::GFX_HACK_CAP_IMMEDIATE_XFB);
  Stock(Config::GFX_HACK_SKIP_DUPLICATE_XFBS);
  Set(Config::GFX_VSYNC, s_state.options.vertical_sync, InFiles::ThePlayersOwn);
}
}  // namespace

// A race's port 1 holds a standard controller on every computer: as the settings have it, or through the GameCube
// adapter, which answers the game as a standard controller does while one is plugged into it (the emulator's
// CSIDevice_GCAdapter is its CSIDevice_GCController). Anything else the settings name there (a steering wheel, a
// keyboard, the bongos, nothing) is a standard controller for the race. By the game's code a step writes the kind
// of controller in a human machine's port into the machine's control mode (main.rel fn_1_331EC: block +0x590, a
// word the state checksum covers), and a steering wheel's modes steer by other rules (fn_1_83858): a player with a
// wheel would race another race than everybody else. A steering wheel is not supported in an online race yet.
// Measured at step 300 of a match race (runs ctlw1, ctlw3): the game's kind for port 1 is 1 with this pin over a
// wheel in the settings and 3 with the wheel left in; every machine's port is none and its control mode 0 either
// way, so the path above was not seen taken there. The kind also picks which of a port's three sets of button
// settings the input layer reads.
// The launcher's Controller page offers a port only Off, Controller and GameCube adapter, so this changes a
// player's port only when it is Off, or when the settings file was changed by hand.
void PinRaceController()
{
  const Config::Info<SerialInterface::SIDevices>& port1 = Config::GetInfoForSIDevice(0);
  const SerialInterface::SIDevices had = Config::Get(port1);
  if (had == SerialInterface::SIDEVICE_GC_CONTROLLER || had == SerialInterface::SIDEVICE_WIIU_ADAPTER)
    return;
  Config::SetCurrent(port1, SerialInterface::SIDEVICE_GC_CONTROLLER);
  s_core_host.Log(fzgx::LogLevel::Notice,
                  "fzgx: port 1 is a standard controller for this race: the settings name device " +
                      std::to_string(static_cast<int>(had)) + " there (Core.SIDevice0), and an online race takes a "
                      "standard controller or the GameCube adapter");
}

void PinSettings()
{
  s_not_used.clear();
  PinPace();
  PinCpu();
  PinConsole();
  PinPictureReadBack();
  PinPictureLook();
  PinPresentation();
  if (s_not_used.empty())
    return;
  std::string names;
  for (const std::string& name : s_not_used)
    names += (names.empty() ? "" : ", ") + name;
  s_core_host.Log(fzgx::LogLevel::Notice, "fzgx: the runner sets what decides a race, so these settings of the user folder are "
                                          "not used: " + names);
}
}  // namespace fzgx_port
