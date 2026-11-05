// The host-side core of the F-Zero GX online mod: what it needs from the
// program that runs the game. One implementation per host: the ModernGekko adapter (port/moderngekko) and the test
// host (tests/). Nothing in the core includes an emulator's header.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fzgx
{
using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using s16 = std::int16_t;
using s32 = std::int32_t;

enum class LogLevel
{
  Notice,
  Warning,
  Error,
};

// The replay scrubber (Options::replay_scrubber): what the bar under a playback's picture shows, given once a
// displayed frame (Host::ReplayBar). Steps are the race's, counted from the relay's go.
struct ScrubBar
{
  u32 first = 0, last = 1;   // the race: its first step, and its last (the recording's last until the end is found)
  u32 played = 0;            // the step shown
  u32 buffered = 0;          // the front edge: every step below it can be shown at once
  bool standing = false;     // the picture stands: paused, at the end, or waiting for the buffer to reach a jump
  bool beyond = false;       // the pointer of a scrub is beyond what can be shown
  bool end_known = false;    // `last` is the replay's end
  bool heard = false;        // the replay plays forward at its own pace: the only time its sound is heard
  u32 laps = 0;              // lap marks: the steps at which the leader completed each lap, so far
  std::array<u32, 16> lap_at{};
};

// The scrubber's keys, pressed in the game's window (Host::ReplayKeys): each press, and each repeat of a key held down
// where it says so, at the system's key-repeat rate.
enum ReplayKey : u32
{
  KeyPause = 1,         // Space: pause or play
  KeyBack = 2,          // Left (repeats): five seconds back, playing or paused as before
  KeyAhead = 4,         // Right (repeats): five seconds on, playing or paused as before
  KeyFirst = 8,         // Home: the race's first step
  KeyLast = 16,         // End: the newest step buffered, or the replay's end
  KeyFrameAhead = 32,   // . (repeats): one frame on, paused (a replay that plays is paused first)
  KeyFrameBack = 64,    // , (repeats): one frame back, paused
  KeyRacerBack = 128,   // [ : the racer before (the race view; as the D-pad's left)
  KeyRacerAhead = 256,  // ] : the next racer (the race view; as the D-pad's right)
};

// The racer timeline under the replay bar (timeline.cpp): the race's events as the
// front edge found them, read from the machines' blocks after each buffered step, and each racer's energy, place
// and whether the computer drives it every SAMPLE_EVERY steps. Given to the host when it changed (Host::ReplayTimeline).
struct TimelineEvent
{
  enum Kind : u8 { KO = 1, Out, Retired, Finish, Lap, Lead, FastLap, Rail, Hit };
  u32 step = 0;     // the step whose run made it: its picture is the first to show it
  u8 kind = 0;
  u8 racer = 0;     // the racer it happened to (the victim of a KO, the new leader; of a hit, the lower seat)
  u8 other = 0xFF;  // the attacker of a KO, the leader before a lead change, the other machine of a hit; 0xFF: none
  u8 place = 0;     // the racer's place then (finish, lap), 1 first; 0: not known. A rail hit: how (timeline_hits.cpp)
  u32 value = 0;    // finish: the race time, ms; lap: the lap completed; fastest lap: its time, ms; a rail hit or a
                    // hit: the same hits folded into it (within a second of it)
};
struct Timeline
{
  static constexpr u32 MOST = 30;
  static constexpr u32 SAMPLE_EVERY = 32;  // steps between two samples of every racer
  u32 version = 0;            // grows with every change
  u32 first = 0;              // the race's first step (the bar's)
  u32 go = 0xFFFFFFFF;        // the step the race clock started at (GO)
  u32 racers = 0;
  u32 front = 0;              // the steps below it are buffered, and read
  u32 colour[MOST] = {};      // 0xRRGGBB, the seat's colour from the race's set-up; bit 24: a stand-in colour
  u32 end[MOST] = {};         // the step a racer's row stops at (finished or out); 0xFFFFFFFF: still racing
  u8 order[MOST] = {};        // the racer of each place: finished first, then racing, then out (final once settled)
  u8 out[MOST] = {};          // 1: the racer is out (its row ends with its death)
  char names[MOST][16] = {};  // the seat's name from the race's set-up (letters, digits and a few signs only)
  std::vector<TimelineEvent> events;  // by step
  // every SAMPLE_EVERY steps from `first`, one byte a racer: its energy, 0 to 127 of its most; bit 7: the computer
  // drives it; and its place then (1 first)
  std::vector<u8> samples, places;
};

// The advanced spectator controls (fzgx/spectator.h, spectator_frame.cpp): what the overlay over the game's picture
// shows, given once a displayed frame of a race watched (Host::SpectatorShow), and what its mouse asked for since the
// last ask (Host::SpectatorClicks). Picture only: the overlay reads these numbers, never the game.
struct SpectatorRow
{
  u8 seat = 0, rank = 0, laps = 0;
  bool out = false, finished = false;
  float energy = 0;  // 0 to 1
  // the observer HUD's (fzgx/observer.h): the seat's colour (0xRRGGBB) and name from the race's set-up, else its
  // machine's name; its gap to the leader (observer::Gap: kind, milliseconds, laps)
  u32 colour = 0;
  char name[16] = {};
  u8 gap_kind = 0;
  u32 gap_ms = 0, gap_laps = 0;
};
// The racer followed, for the observer HUD's racer card.
struct SpectatorCard
{
  char machine[24] = {};  // its machine's name, as the game holds it
  float speed = 0;        // km/h
  u32 lap = 0;            // the lap it is on, 1 first (at most the race's laps)
  u32 lap_ms = 0;         // the time of that lap so far; finished: its race time
};
struct SpectatorShow
{
  bool on = false;          // a race is watched: the overlay may be drawn
  bool overlay = true;      // not the clean picture (view): the course map is shown
  u32 view = 0;             // what is drawn over the picture (spectator::View): Z or the mode button, round
  u32 role = 0;             // who watches (spectator::Role): a spectator, a replay, a racer knocked out
  u32 own = 0xFFFFFFFF;     // a racer knocked out: its own seat
  bool may_scrub = false;   // a spectator or a replay; a knocked-out racer has no scrub, pause or live
  u32 follow = 0xFFFFFFFF;  // the racer followed
  u32 how = 0;              // 0 chosen, 1 the leader, 2 the closest battle
  u32 kind = 0;             // the camera kind (spectator::Kind); kind_chosen: the watcher picked one
  bool kind_chosen = false;
  u32 kinds = 0;            // the kinds the course allows (bits)
  bool follow_others = true;
  u32 lap = 0, laps = 0;    // the leader's lap (1 first) and the race's laps (0: a free run)
  u32 racing = 0;           // racers still racing (neither out nor finished)
  SpectatorCard card;       // the racer followed
  u32 count = 0;            // rows, in race order
  std::array<SpectatorRow, 30> rows{};
};
struct SpectatorClick
{
  u32 follow = 0xFFFFFFFF;  // a racer clicked (a standings row; a timeline lane or circle)
  int kind = -1;            // a camera kind's button
  bool overlay = false;     // the overlay's mode button: the next view (as Z)
  bool leader = false;      // its Leader button (as X)
  bool battle = false;      // its Battle button (as Y)
  u32 jump = 0xFFFFFFFF;    // a step to jump to (an event on the timeline: spectators and replays only)
};

// Our course map (fzgx/minimap.h, minimap_frame.cpp): the course's road as the game's map camera sees it, given once
// a course (Host::MinimapCourseLoaded), and the racers' dots, given once a displayed frame of a race
// (Host::ShowMinimap). Places are the map's own: x right and y up, -1 to 1 across the square the game's map picture
// fills. Picture only: the host draws these numbers, never reads the game.
struct MinimapCourse
{
  u32 course = 0;               // the course's number (0: none: no map)
  std::vector<float> points;    // x, y of each corner
  std::vector<u32> triangles;   // three corners each, far ones first (drawn in this order)
  std::vector<u8> shades;       // a triangle's light, 0 (dark) to 255
  std::vector<u32> edges;       // the road's outline as seen (its silhouette): two corners each, in drawing order
  std::vector<u32> edge_after;  // for each edge, the triangle it is drawn after (nearer triangles may hide it)
  std::vector<float> world;     // x, y, z of each corner in the course (the view turned: fzgx/minimap.h Turned)
  std::array<float, 7> camera{};  // the map camera: field of view (degrees), eye x, y, z, looked-at x, y, z
  // the start mark (fzgx/minimap.h StartMark), over the road in the colour `mark_colour` (0xRRGGBB), opaque: x, y of
  // each corner, x, y, z of each in the course, three corners a triangle (none: no mark)
  std::vector<float> mark;
  std::vector<float> mark_world;
  std::vector<u32> mark_triangles;
  u32 mark_colour = 0;
};
struct MinimapDot
{
  enum Role : u8 { Racer = 0, Rival, Leader, You };
  u8 seat = 0;
  u8 role = Racer;   // the rival, the leader and you are drawn bigger
  float x = 0, y = 0;
  u32 colour = 0;    // 0xRRGGBB, the machine's
  float wx = 0, wy = 0, wz = 0;  // its place in the course (the view turned)
};
// "Show inputs" (Options::show_inputs; inputs_overlay.cpp in the adapter): the local player's pad as the game reads it
// (PADStatus of port 0), or nothing. Picture only.
struct InputsShow
{
  bool on = false;
  std::uint16_t buttons = 0;
  std::int8_t stick_x = 0, stick_y = 0, cstick_x = 0, cstick_y = 0;
  std::uint8_t l = 0, r = 0;
  bool operator==(const InputsShow&) const = default;
};
struct MinimapShow
{
  bool on = false;   // the map shows: the game's HUD does (fzgx/minimap.h Shows)
  u32 course = 0;    // the course these dots are on (MinimapCourse::course)
  u32 count = 0;     // dots, in the order they are drawn (the last on top)
  std::array<MinimapDot, 30> dots{};
  float turn = 0;    // the view's turn asked (the C-stick left or right, fzgx/minimap.h TurnOf): -1 to 1, 0 none
  float tilt = 0;    // the view's tilt asked (the C-stick up or down, fzgx/minimap.h TiltOf): -1 to 1, 0 none
};

// The game thread's clocks so far (Host::GameClocks), for the replay scrubber's budget (scrub_budget.cpp): the time
// it waited for the speed limit (the emulator's throttle, which sleeps where the emulated clock is ahead of the
// host's), and the emulated processor's ticks (486 MHz; 12 to a time base tick), with those it spent idle.
struct GameClocks
{
  std::chrono::nanoseconds waited{};
  u64 ticks = 0, idle_ticks = 0;
};

class Host
{
public:
  virtual ~Host() = default;

  // The game's main memory (MEM1), the buffer the game's own stores land in: guest address 0x80000000 is byte 0.
  // Valid while the game runs; asked for again at every call into the core.
  virtual u8* Ram() = 0;
  virtual u32 RamSize() = 0;

  // The locked data cache (guest address 0xE0000000 on; 16 KiB on the console): no part of main memory, and the
  // game's scratch memory. F-Zero GX keeps its collision work area there (main.rel fn_1_14F04: [0x801A66CC] +
  // 0x1B0, 0xE0001DB0), and a step reads words of it that the step before wrote, so it is simulation state:
  // rollback saves and restores it with main memory. Null and 0 when the host has
  // none. The size is a multiple of 4096.
  virtual u8* Scratch();
  virtual u32 ScratchSize();

  // Stops the game as closing its window does (the launcher then takes over).
  virtual void RequestStop() = 0;

  // The emulation speed (1.0: real time) and the emulated CPU's clock factor, and setting both (lockstep's catch-up).
  virtual float EmulationSpeed() = 0;
  virtual float CpuClock() = 0;
  virtual void SetPace(float emulation_speed, float cpu_clock) = 0;

  virtual void Log(LogLevel level, const std::string& text) = 0;

  // The core has written words of the game's code into main memory (the native form's patch list): whatever the
  // host keeps about code there (compiled blocks, checks of the native module's pieces) is out of date.
  virtual void CodeChanged(u32 address, u32 size);

  // A file of the game's disc by its path on the disc ("vehicle/rgaze/rgaze.tpl"); false when it is not there.
  virtual bool ReadGameFile(const std::string& path, std::vector<u8>* out) = 0;

  // The folder the host keeps the player's memory card in, one file a save (the emulator's GCI files, named
  // <maker>-<game>-<name>.gci), by its UTF-8 path with no separator at its end. The core reads one thing there: the
  // controller options of the player's saved game, for a start that goes straight into a race. Empty (the default)
  // when the card is no folder, or there is none.
  virtual std::string CardFolder();

  // The folder the host keeps this player's own files of the game in (the emulator's user folder: settings, the
  // memory card, the logs), by its UTF-8 path with no separator at its end. The core writes one file there: the
  // line a race leaves when the game stops (race-summary.txt; protocol/formats.md, section 6). Empty (the
  // default): none, and no line is written.
  virtual std::string UserFolder();

  // What runs the game's code, for a replay's header: the engine ("native": the recompiled module; "jit": the
  // emulator's JIT) and the native module's SHA-1 (hex). Either may stay empty: the header then leaves it out.
  // Asked once, when a recording starts.
  virtual void Recorder(std::string* engine, std::string* module_sha1);

  // The times between the pictures the host has handed to the screen since this was last asked, oldest first, in
  // microseconds (from one present call's return to the next's): at most `most` of them into `out`, and how many.
  // Asked once a displayed frame of a race, for the summary line's "shown" and "uneven" (summary.h). A host that
  // shows no picture, or keeps no such times (the default), gives none.
  virtual u32 PictureIntervals(u32* out, u32 most);

  // A race frame's drawing is skipped (a late frame: present.cpp): the game draws nothing in it and its next frame
  // copies no picture, so the screen shows the picture before for one field more. Told on the game's thread when the
  // frame decides, before the frame's drawing would begin. The hook for a presenter that draws the held picture
  // again with the camera corrected (the high refresh re-draw); the default does nothing.
  virtual void PictureHeld();

  // The load before a race (fzgx/panel.h: while LOADING is up). True, before the emulated system starts: no speed
  // limit and no sound from here on. False, on the game's thread, when the course is loaded: the speed limit and
  // the sound as they were. Only the host's pace changes: the emulated clock, the disc's emulated times and what
  // the game computes are the same at any pace. While it is lifted EmulationSpeed answers the speed to go back to.
  // False without a true before it does nothing; the default does nothing.
  virtual void LoadPace(bool lifted);

  // What the panel before a race shows has changed (fzgx/panel.h, TheBoard), told on the game's thread: at the
  // start gate the game draws no picture, so a host that draws the panel presents one of its own now. It must not
  // wait for that picture: the game's thread goes on at once. The default does nothing.
  virtual void PanelChanged();

  // The time the relay session reads: the race clock it follows, the round trip, the length of a displayed frame.
  // The default is the steady clock, and a host that runs a game has no reason for another; the core's tests give
  // one they move themselves, so that a test of when steps run does not depend on how busy the computer is.
  virtual std::chrono::steady_clock::time_point Now();

  // The replay scrubber (scrub.cpp), each asked or told once a displayed frame of a playback's race: the keys pressed
  // since the last ask (ReplayKey bits; 0, the default: none); the step under the pointer while the mouse button is held on the bar, or
  // of a press and release between two asks, once (0xFFFFFFFF, the default: none); what the bar shows (the default
  // shows nothing).
  virtual u32 ReplayKeys();
  virtual u32 ReplayScrub();
  virtual void ReplayBar(const ScrubBar& bar);
  // The spectator overlay (SpectatorShow, SpectatorClick above); the defaults show nothing and click nothing.
  virtual void ShowSpectator(const SpectatorShow& show);
  virtual SpectatorClick SpectatorClicks();
  // The racer timeline (Timeline above), when it changed; the default shows nothing.
  virtual void ReplayTimeline(const Timeline& line);
  // Our course map (MinimapCourse, MinimapShow above), on the game's thread: the road once a course, the dots once a
  // displayed frame of a race; the defaults show nothing.
  virtual void MinimapCourseLoaded(const MinimapCourse& course);
  virtual void ShowMinimap(const MinimapShow& show);
  // The player's pad for "Show inputs" (InputsShow above), on the game's thread, when it changed; the default shows nothing.
  virtual void ShowInputs(const InputsShow& show);
  // The scrubber's undo log is packed by the host's packer: `out` takes the packed bytes, which may refer to `prefix`
  // (the bytes packed before them, given again to Unpack); Unpack fills exactly `out_size` bytes. False (the
  // default): none, and the bytes are kept as they are.
  virtual bool Pack(const u8* data, std::size_t size, const u8* prefix, std::size_t prefix_size, std::vector<u8>* out);
  virtual bool Unpack(const u8* data, std::size_t size, const u8* prefix, std::size_t prefix_size, u8* out,
                      std::size_t out_size);
  // The process's memory as the system counts it, in bytes: its own (private, committed), the most that was, what
  // is in use (the working set), the most that was; for a playback's last lines. False (the default): not known.
  virtual bool ProcessMemory(u64 out[4]);
  // The game thread's clocks (GameClocks above), asked on that thread twice a displayed frame of a playback's race.
  // False (the default): not known; the scrubber then takes every millisecond of a frame for work.
  virtual bool ThreadClocks(GameClocks* out);

  // Whether the undo log may write-protect pages of Ram() to learn of the first write to each (its guard,
  // page_guard.h): true only when every write into main memory, from any thread, goes through Ram()'s own mapping
  // and is made by the processor in user mode (a write through another mapping of the same memory, or one the
  // system makes, an I/O read straight into it, would not fault, and would be missed). `why` says why not. The
  // default: no.
  virtual bool RamGuardable(std::string* why);

  // A 64-bit hash of a page of memory, for the undo log's comparisons (never sent anywhere, so hosts may differ).
  // The default is a plain multiply-and-mix over 8-byte words; a host with XXH3 at hand passes that.
  virtual u64 PageHash(const u8* data, std::size_t size);
};
}  // namespace fzgx
