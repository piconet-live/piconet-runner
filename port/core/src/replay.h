// A race's replay. What it reads of a race it reads through the rollback interface (rollback.h:
// the inputs a step ran with, the checksum kept for it) and from the game's own memory (Guest: the machines' blocks
// and progress records). Nothing here knows the relay session (session.cpp, step.cpp): the session says which steps are final,
// which seats are players' and which seat is the local one, and no more. Lifted from the Dolphin fork
// (dolphin-fzgx Core/FzgxNet.cpp at 5bdc7c640e).
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "fzgx/guest.h"
#include "util.h"

namespace fzgx::net
{
// A racer as the game holds it now, from its block (MACHINE_SIZE bytes each) and its
// progress record (+MACHINE_PROGRESS): flags +0x0 (0x10000 finished, 0x80 out), rank, laps completed, the sum of the
// completed laps (the finish time once finished) and the best lap, each time as frames u32, fraction f32, minutes u8,
// seconds u8, milliseconds u16 (RaceMs). What a replay's results hold (Recorder::TakeResults) and what the status
// board shows of a racer (board::Race) are both read here.
struct RacerRecord
{
  u32 block = 0;
  u32 progress = 0;
  u32 flags = 0;
  bool finished = false;
  bool out = false;  // the flag's word alone (the board asks out_test for the game's own verdict)
  u8 rank = 0;
  s32 laps = 0;
  u32 lap_sum_ms = 0;
  u32 best_lap_ms = 0;
};
// Machine i of the race's blocks at `machines`; none when it has no progress record.
std::optional<RacerRecord> ReadRacer(Guest& g, u32 machines, u32 i);
// A time of a progress record at `at`, in milliseconds.
u32 RaceMs(Guest& g, u32 at);

// A replay being recorded (protocol/formats.md section 1). The file's header and frame preamble are
// written before (boot::StartReplay, or the launcher); this appends the lineup at go, every final step, and at the
// end the race's results.
class Recorder
{
public:
  bool Open(const std::string& path);
  bool IsOpen() const { return m_file != nullptr; }
  void WriteLineup(Guest& g);
  // The steps below final_steps not written yet: for each the player seats' inputs and the state checksum.
  void Record(Guest& g, u32 final_steps, const std::vector<int>& seats);
  // A practice's kept run (practice_session.cpp): its steps one by one, the next step's row and its checksum.
  void WriteStep(const std::vector<u8>& row, u32 checksum);
  void Flush();
  // Every racer's result, read from the game's memory as it is now; written by Close. For the caller to say when:
  // once the race is over and no step so far is a prediction.
  void TakeResults(Guest& g);
  bool HasResults() const { return !m_results.empty(); }
  u32 Recorded() const { return m_recorded; }
  void Close();

private:
  std::unique_ptr<File> m_file;
  u32 m_recorded = 0;
  std::string m_results;
};

// A replay file read back: the player seats, the recorder's seat (and its place among the player seats), and for
// every step the seats' inputs and the checksum the recording game kept.
struct Recording
{
  std::vector<int> seats;
  int seat = -1;
  int index = -1;
  std::vector<std::vector<u8>> rows;
  std::vector<u32> checksums;
};
// false, with a line in the log, when the file is no replay of a version the game plays (2 to 7).
bool ReadRecording(const std::string& path, Recording* out);

}  // namespace fzgx::net
