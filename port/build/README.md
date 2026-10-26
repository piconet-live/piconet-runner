# port/build: the builds, in order

The runner's build is one CMake superbuild at the checkout's root (`CMakeLists.txt`) with a preset per platform
(`CMakePresets.json`; Windows today). After [../setup](../setup/README.md) made the work folder, `build.sh` (below)
builds it from WSL, wherever the clone is (in WSL's own file system, the preferred place, or on a Windows drive). With
the clone on a Windows drive, CMake's commands also work by hand, from a Developer environment (Visual Studio's
`vcvars64.bat`, which `port/setup/tools.py --say` finds, with Python, ninja and LLVM's `bin` on `PATH`, or LLVM named
by `PORT_LLVM_ROOT`; the [runner's README](../../README.md#building) has a batch file):

    cmake --preset windows                          # PORT_WORK: the work folder (default build/work in the clone)
    cmake --build --preset windows                  # the recompiler and the runner
    cmake --build --preset windows --target module  # the native module (and the image it is built from)

The module needs the work folder's inputs from your disc (`setup.sh` makes them) and a training profile:
`PORT_PROFILE=FILE` (default `PORT_WORK/input/module.profdata`, where `setup.sh --profile` puts the one handed out with
a release; none is handed out yet) or `PORT_PROFILE=none` (the same game, slower), and the image needs a Windows Python with the module
keystone (`pip install keystone-engine`; `PORT_PYTHON=...\python.exe`). Your own values go in `CMakeUserPresets.json`
(ignored by git), a preset that inherits `windows`, or on the configure line as `-DNAME=VALUE`:

| Setting | What |
|---|---|
| `PORT_WORK` | The work folder (`port/setup/setup.sh`), on a Windows drive: its trees are what is compiled |
| `PORT_BUILD_NAME` | The build's name (default `dev`): `port/bundle-NAME`, `work/cg-NAME`, `work/mod-cg-NAME` |
| `PORT_PROFILE`, `PORT_PYTHON`, `PORT_LLVM_ROOT` | As above |
| `PORT_SWITCHES`, `PORT_EXTRA`, `PORT_BUNDLE`, `PORT_PROFILE_GENERATE`, `PORT_RECOMPILER` | The module's recompiler switches (default `pins.tsv` `REPO_SWITCHES`, the released ones), its extra text sections (default the movie player's two modules; `none`), another bundle, an instrumented module (the gates' `runs/profile.sh`), another recompiler (`native-module.sh` `TOOL`) |
| `PORT_HOST_TIMERS` | A profiling runner (never a player's) |

| Target | Builds | Into the work folder |
|---|---|---|
| `recompiler` (in `all`) | DolRecomp with the LLVM backend, from `WORK/DolRecomp` | `work/dolrecomp-port/dolrecomp.exe` |
| `runner` (in `all`) | ModernGekko with the port, from `WORK/ModernGekko`; the port's sources mirrored into `WORK/port/src` first | `ModernGekko/build/moderngekko-run.exe` |
| `image` | The mod's native form (`port/image/build.py`) | `port/bundle-NAME` |
| `module` | The recompiler's code for the game with the image (`cmake/PortModule.cmake`), compiled with clang | `work/cg-NAME`, `work/mod-cg-NAME/gGFZE01_recomp.dll` |

Why a superbuild (`ExternalProject`) and not one project with `add_subdirectory`: the three projects are built with
two compilers (MSVC for the recompiler and the runner, clang for the module), the module's project can only be
configured after the recompiler built in the same build has written its sources, and the forks are built as their own
projects, unchanged at their pins (`forks/upstream.tsv`), with the configure lines every earlier build used: their
outputs keep their bytes.

## build.sh: the whole game build, with this PC's machine sharing

In WSL, one command builds everything (steps 1-6) through the same preset and targets:

    port/build/build.sh NAME          # in the clone's build/work; PORT_WORK=WORK for another work folder

| Step | Script | Needs beyond the work folder |
|---|---|---|
| 1. the recompiler | `recompiler.sh` (target `recompiler`) | - |
| 2. the image | target `image` | a Windows Python with `keystone` (`PORT_PYTHON=/mnt/c/.../python.exe`; a venv in the work folder is fine) |
| 3. the game folder | `native-game.sh` | - |
| 4. the runner | `port/moderngekko/build.sh` (target `runner`), kept as `port/runner-NAME` | the work folder's trees at this checkout's pins (`forks/check.sh --trees`) |
| 5. the module | `build-module.sh` (target `module`, through `native-module.sh`) | the profile: `PORT_WORK/input/module.profdata` or `PORT_PROFILE=FILE` (or `PORT_PROFILE=none`) |
| 6. the staged launcher | `stage-launcher.sh`, into `port/launcher-NAME` | `dotnet` (.NET SDK 10) and nuget.org |
| 7. only with `FIRST_BOOT=1`: a first boot, which makes the save file the gates' test games start from | the gates' `runs/launcher-run.sh RUN boot-first` | the gates (piconet-gates, private, checked out at `pins.tsv` REPO_GATES), and the harness's `launch.ps1` in `PORT_WORK/kit-win` (`port/setup/install.sh`) |

The step scripts run the Windows `cmake` in the Developer environment that `port/setup/tools.py` finds (vswhere's
`vcvars64.bat`, then Python and its `Scripts` in front of `PATH`; `port/setup/lib.sh` `port_cmake`), with the
superbuild's build folder in the work folder (`WORK/cmake/<checkout>-<hash>`), so a checkout on WSL's own file system
builds too. Around the targets they keep what is the PC's or the release's, not the compiler's: the busy check, the
build lock and the CPU window, the build manifests, the kept copies (`runner-NAME`), the refusal of a module name that
exists.
Every part is kept under NAME, as `scripts/release-build.sh` keeps them; a step whose result is there is skipped
(the two compilers' always run: ninja rebuilds what changed); the last lines name the game build. A PC's own machine sharing (the busy check, the build lock and the CPU
window, in the PC's own folder PC_SCRIPTS) is used where it is there; a clone builds without it and says so once (`port/setup/lib.sh`, `pc_take`). Then, with the gates, `PORT_WORK=... LAUNCHER=launcher-NAME port/gates/runs/launcher-run.sh RUN` boots the game, goes through the menus and races. `PORT_PROFILE=none` builds the module without a training profile (the same game, slower).

| File | What |
|---|---|
| `build.sh` | The six steps, and the seventh with `FIRST_BOOT=1` |
| `recompiler.sh` | `dolrecomp.exe` from the work folder's DolRecomp tree into `work/dolrecomp-port` (target `recompiler`) |
| `cmake/` | The superbuild's recipes: `PortModule.cmake` (the module: the recompiler's input, the recompiler with the switches, the fast lookup tables, the module's compile), `PortSources.cmake` (the port's sources mirrored for the runner), `PortPins.cmake` (pins.tsv's rows) |
| `win/tools.bat` | Prints the Windows tools' versions (`port/setup/tools.py`, `manifest.py`) |
| `native-game.sh` | The game folder the native form boots: the extracted game with the bundle's words in its executable (`BUNDLE=`, a folder of the image's own) |
| `build-module.sh` | A native module built as a release's is: the released switches (pins.tsv `REPO_SWITCHES`), the movie player's two modules, the profile and the bundle (`PROFILE`, `BUNDLE`, both required). Refuses an existing name; `--dry-run` prints the commands |
| `native-module.sh` | The build itself (target `module`), which `build-module.sh` wraps. It deletes the name's folders first: never call it for a kept build |
| `lanes.switches.env`, `n6.switches.env` | The released module's switches (pins.tsv `REPO_SWITCHES`); module n6's (0.2.0), as its build recorded them |
| `stage-launcher.sh` | A launcher folder with runner, module, bundle and game folder beside it (`MODULE`, `BUNDLE`, `GAME`, `RUNNER`). It deletes its target first: never name a folder someone plays from. It says which released builds' replays the copy refuses (`same_race.py`, by `port/image/same-race.json`), for which `package.py` refuses a player copy |
| `linux.sh` | The port for Linux, in `~/linux-build`: the trees, the recompiler, the runner, a module from a Windows build's inputs, a game folder. Not yet a preset: it becomes the superbuild's `linux` preset (LLVM's clang, libc++ and lld as a toolchain file) when Linux is taken up |
| `package.sh`, `package.py`, `package_refusals.py`, `package_identities.py`, `package_names.py`, `player-copy/` | A player copy, the folder a tester is handed and its zip, in `<work>/dist`: the launcher published from the commit checked out, the named runner, module, bundle, `Sys` and VC runtime, one program at the top and the read-me from the template in `player-copy/` (no batch file), and `source/` (the runner's patches and the builds' manifests). Every part is named and checked before anything is written; nothing of the game is in a copy, and nobody's name: every file is searched for this PC's and the git users' names first, a source of names that cannot be asked refuses the copy (`package_names.py`), and `package_identities.py COPY` does the same for a copy that is already made |
| `manifest.py` | `build-manifest.json` beside each build: the commit, the trees, the patches, switches, inputs, tools, outputs. `show`, `check`, `compare`. A build without a complete manifest is a test build |

The game folder, the module and the staged launcher are the parts of a game build under the work folder, and the
launcher folder that holds them: the copy a gate is given. In order after an image change: the bundle
(`port/image/build.py`), then these. Everything they write is game data and stays in the work folder. Never
`port\bundle-native` or `port\game-native` for a new image: the play copy boots through them.
