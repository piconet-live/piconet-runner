#!/bin/bash
# build-module.sh NAME [--dry-run]: the native module of a game build, the way a release builds it: the recompiler
# work\dolrecomp-port (port/build/recompiler.sh makes it from the pinned tree), the released switches (pins.tsv
# REPO_SWITCHES: the closed roots, DOLRECOMP_PS_MADD=guarded, DOLRECOMP_ENTRY_QUERY=off and the rest), the story's
# two movie modules as further text sections, and a profile trained on the build's image. It wraps
# port/build/native-module.sh (the superbuild's target module), which passes the switches to fastlookup.py (the recompiler fork's tools/, in WORK/DolRecomp) too and refuses a closed build whose table
# does not set the closed bytes.
#   NAME        the build's name: <work>/work/cg-NAME and <work>/work/mod-cg-NAME. Refused when either exists
#               (native-module.sh would delete them).
#   --dry-run   prints the checks and the exact commands, runs nothing and writes nothing
#   PROFILE=<file>   the training profile (required: a release hands its own out, the gates' runs/profile.sh trains
#               one; PROFILE=none: built without one, the same game, slower). Functions the image changed since the
#               profile was trained are compiled without one
#   BUNDLE=<folder>  the image's bundle, port/image/build.py's output (required)
#   EXTRA, SWITCHES, TOOL   as native-module.sh takes them (default: the movie modules, REPO_SWITCHES, dolrecomp-port)
#   PORT_WORK, PORT_WORK_WIN   another work folder (port/setup/setup.sh makes one); PORT_AGENT names the request file
# About 11 minutes at 16 jobs (n6: 10.8), in a CPU window of its own (BUILD.wanted.port) and under NATIVE-BUILD.lock
# (one native build at a time; a timed run waits for this build until the lock's stated end), after the busy check. That
# is over the ten minutes machine-sharing allows without asking: agree it with the PC's other users first. Without this
# PC's machine sharing (a clone: no gates, pins.tsv REPO_GATES, no busy check) the build goes on without it, and says so once.
# The inputs are recorded before the build and the manifest is written after it into the module's own folder
# (work/mod-cg-NAME/build-manifest.json, port/build/manifest.py). A module without a complete manifest is a test
# build, never one anyone ships: the last line says which it is (`manifest.py check FILE` asks again later).
# Exit status: 0 built, 1 refused or failed, 3 the busy check says wait.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd); root=$(cd "$here/../.." && pwd)
. "$root/port/setup/lib.sh"; W=${PORT_WORK:-$PORT_WORK_DEFAULT}
N=""; DRY=""
for a in "$@"; do case "$a" in --dry-run) DRY=1;; -*) echo "build-module: unknown option $a" >&2; exit 1;; *) N=$a;; esac; done
[ -n "$N" ] || { sed -n 2,24p "$0"; exit 1; }
TOOL=${TOOL:-dolrecomp-port}
SWITCHES=${SWITCHES:-$root/$REPO_SWITCHES}
EXTRA=${EXTRA:-"805960D4:$W/derived/loaded-05-movie.bin 8059EF6C:$W/derived/loaded-0c-movie_module.bin"}
PROFILE=${PROFILE:-}; BUNDLE=${BUNDLE:-}
BUSY=${BUSY_CHECK:-$W/port/scratch/rp/chk.sh}
problems=0
refuse() { echo "build-module: $*" >&2; problems=1; }
# AGAIN=1: the two folders are the caller's own, from a try of this build that stopped (scripts/release-build.sh
# --from): they are replaced. Without it nothing that is there is ever overwritten.
[ -n "${AGAIN:-}" ] || [ ! -e "$W/work/mod-cg-$N" ] || refuse "$W/work/mod-cg-$N exists; a build is never overwritten: choose another name"
[ -n "${AGAIN:-}" ] || [ ! -e "$W/work/cg-$N" ] || refuse "$W/work/cg-$N exists; a build is never overwritten: choose another name"
[ -f "$W/work/$TOOL/dolrecomp.exe" ] || refuse "no recompiler at $W/work/$TOOL/dolrecomp.exe"
[ -f "$SWITCHES" ] || refuse "no switches file $SWITCHES"
[ -n "$PROFILE" ] || refuse "PROFILE=<file> is required: the profile the module is trained with (a release's, or the gates' runs/profile.sh), or PROFILE=none for a module without one"
[ -z "$PROFILE" ] || [ "$PROFILE" = none ] || [ -f "$PROFILE" ] || refuse "no profile $PROFILE"
[ "$PROFILE" != none ] || PROFILE=""   # asked for: no --profile-use, and the manifest says "none"
[ -n "$BUNDLE" ] || refuse "BUNDLE=<folder> is required: the image's bundle (port/image/build.py)"
[ -z "$BUNDLE" ] || [ -f "$BUNDLE/patches.json" ] || refuse "$BUNDLE is no native bundle (port/image/build.py makes it)"
for e in $EXTRA; do [ -f "${e#*:}" ] || refuse "no module image ${e#*:}"; done
for s in DOLRECOMP_CLOSED_ROOTS= DOLRECOMP_PS_MADD=guarded DOLRECOMP_ENTRY_QUERY=off; do
  grep -q "set $s" "$SWITCHES" 2> /dev/null || refuse "$SWITCHES lacks $s"
