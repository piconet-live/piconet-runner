#!/bin/bash
# setup.sh [WORK [DISC]] [--profile FILE] [--no-externals]: a complete work folder for building the game, from this
# checkout, the player's disc image and the public source trees. WORK is a folder on a Windows drive
# (/mnt/<drive>/..., no spaces), by default PORT_WORK or work-folder.env's default: build/work in this checkout;
# DISC is your own disc image, by default your setting DISC_DEFAULT (~/.config/piconet/pins.tsv). It can be run
# again: what is there is checked and left alone.
#   1. the tools are looked for (port/setup/tools.py: what is missing is named with how to get it or point at it;
#      nothing is written then)
#   2. trees.sh     the five source trees of forks/upstream.tsv, checked out at the submodules' commits by forks/apply.sh; then forks/check.sh --trees
#   3. disc.sh      the disc as a folder with the two pins, main.rel, the movie player's two images
#   4. install.sh   the build scripts, the machine files, the emulator's configuration files, the busy check and
#                   the quiet check, the harness's Windows scripts, each where the scripts expect it
#   5. --profile FILE   the module's training profile, a build input handed out beside the repository (never
#                   committed): copied to WORK/input/module.profdata, where port/build/build.sh looks for it.
#                   Without one, the gates' runs/profile.sh trains one after the runner is built, or
#                   PORT_PROFILE=none port/build/build.sh NAME builds a module without one.
# setup.sh --check WORK   writes nothing: is every installed file of WORK the tracked one (install.sh --check)?
# Then it prints the next command. It never writes outside WORK, and it refuses the shared work folder
# (work-folder.env names it), which every build and game on the PC use.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd); root=$(cd "$here/../.." && pwd)
. "$here/lib.sh"
WORK=""; DISC=$DISC_DEFAULT; EXT=""; PROFILE=""
[ "${1:-}" != --check ] || exec "$here/install.sh" "${2:?setup.sh --check WORK}" --check
while [ $# -gt 0 ]; do case "$1" in
  --no-externals) EXT=$1;; --profile) PROFILE=${2:?--profile FILE}; shift;; -*) echo "setup: unknown option $1" >&2; exit 1;;
  *) [ -z "$WORK" ] && WORK=$1 || DISC=$1;; esac; shift; done
WORK=${WORK:-${PORT_WORK:-$PORT_WORK_DEFAULT}}
windows_folder "$WORK" setup || exit 1
! pc_shared_work "$WORK" || { echo "setup: that is this PC's shared work folder (~/.config/piconet/work-folder.env); choose a folder of your own" >&2; exit 1; }

missing=0
need() { [ -e "$1" ] && echo "setup: found $2" || { echo "setup: MISSING $2: $3" >&2; missing=1; }; }
have() { command -v "$1" > /dev/null && echo "setup: found $1" || { echo "setup: MISSING $1: $2" >&2; missing=1; }; }
have git "apt install git (makes the checkouts)"; have python3 "apt install python3 (3.11 or later)"; have rsync "apt install rsync (the runner's build)"
have "$TOOL_GIT" "Git for Windows, git-scm.com, on PATH (forks/check.sh reads the trees with it)"
python3 "$here/tools.py" --say || missing=1   # Visual Studio, LLVM, Python, cmake, ninja (pins.tsv's VERSION_ rows)
if [ -z "$DISC" ]; then echo "setup: MISSING your disc image: $DISC_HOW (or give it: setup.sh WORK DISC)" >&2; missing=1
else need "$DISC" "the disc image" "your own F-Zero GX (USA) disc, as an ISO (DISC_DEFAULT in ~/.config/piconet/pins.tsv, or setup.sh WORK DISC)"; fi
[ -z "$PROFILE" ] || need "$PROFILE" "the profile" "the file named by --profile"
[ $missing = 0 ] || { echo "setup: stopped; nothing was written" >&2; exit 1; }
# needed later, by port/build/build.sh: said now, so that nobody finds out after ten minutes
"${PORT_PYTHON:-python3}" -c "import keystone" 2> /dev/null || echo "setup: note: no Python module keystone (the image's assembler): pip install keystone-engine, or PORT_PYTHON=<a python that has it>, before port/build/build.sh"
source "$root/port/setup/app.sh"
command -v dotnet > /dev/null || echo "setup: note: no dotnet (.NET SDK 10, $APP_SDK): the launcher cannot be staged, so no game can be started by the scripts"

mkdir -p "$WORK"; WORK=$(cd "$WORK" && pwd)
"$here/trees.sh" "$WORK" $EXT
"$here/disc.sh" "$WORK" "$DISC"
"$here/install.sh" "$WORK"
if [ -n "$PROFILE" ]; then
  mkdir -p "$WORK/input"; cp "$PROFILE" "$WORK/input/module.profdata"
  echo "setup: the profile $(sha1sum "$WORK/input/module.profdata" | cut -c1-8) is in $WORK/input/module.profdata"
fi
echo "setup: $WORK is ready. Next: PORT_WORK=$WORK port/build/build.sh NAME    (NAME: a name of your own for this build)"
