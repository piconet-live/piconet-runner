# piconet-runner

[![CI](https://github.com/piconet-live/piconet-runner/actions/workflows/ci.yml/badge.svg)](https://github.com/piconet-live/piconet-runner/actions/workflows/ci.yml)

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

A Windows PC with WSL (Ubuntu). On Windows:

- Visual Studio 2022 Build Tools (17.x) with the C++ workload: `winget install Microsoft.VisualStudio.2022.BuildTools
  --override "--add Microsoft.VisualStudio.Workload.VCTools --includeRecommended --passive"`
- LLVM 20.1.8 for Windows with `lib/cmake`: `clang+llvm-20.1.8-x86_64-pc-windows-msvc.tar.xz` from
  github.com/llvm/llvm-project releases, unpacked. It is not on `PATH` by itself: put its `bin` on `PATH`, or name its
  folder as `TOOL_LLVM_WIN` in your settings (below)
- Python 3.10 for Windows: `winget install Python.Python.3.10`
- cmake 4.1.1 and ninja 1.11.1 on `PATH`: `py -3 -m pip install cmake==4.1.1 ninja==1.11.1`
- Git for Windows (`forks/check.sh` reads the work folder's trees with it): `winget install Git.Git`
- your own disc image of F-Zero GX (USA), as an ISO

In WSL:

- git, Python 3.11 or later, rsync, g++ with C++20 (for the core's tests): `sudo apt install build-essential python3
  python3-venv rsync git`
- the .NET SDK 10 (for the staged launcher). Ubuntu's package is older than the 10.0.401 that
  piconet-launcher's `global.json` needs, so install it with Microsoft's script:

      curl -sSLO https://dot.net/v1/dotnet-install.sh && bash dotnet-install.sh --channel 10.0
      export DOTNET_ROOT=$HOME/.dotnet PATH=$HOME/.dotnet:$PATH    # in ~/.bashrc too

  In WSL, Windows' `dotnet.exe` can come first on `PATH`; `command -v dotnet` should name `~/.dotnet/dotnet`.

`port/setup/pins.tsv` pins each tool's version, never its place; `port/setup/tools.py --say` shows what is found
where (Visual Studio's `vcvars64.bat`, LLVM, Python, cmake, ninja). Your own settings live outside every clone, in
`~/.config/piconet/` ([port/setup/README.md](port/setup/README.md)): `TOOL_LLVM_WIN` and the other tools' places, and
`DISC_DEFAULT`, your disc image.

The clone below is about 1.1 GB (a `--recurse-submodules` clone, about 5.3 GB, is not needed) and the work folder
about 5 GB; with the build's outputs, plan for about 25 GB. A first build takes about 40 minutes (the module about
12, the runner about 10).

## Building

Clone with git inside WSL, not Git for Windows: its `core.autocrlf` would give the `.sh` files Windows line ends.
The Windows compilers need the work folder on a Windows drive (no spaces in its path). The clone can be in either
place:

- **In WSL's own file system, with the work folder on a Windows drive (preferred).** Git is much faster there than
  under `/mnt/<drive>` (a `--recurse-submodules` clone took about 6 minutes there, against 24). You build with
  `port/build/build.sh`.
- On a Windows drive (under `/mnt/c/`, say; no spaces in its path), with the work folder in the clone
  (`build/work`). You can also build with CMake from a Developer prompt.

The module's image needs a Windows Python with keystone. A venv anywhere on a Windows drive, from a Windows prompt:

    py -3 -m venv C:\piconet-venv
    C:\piconet-venv\Scripts\python.exe -m pip install keystone-engine

and that `python.exe` is `PORT_PYTHON` (`/mnt/c/piconet-venv/Scripts/python.exe` from WSL).

The native module is built with a training profile, `PORT_PROFILE=FILE`. No release carries one yet, so for now build
with `PORT_PROFILE=none` (the same game, slower); a release will carry the profile (`setup.sh --profile FILE` puts it
where the build looks).

In WSL, for the preferred place:

    git clone https://github.com/piconet-live/piconet-runner.git
    git clone --recurse-submodules https://github.com/piconet-live/piconet-launcher.git    # beside it
    cd piconet-runner
    git submodule update --init                     # the protocol and the forks; not Dolphin's bundled libraries
    port/setup/setup.sh /mnt/c/piconet-work         # the work folder: trees, tools, disc (a WSL clone names one)
    PORT_WORK=/mnt/c/piconet-work PORT_PROFILE=none PORT_PYTHON=/mnt/c/piconet-venv/Scripts/python.exe \
        port/build/build.sh NAME

`setup.sh` fetches the libraries the runner needs (RecompCore's Externals) into the work folder itself, so a
`--recurse-submodules` clone is not needed. With the clone on a Windows drive, leave out the folder:
`port/setup/setup.sh` uses `build/work`, and `build.sh` needs no `PORT_WORK`.

The build is one CMake project with a preset per platform (`CMakePresets.json`; Windows today), which `build.sh`
runs through Visual Studio's environment, and adds the game folder and a staged launcher. With the clone on a Windows
drive you can run CMake yourself, from a Developer prompt: a batch file such as

    @echo off
    call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" || exit /b 1
    set "PATH=C:\llvm-20.1.8\bin;%PATH%"
    cd /d C:\piconet-runner
    cmake --preset windows -DPORT_PROFILE=none -DPORT_PYTHON=C:\piconet-venv\Scripts\python.exe
    cmake --build --preset windows
    cmake --build --preset windows --target module

builds the recompiler and the runner, then the native module from your disc's files in the work folder.
`port/setup/tools.py --say` prints where your `vcvars64.bat` is. Settings of your own (another work folder, your
tools' places) can go in a `CMakeUserPresets.json` beside `CMakePresets.json`, which git ignores. The settings, the
targets and the steps: [port/build/README.md](port/build/README.md).

## Check

    sudo apt install python3-venv
    python3 -m venv .venv && .venv/bin/pip install keystone-engine    # in the checkout's root, for the image's checks
    port/check.sh
    git ls-files -z '*.sh' | xargs -0 -n1 bash -n     # the scripts parse

The plain check: the facts table's, the image's, the core's (built with g++) and the setup's checks. It needs no
work folder and no disc. Without `.venv` (with keystone) the image's code builds and tests are skipped, and said;
without the gates' checkout it says that their checks are left out. CI runs the same on Linux
(`.github/workflows/ci.yml`) once the repositories are public; the Windows build above is a workflow run by hand
(`windows-build.yml`).

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
