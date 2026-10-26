// The status board under the fuzzer (fuzz_packs.cpp calls it every fourth input; fuzz_replays.cpp says what the
// fuzzers do): the game's side of the launcher's board (board.cpp), its file scribbled over between pictures.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fuzz.h"

#include "board_net.h"
#include "fzgx/board.h"

// The status board (board.cpp): a board the launcher made, then scribbled over by another process between the game's
// pictures (any byte of the mailbox, the status, the events, the pulse, now and then the header), the game reading
// its mailbox once a picture. The rule: the process survives, and nothing but the launcher's stop stops it.
void FuzzABoard(Rng& rng, FuzzHost& host, const std::string& dir)
{
  const std::string path = dir + "/current.board";
  std::string bytes(board::SIZE, '\0');
  board::View(bytes.data()).Make(4321);
  WriteFile(path, bytes);
  Options options;
  options.board = path;
  host.stops = 0;
  Init(host, options);
  if (!board::Taken())
  {
    std::fprintf(stderr, "fuzz board: the board made as the launcher makes it was not taken\n");
    std::abort();
  }
  for (u32 frame = 0; frame < 40; ++frame)
  {
    board::Field();
    std::fstream f(path, std::ios::in | std::ios::out | std::ios::binary);
    for (u32 k = rng() % 6; k > 0; --k)
    {
      const u32 at = rng() % 8 == 0 ? rng() % 32 : board::at::MAILBOX + rng() % (board::SIZE - board::at::MAILBOX);
      const u32 where = rng() % 3 == 0 ? rng() % board::SIZE : at;
      f.seekp(where);
      const char c = static_cast<char>(rng() % 4 == 0 ? 0xFF : rng());
      f.write(&c, 1);
    }
  }
  Shutdown();
}
