#!/bin/bash
# install.sh WORK [--check | --shared]: the tracked files a work folder runs, put where every script expects them:
#   WORK/scripts/fastlookup.py   from the recompiler's tree (WORK/DolRecomp/tools, in forks/dolrecomp's commit; trees.sh
#                 makes it first: without it this is said and it is left out; the machine files' post_emit runs it),
#                 gcextract.py from extract-disc.py. (The builds' own scripts are not installed: the build is
#                 the CMake superbuild at the checkout's root, which reads the checkout itself.)
#   WORK/scripts/progress-viewer.ps1, progress-viewer.bat   from scripts/: the window that shows where a long run
#                 stands; it reads WORK/progress, the folder beside its own. This PC's:
#                 when the checkout has no scripts/progress-viewer.ps1 this is said and left out (a build needs none)
#   WORK/port/machine-gate-n.toml, machine-n.toml, machine-first.toml    from recomp/ (the harness's machine files)
#   WORK/port/user-first-pristine/   the user folder of a first boot (no memory card): from user-first/, with
#                 Dolphin.ini.pristine as its Config/Dolphin.ini
#   WORK/port/Dolphin.ini.gate-nocheats, Dolphin.ini.pristine, WORK/scratch/Dolphin.ini.single   from port/setup
#   WORK/user/    the gate scenes' user folder (machine-gate-n.toml: rows batch and timing start from a saved
#                 state in it): the first boot's three files, and Dolphin.ini.single as its Config/Dolphin.ini,
#                 where gate-native.sh leaves it after a batch
#   WORK/port/scratch/rp/chk.sh, quiet.sh   the busy check and the quiet check, from busy-check.sh and quiet.sh of
#                 the PC's own scripts (PC_SCRIPTS, lib.sh; no repository holds them: a PC without them is said and
#                 they are left out): installed when absent and never overwritten (the busy check is a safeguard); and beside them
#                 work-folder.env with the folders port/setup/work-folder.env resolves to (the busy check reads
#                 the folder of the PC's lock files there)
#   WORK/kit-win/   the harness's Windows scripts (the gates' vendor/harness/win, pins.tsv REPO_GATES; RECOMP_KIT=<a
#                 recomp-kit checkout>: its tools/harness/win), which start every test game (launch.ps1). A build needs nothing of them, a test
#                 run does; when the folder named is not there this is said and they are left out
# The tracked build scripts and machine files name no work folder and no tool's place: where one belongs they say
# @WORK@ and @LLVM@ (/mnt/<drive>/...), @WORK_WIN@ (as Windows spells it), @LLVM_WIN@ (the same with slashes),
# @VCVARS_WIN@ and @PYTHON_WIN@, and WORK and the tools as port/setup/tools.py finds them (lib.sh find_tools) are
# written there. `recompiler` is
# work/dolrecomp-port (the tool recompiler.sh builds there). The configuration files' analytics ID is zeros (the
# emulator sends nothing: Enabled = False); the comparison leaves that line out.
#   --check   writes nothing: says for each file whether the work folder's copy is what would be installed.
#             Exit status 1 when one differs or is missing.
#   --shared  this PC's shared work folder (a PC's ~/.config/piconet/work-folder.env names it; a checkout's own
#             build/work is no shared folder) is written only with this word: its files are
#             what every build and game on the PC reads. For whoever keeps that folder, after a tracked file or a tool's
#             place changed (the tools moved to another drive, say).
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd); root=$(cd "$here/../.." && pwd)
. "$here/lib.sh"
WORK=${1:?install.sh WORK [--check]}; CHECK=${2:-}
case "$CHECK" in ""|--check|--shared) ;; *) echo "install: unknown option $CHECK" >&2; exit 1;; esac
if pc_shared_work "$WORK"; then
  [ -n "$CHECK" ] || { echo "install: $WORK is this PC's shared work folder: only --check there, or --shared to write it" >&2; exit 1; }
elif [ "$CHECK" = --shared ]; then echo "install: --shared is for this PC's shared work folder (~/.config/piconet/work-folder.env), not $WORK" >&2; exit 1; fi
[ "$CHECK" != --shared ] || CHECK=""
find_tools || { echo "install: the build scripts and machine files name the tools: stopped" >&2; exit 1; }
[ -n "$CHECK" ] || mkdir -p "$WORK"
WORK=$(cd "$WORK" && pwd)
python3 - "$root" "$WORK" "$(wslpath -w "$WORK")" "$CHECK" "$root/$REPO_BUILD" "$root/$REPO_MACHINE" "$here" \
    "$TOOL_PYTHON_WIN" "$TOOL_LLVM_WIN" "$TOOL_VCVARS_WIN" "$HARNESS_DIR/win" "$PORT_WORK_DEFAULT" "$PORT_LOCKS_DEFAULT" "$PC_SCRIPTS" <<'PY'
import os, sys
root, work, work_win, check, build, machine, setup, python, llvm, vcvars, kit, work_default, locks_default, pc = sys.argv[1:]
slash, toml = (lambda p: p.replace("\\", "/")), (lambda p: p.replace("\\", "\\\\"))
unix = lambda p: "/mnt/" + p[0].lower() + slash(p[2:])
# the work folder and the tools, where a tracked file says so: a machine file names them in both spellings
placed = {"swap": [(b"@WORK@", work.encode()), (b"@WORK_WIN@", toml(work_win).encode()),
                   (b"@LLVM@", unix(llvm).encode()), (b"@LLVM_WIN@", slash(llvm).encode()), (b"@VCVARS_WIN@", toml(vcvars).encode())]}
