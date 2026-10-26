#!/bin/bash
# native-module.sh NAME [--profile-generate PATTERN]: the native module of the native form
# into <work>/work/cg-NAME and <work>/work/mod-cg-NAME: the CMake superbuild's target `module`
# (CMakeLists.txt at the checkout's root, the preset windows; the recipe is port/build/cmake/PortModule.cmake). Its
# input is the stock executable with the patch list's words, main.rel's clean .text (linked at 0x801BA660) with its
# words, and the mod image as a text section at 0x81800000 (port/image/gameinput.py): the patched functions are
# compiled as patched, and the image's routines are native code.
#   BUNDLE=<folder>    the native bundle, port/image/build.py's output (required)
#   SWITCHES=<file>    the DOLRECOMP_* switches (required; a release's are pins.tsv's REPO_SWITCHES)
#   EXTRA="ADDR:FILE ..."  more text sections: on-demand modules as they stand in memory (port/setup/ondemand.py)
#   TOOL=<work folder> the recompiler's build folder under <work>/work (default dolrecomp-port, the superbuild's
#                      target recompiler; another, such as a speed experiment's, is taken as it is: PORT_RECOMPILER)
#   PROFILE=<file>     a profile for --profile-use. Functions the patches changed do not match it and are compiled
#                      without. Empty: none
#   --profile-generate PATTERN  an instrumented module (the gates' runs/profile.sh), its profiles written to PATTERN
#                      (as Windows names it, %m for the module)
# NAME's two folders are deleted first: build-module.sh refuses a name that is there. About 8.5 minutes at 16 jobs;
# the logs are <work>/work/cg-NAME.log (the recompiler), cg-NAME-mod.log (the module's compile) and
# cg-NAME-build.log (all of it). Run it after the busy check (that no game is being played), in a CPU window (this PC's
# cpu-window.sh, PC_SCRIPTS). It takes NATIVE-BUILD.lock itself (one native build at a time on the PC:
# native-build-lock.sh), where this PC's lock is there.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
. "$here/../setup/lib.sh"; D=${PORT_WORK:-$PORT_WORK_DEFAULT}; N=${1:?native-module.sh NAME [--profile-generate PATTERN]}; shift
GEN=""
case "${1:-}" in --profile-generate) GEN=${2:?--profile-generate PATTERN};; "") ;; *) echo "native-module: unknown argument $1" >&2; exit 1;; esac
tool=""; [ "${TOOL:-dolrecomp-port}" = dolrecomp-port ] || tool=$(wslpath -m "$D/work/$TOOL/dolrecomp.exe")
pc_sharing; pc_take native-build-lock.sh "native module $N" 15
B=${BUNDLE:?native-module: BUNDLE=<folder> is required}; S=${SWITCHES:?native-module: SWITCHES=<file> is required}
extra=""; for e in ${EXTRA:-}; do extra="$extra;${e%%:*}:$(wslpath -m "${e#*:}")"; done
extra=${extra#;}; extra=${extra:-none}
profile=none; [ -z "${PROFILE:-}" ] || [ "$PROFILE" = none ] || profile=$(wslpath -m "$PROFILE")
date +%T
port_cmake "$D" "-DPORT_BUILD_NAME=$N" "-DPORT_BUNDLE=$(wslpath -m "$B")" "-DPORT_SWITCHES=$(wslpath -m "$S")" \
  "-DPORT_EXTRA=$extra" "-DPORT_PROFILE=$profile" "-DPORT_PROFILE_GENERATE=$GEN" "-DPORT_RECOMPILER=$tool"
port_cmake_build "$D" module "$D/work/cg-$N-build.log"
date +%T
grep "module $N:" "$D/work/cg-$N-build.log" | tail -1
ls -la "$D/work/mod-cg-$N"/*.dll
