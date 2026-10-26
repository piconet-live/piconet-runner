// What the adapter's files have of each other: the adapter's state, the host the core is given, and the functions
// one file calls of another. host.cpp: the emulator's settings; pins.cpp: the settings the
// runner sets whatever the user folder says; pictures.cpp: when a picture goes to the screen, and the times between
// the pictures; window.cpp: the data window and
// the host calls; switches.cpp: the arguments, the launch file and what of it goes to the emulator's side;
// frame.cpp, frame_paint.cpp (frame.h): the game window's frame, a strip in the launcher's look;
// panel.cpp: the panel over the picture before a race;
// taskbar.cpp: the game window under the launcher's taskbar button, and its icon;
// lanes*.cpp (lanes.h): the machines' task as a host call, for the per-machine rollback; copies.cpp: frame-buffer
// copies under a rollback; rumble.cpp: the motors held still while the player does not drive;
// adapter.cpp: the core's host, the files the adapter opens (the log file, the game folder's, the native module) and
// the three calls from the runner.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <future>
#include <optional>
#include <string>
#include <vector>

#include "Common/CommonTypes.h"
#include "Core/HostHooks.h"
#include "Core/PowerPC/StaticRecomp/StaticRecompModuleSource.h"

#include "fzgx/board.h"
#include "fzgx/core.h"

