# piconet-runner

F-Zero GX recompiled to native code, as Piconet's races run it: the game's port (`port/`), the recompiler's
machine files (`recomp/`), and the pins of the upstream trees it is built from (`forks/`). The launcher
([piconet-launcher](https://github.com/piconet-live/piconet-launcher)) starts it for offline play, replays and
online races with rollback. Piconet is online multiplayer for F-Zero GX: see
[piconet](https://github.com/piconet-live/piconet).

No game data is stored here: no generated code, modules, dumps, saved states, profiles or replays of real races.
A build reads your own disc image.

## Layout

| Folder | What it is |
|---|---|
| `port/` | The port: the host-side core (`core/`), the game image's patches (`image/`), the adapter in ModernGekko's runner (`moderngekko/`), the facts table (`facts/`), the work folder's setup (`setup/`), the builds (`build/`), the run configurations (`configs/`), the speed tools (`speed/`): [port/README.md](port/README.md) |
| `recomp/` | The recompiler's machine files and the disc's manifest |
| `forks/` | Our forks of DolRecomp, RecompCore and ModernGekko as submodules, and `upstream.tsv`, the one table of their pins: [forks/README.md](forks/README.md) |
| `external/protocol` | [piconet-protocol](https://github.com/piconet-live/piconet-protocol), a submodule: the generated headers the core includes, the shared test vectors |

The other parts are found through `port/setup/pins.tsv`: `REPO_PROTOCOL` (`external/protocol`), and the launcher's
and the service's checkouts beside this one (`REPO_LAUNCHER` `../piconet-launcher`, `REPO_SERVICE`
`../piconet-service`). The runner's build takes its icon and the staged launcher from the launcher's checkout, and
the core's tests and the setup's read a few of its files, so clone piconet-launcher beside this repository (with its
submodules). The service's checkout is read only for a launcher made for one of our online stacks. The gates are not part of this
repository: they are optional, checked out at `REPO_GATES` (`port/gates`) where they are used.

## Requirements

A Windows PC, and a clone on a Windows drive (no spaces in its path):

- Visual Studio 2022 Build Tools (17.x) with the C++ workload
- LLVM 20.1.8 for Windows with `lib/cmake` (`clang+llvm-20.1.8-x86_64-pc-windows-msvc`)
- cmake 4.1.1 and ninja 1.11.1 (on `PATH`; `pip install cmake ninja` into Python's `Scripts` is fine), Git for Windows
- Python 3.10 for Windows; the module's image also needs the module `keystone` (`pip install keystone-engine`)
- WSL, for the work folder's setup and the port's scripts: git, Python 3.11 or later, rsync, g++ with C++20 (for the
  core's tests), the .NET SDK 10 (for the staged launcher)
- your own disc image of F-Zero GX (USA), as an ISO

`port/setup/pins.tsv` pins each tool's version, never its place; `port/setup/tools.py --say` shows what is found
where. Your own settings live outside every clone, in `~/.config/piconet/` ([port/setup/README.md](port/setup/README.md)).

## Building

    git clone --recurse-submodules https://github.com/piconet-live/piconet-runner.git
    port/setup/setup.sh [--profile FILE]     # in WSL: the work folder (build/work in the clone): trees, tools, disc

The build is one CMake project with a preset per platform (`CMakePresets.json`; Windows today). From a Developer
PowerShell or Command Prompt for Visual Studio, with LLVM's `bin`, Python and ninja on `PATH`:

    cmake --preset windows
    cmake --build --preset windows                    # the recompiler and the runner
    cmake --build --preset windows --target module    # the native module, from your disc's files in the work folder

The native module needs a training profile, the one handed out with a release (`setup.sh --profile FILE` puts it where
the build looks) or another (`-DPORT_PROFILE=FILE`), or `-DPORT_PROFILE=none` for a module built without one (the same
game, slower), and a Python with keystone for its image (`-DPORT_PYTHON=...\python.exe`). Settings of your own (another
work folder, your tools' places) go in a `CMakeUserPresets.json` beside `CMakePresets.json`, which git ignores.

In WSL, `port/build/build.sh NAME` runs the same targets in order and adds the game folder and a staged launcher
(`PORT_PYTHON=/mnt/c/.../python.exe`). The settings, the targets and the steps: [port/build/README.md](port/build/README.md).

## Check

    port/check.sh

The plain check: the facts table's, the image's, the core's (built with g++) and the setup's checks. It needs no
work folder and no disc; without the gates' checkout it says that their checks are left out.

## Contributing

Fork the repository, make a branch, and open a pull request; `port/check.sh` must pass. Please discuss bigger
changes on Piconet's [Discord](https://discord.gg/DcauFmsGh) first. The details are in
[CONTRIBUTING.md](CONTRIBUTING.md).

## Licence

GPL-3.0-or-later (`LICENSE`). Files derived from Dolphin keep their GPL-2.0-or-later headers
(`port/core/src/util.h`); `port/core/third_party/picojson` is BSD-2-Clause. The forks keep their upstreams' licences.

The source of a released build is this repository at the release's tag (`vX.Y.Z`) with its submodules at the
commits that tag records (`forks/`: our forks of DolRecomp, RecompCore and ModernGekko; `external/protocol`), and
[piconet-launcher](https://github.com/piconet-live/piconet-launcher) at the same tag. A player copy's read-me says
so, and its `source/` folder holds the runner's and the module's build manifests, which name the commit and tree of
every source tree they were built from.
