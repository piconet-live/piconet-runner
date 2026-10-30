// The host-side core of the F-Zero GX online mod: a race booted from a launch file, the relay client with lockstep
// and rollback, replay recording and playback, the status board, machine colours. Lifted from the Dolphin fork
// (dolphin-fzgx Core/FzgxBoot, FzgxNet, FzgxPaint at 5bdc7c640e); what the fork took from Dolphin it now takes from
// fzgx::Host, and the mod's memory is fzgx::ModData. One game per process.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "fzgx/guest.h"
#include "fzgx/host.h"

namespace fzgx
{
// A test setting of the emulator's side (the runner's patched emulator core and its runtime: the probe, the pad
// script, the pokes, the logs, the CPU core's comparison switches), as a launch file's "debug" part gave it: the key
// it was given under, the name the emulator knows it by (an environment variable's name) and its value there.
// The core only reads and carries them; the adapter hands them on (forks/recompcore/2-port: Common/RunSettings.h).
struct EmulatorSetting
{
  std::string key, name, value;
};

// The test settings of one start: a launch file's "debug" part, key by key. Players and tests run one build:
// nothing here is on unless the launch file names it, and nothing else turns it on. The core
// reads no environment variable and no argument.
struct Debug
{
  u32 lag_ms = 0;            // "testLagMs": a round trip added to every packet between this game and the relay
  u32 jitter_ms = 0;         // "jitterMs": each packet is held up to this much longer
  bool autopilot = false;    // "autopilot": the game's AI drives this player's seat (the race then ends by itself)
  // "blackoutStep", "blackoutSeconds": from that race step on, nothing is sent to or taken from the relay for so many
  // seconds (a network drop with the game still open: it asks for its seat again and rolls forward)
  u32 blackout_step = 0;
  u32 blackout_seconds = 0;
  // "forceRollback" {"every", "depths"}: every force_every displayed race frames one rollback, its depth (in steps)
  // the next of force_depths in turn; 0 or empty: off. In a playback too, whose race then runs through the rollback
  // path.
  u32 force_every = 0;
  std::vector<u32> force_depths;
  std::string rollback_log;  // "rollbackLog": a file, one line for every displayed race frame (step, depth, the undo
                             // log's, the restore's and the steps' times)
  u32 alter_step = 0;        // "alterStep": this game's state is altered before this step (a desync on purpose)
  u32 handback_at = 0;       // "handbackAt": a rejoin's hand-back shown with control back at this step (the 3-2-1
                             // monitors in a playback, nothing else changed)
  u32 clock = 0;             // "clock": the real-time clock value the game starts from instead of the fixed one
  bool force_patches = false;  // "forcePatches": a patched word is written whatever the game holds there (a saved
                               // state recorded with older codes at two of the sites)
  bool single_core = false;  // "singleCore": the picture's work on the emulated CPU's thread (the gates' single-core
                             // runs; a game runs with the picture on its own thread)
  // Exactness under rollback.
  // "drawnFrames": frames drawn between two steps in every pass, whatever the display and the time left (1 to 3;
  // the exactness gate of the high refresh rate: hfr.h). 0: as "refreshRate" says.
  u32 drawn_frames = 0;
  // "redraw" {"replays", "compareEvery", "log", "dump"}: the emulator records every frame's converted draws and draws
  // each again so many times (1 to 3) into a second frame buffer at its copy to the display, nothing shown
  // (VideoCommon/FrameRedraw.h, the high refresh rate's re-draw spike); every compareEvery frames the first replay
  // and the true frame are read back and the pixels that differ counted; "log" a CSV line a frame, "dump" the first
  // differing pictures' files (a prefix); "turn": every replay's 3D draws seen from a camera turned by so many
  // degrees about its up axis (a correction of the projection alone, the HUD untouched; -90 to 90); "budgetMs":
  // the replays of one frame stop once the next would pass this much video thread time; "at": "copy" (the
  // default: at the frame's copy to the display) or "drawDone" (at the next draw-done, before it is signalled);
  // "drawsAt": with the high refresh rate, the Nth frame drawn again has its draws written to "<dump>-draws-N.csv";
  // "stillsAt" and "stills": the pictures of so many frames from the Nth XFB copy saved as PNG files beside "dump"
  // (the true one and those drawn again after it), and again every "stillsEvery" copies; "perStep": the display
  // taken as so many pictures a step (6.0: a 360 Hz display's), whatever "refreshRate" and the display say (0: as
  // they say)
  u32 redraw_replays = 0, redraw_compare = 0, redraw_draws_at = 0, redraw_stills_at = 0, redraw_stills = 0,
      redraw_stills_every = 0;
  float redraw_turn = 0, redraw_budget_ms = 0, redraw_per_step = 0;
  bool redraw_at_draw_done = false;
  std::string redraw_log, redraw_dump;
  u32 late_steps = 0;        // "lateSteps": a playback whose inputs become known this many steps late, so its race
                             // runs on predictions (each remote seat's last known input) and rolls back where one
                             // was wrong, as a live race does
  std::vector<u32> late_seats;  // "lateSeats": with "lateSteps", only these seats' rows are known late; every other
                                // seat's row is known when its step runs (one late player)
  u32 late_noise = 0;           // "lateNoise": a late seat's predicted row has its steering moved by up to this much
                                // (a prediction that is wrong in most steps, as a player's moving stick makes it)
  // "partialRollback": the per-machine rollback (the adapter's lanes_partial.h), on unless this says otherwise: "off"
  // (a step run again runs whole, the CPU core running the task), "check" (each also run whole from the same memory,
  // the two compared word for word, for the steps of traceSteps [from, to]), "writes" (the path on, and every word a
  // job writes outside its machine's records noted, for the steps of traceSteps). As an object: {"mode", "log" (a
  // file, a line a batch), "traceSteps", "widen" (check only: a job whose locked-cache check fails is put back all
  // the same), "watch", "guard" (false: no guard; for tests)}; a "log" alone keeps the path on and logs its batches (a
  // live race's counts). The contact task always runs whole: a file asking for contact jobs ("contact") or the
  // "trace" mode is refused
  std::string partial_mode;
  std::string partial_log;
  u32 partial_from = 0, partial_to = 0;
  bool partial_widen = false;
  std::string partial_watch;  // "watch": hexadecimal guest addresses with commas, each logged when a job changes it
  bool partial_guard = true;  // "guard"
  std::string trace_file;    // "trace": a file, a line for every checksum kept: the step, how that step ran (L a
                             // displayed frame's last or only step, F its first with more to follow, M one between),
                             // the value
  u32 trace_from = 0;        // "traceStates" [from, to]: for the steps from here up to trace_to, the machines' blocks
  u32 trace_to = 0;          // and progress records into <trace_file>.states
  u32 dump_step = 0;         // "dump" {"step", "prefix"}: the game's whole memory, the locked cache and the mod's data
  std::string dump_prefix;   // window into files, every time this step is about to run (<prefix>-before-N) and has
                             // just run (-after-N)
  bool keep_scratch = false; // "keepScratch": the locked cache is left out of the undo log, the fault this shows: a
                             // rollback then leaves it as the newest step left it (the fault; kept to show the gates
                             // can fail)
  std::string undo_log;        // "undoLog": how the undo log finds the first write to a page it does not watch:
                               // "guard" (the default: write-protected pages, page_guard.h), "scan" (every page
                               // compared before a frame's own step and the one after), "both"
                               // (the guard, checked at every step against every page compared: a test)
  std::string hit_log;         // "hitLog": a file, a line for every step that left a machine contact, a side attack or a
                               // sound command, and for every frame that showed a hit or a lunge (hits.cpp)
  u32 skip_draws = 0;          // "skipDraws": the drawing of every Nth race frame is skipped whatever its time
                               // (present.cpp; 1: every race frame), so a test sees the skip often
  u32 slow_from = 0, slow_to = 0;  // "slowSteps" "FROM-TO": while a frame shows one of these steps the game runs at
                                   // a hundredth of its speed, so a harness takes a picture of every frame (hits.cpp)
  // "undoTrace" {"file", "heaps"}: the undo log's own account into a file (undo_trace.cpp): each page first seen
  // changing late and which steps its change may be, each restore that puts such a page back to another moment
  // than the rest of memory (both: the scan way's fault; with the guard on, its counts per step instead), and,
  // with "heaps" (hexadecimal: the address of the game's OS heap table), a line when
  // a heap's cells stop or start adding up to its size; with "check" [FROM, TO], the whole tracked memory kept
  // before each of those steps and compared after every restore to one of them (slow: 19 MiB copied a step), a
  // line for every page that did not come back as it was. It reads only.
  std::string undo_trace;
  u32 undo_trace_heaps = 0;
  u32 undo_trace_from = 0, undo_trace_to = 0;
  bool full_screen_without_focus = false;  // "fullScreenWithoutFocus": a window in full screen asks the video
                             // backend for its own, exclusive full screen whatever "fullScreen" says and also
                             // while another window has the focus (a test's window is shown without it)
  // "standStill" {"steps", "tenths"}: before the displayed frame that begins at each of these steps (ascending) the
  // game's thread sleeps for so many tenths of a second, once each: a freeze on purpose, for the freeze report
  // (fzgx/freeze.h). Nothing of the game is touched: the race is the same race, late.
  std::vector<u32> stand_steps, stand_tenths;
  std::string relay;         // no key: the core's own tests join a relay without a match (host:port:token(hex))
  // The replay scrubber's tests (Options::replay_scrubber; scrub.cpp).
  u32 buffer_share = 40;       // "bufferShare": the share of a displayed frame's time, in percent, the steps ahead may
                               // take while the replay plays (5 to 95)
  u32 buffer_steps = 0;        // "bufferSteps": that many steps ahead in every frame instead, whatever the time (1
                               // to 64)
  u32 replay_memory = 512;     // "replayMemory": MiB the replay may hold beyond the game's own (logs, keys, copies);
                               // beyond it the packed log farthest from the play position is dropped
  u32 replay_near = 1024;      // "replayNear": steps either side of the play position whose log and keys the budget
                               // never drops (64 to 65,536)
  std::string replay_script;   // "replayScript": "FRAME:WHAT[:N],...", by displayed frames of the race: p pause or
                               // play, j:STEP jump, h to the race's first step, e to the newest step buffered; a
                               // scrub: s:STEP the bar is pressed at that step, f:N and k:N the pointer then moves N
                               // steps a frame forwards and backwards, r it is released; n and v a frame on and
                               // back; u and i the racer before and the next
  bool replay_end_quits = false;  // "replayEndQuits": at the replay's end, once the script is done, the game says
                                  // its playback's summary and stops (a check's; a player's stays paused at the end)
  // The emulator's side. Without these keys the runner refuses the option or leaves the thing unread, and the
  // environment is asked for none of it.
  bool jit = false;          // "jit": the emulator's own compiler runs the game, not the native module (the gates'
                             // reference; the runner's --allow-interpreter is refused without it)
  bool cheats = false;       // "cheats": the user folder's settings file for the game is read (its settings, codes
                             // and every-frame patches), and cheats are as the settings say; without it: off, unread
  bool mods = false;         // "mods": the runner's mod folders are searched (--mods DIR; Mods beside it and in the
                             // user folder)
  bool automation = false;   // "automation": the runner's --automation-dir (commands from files: stop, screenshot,
                             // controller input)
  bool load_state = false;   // "loadState": the runner's --load-state (a start from a saved state)
  bool user_settings = false;  // "userSettings": the emulator's settings are the user folder's (Config/Dolphin.ini,
                               // GFX.ini: a test's speed limit, clock and the like); without it the runner sets every
                               // setting that decides what the game computes or how fast it runs, whatever they say
  // "renderer": who draws the picture shown, "dolphin" (the emulator) or "aurora" (Aurora beside it, presentation
  // only: aurora.cpp in the adapter); "auroraShots", "auroraLog": its test pictures (XFB copies, 1 the first) and its
  // logs' file prefix
  std::string renderer = "dolphin";
  std::vector<u32> aurora_shots;
  u32 aurora_scale = 0;  // "auroraScale": its render scale, 1 to 8; 0 (or none): AutoAuroraScale of the display's height
  std::string aurora_log;
  std::vector<EmulatorSetting> emulator;  // every other key of the emulator's side that was given (files_launch.cpp
                                          // holds the table: "pad", "poke", "probe", "paceLog", ...)
};

// A small picture a launch file carries: `width` by `height` pixels of four bytes each, red, green, blue and
// opacity, row by row from the top. Empty (no width): none.
struct WindowPicture
{
  u32 width = 0, height = 0;
  std::vector<u8> rgba;
};

// The launcher's button on the Windows taskbar, which the game window joins as a second window of one program.
// Empty (no id): the game window has a button of its own.
struct TaskbarGroup
{
  std::string id;       // the launcher's application id, as it gave it to Windows for itself
  std::string program;  // the launcher's program file: what a pinned button starts, and whose icon it shows
  std::string name;     // what a pinned button is called
};

// The settings of one start of the game: a launch file (--config FILE; protocol/formats.md section 2), read by
// ReadLaunchFile and by nothing else.

struct Options
{
  std::string launch_file;   // the file these were read from, for the log
  std::string about;         // "about": what the start is for, in its author's words
  std::string bundle_dir;    // "bundle": the mod bundle: mod.bin, manifest.json, patches.json (the fork's kind: codes.txt)
  bool widescreen = true;    // "widescreen": the game's own 16:9 mode
  bool true_widescreen = false;  // "trueWidescreen": in a race or a playback, the 16:9 view keeps the whole vertical
                                 // view of Normal and widens the sides (image: sites_view.py); needs "widescreen"
  std::string window_name;   // "windowName": the name in the game window's strip, as the launcher gives it (the
                             // runner has no name of its own); none: the strip shows no name
  WindowPicture window_name_picture;  // "windowNamePicture": the name as a picture, drawn in place of the text
  bool show_frame_rate = false;  // "showFrameRate": the game window's strip shows the frame rate (a player's setting)
  bool rumble = true;        // "rumble": the player's controller may rumble (a player's setting); false: no rumble
                             // command reaches a pad in this start (the game still asks for it: RumbleAllowed)
  TaskbarGroup taskbar;      // "taskbar": the launcher's taskbar button, which the game window joins (a player's
                             // key; a start without it, as every test's from a file, keeps a button of its own)
  std::string board;         // "board": the launcher's status board for this game, a file both map (fzgx/board.h;
                             // protocol/formats.md section 8); none: nothing is told the launcher but the end
  // "match": a race
  std::string match;         // "match.match": the coordinator's match start, as JSON text
  std::string relay_host;    // "match.relayHost": the relay's host when the match start names none
  u32 delay = 2;             // "match.delay": input delay in steps
  float cpu_clock = 6.0f;    // "match.overclock": the emulated CPU's clock factor (re-simulation needs the headroom)
  std::string replay_file;   // "match.record": where the race is recorded
  bool rejoin = false;       // "match.rejoin": the game was started again into its running race (the launcher's
                             // Rejoin): LOADING stays up until it has caught up
  bool spectate = false;     // "match.spectate": the match start is a spectator's (the launcher's Watch): LOADING
                             // stays up until the game has caught up with a race already running, as a rejoin's
  bool practice = false;     // "match.practice": a solo practice with rewind and no relay (practice_session.cpp):
                             // the match names one seat and no relay; the pad gives each step's row
  u32 camera = 0;            // "match.camera": the chase camera's view (1 to 3) the local racer's race starts in, the
                             // player's last (boot.cpp WriteStartView; the launcher keeps it from the board's Camera
                             // events); 0: the game's own. The picture only: no step reads it
  // "replay": a replay's playback
  std::string play_file;     // "replay.file"
  std::string other_sounds;  // "match.otherSounds", "replay.otherSounds": other racers' sounds: full, quieter, off
  bool replay_scrubber = false;  // "replay.scrubber": a bar under the picture to pause, jump and scrub, the rest of
                                 // the race simulated ahead of the picture (scrub.cpp). A launch file's playback has
                                 // it on unless the file says false; off with forced rollbacks or late inputs (tests
                                 // of the rollback path)
  // "present": when a picture goes to the screen (the host's matter: the core only carries it, names it in the log
  // and in a race's summary line). "console", and a file without the key: at the time of the picture's first line
  // on the host's clock, 58 half-lines after the field's start, as a console puts it out (every player's game).
  // The two other ways are kept for comparison, and the log calls each a test
  // setting: "field", when the emulated video output reaches that line, which the busy game
  // thread reaches some milliseconds late; "copy", when the game copies the picture out of its frame buffer, sooner
  // than a console.
  std::string present = "console";
  // "refreshRate": pictures a second in a race or a replay (the high refresh rate, fzgx/hfr.h). "60", and a file
  // without the key: the game's own picture per step. "120", "240": frames drawn between two steps, machines and
  // cameras between where they were and where they are, each shown at its time on the display's clock (the
  // simulation keeps the console's 59.94 steps a second). "display": as many as the display's own refresh rate.
  // A player's setting.
  std::string refresh_rate = "60";
  // "fullScreen": what the window's full screen is (fzgx/screen.h). "borderless", and a file without the key: the
  // window without a frame, the size of its monitor. "exclusive": on top of that the video
  // backend's own full screen while the window has the focus (a display with variable refresh follows it with the
  // drivers' usual settings; a display of a fixed rate tears in it unless vertical sync is on). A player's setting.
  std::string full_screen = "borderless";
  // "verticalSync": pictures wait for the display's refresh (a player's setting, off unless the file says so). The
  // runner sets the video backend's own setting by it, whatever the user folder's file says.
  bool vertical_sync = false;
  bool controller_setup = false;  // "controllerSetup": a plain start that opens the game's own controller options
                                  // (Options, Controller) once the title stands, and stops the game when the
                                  // player leaves them; no test setting: a player's start may ask for it
  // "lanes": 0 or no key (launchers write 0): the multicore physics is parked on branch parked/lanes; any other
  // count is refused
  Debug debug;               // "debug"; a file without one leaves every test setting off
};

// Reads a launch file into the settings of one start. Says what is wrong with it, naming the key (an unknown key,
// a value of the wrong kind or outside its range: the game then does not start), or nothing. Needs no Init.
std::optional<std::string> ReadLaunchFile(const std::string& path, Options* out);
// The same from the file's text.
std::optional<std::string> ParseLaunchFile(const std::string& text, Options* out);
// The launch file's keys of the test settings that are on in `debug` ("debug.lateSteps"); none: a player's start.
std::vector<std::string> DebugSettingsOn(const Debug& debug);

// The input delay a race starts with unless its player chose less, and the most a race takes (each player picks 0,
// 1 or 2 for their own game; never above 2). A launch file that asks for more is refused.
// Aurora's render scale when the launch file gives none: the smallest whole number whose picture (448 lines of the
// game's, each drawn this many times) covers the display's height, at most 4 (720 lines: 2, 1080: 3, 1440 and more: 4).
// A height not known (0) is 1, the game's own size.
constexpr u32 AUTO_AURORA_SCALE_LINES = 448;
constexpr u32 AUTO_AURORA_SCALE_MOST = 4;
constexpr u32 AutoAuroraScale(u32 display_height)
{
  // (with the height at its top the sum would wrap: anything that tall is at the cap anyway)
  if (display_height > (AUTO_AURORA_SCALE_MOST - 1) * AUTO_AURORA_SCALE_LINES)
    return AUTO_AURORA_SCALE_MOST;
  const u32 scale = (display_height + AUTO_AURORA_SCALE_LINES - 1) / AUTO_AURORA_SCALE_LINES;
  return scale == 0 ? 1 : scale;
}

constexpr u32 PLAYERS_INPUT_DELAY = 2;
// The ways of showing a picture that are kept for comparison and are on in a start, a sentence each for the log
// "present" as "field" or "copy". None: a start as players have it.
// They are no part of "debug": a launcher's settings file names them, on a server that takes no game with a debug
// setting.
std::vector<std::string> LatencySettingsOn(const Options& options);
// A file's name without the folders it is in, for the log: a player's data folder is under their profile, whose
// path holds their name, and players send and post these logs.
std::string FileNameAlone(const std::string& path);
// What a start says of its picture and its delay in the log, a sentence each, test setting or not: how a picture
// goes to the screen, and for a race the input delay its player chose.
std::vector<std::string> PlayersSettingsSaid(const Options& options);

// What the runner was asked for on its command line that a start with a launch file gets only when the file's
// "debug" part allows it.
struct RunnerAsked
{
  bool interpreter = false;  // --allow-interpreter: the game may run without a native module, on the emulator's JIT
  bool mods = false;         // --mods DIR: a folder of mods, which are host code called from the game
  bool automation = false;   // --automation-dir DIR: commands from files, controller input among them
  bool load_state = false;   // --load-state FILE: the game starts from a saved state, which is memory someone made
};
// The first of them the launch file does not allow, as a sentence that names the option and the key it lacks (the
// game then does not start); nothing when all that was asked is allowed.
std::optional<std::string> RefusedRunnerOption(const Debug& debug, const RunnerAsked& asked);

// What the emulator must be set to before the game starts (the fork's ApplyBootConfig).
struct BootSettings
{
  bool dual_core = true;       // with a rollback every frame only two host threads hold 60 fps
  float cpu_clock = 6.0f;      // the emulated CPU's clock factor (re-simulation needs the headroom)
  bool fixed_clock = true;     // every race and replay starts from one real-time clock value
  u32 clock = 0x386D4380;
  bool widescreen = true;      // show the picture at 16:9
  bool true_widescreen = false;  // the 16:9 race view of true widescreen
};

// A Gecko code of the bundle (only while the game patches are still Gecko codes: temporary
// shortcuts).
struct Code
{
  std::string name;
  std::vector<std::pair<u32, u32>> lines;
};

// One word of the game's code that the native form changes (the bundle's patches.json, port/image/build.py): the
// native module is compiled from the game's code with `replacement` in, so the game must hold it too.
struct Patch
{
  u32 address = 0;
  u32 original = 0;     // the stock game's word, checked before the write
  u32 replacement = 0;
  std::string what;
};

// What became of the list's words at one call.
struct PatchReport
{
  int written = 0;  // held the stock word: written
  int already = 0;  // held the new word
  int refused = 0;  // held something else: left alone and logged (written all the same with force_patches)
};

// Before the emulated system starts. Reads the match or replay file and the bundle, and fills the mod's data.
void Init(Host& host, const Options& options);
// A race or a replay's playback was asked for and its files were read.
bool Active();
BootSettings Boot();
std::vector<Code> Codes();
// The bundle is the native form's: the image is native code with this process's
// buffer behind its data, the game's code is patched from a static list, and there are no Gecko codes. True for a
// plain start too: the patched code is in the native module in every start, so the image and the list are needed
// in every start. The image and the mode word are in the data window from Init on.
bool NativeForm();
const std::vector<Patch>& Patches();
// main.rel is linked at `module` and none of its code has run (the host call from the executable's loader): writes
// main.rel's words of the list. The module must be where the native module was built for (layout::MAIN_REL).
// The loader's call comes for every module it links; one that is not main.rel (by its module id) is nothing to do.
PatchReport Linked(u32 module);
// Every frame of the picture: writes the list again when the game holds stock words where patched ones should be
// (a saved state was loaded; a boot whose "linked" call never came). Three reads when nothing is to do. It acts
// only when every word of the list is the stock game's or the patched one: main.rel half relocated, or a saved
// state with other codes in, is left alone (force_patches writes all the same).
void EnsurePatched();
// The mod's data window, for the runtime's memory hooks and for tests.
ModData& Data();

// Once the host has set the emulator up and before the emulated system starts: a start that goes straight into a
// race or a replay loads its course with no speed limit and no sound (Host::LoadPace; the load is 7 to 9 s of the
// game's own work at the console's pace), until the frame tick that finds the course loaded. Not in a plain start.
void BeforeTheGameStarts();
// Every frame of a race or replay start: in the reference configuration before the game's code handler would run
// (the fork's FzgxBoot::OnFrame), in the native form the host call from the mode machine's routine (HOST_TICK).
void FrameTick();
// Every field of a race or replay start, on the game's thread (the frame tick does not come while the game loads:
// its loop stands in the load): while LOADING is up, how far the load is, from the game's own queue of file
// preloads, into the panel's board (fzgx/panel.h, Progress). Reads only; one load of a flag once LOADING is down.
// True while the game draws nothing under LOADING (the load), so the host presents the panel itself; false once
// the game draws again (in a replay held to the race, the intro's fade and the countdown run unseen under it).
bool LoadField();
// Every picture of a plain start: with Options::controller_setup, the watch that opens the game's own controller
// options and stops the game when the player leaves them (options_screen.cpp). One test of a flag without it.
void ControllerSetupTick();
// The game's own screen option as the game holds it now (Options, Screen Mode: from the player's save, or from a
// race's set-up): true for Wide Screen, false for Normal. No value while the game has not loaded main.rel, or holds
// something else there. The picture's shape follows it. Needs Init; reads only.
std::optional<bool> GameWidescreen();
// The mod's network hook: before every simulation step and in every pass of the race-start gate's wait.
void NetSync();
// The game wrote a word of the data window (the native form: every access of it passes the host). The replay
// scrubber takes one of them as a call: the frame routine's "the extra steps are done" (control block +12 set to
// 0), which is the only moment the host has after a frame's last step and before its picture. The image's two
// stores into DRAW_ASK are the drawing's start and end: a late frame's drawing skipped (present.cpp).
void WindowWritten(u32 address, u64 value);
// The per-machine rollback's log lines ("debug.partialRollback.log"; the adapter's lanes_partial.h): a line kept,
// written out now and then and at the end.
void PartialLine(const std::string& line);
void PartialFlush();
// A count that grows whenever the simulation's memory becomes another moment than a rollback's restore makes it
// (net::Rollback::Moments): the per-machine rollback forgets every step's last run when it changes.
u32 RollbackMoments();
// When the game stops: closes the session and finishes a recording.
void Shutdown();
// The game was started again into a race (Options::rejoin) and the relay said the race is over: nothing to rejoin.
// The core stopped the game; the host ends its process with this exit code (the launcher's
// RecompLaunch.RaceOverExitCode), and the launcher takes the player back to the lobby with no error.
constexpr int EXIT_RACE_OVER = 3;
// The game stopped by no stop of its own (its window closed, Escape, the emulator's own stop) while the player's
// seat still raced: not finished, not out, the race not over, no desync, not a spectator. The host ends its process
// with this exit code (the launcher's RecompLaunch.LeftRacingExitCode), and the launcher keeps the race and offers
// Rejoin, as after a crash.
constexpr int EXIT_LEFT_RACING = 4;
// The exit code the core asks the host's process to end with when the game has stopped (0: the host's own).
int ExitCode();
// Whether the player's controllers may rumble now (rumble.cpp; on the CPU thread): the launch file's "rumble", and
// the player drives a seat of their own. Never in a spectator's start or a playback, nor while the player's racer is
// out or over the line (watching), nor while a game started again catches up with the AI driving its seat. A plain
// start (the menus, offline play) and a race before its session: as "rumble" says. The host holds every motor still
// while this says false; the game computes its rumble as ever.
bool RumbleAllowed();
// The game's main memory is gone: the emulator has stopped and let it go. A host whose emulator stops before
// Shutdown (the runner's: Core::Shutdown, then Shutdown) says so first: the undo log's guard, if it is on, is then
// dropped without touching the memory's page protection, which is no longer there to touch and whose addresses may
// be another allocation's by then.
void MemoryGone();
// Asked by a thread of the host's other than the game's, before it writes bytes of the game's main memory by
// itself (`size` bytes from the guest address `address`): does a rollback put any of them back? Then the write must
// not be made. The emulator's picture thread makes such writes: it clears the destination of a frame-buffer copy
// it keeps only as a texture, when it comes to the copy, which is milliseconds after the game asked for it. By
// then a rollback has put the memory back to an earlier step and is running the steps again, and at that earlier
// step those bytes may be other things: F-Zero GX's boost effect copies the picture into a buffer it allocates
// from the main heap when the boost begins, and the clear then went over the heap's cell headers that stood there
// before (the heap stopped adding up, and the game stopped in its own panic).
// True from the undo log's start to its stop for every range that touches main memory the log puts back: the
// simulation's memory less the ranges the image lists as never rolled back, and the executable's generator word
// (0x801A63C0, the one word below the log a restore writes). False for everything else, and whenever no log is
// kept: before a race's first rollback frame, after its end, and after a session that failed and left its race
// (session.cpp, Fail, stops the log). Safe to call from any thread.
bool UnderRollback(u32 address, u32 size);
// SHA-1 of bytes as lower-case hex: how a replay's header names the mod image and the native module.
std::string Sha1(const u8* data, std::size_t size);
}  // namespace fzgx
