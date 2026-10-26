// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <optional>
#include <string>

#include "fzgx/core.h"
#include "util.h"

namespace fzgx
{
Host& TheHost();
const Options& TheOptions();

// boot.cpp, apart from IsNative, Patches and ApplyPatches (files_bundle.cpp) and StartReplay (replay.cpp). What the file readers give each other and boot.cpp is in files.h.
namespace boot
{
struct Relay
{
  std::string host;
  u16 port = 0;
  u64 token = 0;
};

// Reads the match or replay file again (Init).
void Reset();
bool IsActive();
std::vector<Code> Codes();
// The native form: the bundle is that kind; its patch list; before boot, the image, the mode word and (for a race or
// a replay) the race's values into the data window; the list's words into the game's code.
bool IsNative();
const std::vector<Patch>& Patches();
void Prepare(ModData& data);
PatchReport ApplyPatches(Guest& g);
void OnFrame(Guest& g);
// main.rel's header as the game holds it now: the head of the system's module list when that is main.rel (by its
// module number) with its data in main memory after it; 0 while none is linked, or another module is at the head.
u32 MainRel(const Guest& g);
std::optional<bool> Widescreen(const Guest& g);
// The word of true widescreen (layout::TRUE_ASPECT) for these options: 0, or the aspect's float bits.
u32 TrueAspectWord(const Options& o);
// options_screen.cpp: a start that opens the game's own controller options (Options::controller_setup). Every
// picture of such a start: asks the game for its Options once the title stands after its memory card sequence,
// then for the controller menu, and stops the game when the player leaves that menu.
void OptionsScreenTick(Guest& g);
void ResetOptionsScreen();
// stat_pack.cpp: the race's stat pack, if it has one. Every frame: its values over the game's car data, until the
// race runs. When the race is let go: why it may not start (the table does not hold the pack); nothing: it may, and
// so may a race without a pack. What the racers hold of the pack then is a line of the log, never a refusal.
struct StatPack;
struct Match;
void LayStats(Guest& g, const StatPack& pack);
std::optional<std::string> StatsRefusal(const Guest& g);
void ResetStats();
// The loaded car data stands where the game keeps it, as its loader leaves it (stat_pack.cpp).
bool TableStands(const Guest& g);
// const_pack.cpp: a stat pack a seat. Every frame until made: the rows of the seats whose stock machine races a pack
// of its own, from the loaded car data before the race's pack is laid over it. At the go: why the race may not start
// (a row not on, the seats' constants not the match's); nothing: it may.
void LaySeatRows(Guest& g, const Match& m);
std::optional<std::string> SeatsRefusal(const Guest& g, const Match& m);
void ResetSeats();
// Once the machines are loaded (after the countdown's load): repaints every seat with a colour in the match.
void PaintMachines(Guest& g);
std::optional<Relay> GetRelay();
// Starts a replay file at path: the header and the frame preamble.
bool StartReplay(const std::string& path);
}  // namespace boot

namespace net
{
void Run(Guest& g);
void Shutdown();
// The exit code the process ends with (fzgx::ExitCode): EXIT_RACE_OVER once a relay said the race a game was started
// again into is over (relay_wire.cpp); EXIT_LEFT_RACING for a game that stops while its seat races (Shutdown); else 0.
int ExitCode();
void ClearExitCode();
void SetExitCode(int code);
// scrub.cpp (the replay scrubber): the frame routine's extra steps are done (WindowWritten).
void StepsDone(Guest& g);
// Our course map (minimap_frame.cpp), at every game frame of a race or replay start.
void MinimapFrame(Guest& g);
void MinimapSummary();  // its line in the log, at the end
// A late frame's drawing skipped (present.cpp): the frame tick; NormalStep's word that the frame is a race frame
// that may skip; the image's store into DRAW_ASK (WindowWritten); the session's end (its line in the log).
void PresentTick(Guest& g);
void PresentRaceFrame();
void PresentAsked(Guest& g, u32 value);
void PresentEnd();
}  // namespace net

namespace paint
{
struct Rgb
{
  u8 r = 0, g = 0, b = 0;
};

void PaintMachines(Guest& g, const std::array<std::optional<Rgb>, 30>& colors, const std::array<int, 30>& stock,
                   int main_racer);
}  // namespace paint
}  // namespace fzgx
