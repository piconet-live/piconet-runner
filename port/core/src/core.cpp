// SPDX-License-Identifier: GPL-3.0-or-later

#include "internal.h"
#include "mode.h"
#include "mode_hud.h"

#include "fzgx/freeze.h"
#include "fzgx/panel.h"

#include <algorithm>
#include <memory>

#include "board_net.h"
#include "host_timers.h"
#include "page_guard.h"
#include "rollback.h"

namespace fzgx
{
namespace
{
Host* s_host = nullptr;
Options s_options;
std::unique_ptr<ModData> s_data;
panel::Progress s_load;  // how far the start's load is (LoadField)
}  // namespace

Host& TheHost()
{
  return *s_host;
}

const Options& TheOptions()
{
  return s_options;
}

void LogText(LogLevel level, const std::string& text)
{
  if (s_host)
    s_host->Log(level, text);
}

ModData& Data()
{
  if (!s_data)
    s_data = std::make_unique<ModData>();
  return *s_data;
}

void Init(Host& host, const Options& options)
{
  net::Shutdown();
  net::ClearExitCode();
  s_host = &host;
  s_options = options;
  Data().Clear();
  boot::Reset();
  boot::Prepare(Data());
  // a start that goes straight into a race or a replay: LOADING over the start-up pictures, from the first one
  panel::TheBoard().Reset();
  s_load = {};
  // a replay with its scrubber (every player's): LOADING until the race's first step, the bar's first, and the
  // course intro cut short unseen (FrameTick). A live race never: it has no replay file
  if (boot::IsActive())
    panel::Begin(panel::TheBoard(), !options.play_file.empty() && options.replay_scrubber,
                 options.play_file.empty() && (options.rejoin || options.spectate));
  // the launcher's status board (after the files are read: whether this is a race's start decides the heartbeat)
  board::Open(options.board);
}

bool Active()
{
  return s_host != nullptr && boot::IsActive();
}

BootSettings Boot()
{
  // What the fork's ApplyBootConfig forced: the CPU thread (the game's own settings turn it off; with a rollback
  // every frame only two host threads hold 60 fps), the emulated CPU clock (re-simulation), the fixed real-time clock
  // every race and replay starts from, and the game's 16:9 mode shown at 16:9. The fork also forced 32 MiB of memory
  // for the mod and cheats for its codes; the port has neither.
  BootSettings settings;
  settings.widescreen = TheOptions().widescreen;
  settings.true_widescreen = TheOptions().true_widescreen;
  settings.cpu_clock = TheOptions().cpu_clock;
  const Debug& debug = TheOptions().debug;
  settings.dual_core = !debug.single_core;
  // tests: this game starts from another real-time clock value than every other (two players' games must race the
  // same race whatever their clocks say)
  if (debug.clock != 0)
    settings.clock = debug.clock;
  return settings;
}

std::vector<Code> Codes()
{
  return boot::Codes();
}

bool NativeForm()
{
  return s_host != nullptr && boot::IsNative();
}

const std::vector<Patch>& Patches()
{
  return boot::Patches();
}

PatchReport Linked(u32 module)
{
  if (!NativeForm())
    return {};
  Guest g(*s_host, Data());
  // The loader links other modules through the same call (fze.sample.rel, id 0x12, before main.rel in every boot;
  // the menus' modules later): only main.rel, module id 1, is this list's.
  constexpr u32 MAIN_REL_ID = 1;
  if (module != layout::MAIN_REL && (!g.Ram(module, 4) || g.Read32(module) != MAIN_REL_ID))
    return {};
  if (module != layout::MAIN_REL)
  {
    Error("fzgx: main.rel is linked at {:08X}; the native module is built for {:08X}. Its patched pieces are not "
          "written and the game's own run on the fallback",
          module, layout::MAIN_REL);
    return {};
  }
  return boot::ApplyPatches(g);
}

void EnsurePatched()
{
  if (!NativeForm())
    return;
  const std::vector<Patch>& patches = boot::Patches();
  Guest g(*s_host, Data());
  // main.rel linked where the list expects it, and the list's first word of main.rel and its last word in place:
  // nothing to do. (The list is sorted by address; main.rel's words follow the executable's.)
  if (patches.empty() || !g.Ram(net::MODULE_POINTER, 4) || g.Read32(net::MODULE_POINTER) != layout::MAIN_REL)
    return;
  const auto first = std::find_if(patches.begin(), patches.end(),
                                  [](const Patch& p) { return p.address >= layout::MAIN_REL; });
  if (first == patches.end() || (g.Read32(first->address) == first->replacement &&
                                 g.Read32(patches.back().address) == patches.back().replacement &&
                                 g.Read32(patches.front().address) == patches.front().replacement))
    return;
  // Only when every word of the list holds the stock game's or the patched word. While OSLink is still relocating
  // main.rel (a picture can fall into that), words with a relocation hold neither: writing the others then would be
  // early, and the host call "main.rel is linked" comes when it is done. Tests force it for a saved state that
  // holds older codes' words.
  if (!TheOptions().debug.force_patches)
  {
    for (const Patch& p : patches)
    {
      const u32 found = g.Ram(p.address, 4) ? g.Read32(p.address) : p.replacement;
      if (found != p.original && found != p.replacement)
        return;
    }
  }
  boot::ApplyPatches(g);
}

namespace
{

// The bytes of the files the preload queue's entries `from` up to `to` name, by the disc's file table in memory.
u64 QueuedBytes(void* context, u32 from, u32 to)
{
  const Guest& g = *static_cast<const Guest*>(context);
  if (!g.Ram(game::DISC_TABLE, 4))
    return 0;
  const u32 table = g.Read32(game::DISC_TABLE);
  if (!g.Ram(table, 12))
    return 0;
  const u32 files = g.Read32(table + 8);
  u64 bytes = 0;
  for (u32 k = from; k < to && k - from < panel::QUEUE_ENTRIES; ++k)
  {
    const u32 number = g.Read32(game::LOAD_QUEUE_AT + (k % panel::QUEUE_ENTRIES) * 12 + 4);
    if (number != 0 && number < files && g.Ram(table + number * 12, 12) && g.Read8(table + number * 12) == 0)
      bytes += g.Read32(table + number * 12 + 8);
  }
  return bytes;
}

// A replay held to the race: its course intro is cut short as the game's own skip does it (fn_1_391F0: player 1's
// A or START, when the skip is open, puts the intro's countdown at 30 when it is above, and those 30 fields are the
// closing fade; then the
// start gate, then the countdown's steps from the recording). The intro is before the recording's first step:
// nothing of it reaches the race (a playback with the intro skipped by A had 0
// of 4,240 steps differing, run rpi-skipA-1), and races were recorded with and without it skipped.
// The cut waits for the game's own skip to be open (fn_1_391F0 takes A or START only then): the load queue idle
// (fn_1_467F4: the count queued is the count done), no fade running (fn_1_4C10), and fn_1_EB200's state not 0xE
// to 0x10. Cut at the intro's first frame instead, the course's last loads were still queued and a replay as the
// race crashed in its countdown (runs rpi-v020-race, rpi-v022-race).
bool TheIntroTakesItsSkip(const Guest& g)
{
  if (g.Read32(game::LOAD_QUEUED_AT) != g.Read32(game::LOAD_DONE_AT) || (g.Read8(game::INTRO_FADE) & 1) != 0)
    return false;
  const u32 state = g.Read32(game::INTRO_SKIP_STATE);
  return g.Read32(game::INTRO_SKIP_ON_AT) == 0 || state < 0xE || state > 0x10;
}

void CutTheIntro(Guest& g)
{
  const auto left = static_cast<s32>(g.Read32(game::INTRO_COUNTDOWN_AT));
  if (left <= static_cast<s32>(game::INTRO_CUT) || !TheIntroTakesItsSkip(g))
    return;
  g.Write32(game::INTRO_COUNTDOWN_AT, game::INTRO_CUT);
  Notice("fzgx: replay: the course intro is cut short unseen ({} fields of it left out), as the game's own skip does",
         left - static_cast<s32>(game::INTRO_CUT));
}
}  // namespace

bool LoadField()
{
  panel::Board& board = panel::TheBoard();
  if (!s_host || board.Now() != panel::Shows::Loading)
    return false;
  Guest g(*s_host, Data());
  if (!g.Ram(net::MODULE_POINTER, 4) || g.Read32(net::MODULE_POINTER) != layout::MAIN_REL)
    return true;
  const u16 mode = g.Read16(game::GAME_MODE_AT);
  u32 progress = s_load.Field(mode == game::MODE_GAME, g.Read32(game::LOAD_QUEUED_AT),
                              g.Read32(game::LOAD_DONE_AT), &QueuedBytes, &g);
  const bool shown = panel::CourseShown(mode, g.Read16(game::PHASE_AT));
  if (board.until_race.load(std::memory_order_relaxed))
    progress = s_load.Held(progress, shown);
  else if (board.until_live.load(std::memory_order_relaxed))
    progress = progress * panel::HELD_LOAD / panel::FULL;  // (the race's steps run again fill the rest: CatchingUp)
  if (board.until_live.load(std::memory_order_relaxed))
    progress = std::max(progress, board.progress.load(std::memory_order_relaxed));
  board.progress.store(progress, std::memory_order_relaxed);
  return !shown;
}

void BeforeTheGameStarts()
{
  if (s_host && panel::TheBoard().Now() == panel::Shows::Loading)
    s_host->LoadPace(true);
}

void FrameTick()
{
  if (!Active())
    return;
  // the heartbeat a watching thread reads (fzgx/freeze.h): one store a game frame
  freeze::ThePulse().frames.fetch_add(1, std::memory_order_relaxed);
  board::Beat();  // (and the launcher's: the status board's heartbeat)
  Guest g(*s_host, Data());
  boot::OnFrame(g);
  net::PresentTick(g);
  net::MinimapFrame(g);
  net::InputsFrame(g);
  // the panel comes down when the course is loaded (one load of a flag once it is down)
  panel::Board& board = panel::TheBoard();
  if (board.Now() == panel::Shows::Loading && g.Ram(net::MODULE_POINTER, 4) &&
      g.Read32(net::MODULE_POINTER) == layout::MAIN_REL)
  {
    const u32 mode = g.Read16(game::GAME_MODE_AT), phase = g.Read16(game::PHASE_AT);
    // the course is loaded: the intro runs at the game's own pace, with its sound
    if (panel::Frame(board, mode, phase))
      s_host->LoadPace(false);
    else if (board.until_race.load(std::memory_order_relaxed) && panel::CourseShown(mode, phase) &&
             phase == panel::PHASE_INTRO)
      CutTheIntro(g);
  }
}

void WindowWritten(u32 address, u64 value)
{
  if (!s_host)
    return;
  if (address == layout::DRAW_ASK)
  {
    Guest g(*s_host, Data());
    if (value == 1)
    {
      mode::hud::Hud(g);  // the game mode's displays: after the frame routine's HUD passes, before the drawing
      mode::hud::Board(g);  // and its values, for the launcher
      net::ModernHudFrame(g);  // the modern HUD: last, over the mask as the mode's pass left it
    }
    net::PresentAsked(g, static_cast<u32>(value));
    return;
  }
  if (address != layout::CONTROL + 12 || value != 0 || !s_options.replay_scrubber)
    return;
  Guest g(*s_host, Data());
  net::StepsDone(g);
}

void ControllerSetupTick()
{
  if (!s_host || !s_options.controller_setup)
    return;
  Guest g(*s_host, Data());
  boot::OptionsScreenTick(g);
}

std::optional<bool> GameWidescreen()
{
  if (!s_host)
    return std::nullopt;
  const Guest g(*s_host, Data());
  return boot::Widescreen(g);
}

void NetSync()
{
  if (!s_host)
    return;
  Guest g(*s_host, Data());
  net::Run(g);
}

void ModeCall()
{
  if (!s_host)
    return;
  Guest g(*s_host, Data());
  FZGX_TIME(Mode);
  mode::Step(g);
}

u32 ModeRecord(u32 seat)
{
  if (!s_host)
    return 0;
  Guest g(*s_host, Data());
  return mode::Record(g, seat);
}

u32 ModeMove(u32 seat)
{
  if (!s_host)
    return 0;
  Guest g(*s_host, Data());
  return mode::Move(g, seat);
}

u32 ModeStat(u32 seat)
{
  if (!s_host)
    return 0;
  Guest g(*s_host, Data());
  return mode::Stat(g, seat);
}

u32 ModeRollSave(u32 seat)
{
  if (!s_host)
    return 0;
  Guest g(*s_host, Data());
  return mode::RollSave(g, seat);
}

void Shutdown()
{
  net::Shutdown();
}
int ExitCode()
{
  return net::ExitCode();
}
void MemoryGone()
{
  net::guard::Abandon();
}
std::string Sha1(const u8* data, std::size_t size)
{
  return Sha1Hex(data, size);
}
}  // namespace fzgx
