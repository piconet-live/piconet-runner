# The core

The part of the runner we wrote, as a C++20 library with no emulator in it: it sets up a race from a launch file or
a replay, links the game to the relay, rolls back, and records and plays replays. The runner reaches it through
`include/fzgx/`. Its tests run against plain memory, with no game. It is built into the runner by the CMake
superbuild (`CMakeLists.txt` at the root); the files it reads and writes: `protocol/formats.md`.

| Path | What |
|---|---|
| `include/fzgx/core.h` | What the runner calls: `Init` with the options, the boot settings, the frame and step hooks |
| `include/fzgx/host.h` | What the runner gives the core: the game's memory, the log, the disc's files, the folder of the player's saves, the pace, the time |
| `include/fzgx/guest.h` | The game's memory as the core reads and writes it, and the image's data window (`ModData`). It includes the facts table's generated header (`port/facts/generated/facts.h`): every address the core names (`layout::NAME`, `game::NAME`) |
| `include/fzgx/strip.h` | The game window's title strip as numbers: the launcher's colours and sizes, where its parts are at a width and a scaling, what a point of the window is. The adapter draws it (`port/moderngekko/src/frame*.cpp`) |
| `src/` | The library: [src/README.md](src/README.md) |
| `tests/` | The tests and the fuzzers: [tests/README.md](tests/README.md) |
| `third_party/picojson/` | The JSON parser the core uses (vendored, BSD 2-clause) |
| `check.sh` | The core's checks, run by `port/check.sh`: `tests/run.sh` |

The runner's build takes every `*.cpp` of `src/` (`port/moderngekko/port.cmake`), as `tests/run.sh` and
`tests/fuzz.sh` do: a file added there needs no line anywhere.
