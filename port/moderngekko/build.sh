#!/bin/bash
# build.sh [LOG]: the runner with the port in it: the CMake superbuild's target `runner` (CMakeLists.txt at the
# checkout's root, the preset windows; port/setup/lib.sh port_cmake), which mirrors port/core, port/moderngekko,
# port/facts/generated, the protocol's generated code and the app's icon into the work folder's port/src (where the
# Windows compiler reads them; only changed files are written, so only those are compiled again) and builds
# ModernGekko there with MODERNGEKKO_FZGX_PORT_DIR pointing at it. 1-2 minutes when little changed; the log is
# <work>/port/LOG.log (default build-mg). It takes NATIVE-BUILD.lock itself (one native build at a time on the PC:
# native-build-lock.sh), where this PC's lock is there (port/setup/lib.sh, pc_take).
# The result is <work>/ModernGekko/build/moderngekko-run.exe, the runner every harness run uses, and beside it
# build-manifest.json (port/build/manifest.py): what it was built from. A runner whose manifest is not complete (a
# file that differs from the commit, a tree with uncommitted changes) is a test build: it is said, and never shipped.
# PORT_WORK names another work folder (port/setup/setup.sh makes one). It refuses a work folder whose source trees are
# not the ones this checkout pins (forks/check.sh --trees): such a runner is no build of it.
# OUT=NAME: the runner is also kept as <work>/port/runner-NAME (the exe, its manifest, its link map and the DLLs beside it: Aurora's), copied under
# the same hold of the lock, so another build's runner cannot stand in ModernGekko/build when it is copied.
# HOST_TIMERS=1: a profiling build, with the core's scoped timers (port/core/src/host_timers.h); never a player's. The
# setting is given at every build, so a build without it is a build without them.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
. "$here/../setup/lib.sh"; W=${PORT_WORK:-$PORT_WORK_DEFAULT}; LOG=${1:-build-mg}
timers=OFF; [ "${HOST_TIMERS:-0}" != 1 ] || timers=ON
[ -z "${OUT:-}" ] || [[ $OUT =~ ^[A-Za-z0-9]+$ ]] || { echo "build: OUT is a build's name, letters and digits" >&2; exit 1; }
trees=$("$here/../../$REPO_FORKS/check.sh" --trees "$W" 2>&1) || { echo "build: REFUSED: the source trees in $W are not the ones this checkout pins (forks/check.sh --trees):" >&2; grep "^forks/check: " <<< "$trees" >&2; exit 1; }
pc_sharing; pc_take native-build-lock.sh "the runner ($LOG)" 14
port_cmake "$W" -DPORT_HOST_TIMERS=$timers
port_cmake_build "$W" runner "$W/port/$LOG.log"
echo "built: $W/ModernGekko/build/moderngekko-run.exe ($(sha1sum "$W/ModernGekko/build/moderngekko-run.exe" | cut -c1-8))"
python3 "$here/../build/manifest.py" runner "$W" --host-timers $timers || echo "build: a test build only (its manifest is not complete): not a runner anyone ships"
if [ -n "${OUT:-}" ]; then
  K=$W/port/runner-$OUT; mkdir -p "$K"
  cp "$W/ModernGekko/build/moderngekko-run.exe" "$W/ModernGekko/build/build-manifest.json" "$K/"
  [ ! -f "$W/ModernGekko/build/moderngekko-run.map" ] || cp "$W/ModernGekko/build/moderngekko-run.map" "$K/"
  # Aurora (debug.renderer "aurora"): aurora-present.dll and the libraries it loads, when the aurora target put them
  # beside the runner (port/build/cmake/AuroraCopy.cmake); the runner loads them from its own folder
  for f in "$W"/ModernGekko/build/*.dll; do [ ! -f "$f" ] || cp "$f" "$K/"; done
  echo "kept: $K/moderngekko-run.exe"
fi