no_id = lambda data: b"\n".join(line for line in data.split(b"\n") if not line.startswith(b"ID = "))
plan = []
if os.path.isfile(f"{work}/DolRecomp/tools/fastlookup.py"):
    plan += [(f"{work}/DolRecomp/tools/fastlookup.py", "scripts/fastlookup.py", "")]
else:
    print("install: no DolRecomp/tools/fastlookup.py in the work folder (port/setup/trees.sh makes the tree): left out")
plan += [(f"{setup}/extract-disc.py", "scripts/gcextract.py", "")]
if os.path.isfile(f"{root}/scripts/progress-viewer.ps1"):
    plan += [(f"{root}/scripts/{n}", f"scripts/{n}", "") for n in ("progress-viewer.ps1", "progress-viewer.bat")]
else:
    print("install: no scripts/progress-viewer.ps1 in this checkout (this PC's window for long runs): left out")
plan += [(f"{machine}/{n}", f"port/{n}", "swap") for n in ("machine-gate-n.toml", "machine-n.toml", "machine-first.toml")]
plan += [(f"{setup}/user-first/{n}", f"port/user-first-pristine/{n}", "") for n in ("config.ini", "Config/GFX.ini", "Config/GCPadNew.ini")]
plan += [(f"{setup}/Dolphin.ini.pristine", "port/user-first-pristine/Config/Dolphin.ini", "ini")]
plan += [(f"{setup}/Dolphin.ini.gate-nocheats", "port/Dolphin.ini.gate-nocheats", "ini"), (f"{setup}/Dolphin.ini.pristine", "port/Dolphin.ini.pristine", "ini"),
         (f"{setup}/Dolphin.ini.single", "scratch/Dolphin.ini.single", "ini")]
plan += [(f"{setup}/user-first/{n}", f"user/{n}", "") for n in ("config.ini", "Config/GFX.ini", "Config/GCPadNew.ini")]
plan += [(f"{setup}/Dolphin.ini.single", "user/Config/Dolphin.ini", "ini")]
if pc and os.path.isfile(f"{pc}/busy-check.sh") and os.path.isfile(f"{pc}/quiet.sh"):
    plan += [(f"{pc}/busy-check.sh", "port/scratch/rp/chk.sh", "keep"), (f"{pc}/quiet.sh", "port/scratch/rp/quiet.sh", "keep")]
else:
    print(f"install: no busy check and quiet check (busy-check.sh, quiet.sh) in this PC's own scripts ({pc or 'PC_SCRIPTS names none'}): left out")
plan += [(f"{setup}/work-folder.env", "port/scratch/rp/work-folder.env", "folders")]
if os.path.isfile(f"{kit}/launch.ps1"):
    plan += [(f"{kit}/{n}", f"kit-win/{n}", "raw") for n in sorted(os.listdir(kit)) if os.path.isfile(f"{kit}/{n}")]
else:
    print(f"install: no harness at {os.path.normpath(kit)} (the gates, pins.tsv REPO_GATES, or RECOMP_KIT): kit-win is left out, so no test game can be started here")
CRLF = ("port/Dolphin.ini.gate-nocheats", "scratch/Dolphin.ini.single", "port/user-first-pristine/config.ini", "port/user-first-pristine/Config/GFX.ini",
        "user/Config/Dolphin.ini", "user/config.ini", "user/Config/GFX.ini")
bad = 0
for source, to, kind in plan:
    with open(source, "rb") as f:
        data = f.read()
    if kind != "raw":
        data = data.replace(b"\r\n", b"\n")              # whatever line ends the checkout has: LF, as every build ran the scripts
    if to in CRLF:
        data = data.replace(b"\n", b"\r\n")              # the emulator wrote these files itself, with CRLF
    if kind == "folders":                                # the folders the tracked file resolves to, for the busy check beside it
        data = (b"# work-folder.env as port/setup/install.sh wrote it: the folders port/setup/work-folder.env resolved to\n"
                b"# (the busy check beside it reads the folder of the PC's lock files)\n"
                + f"PORT_WORK_DEFAULT={work_default}\nPORT_LOCKS_DEFAULT={locks_default}\n".encode())
    if kind == "swap":
        for was, now in placed[kind]:
            data = data.replace(was, now)
    path = os.path.join(work, to)
    have = open(path, "rb").read() if os.path.isfile(path) else None
    same = have is not None and (no_id(have) == no_id(data) if kind == "ini" else have == data)
    if check:
        print(f"install: {'equal    ' if same else 'DIFFERENT' if have is not None else 'MISSING  '} {to}  ({os.path.relpath(source, root) if source.startswith(root) else source})")
        bad |= not same
    elif kind == "keep" and have is not None:
        print(f"install: kept      {to} (never overwritten)" + ("" if same else "; it DIFFERS from the tracked file"))
    else:
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb") as f:
            f.write(data)
        if to.endswith(".sh"):
            os.chmod(path, 0o755)
        print(f"install: wrote     {to}")
sys.exit(1 if bad else 0)
PY
