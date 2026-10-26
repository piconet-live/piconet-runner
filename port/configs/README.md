# Named launch files

A launch file is every setting of one start of the game (`--config FILE`; `protocol/formats.md` section 2). The
files here are the set-ups the tests and the gates (piconet-gates, private, checked out at `port/setup/pins.tsv`
REPO_GATES) start a game with: a script names one (`CONFIG=forced-7x17 replay-check.sh ...`) and exports no setting.
The gates' `runs/launch-file.py NAME OUT bundle=... replay=...`
fills the places in braces (`{bundle}`, `{replay}`, `{run}`: this run's folders and files) and writes the run's own
file, which is kept in the run's folder as `launch.json`. For a run of the harness it also adds what the harness
needs of the game (`--harness SCENE`: the automation folder and a saved state allowed, the user folder's emulator
settings left as the harness wrote them (`userSettings`), the probe's file, the scene's pad script, the timers and
dumps): the runner takes none of that from the environment or the command line
unless the launch file's `debug` part says so, so the files here name only what each test is about.

| File | The start | Used by |
|---|---|---|
| `gate-scene.json`, `gate-scene-single-core.json` | a saved-state scene on the native module, the patch list forced; the second with the picture on the CPU's thread | `gate-native.sh`, `time-native.sh` (rows `batch`, `timing`) |
| `gate-scene-reference.json`, `gate-scene-reference-single-core.json` | the same scenes on the emulator's own compiler (`debug.jit`), no bundle: the reference | `gate-native.sh` (row `batch`); the first also `time-native.sh` (row `timing`) |
| `playback.json` | a recording played back as the race (every playback is: the game's replay mode is not used), the scrubber off (`replay.scrubber`) | `replay-check.sh`, its default (rows `roundtrip`, `fork`; the end of `pair-run.sh` and `quickplay-run.sh`) |
| `scrubber.json` | a recording played back with the replay scrubber, stopping at the replay's end (`debug.replayEndQuits`); a script of pauses, jumps and scrubs given with `SET=debug.replayScript=...` | `replay-check.sh` (the scrubber's checks) |
| `playback-race-late-2.json` | as the race, its inputs known two steps late: real rollbacks | `replay-check.sh` (row `pair`) |
| `forced-*.json` (`EVERYxDEPTHS`) | a playback with rollbacks forced every so many frames | `replay-check.sh` (row `forced`) |
| `control.json` | the playback that must fail: the locked cache left out of the undo log | row `control` |
| `crash.json` | the 30-player crash (race w4b) without a network, the undo log's guard checked at every step (`undoLog` both) | row `crash` |
| `boot-single-core.json` | a boot on the native form, the picture on the CPU's thread | the gates' `runs/profile.sh`, `state.sh` |
| `stat-pack-refused.json` | a match whose stat pack the game must refuse: it says which value and starts no race | `runs/stat-pack-refused.sh` (for `stat-pack-run.sh`; no gate row) |
| `offline.json` | a plain start with no bundle | the manifest's scenes `story-mission1`, `machine-preview-cstick` (written for each run: the scenes' notes) |

A new set-up gets a file here with an `"about"` line that says what it is for and which script uses it. A file
without a `"debug"` part starts the game as a player's start: no test setting is on. The core's tests read every
file here through the game's own reader (`port/core/tests/test_launch.cpp`), so a key that has gone fails a commit.
A game started through the launcher (`launcher-run.sh`, `pair-run.sh`, `quickplay-run.sh`) gets its launch file
from the launcher, which hands on the test settings of its own launch file (`autopilot`, `testLagMs`, `game`): the
scripts put the probe's file, the pad script, the pacing log, the automation folder's permission and, for a
reference run, `jit` into `game`.

`launcher/` holds named launch files of the **launcher's** (`Launcher.App --config FILE`: `title`, `runnerArguments`,
and what it hands on to the game). The game's reader refuses their keys, so they are not in this folder itself.

| File | The start | Used by |
|---|---|---|
