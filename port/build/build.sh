#!/bin/bash
# build.sh NAME: every build of the game, in order, in the work folder that port/setup/setup.sh made (PORT_WORK; by
# default work-folder.env's, build/work in this checkout, as setup.sh's; never a PC's shared one unasked). NAME
# is the build's name, of your choice (letters and digits): every part is kept under it (bundle-NAME, game-NAME,
# runner-NAME, mod-cg-NAME, launcher-NAME), as a release build keeps them (the gates' release/release.sh runs this).
# The compiling is CMake's: the superbuild at the checkout's root with its preset windows (CMakeLists.txt,
# CMakePresets.json; port/build/README.md), whose targets the step scripts build in the PC's own order, with its
# machine sharing, the build manifests and the kept copies around them. The two compilers' steps always run (ninja
# rebuilds what changed); the others are skipped when their result is there, so a run that stopped is started again
# with the same command.
#   1. the recompiler      port/build/recompiler.sh  (target recompiler) work/dolrecomp-port/dolrecomp.exe      (1 min)
#   2. the image           target image (port/image/build.py)          port/bundle-NAME                       (seconds)
#   3. the game folder     port/build/native-game.sh                    port/game-NAME                         (seconds)
#   4. the runner          port/moderngekko/build.sh OUT=NAME (runner)  port/runner-NAME/moderngekko-run.exe   (8 min from nothing)
#   5. the module          port/build/build-module.sh NAME (module)     work/mod-cg-NAME/gGFZE01_recomp.dll    (12 min; the
#                          release's switches, pins.tsv REPO_SWITCHES)
#   6. the staged launcher port/build/stage-launcher.sh                 port/launcher-NAME                     (1 min; needs dotnet)
# The build ends there. FIRST_BOOT=1 adds a step for the gates' test games, once in a work folder:
#   7. a first boot        the gates' runs/launcher-run.sh (pins.tsv REPO_GATES), scene boot-first: the game makes its
#                          save file, kept as port/user-pristine for every later test game (1 min; needs the gates
#                          and kit-win, port/setup/install.sh). Left out, and said, when they are not there
#   PORT_PROFILE=<file>  the module's training profile (PROFILE=<file> is the same; default
#                      PORT_WORK/input/module.profdata, where setup.sh --profile puts it: the one handed out with a release). PORT_PROFILE=none: a module built without
#                      one (the same game, slower), said. Without either the script stops before step 5 and says so.
#   PORT_PYTHON=<python.exe>  a Windows Python with the module keystone, for step 2 (pip install keystone-engine; a
#                      venv in the work folder is fine). Given once, the build folder keeps it; by default CMake's
#                      Python3, which is refused when it has no keystone
#   PORT_PAUSE=<seconds>  a pause between the steps, on a PC that other work shares (default 0)
#   PORT_AGENT, PORT_LOCKS, BUSY_CHECK   as this PC's cpu-window.sh (PC_SCRIPTS) and build-module.sh take them
# Every step that compiles or runs a game waits for the busy check and takes the PC by a request file of its own;
# the native builds (the recompiler, the runner, the module) take NATIVE-BUILD.lock first, one at a time on the PC.
# Those are this PC's machine sharing: where they are not (a clone), the build goes on without them and says so once
# (pc_sharing, port/setup/lib.sh).
# The last lines name the game build: eight digits of the SHA-1 of the image, the module and the runner.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd); root=$(cd "$here/../.." && pwd)
. "$root/port/setup/lib.sh"
N=${1:?build.sh NAME (PORT_WORK=<the work folder setup.sh made>; by default build/work in this checkout)}
[[ $N =~ ^[A-Za-z0-9]+$ ]] || { echo "build: NAME is letters and digits" >&2; exit 1; }
W=${PORT_WORK:-$PORT_WORK_DEFAULT}
! pc_shared_work "$W" || [ -n "${PORT_WORK:-}" ] || { echo "build: PORT_WORK=<a work folder of your own>: the default is this PC's shared work folder (~/.config/piconet/work-folder.env)" >&2; exit 1; }
[ -d "$W" ] || { echo "build: no work folder at $W: port/setup/setup.sh makes it (or PORT_WORK=<a work folder setup.sh made>)" >&2; exit 1; }
W=$(cd "$W" && pwd)
export PORT_WORK=$W PORT_WORK_WIN; PORT_WORK_WIN=$(wslpath -w "$W")
export BUSY_CHECK=${BUSY_CHECK:-$W/port/scratch/rp/chk.sh}
PROFILE=${PORT_PROFILE:-${PROFILE:-$W/input/module.profdata}}   # PORT_PROFILE, as CMake names it; PROFILE still works
B=$W/port/bundle-$N; G=$W/port/game-$N; L=$W/port/launcher-$N
RUNNER=$W/port/runner-$N/moderngekko-run.exe; MODULE=$W/work/mod-cg-$N/gGFZE01_recomp.dll
step() { sleep "${PORT_PAUSE:-0}"; echo "== $(date +%T) $*"; }
sha() { sha1sum "$1" | cut -c1-8; }
cd "$root"
[ -f "$W/extracted/GFZE01/sys/main.dol.orig" ] && [ -f "$W/scripts/fastlookup.py" ] || { echo "build: $W is not a work folder port/setup/setup.sh finished" >&2; exit 1; }
py=(); if [ -n "${PORT_PYTHON:-}" ]; then
  case "$PORT_PYTHON" in /mnt/?/*.exe) py=("-DPORT_PYTHON=$(wslpath -m "$PORT_PYTHON")");; [A-Za-z]:*) py=("-DPORT_PYTHON=$PORT_PYTHON");;
    *) echo "build: PORT_PYTHON=$PORT_PYTHON: the image is built by the Windows build, with a Windows Python (python.exe) that has the module keystone" >&2; exit 1;; esac
fi
"$root/$REPO_FORKS/check.sh" --trees "$W" | tail -1
pc_sharing "$BUSY_CHECK"

step "1. the recompiler"; wait_while_busy "$BUSY_CHECK"   # always: ninja rebuilds a tool that is not its tree's
( pc_take native-build-lock.sh "build $N: the recompiler" 6
  pc_take cpu-window.sh "build $N: the recompiler" 6; port/build/recompiler.sh "$W" )
if [ ! -f "$B/mod.bin" ]; then
  step "2. the image"
  port_cmake "$W" "-DPORT_BUILD_NAME=$N" "${py[@]}"
  port_cmake_build "$W" image "$W/port/bundle-$N.log"
  echo "image: $B ($(sha "$B/mod.bin"))"
fi
if [ ! -f "$G/sys/main.dol" ]; then step "3. the game folder"; BUNDLE=$B port/build/native-game.sh "$G"; fi
step "4. the runner"; wait_while_busy "$BUSY_CHECK"
( pc_take native-build-lock.sh "build $N: the runner" 14
  pc_take cpu-window.sh "build $N: the runner" 14; OUT=$N port/moderngekko/build.sh "build-mg-$N" )
if [ ! -f "$MODULE" ]; then
  step "5. the module"
  [ "$PROFILE" = none ] || [ -f "$PROFILE" ] || { echo "build: no profile at $PROFILE. Either the one handed out with the release (setup.sh --profile FILE, or PORT_PROFILE=FILE), or PORT_PROFILE=none for a module built without one (the same game, slower); a profile is trained by the gates (runs/profile.sh)" >&2; exit 1; }
  [ "$PROFILE" != none ] || echo "build: PORT_PROFILE=none: the module is built without a training profile (the same game, slower)"
  wait_while_busy "$BUSY_CHECK"
  SWITCHES=$root/$REPO_SWITCHES PROFILE=$PROFILE BUNDLE=$B port/build/build-module.sh "$N"
fi
if ! command -v dotnet > /dev/null; then echo "build: no dotnet: step 6 is left out (no launcher)"
else
  if ! cmp -s "$RUNNER" "$L/recomp/moderngekko-run.exe" || ! cmp -s "$MODULE" "$L/recomp/gGFZE01_recomp.dll" || ! cmp -s "$B/mod.bin" "$L/recomp/fzgx/mod.bin"; then
    step "6. the staged launcher"; wait_while_busy "$BUSY_CHECK"
    ( pc_take cpu-window.sh "build $N: the launcher, staged" 5; MODULE=cg-$N BUNDLE=$B GAME=$G RUNNER=$RUNNER port/build/stage-launcher.sh "$L" | tail -3 )
  fi
  if [ "${FIRST_BOOT:-0}" != 1 ] || [ -d "$W/port/user-pristine/GC" ]; then :
  elif [ ! -f "$GATES_DIR/runs/launcher-run.sh" ]; then echo "build: no gates at $REPO_GATES (piconet-gates, pins.tsv REPO_GATES): step 7 is left out"
  elif [ ! -f "$W/kit-win/launch.ps1" ]; then echo "build: no $W/kit-win/launch.ps1 (the harness's; port/setup/install.sh): step 7 is left out"
  else
    step "7. a first boot, for the save file (a muted test game, about a minute)"; wait_while_busy "$BUSY_CHECK"
    CARD=0 LAUNCHER=launcher-$N TITLE="Port-test--$N-first-boot--closes-by-itself" "$GATES_DIR/runs/launcher-run.sh" "$N-first" boot-first | tail -2
    ls "$L-data/recomp-user/GC"/*/*/*.gci > /dev/null 2>&1 || { echo "build: the first boot made no save file (see $W/port/runs/$N-first)" >&2; exit 1; }
    mkdir -p "$W/port/user-pristine"; cp -r "$L-data/recomp-user/GC" "$W/port/user-pristine/"
    echo "build: the save file is in $W/port/user-pristine"
  fi
fi
echo "build: image $(sha "$B/mod.bin"), module $(sha "$MODULE"), runner $(sha "$RUNNER"): game build $(sha "$B/mod.bin")/$(sha "$MODULE")/$(sha "$RUNNER")"
[ ! -f "$GATES_DIR/runs/launcher-run.sh" ] || echo "build: a game, offline: PORT_WORK=$W LAUNCHER=launcher-$N $REPO_GATES/runs/launcher-run.sh RUN    (boot, menus, a Grand Prix race; 170 s at most, muted)"
