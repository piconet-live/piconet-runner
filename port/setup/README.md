# port/setup: a work folder from a checkout and a disc

A newcomer's first command, in WSL on a Windows PC, from the root of a clone made with git inside WSL (Git for
Windows' `core.autocrlf` would give the `.sh` files Windows line ends). The Windows compilers need the work folder on
a Windows drive (no spaces in its path), and the clone can be in either place
([the runner's README](../../README.md#building)):

- in WSL's own file system, with the work folder on a Windows drive (preferred: git is much faster there): `setup.sh
  WORK`, then `PORT_WORK=WORK` for the build, or once in your settings below. The Windows compilers cannot work in
  WSL's own file system (`cmd.exe` refuses a `\\wsl.localhost` folder), so the trees they compile are in WORK;
- on a Windows drive (under `/mnt/c/`, say): by default the work folder is `build/work` in the clone, ignored by git.

Then the build: `port/build/build.sh NAME` in WSL, which builds CMake's preset `windows` and stages a launcher, or,
with the clone on a Windows drive, CMake's own commands from a Developer prompt: [../build](../build/README.md).

    port/setup/setup.sh [WORK] [--profile FILE]

`setup.sh` looks for everything first and says in one line each what is missing and how to get it or point at it:

| What | Where from |
|---|---|
| Your own disc image, F-Zero GX (USA), as an ISO | you: the setting `DISC_DEFAULT` (below), or `setup.sh WORK DISC` |
| Visual Studio 2022 Build Tools, C++ workload | visualstudio.microsoft.com; found by `vswhere.exe`, the installer's own query |
| LLVM 20.1.8 for Windows with `lib/cmake` (`clang+llvm-20.1.8-x86_64-pc-windows-msvc`) | github.com/llvm/llvm-project releases; found on `PATH` (`clang.exe`), or by `TOOL_LLVM_WIN` (below) |
| Python 3.10 for Windows, `cmake`, `ninja`, Git for Windows | python.org (the `py` launcher or `PATH`); the others on `PATH` (cmake and ninja also by `pip` into that Python). The image needs a Windows Python with `keystone`: `py -3 -m venv DIR`, `DIR\Scripts\python.exe -m pip install keystone-engine`, then that python as `PORT_PYTHON` |
| In WSL: `git`, `python3` (3.11 or later), `rsync`; github.com (`sudo apt install build-essential python3 python3-venv rsync git`). `--profile FILE`: the module's training profile | `apt`; the profile is handed out beside a release (none is handed out yet: `PORT_PROFILE=none` builds without one; the gates' `runs/profile.sh` trains one) |

`pins.tsv` pins each tool's version (the `VERSION_` rows), never its place; `tools.py --say` shows what is found where.
Your settings, outside every clone: `~/.config/piconet/pins.tsv` (rows `DISC_DEFAULT`, your disc image, and for a tool
kept off `PATH` `TOOL_LLVM_WIN`, `TOOL_PYTHON_WIN`: its folder as Windows spells it, `TOOL_VCVARS_WIN`: its
`vcvars64.bat`) and `~/.config/piconet/work-folder.env` (`PORT_WORK_DEFAULT=/mnt/<drive>/...`: your own work folder).
`pins.tsv` has three tab-separated columns under a header row, as `port/setup/pins.tsv` has:

    name	value	note
    DISC_DEFAULT	/path/to/F-Zero GX (USA).iso	your own disc
    TOOL_LLVM_WIN	C:\llvm-20.1.8	LLVM, kept off PATH

| File | What |
|---|---|
| `setup.sh` | Runs the three below; `--check WORK` says whether every installed file of a work folder is the tracked one |
| `trees.sh` | The five source trees of `forks/upstream.tsv` checked out at the submodules' commits by `forks/apply.sh` (Linux git, LF line ends), RecompCore's Externals, then `forks/check.sh --trees` |
| `linux-tools.sh` | The tools of a Linux build (LLVM 20.1.8's release, cmake 3.31.6, zlib, zstd and pkgconf unpacked from Ubuntu's packages), without root, into `~/linux-build/tools` |
| `disc.sh` | The disc as a folder with its two pins (`extract-disc.py`), `main.rel` (`mainrel.py`), the movie player's two images (`ondemand.py`), each checked against its hash in `pins.tsv` |
| `install.sh` | `fastlookup.py` (the recompiler's tree's) and machine files (`recomp/`), with the work folder and the tools (`tools.py`) written where they say `@WORK@`, `@WORK_WIN@`, `@LLVM@`, `@LLVM_WIN@`; emulator configuration; the busy check and the quiet check, from the PC's own scripts where it has them (`pins.tsv` PC_SCRIPTS_DEFAULT; never overwritten); the harness's Windows scripts (the gates' `vendor/harness/win`, where they are). `--check` compares; the shared folder is written only with `--shared` |
| `work-folder.env`, `work_folder.py` | **The one place that says where the work folder and the PC's lock files are** (by default `build/work` in the clone; your own folders in `~/.config/piconet/work-folder.env`, read over it). Shell scripts source it, Python imports the reader; `PORT_WORK` and `PORT_LOCKS` decide first. No other file may write the path (`scripts/structure_check.py --explain 18`), but the CMake superbuild's `PORT_WORK` default, the same `build/work`, as CMake cannot read this file |
| `pins.tsv`, `lib.sh`, `parts.py`, `tools.py` | `tools.py` finds the Windows tools (`--say` for a person, `--shell` for `lib.sh` `find_tools`, `find()` for `port/build/manifest.py`). Repository paths (the other parts' checkouts too, for `port/setup/app.sh` and, through `parts.py`, the runner's Python), the tools' versions, the hashes of what the disc gives. The trees' pins are in `forks/upstream.tsv` only |
| `app.sh` | Sourced by the port's scripts (path lookup, so it is here and not with the gates): the application's side (the built test server and simulated players, what builds them, the launcher's project, a launcher's stack stamp (app_stamp), the protocol's folder), each in the checkout that `port/setup/pins.tsv`'s rows `REPO_SERVICE`, `REPO_LAUNCHER` and `REPO_PROTOCOL` name (`.` while all is one repository). The only file under `port/` that names `net/` |
| `kept.tsv`, `kept.sh` | The gates' inputs no recipe makes again (recordings, the JIT reference's match race, the two saved states): `kept.sh WORK --from REFERENCE` copies and checks them |
| `inputs.md` | Every input of a build: what makes it, and whether it comes out as the same bytes |

No game data lives here: hashes, addresses and recipes only. Everything made from the disc stays in the work folder.