done
cmd="TOOL=$TOOL SWITCHES=$SWITCHES EXTRA=\"$EXTRA\" PROFILE=$PROFILE BUNDLE=$BUNDLE port/build/native-module.sh $N"
pc_sharing "$BUSY"
if [ -n "$DRY" ]; then
  echo "cd $root"
  [ ! -e "$BUSY" ] || echo "$BUSY    # exit 0 (\"none\"): go on; exit 3: wait"
  lead="( "; line() { echo "$lead$*"; lead="  "; }
  ! pc_has native-build-lock.sh || line "source $PC_SCRIPTS/native-build-lock.sh \"native module $N, optimised (about 11 min)\" 15"
  ! pc_has cpu-window.sh || line "source $PC_SCRIPTS/cpu-window.sh \"native module $N, optimised (about 11 min)\" 12"
  line "python3 port/build/manifest.py module-begin $W $N --switches $SWITCHES --bundle $BUNDLE --profile $PROFILE --extra \"$EXTRA\" --tool $TOOL"
  echo "  $cmd )"
  echo "python3 port/build/manifest.py module-finish $W $N    # the manifest, and whether it is complete"
  echo "ls $W/work/mod-cg-$N/gGFZE01_recomp.dll"
  echo "grep -c closed_ok $W/work/cg-$N/generated/native_tables.c    # n6: 14290; 0 means no closed body is ever entered"
  echo "grep -c 'hash mismatch' $W/work/cg-$N.log                    # n6: 0 functions without their profile"
  [ $problems = 0 ] && echo "build-module: dry run; nothing was started or written" || echo "build-module: dry run; a real run would be refused (above)"
  exit $problems
fi
[ $problems = 0 ] || exit 1
cd "$root"
[ ! -e "$BUSY" ] || "$BUSY" > /dev/null || { echo "build-module: the busy check says wait:"; "$BUSY" || true; exit 3; }
( pc_take native-build-lock.sh "native module $N, optimised (about 11 min)" 15
  pc_take cpu-window.sh "native module $N, optimised (about 11 min)" 12
  python3 port/build/manifest.py module-begin "$W" "$N" --switches "$SWITCHES" --bundle "$BUNDLE" --profile "$PROFILE" --extra "$EXTRA" --tool "$TOOL"
  TOOL=$TOOL SWITCHES=$SWITCHES EXTRA="$EXTRA" PROFILE=$PROFILE BUNDLE=$BUNDLE port/build/native-module.sh "$N" )
[ -f "$W/work/mod-cg-$N/gGFZE01_recomp.dll" ] || { echo "build-module: no module was written (see $W/work/cg-$N.log and cg-$N-mod.log)" >&2; exit 1; }
echo "build-module: $N built; SHA-1 $(sha1sum "$W/work/mod-cg-$N/gGFZE01_recomp.dll" | cut -c1-8); closed_ok mentions $(grep -c closed_ok "$W/work/cg-$N/generated/native_tables.c"); profile mismatches $(grep -c 'hash mismatch' "$W/work/cg-$N.log" || true)"
python3 port/build/manifest.py module-finish "$W" "$N" || echo "build-module: $N is a test build only (its manifest is not complete): not a module anyone ships"
