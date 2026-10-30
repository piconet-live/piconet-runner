// The status board inside the core (fzgx/board.h is the host's side): opening it, the frame tick's heartbeat, the
// start gate, a desync, the race's snapshot, the mailbox's stop. board.cpp keeps the board; board_race.cpp reads the
// race and the session for it; board_map.cpp maps the file.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include "fzgx/board.h"
#include "fzgx/guest.h"

namespace fzgx::board
{
// Init: maps the launch file's board and takes it (an empty path: none, and nothing else here does anything). A
// board that is not one, or of another version, or too small, is said in the log and left (the launcher then goes
// without: no live race, no freeze detection; the exit code and the log's stop lines stay).
void Open(const std::string& path);
// Lets the board go (Init's next start, tests; the process's end lets it go by itself).
void Close();
// The frame tick of a race's or replay's start, on the game's thread: the heartbeat.
void Beat();
// The start gate's wait, on the game's thread (no game frame begins there, so no picture says it).
void AtGate();
// The core's own wait for the relay's official inputs has begun, on the game's thread (no frame begins meanwhile):
// the status says so until the next frame.
void Waits();
// The relay found this game's state different from the others' at `step`.
void Desync(u32 step);
// Every displayed frame of a race (not a replay), on the game's thread: the race as this game simulates it.
void Race(Guest& g, u32 step, int local_seat);
// Every displayed frame of a race (not a replay), on the game's thread, after Race: the local racer's chase camera
// view (1 to 3) as a Camera event, at its first reading in the race and when it changed. `racing`: the local racer
// races (its camera is the player's own: neither a spectator's nor a finished or out racer's).
void CameraView(Guest& g, bool racing);
// board.cpp's side for CameraView: the event.
void Camera(u32 view, u32 before);
// The launcher asked for the stop through the mailbox: the game stopped because the launcher wanted it, so no race
// is kept for a rejoin (session.cpp Shutdown).
bool LauncherStopped();
// board.cpp's side for board_race.cpp: the snapshot onto the board; and forgetting a race's counts (Open).
void PutSnapshot(const Snapshot& snapshot);
// A game mode's blocks (mode_board.cpp): its head, once a start; its values, each with the next serial.
bool ModeHeadWritten();
void PutModeHead(const ModeHead& head);
void PutModeValues(ModeValues& values);
void ResetRace();

// The mapping (board_map.cpp): the file's bytes in this process, or none.
class Mapping
{
public:
  Mapping() = default;
  Mapping(const Mapping&) = delete;
  Mapping& operator=(const Mapping&) = delete;
  ~Mapping() { Close(); }
  // False, with `error` saying why, when the file cannot be opened or mapped.
  bool Open(const std::string& path, std::string* error);
  void Close();
  void* At() const { return m_at; }
  std::size_t Size() const { return m_size; }

private:
  void* m_at = nullptr;
  std::size_t m_size = 0;
  void* m_file = nullptr;     // Windows: the file's and the mapping's handles
  void* m_mapping = nullptr;
};
u32 ProcessId();
}  // namespace fzgx::board

namespace fzgx::net
{
// What the session says of where the game is (board_race.cpp), for the board's phase: nothing while there is no
// session (a race's start before its first network hook: the course loads). `step`: the race's step then.
std::optional<board::Phase> SessionPhase(u32* step);
}  // namespace fzgx::net
