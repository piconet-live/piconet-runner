# The patch image

Our online part of the game: PowerPC routines and data at 0x81800000 (`mod.bin`, the **image**) and the list of
patched words of the game's code (`patches.json`). With `symbols.json` and `manifest.json` they are a **bundle**. The
native module is compiled from the game's code with these bytes in: bundle, module and game folder come from one image.

| File | What |
|---|---|
| `build.py` | Builds the native bundle: runs the generators of `routines/`, adds the patched sites' routines, proves every site in the middle of a function safe, writes the four files. Holds the sites' constants |
| `sites.py`, `sites_camera.py`, `sites_drawn.py`, `sites_shadow.py`, `sites_effects.py` | The patch list: a routine and an entry for every patched site, in order (`build.py` calls `add()`); the cameras' own words; a machine's drawn transform and its one reader, which the shadow and the effects take after a rollback |
| `dol.py` | Reads the stock executable: the word at an address |
| `liveness.py` | The proof that a register is dead after a patched site |
| `relink.py` | main.rel relocated as the game's loader links it |
| `gameinput.py` | From the game's files and a bundle: the executable that is booted, and the recompiler's input |
| `fork_bundle.py` | The retired fork's bundle (`mod.bin`, `codes.txt`, `manifest.json`): what a reference run on the emulator's own compiler is given. It is the only bundle a race on the JIT can run: the JIT has no host calls (only the native core's `StaticRecompCore` takes them), so a native bundle under `debug.jit` boots but never gets the frame tick, and the race is set up only by the fork's codes (`ApplyReferenceConfiguration`, `port/moderngekko/src/host.cpp`). It has none of the native form's race sites (`sites_cpu.py`: it keeps the rubber band; `sites_grid.py`, `sites_mines.py`, `sites_seats.py`, `sites_custom.py`), so it can be the reference only of a race that uses none of them: never of a race from 0.4.0 on, which runs with the band off. No tracked script builds or stages it (`port/build/stage-launcher.sh` takes its `codes.txt`); the gates' `launcher-ref` is archived. Kept as the JIT's only way into a race (the duplicate-paths audit, row 11) |
| `same-race.json` | Which images play each other's replays. A row comes from a run, never from reasoning. `released`: the images handed to players; a new image needs a row from each (a player copy is refused without: `port/build/same_race.py`) |
| `routines/` | The generators: the assembly text of every routine ([README](routines/README.md)) |
| `tests/`, `check.sh` | The unit tests, and the check that builds every generator and runs them (`port/check.sh` calls it) |

## Build

```
W=<work>; py=.venv/bin/python  # the work folder; the checkout's venv has the assembler
$py port/image/build.py OUT --rel $W/derived/main.rel --dol $W/extracted/GFZE01/sys/main.dol.orig  # the native bundle
$py port/image/fork_bundle.py OUT2                                                                 # the fork's
```
Without `--rel` and `--dol` the bundle is marked incomplete (the tests' form; a host refuses it). `OUT` is outside
the repository. After an image change the game folder, the module and the staged launcher are made again
(`port/build/README.md`).

## Check

`bash port/image/check.sh`: every generator builds (its assertions run), then `tests/`. Without the `.venv` it says so.

## The oracle

A move, a split or a tidy-up here changes no byte. Build both bundles before and after and compare `mod.bin`,
`patches.json`, `symbols.json` and `codes.txt` with `cmp`; `manifest.json` differs in `commit` only. This tree's image
(the final build's: aa956d74, 14,832 bytes, 83 patched words): `mod.bin` d596453b (19,828 bytes, 97 patched words),
`patches.json` 29a9503a, `symbols.json` 8df93c3a; the fork's `mod.bin` 538d4b16, `codes.txt` 4bdbf395. A changed image is a new game build: say so.
