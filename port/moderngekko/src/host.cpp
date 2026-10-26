// The emulator's settings the game, a race and the reference configuration need. (The core's host itself is in
// adapter.cpp, with the adapter's file reads.)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "adapter_internal.h"

#include <cstdio>
#include <chrono>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

#include "Common/Config/Config.h"
#include "AudioCommon/AudioCommon.h"
#include "Common/RunSettings.h"
#include "Core/Cheats/GeckoCode.h"
#include "Core/Config/CheatSettings.h"
#include "Core/Config/GraphicsSettings.h"
#include "Core/Config/MainSettings.h"
#include "Core/HostHooks.h"
#include "Core/PowerPC/PowerPC.h"
#include "Core/System.h"
#include "VideoCommon/VideoConfig.h"

namespace fzgx_port
{
// What the game needs however it is started (they were the launcher's three environment variables):
//  - the picture on its own thread: the emulator's stock settings for the game turn that off, and that layer wins
//    over the main settings. Only the launch file's "debug.singleCore" (the gates' single-core runs) leaves it on
//    the CPU's thread;
//  - the two things the native module is built for, which the CPU core takes as settings of the run
//    (Common/RunSettings.h, like the launch file's test settings): its second
//    memory window on the game's scratchpad, and the game's waits counted as idle time: the wait for the picture
//    and the two waits for the disc, which spin on the drive's status (in the executable's synchronous read and in
//    main.rel's wait for its load queue; the facts table's three IDLE rows). On native code a turn of those two
//    costs seven trips through the run loop; unidled they held every load at 60 to 140 ms a field.
//  - cheats off, whatever the user folder's settings say (Dolphin.ini's EnableCheats; the game's own settings file
//    there is not read at all, switches.cpp): the code handler and Action Replay then do nothing. Only the launch
//    file's "debug.cheats" leaves both as the user folder has them. The reference configuration turns the code
//    handler on for the bundle's own codes, after this.
//  - every other setting that decides what the game computes or how fast it runs (pins.cpp), whatever the user
//    folder's files and the system folder's file for the game say. Only "debug.userSettings" leaves them to the files.
// With a launch file the adapter decides all of it: what the environment held is not asked.
void ApplyGameSettings()
{
  if (!s_state.options.debug.user_settings)
    PinSettings();
  Config::SetCurrent(Config::MAIN_CPU_THREAD, !s_state.options.debug.single_core);
  if (!s_state.options.debug.cheats)
    Config::SetCurrent(Config::MAIN_ENABLE_CHEATS, false);
  RunSettings::Set("FZGX_L1_AS_MEM2", "1");
  char idle[32];
  std::snprintf(idle, sizeof(idle), "%08X,%08X,%08X", game::IDLE_GPU_SYNC, game::IDLE_DISC_READ, game::IDLE_LOAD_QUEUE);
  RunSettings::Set("FZGX_IDLE_PCS", idle);
  // A controller goes on working when another window is in front (a player who clicks on the launcher in a race
  // keeps their machine): the emulator's input gate stays open whatever has the focus ("background input"). The
  // keyboard and the mouse do not come through by this: their device holds nothing while the game's window has no
  // focus (the window says so and the DInput keyboard asks the host: forks/recompcore/2-port). The GameCube adapter
  // is read past the gate in any case.
  Config::SetCurrent(Config::MAIN_INPUT_BACKGROUND_INPUT, true);
  // The undo log's guard (port/core/src/page_guard.h) write-protects pages of main memory's own mapping. Under the
  // native core the fallback JIT never takes the fast memory's faults, but with that arena mapped its checked fast
  // path still writes through the arena's other mappings of the same memory (Jit64 EmuCodeBlock: fastmem_arena),
  // where no protection of ours is: without the arena it writes through the page mappings, which are main memory's
  // own. Nothing it computes changes; what little the fallback runs is slower (RuntimeHost::RamGuardable).
  if (Config::Get(Config::MAIN_CPU_CORE) == PowerPC::CPUCore::StaticRecomp)
    Config::SetCurrent(Config::MAIN_FASTMEM_ARENA, false);
}

// The picture's shape is the game's own: 16:9 when its screen option says Wide Screen, 4:3 when it says Normal, set
// as a fixed shape either way. Left to itself the emulator guesses the shape anew for every frame from the
// projections the game draws with, and one page of a 16:9 game (the story's machine settings) looks 4:3 to it: that
// page was drawn squeezed with the game still in 16:9. A fixed shape turns the guess off.
void ShowPicture(bool wide)
{
  if (s_state.picture_wide == (wide ? 1 : 0))
    return;
  s_state.picture_wide = wide ? 1 : 0;
  Config::SetCurrent(Config::GFX_ASPECT_RATIO, wide ? AspectMode::ForceWide : AspectMode::ForceStandard);
  s_core_host.Log(fzgx::LogLevel::Notice, wide ? "fzgx: the picture is 16:9 (the game's screen option: Wide Screen)" :
                                            "fzgx: the picture is 4:3 (the game's screen option: Normal)");
}

// Every frame: the picture follows the game's screen option from the moment the game holds one (it reads the
// player's save some seconds into a boot; a race's set-up writes it every frame). Two reads of guest memory.
void FollowScreenOption()
{
  if (const std::optional<bool> wide = fzgx::GameWidescreen())
    ShowPicture(*wide);
}

// What a race or a replay needs of the emulator (the fork's ApplyBootConfig, and the one setting the launcher gave
// the fork for every race: no emulated disc delays).
void ApplyRaceSettings()
{
  const fzgx::BootSettings boot = fzgx::Boot();
  Config::SetCurrent(Config::MAIN_FAST_DISC_SPEED, true);
  Config::SetCurrent(Config::MAIN_CPU_THREAD, boot.dual_core);
  Config::SetCurrent(Config::MAIN_OVERCLOCK_ENABLE, boot.cpu_clock != 1.0f);
  Config::SetCurrent(Config::MAIN_OVERCLOCK, boot.cpu_clock);
  Config::SetCurrent(Config::MAIN_CUSTOM_RTC_ENABLE, boot.fixed_clock);
  Config::SetCurrent(Config::MAIN_CUSTOM_RTC_VALUE, boot.clock);
  // a race (not a playback, whose control modes are the recording's): the same kind of controller in port 1 on
  // every computer. Like the other pins, not with "debug.userSettings"
  if (!s_state.options.match.empty() && !s_state.options.debug.user_settings)
    PinRaceController();
  if (s_state.options.debug.clock != 0)
  {
    char text[96];
    std::snprintf(text, sizeof(text), "fzgx: TEST: this game's real-time clock starts from %08X, not the fixed value",
                  boot.clock);
    s_core_host.Log(fzgx::LogLevel::Warning, text);
  }
  ShowPicture(boot.widescreen);
  // last, with every setting above in place: the course's load with no speed limit and no sound (LiftLoadPace)
  fzgx::BeforeTheGameStarts();
}

// The reference configuration: a bundle of the fork's kind (Gecko codes). The race
// runs as it did on the fork: the codes go to the emulator's code handler, the mod image lives in real memory above
// the retail 24 MiB (32 MiB emulated), and the game is told it has 24. Test-only: it is the JIT reference the native
// form's gates compare with (code with no memory behind it cannot run on a JIT). Players get the native form.
void ApplyReferenceConfiguration(const std::vector<fzgx::Code>& bundle_codes)
{
  Config::SetCurrent(Config::MAIN_RAM_OVERRIDE_ENABLE, true);
  Config::SetCurrent(Config::MAIN_MEM1_SIZE, u32{game::REFERENCE_RAM_SIZE});
  Config::SetCurrent(Config::MAIN_ENABLE_CHEATS, true);
  HostHooks::Hooks& hooks = HostHooks::Get();
  hooks.reported_ram_size = game::RETAIL_RAM_SIZE;
  hooks.map_ram_above_reported = true;
  hooks.gecko_codes = [bundle_codes] {
    std::vector<Gecko::GeckoCode> codes;
    for (const fzgx::Code& from : bundle_codes)
    {
      Gecko::GeckoCode code;
      code.name = from.name;
      code.creator = "fzgx";
      code.enabled = code.default_enabled = true;
      for (const auto& [address, data] : from.lines)
      {
        Gecko::GeckoCode::Code line;
        line.address = address;
        line.data = data;
        char text[24];
        std::snprintf(text, sizeof(text), "%08X %08X", address, data);
        line.original_line = text;
        code.codes.push_back(std::move(line));
      }
      codes.push_back(std::move(code));
    }
    return codes;
  };
}
// The load before a race, at the host's own pace. A start that goes straight into a race or a replay spends 7 to
// 9 s loading the course at the console's pace: emulated disc reads and the game's unpacking, with the speed limit
// holding the emulated clock to real time. Without the limit the same emulated seconds pass as fast as this
// computer runs them. Only the host's waiting changes: the emulated clock, the disc's emulated times, the fixed
// real-time clock and every word the game computes are those of a paced load (the gates' playbacks have always
// run unpaced against recordings made at real pace). The sound is muted meanwhile: sound made faster than it is
// played is noise. Both are settings of the run, set back when the core finds the course loaded:
//   - the speed: while there is no limit the emulator's throttle takes "now" as its reference at every look, so
//     the limit starts from the moment it is back, with nothing to catch up and nothing to wait off;
//   - the sound: the stream's volume, as the window's Mute does it. A game started muted (--mute: volume 0)
//     stays silent.
namespace
{
bool s_load_lifted = false;
float s_paced_speed = 1.0f;
bool s_muted_before = false;
std::chrono::steady_clock::time_point s_lifted_at{};
}  // namespace

float PacedSpeed()
{
  return s_load_lifted ? s_paced_speed : Config::Get(Config::MAIN_EMULATION_SPEED);
}

void LiftLoadPace(bool lifted)
{
  if (lifted == s_load_lifted)
    return;
  char text[200];
  if (lifted)
  {
    s_paced_speed = Config::Get(Config::MAIN_EMULATION_SPEED);
    s_muted_before = Config::Get(Config::MAIN_AUDIO_MUTED);
    s_lifted_at = std::chrono::steady_clock::now();
    s_load_lifted = true;
    Config::SetCurrent(Config::MAIN_EMULATION_SPEED, 0.0f);
    Config::SetCurrent(Config::MAIN_AUDIO_MUTED, true);
    std::snprintf(text, sizeof(text), "fzgx: load: no speed limit and no sound until the course is loaded");
  }
  else
  {
    s_load_lifted = false;
    Config::SetCurrent(Config::MAIN_EMULATION_SPEED, s_paced_speed);
    Config::SetCurrent(Config::MAIN_AUDIO_MUTED, s_muted_before);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - s_lifted_at).count();
    std::snprintf(text, sizeof(text), "fzgx: load: the speed limit (%.2f) and the sound are back, %.2f s after they "
                                      "were lifted", s_paced_speed, seconds);
  }
  AudioCommon::UpdateSoundStream(Core::System::GetInstance());
  ReplaySoundVolumeReset();  // (the replay's sound gate puts its own volume over this at once)
  s_core_host.Log(fzgx::LogLevel::Notice, text);
}
}  // namespace fzgx_port
