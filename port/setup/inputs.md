# What a build needs that is not in the repository

`N` is a work folder made by `setup.sh`; `W` the work folder `work-folder.env` names (the clone's `build/work`, or a
PC's own in `~/.config/piconet/work-folder.env`; the dated passages below were seen in earlier work folders).

**The rule: nothing old is kept to make a build reproducible.** Every build input has a
recipe here and is made fresh; a build is "rebuildable" when a fresh build from the repository and the disc passes
the gates, not when it has an older build's bytes. The one input that may be handed out beside a release is the
module's training profile; it is never committed.

| Input | Made by | The same bytes every time? |
|---|---|---|
| The source trees (`DolRecomp`, `ModernGekko`, `ModernGekko\vendor\dolphin` and its `DolRecomp`, `work\module-template`) | `trees.sh`: `forks/apply.sh` for each row of `forks/upstream.tsv` (our commit, the submodule `forks/<tree>`'s, fetched and checked out), by Linux git with LF line ends; `forks/check.sh --trees` | Yes: each tree has its pinned tree id, or the step fails |
| RecompCore's Externals (34 submodules and three of theirs) | `trees.sh`: upstream's, at the commits RecompCore's tree records | Yes |
| `extracted\GFZE01` | `disc.sh`: the disc as a folder, the two `*.orig` copies, the two pins | Yes: checked against `pins.tsv` |
| `derived\main.rel` | `disc.sh` (`mainrel.py`: decrypts and unpacks the disc's `enemy_line/line__.bin`) | Yes: checked |
| The movie player's two images (`derived\loaded-*.bin`) | `disc.sh` (`ondemand.py`: the disc's two module files relinked as the game links them). No game is run | Yes: checked |
| The build scripts, machine files, emulator configuration, a first boot's user folder | Tracked (`port/build`, `recomp/`, here); `install.sh` copies them and writes the work folder and LLVM where they say `@WORK@`, `@WORK_WIN@`, `@LLVM@` and `@LLVM_WIN@`, and the two other tools' paths, as `tools.py` finds them | Yes |
| The busy check, the quiet check | A PC's own (`busy-check.sh`, `quiet.sh` in the folder `pins.tsv` PC_SCRIPTS_DEFAULT names; in no repository); installed when there and absent from the work folder, never overwritten. a `work-folder.env` with the folders the tracked one resolves to is put beside them: the busy check reads the locks' folder from it | Yes |
| `kit-win\launch.ps1` (starts every test game) | Tracked in the gates (piconet-gates, private; `pins.tsv` REPO_GATES): the harness's Windows scripts, `vendor/harness/win` (copied from recomp-kit); `install.sh` copies them where the gates are (`RECOMP_KIT=<a recomp-kit checkout>`: that one's) | Not a build input |
| The tools | Installed by hand; `setup.sh` looks for them, `pins.tsv` says where they are and where to get them | Public downloads, fetched by no script |
| `work\dolrecomp-port\dolrecomp.exe` | `port/build/build.sh`, step 1 | See "Built twice" |
| The bundle, the game folder | Steps 2 and 3 (`port/image/build.py`, which needs the Python module `keystone`; `native-game.sh`) | Yes; the bundle's `manifest.json` holds the repository's commit |
| The runner | Step 4 | See "Built twice" |
| The profile | Handed out with a release (`setup.sh --profile FILE` puts it in `N\input`), or trained by the gates' `runs/profile.sh NAME`: an instrumented module and two test games (needs the gates and their harness); `PORT_PROFILE=none` builds a module without one | **No**: its counts come from real runs. A module's manifest records its SHA-1 |
| The module | Step 5 | See "Built twice" |
| A save file for boots (`port\user-pristine`) | Step 7 (`FIRST_BOOT=1 port/build/build.sh NAME`): a first boot of the game through the staged launcher creates it | No (a memory card holds times); not a build input |
| The gate scenes' saved states | **Kept** (`kept.tsv`): no recipe makes one the gates can use. See "The gate scenes' saved states" | No. Gate inputs, not build inputs |
| Recordings the gates play back; the JIT reference's match race (run nmj) | **Cannot be made again**: `kept.tsv`. `kept.sh WORK --from REFERENCE` copies them from a folder that has them and checks them | - |

## Line ends

`forks/apply.sh` makes every checkout with Linux git, `core.autocrlf=false` and `core.eol=lf`, so a file has the
line ends the repository stores, whatever Git for Windows is set to. That matters for DolRecomp, whose build
hashes its own sources into every key of its object cache (its configure step prints "Codegen source digest"), and
it differs from `W`, whose runner trees were checked out by Git for Windows with CRLF: same content, same tree ids.

## Built twice

The three links are told to leave the time out of what they write: `/Brepro`, one switch in each of the
three configure lines (the superbuild's `CMakeLists.txt` and `port/build/cmake/PortModule.cmake`). Without it (2026-10-06,
a second work folder) two builds of the same inputs differed only in the linker's time stamp: the recompiler in 6
bytes, the module in 2 or 3, the runner in 4. With it, seen on 2026-10-07:

| Built twice | Result |
|---|---|
| The runner, in the same folder, each time from an empty build folder | One SHA-1 (94e9fde5) |
| The runner, in two folders (`W3`, `W3-try`) | **Different**, in size too: the runner holds its source folder's path. Two PCs give the same runner only from the same folder path |
| The recompiler, in those two folders | One SHA-1 (457ca601), and the same "Codegen source digest" as n6's tool (14fc1ecfb571b14c) |
| The module | Not built twice with the switch in the build files (on 2026-10-06 the same objects linked twice with it had one SHA-1) |

Two builds are otherwise compared by what they were made from: `port/build/manifest.py compare A B`.

## A fresh build, end to end

Seen on 2026-10-07 from a clean clone at cde48b4, by the two commands of the README and nothing else, in
a new work folder `W3`: `setup.sh` 10 minutes (the trees 9, of which the Externals most); `build.sh w3a` 24
minutes with its waits for other agents (recompiler half a minute, runner 6, module 12.5, launcher 1, first boot
1). Image 62b3d9bf (n7's), module db51b4b9 (7,159 objects, 14,318 `closed_ok`, 1 function without its profile: n7's
figures), runner 94e9fde5; all three manifests complete. The profile was n7's (3238b80c), given by `--profile`.
The first boot made the save file and reached the mode menu; the gate rows `checks` and `offline` passed on it
(a race of 587 fields at 16.683 ms a field, modes at fields 1,218 and 2,502). No other gate has seen it.

Seen again on 2026-10-08 on another drive, the shared work folder (the final build; branch `chore/w12-final-build`), with a
profile trained on the spot: `setup.sh` 14.3 minutes; recompiler 24 s (457ca601 again); runner 6.5 minutes from
nothing; `profile.sh` 9 minutes (the instrumented module 7.8 on an empty cache, a first boot and 7,087 fields of
training 72 s); module 9.3 minutes (0 functions without their profile, 14,354 `closed_ok`); launcher 21 s; first
boot 63 s. The cache after both module builds: 1.1 GB. Image aa956d74, as `main` built it then (`port/image/README.md`
names today's).

## The gate scenes' saved states

`derived\race300.sav` and `racego.sav` are kept inputs (`kept.tsv`), recorded by hand under the JIT on 4 and 5
October from a stock boot with two older codes. `state.sh` does not replace them (tried 2026-10-08):

- `state.sh NAME FIELD` (the JIT, as its header had it) boots the native bundle under the emulator's own compiler:
  half a field a second, 85 of 5,155 fields in its 170 s. No state.
- `state.sh NAME FIELD BUILD` (a native module) works: under a minute, the state asked for 20 to 40 fields late.
- But a state from a boot of the native form holds the patch list's words, and row `batch`'s reference is the JIT
  with no bundle: it stands on such a state (race frame +0, 62 of 1,800 fields in 150 s), while the native run of
  the pair is whole.

A recipe needs a state from a stock boot under the JIT: `extracted\GFZE01`, no bundle, a launch file with `debug.jit`
only, which is how the kept two were made (the JIT runs the stock game at speed). The other way, a reference with
the bundle, needs the JIT to run the native bundle at speed, and the fallback JIT is never made faster.

## The manifest

`port/build/manifest.py` writes `build-manifest.json` into a build's own output folder (`work\mod-cg-NAME`,
`work\dolrecomp-port`, `ModernGekko\build`): the repository's commit and every file that differs from it, each
tree's commit and tree id, the SHA-1 of `forks/upstream.tsv` and of every patch, of the build scripts as the work
folder runs them, every switch, the SHA-1 of every input (the game's code, the bundle, the movie player's images,
the profile, the module template), the tools' versions, the outputs' hashes. `complete` only when nothing is
missing, nothing differs from a commit and no input changed while the build ran: **a build without a complete
manifest is a test build and is never shipped.** `show FILE`, `check FILE`, `compare A B`.
