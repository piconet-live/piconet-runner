# The recomp port

F-Zero GX recompiled to native code (ModernGekko + DolRecomp) brought to parity with the retired Dolphin fork:
offline play from the launcher, races from a match file, replays, online races with rollback.

| Folder | What |
|---|---|
| `core/` | The host-side core: a C++20 library with no emulator include. `tests/run.sh` builds and runs its tests with g++ (part of `scripts/check.sh`). Its addresses are the facts table's (`facts/generated/facts.h`, which `include/fzgx/guest.h` includes). `third_party/picojson` is the JSON reader the fork used (BSD 2-clause). |
| `gates/` | The gates of the native form, a private repository of their own from the split (piconet-gates), checked out here where `setup/pins.tsv` REPO_GATES says, and optional: without them the runner builds and runs its core tests (`check.sh` says what it leaves out). They hold `gates.sh` (one table, every row), the judges, `runs/` (one game through a staged launcher, a match race, two games, Quick Play; the profile's training, `runs/profile.sh`), `reports/` and the test harness. A PC's own machine sharing (`cpu-window.sh`, the build lock) is in no repository: a folder the PC names, `setup/pins.tsv` PC_SCRIPTS_DEFAULT, used where it is there (`setup/lib.sh` `pc_take`). |
| `moderngekko/` | `src/`: the adapter, the one place where the core meets ModernGekko's runner: the runner's `--fzgx-*` arguments, `fzgx::Host` from the emulator core, the emulator settings a race needs, the per-frame tick, the network hook as a host call (and as the emulator's own hook for a run on the JIT), the stop request. A start needs its `--config FILE`: without one the runner starts no game. `port.cmake`: included by ModernGekko's build when `MODERNGEKKO_FZGX_PORT_DIR` names the port folder. `build.sh`: the superbuild's target `runner`, which mirrors `core/` and `moderngekko/` into the work folder and builds the runner there. The port's changes to ModernGekko and to its `vendor/dolphin` (RecompCore) are not here: they are commits on our forks, the submodules under `forks/`. |

No game data lives here: no generated code, modules, dumps, saved states, profiles or replays of real races.