// Every address and offset of the game and of the mod that the adapter names comes from the facts table
// (port/facts/game.tsv; generated: facts.h, which the core's fzgx/guest.h includes): no such number is typed in this
// folder.
namespace fzgx_port
{
namespace table = fzgx::layout;  // the mod's memory: the data window, the image, the host calls
namespace game = fzgx::game;     // the game's own: main memory, main.rel, its waits, the executable file

// The mod image's network hook: the frame routine calls it before every simulation step and in every pass of the
// race-start gate's wait. In the reference configuration it is a word of the image itself (the image is in real
// memory there). In the native form the host's services are three addresses with nothing behind them, outside the
// data window and every piece of compiled code: the network hook, the frame tick from the mode machine's routine,
// and "main.rel is linked" from the executable's loader.
constexpr u32 NET_HOOK = table::IMAGE_HOOK_AT;
constexpr u32 HOST_CALLS = table::HOSTCALLS, HOST_CALLS_END = table::HOSTCALLS_END;

struct State
{
  std::string launch_file;  // --config FILE; none: the adapter does nothing at all
  fzgx::Options options;    // that file's settings: the one settings struct of the adapter and the core
  bool native = false;      // the bundle is the native form's (fzgx::NativeForm): set before boot
  // the data window's accesses (the coordinator's condition 3): all of them, whoever makes them, pass the two
  // functions the emulator is given; counted there, summed up at every frame tick and at every step
  fzgx::u64 window_reads = 0, window_writes = 0;
  fzgx::u64 counted_reads = 0, counted_writes = 0;  // at the last frame tick
  fzgx::u64 frames = 0, frame_accesses = 0, frame_most = 0, steps = 0;
  int picture_wide = -1;  // the shape the picture was last set to: 1 for 16:9, 0 for 4:3, -1 not set (ShowPicture)
  std::filesystem::path game_root;
  std::filesystem::path user_folder;  // --user-dir: where the core writes a race's summary line (Host::UserFolder)
  std::FILE* log = nullptr;
  // the host calls `source` held before ours
  StaticRecompModuleSource::HostCall next_call = nullptr;
  StaticRecompModuleSource::HostCallContains next_contains = nullptr;
  StaticRecompModuleSource::HostCallRangeContains next_range_contains = nullptr;
  void* next_user = nullptr;
  std::string engine;                     // "native" or "jit": what runs the game's code (BeforeBoot)
  std::future<std::string> module_sha1;   // the native module's, when this start records a replay
};

extern State s_state;
// The core's host (adapter.cpp). Not named s_host: the Windows socket headers define that name as a macro, and a file
// that includes them after this header then reads every use of it as something else.
extern fzgx::Host& s_core_host;

// host.cpp: the emulator's settings for the game, a race, the picture, the reference configuration
void ApplyGameSettings();
// pins.cpp: every setting of the emulator that decides what the game computes or how fast it runs, set as the run's
// own value (not with the launch file's "debug.userSettings")
void PinSettings();
// pins.cpp: a race's port 1 holds a standard controller (or the GameCube adapter), whatever the settings name there
void PinRaceController();
// host.cpp: the load before a race with no speed limit and no sound (true: before the emulated system starts),
// and the speed limit and the sound as they were (false: on the game's thread, or after the stop; nothing when
// nothing is lifted). And the emulation speed a race goes back to: the setting's, or while it is lifted the one
// it had.
void LiftLoadPace(bool lifted);
float PacedSpeed();
void ShowPicture(bool wide);
void FollowScreenOption();
void ApplyRaceSettings();
void ApplyReferenceConfiguration(const std::vector<fzgx::Code>& bundle_codes);
// window.cpp: the data window and the host calls
void ReportWindow(bool last);
// copies.cpp: a frame-buffer copy into memory a rollback keeps leaves it as the game had it (installed before boot;
// the count said at the stop).
void KeepCopiesUnderRollback(HostHooks::Hooks& hooks);
void ReportCopiesKept();
// rumble.cpp: the controllers' motors held still while fzgx::RumbleAllowed says no (installed before boot; checked
// every field; the counts said at the stop).
void HoldRumble(HostHooks::Hooks& hooks);
void RumbleField();
void ReportRumble();
void RegisterWindow(HostHooks::Hooks& hooks);
void TakeHostCalls(StaticRecompModuleSource& source);
// switches.cpp: a launch file was named (--config), so the adapter is in use; its settings read into s_state.options
// (what is wrong with the file, or nothing); a line for the log that says which test settings are on
bool InUse();
std::optional<std::string> ReadSettings();
void ReportSettings();
// the launch file's settings of the emulator's side, handed to it (Common/RunSettings.h)
void GiveEmulatorSettings();
// frame.cpp: the game window's frame is the adapter's (the strip with the launch file's "windowName" and, with
// "showFrameRate", the frame rate), given to the emulator's window before it is made (Common/WindowFrame.h); not
// with "debug.windowControls", whose window is the system's own with its menu bar
void GiveWindowFrame();
// taskbar.cpp: with the launch file's "taskbar" the process takes the launcher's application id, before the window
// is made; the window, once it exists, gets the app's icon and says what a pinned button starts (the launcher),
// and has that taken off again when it goes (both from the frame's hook: not with "debug.windowControls")
void JoinTaskbarGroup();
void DressWindow(void* window);
void UndressWindow(void* window);
// pictures.cpp: the launch file's "present" and "verticalSync" as the emulator's settings and the wait for a
// console's moment (after PinSettings); the hook that stamps every picture handed to the screen; and the times
// between them since they were last taken, for the core's summary line (Host::PictureIntervals)
void ApplyPresentation();
// screen.cpp: the window went to full screen or came back (frame_screen.cpp tells, from the window's thread): in
// full screen the video backend is asked for its own, exclusive one while the window has the focus. And, on the
// thread that presents: before a present the backend is asked or told to give it back; after one the log gets a
// line that says what the swap chain is, whenever that changed.
void TellFullscreen(bool on);
void FollowFullscreen();
void SayTheScreen();
void ForgetTheScreen();
void WatchPictures();
void StopWatchingPictures();
// panel.cpp: the panel before a race (fzgx/panel.h: LOADING, WAITING FOR PLAYERS). On the thread that presents,
// before every present: the panel into the picture while the core's board shows one (one load of a flag when it
// does not). From the game's thread, when the board changed at the start gate, where the game hands no picture
// over: a present of its own is queued, and not waited for.
// And every field of a race or replay start, on the game's thread: the load's progress read (fzgx::LoadField), and
// while LOADING is up a present asked for, at most once in 8 ms, so that the meter moves.
void DrawPanel();
void PresentPanel();
void PanelField();
fzgx::u32 TakePictureIntervals(fzgx::u32* out, fzgx::u32 most);
// drawn_frames.cpp: the pictures between two steps (fzgx/hfr.h): the core's high refresh rate started with the
// emulator's side (the GPU thread's re-draw, keyed by the command stream, and the display's rate), and its lines at
// the stop.
void StartDrawnFrames();
void StopDrawnFrames();
// freeze_watch.cpp: a thread that says in the log when a race's game stands still, and where (fzgx/freeze.h),
// and the emulator's lines (below): both start at the game's first picture (StartWatches, from the picture's hook:
// one flag read a picture after the first), so that a boot that fails has started neither. Every way the game is
// asked to stop says why, here, before the shutdown; when the game has stopped the watch ends (and says so when
// nothing had asked for the stop)
void StartWatches();
// (`reason`: the launcher's status board's word for it, fzgx/board.h)
void NoteStop(const std::string& why, fzgx::board::StopReason reason);
void EndFreezeWatch();
// the game window's messages pass through it on their way to the frame's own handling (`next`): the close message
// says what asked for it (the strip's button says so itself, before it sends one)
using WindowMessage = bool (*)(void* window, unsigned message, std::uintptr_t wparam, std::intptr_t lparam, std::intptr_t* answer);
WindowMessage WatchStops(WindowMessage next);
void NoteStripClose();
// emulator_lines.cpp: the emulator's errors and the game's own report lines into the game's log, from the first
// picture until the game has stopped; and what the filter holds back of them written now
void TakeEmulatorLines();
void FlushEmulatorLines();
void LeaveEmulatorLines();
// The replay scrubber (the core's scrub.cpp; on in a playback unless its launch file says "replay.scrubber": false).
// replay_bar.cpp: the bar drawn over the bottom of the game's picture (it takes no room: ReplayBarHeight is 0, and
// LayReplayBar does nothing); its sound gate's timer window (MakeReplayBar, the window's thread); told what to show,
// and asked for the step under the pointer of a scrub (the game's thread; fzgx/host.h); drawn before every present
// (its opacity, 0 when not shown; `bar_top` the bar's top edge in the picture); the game window's mouse in the
// picture's pixels (true: the bar's); the timeline lanes' height above it.
int ReplayBarHeight(int dpi);
void MakeReplayBar(void* window);
void LayReplayBar(int top, int width, int height);
void ShowReplayBar(const fzgx::ScrubBar& bar);
fzgx::u32 TakeReplayScrub();
struct BarPlace
{
  float x0 = 0, x1 = 1, top = 0, width = 0, s = 1;  // the track's ends, the bar's top, the picture's width, its scale
  fzgx::u32 first = 0, last = 1;                    // the steps at the track's ends
};
float DrawReplayBar(BarPlace* place);
bool ReplayBarMouse(void* window, unsigned message, float x, float y);
void SetReplayLanesHeight(float height);
float ReplayLanesHeight();  // (as last drawn: the observer HUD keeps clear of it)
// replay_lanes.cpp: the racer timeline's lanes above the bar, fading with it (fzgx/host.h Timeline): told the timeline
// when it changed (the game's thread) and the racer followed (spectator_overlay.cpp); a racer's name from the race's
// set-up; drawn after the bar; the mouse in the picture's pixels (true: a click of the lanes', into `click`).
void ShowReplayTimeline(const fzgx::Timeline& line);
void ReplayLanesFollow(fzgx::u32 follow, bool may_scrub);
bool ReplayLaneName(fzgx::u32 racer, char* out, std::size_t size);
void DrawReplayLanes(const BarPlace& place, float alpha);
bool ReplayLanesMouse(unsigned message, float x, float y, fzgx::SpectatorClick* click);
// replay_audio.cpp: the sound gate (fzgx/sound_gate.h): told by the bar what the core said of the frame, and ticked
// by the bar's window; told when another part of the runner set the stream's volume (the load's pace).
void ReplaySoundHeard(bool heard);
void ReplaySoundVolumeReset();
// replay_host.cpp: a key message of the game's window, taken when it is one of the scrubber's (true), and the presses
// since the core last asked (fzgx::ReplayKey bits); the undo log packed by the emulator's zstd (false: no zstd in this
// build, the log stays flat); the process's memory.
bool ReplayKeyMessage(unsigned message, std::uintptr_t wparam, std::intptr_t lparam);
// spectator_overlay.cpp: the spectator overlay over the picture (fzgx/host.h SpectatorShow): told once a displayed
// frame (the game's thread), drawn before every present (the thread that presents; nothing when no race is watched),
// its mouse from the game window's messages (true: the click was the overlay's), its clicks taken by the core.
void ShowSpectatorOverlay(const fzgx::SpectatorShow& show);
// minimap.cpp: our course map over the picture (fzgx/host.h MinimapCourse, MinimapShow): the road once a course and
// the dots once a displayed frame (the game's thread), drawn first of the overlays (DrawOverlays).
void MinimapCourseLoaded(const fzgx::MinimapCourse& course);
void ShowMinimap(const fzgx::MinimapShow& show);
void DrawMinimap();
fzgx::SpectatorClick TakeSpectatorClicks();
void DrawOverlays();
bool SpectatorMouse(void* window, unsigned message, std::intptr_t lparam);
fzgx::u32 TakeReplayKeys();
bool PackReplayLog(const fzgx::u8* data, std::size_t size, const fzgx::u8* prefix, std::size_t prefix_size,
                   std::vector<fzgx::u8>* out);
bool UnpackReplayLog(const fzgx::u8* data, std::size_t size, const fzgx::u8* prefix, std::size_t prefix_size,
                     fzgx::u8* out, std::size_t out_size);
bool ProcessMemoryNow(fzgx::u64 out[4]);
// the game thread's clocks for the scrubber's budget (Host::ThreadClocks), on that thread
bool GameThreadClocks(fzgx::GameClocks* out);
// adapter.cpp: the runner's output into a file
bool WriteOutputTo(const std::filesystem::path& file);
}  // namespace fzgx_port
