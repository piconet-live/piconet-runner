#!/bin/bash
# hybrid-module.sh NAME BASE SWITCHES ADDR,ADDR...: a test module for finding which switch breaks a gate, in two
# minutes instead of a module build. It is build BASE's chunk objects
# (<work>/work/cg-BASE, only read) with the chunks that hold the addresses emitted again under the switch list
# SWITCHES, linked as <work>/work/mod-cg-NAME. So a switch can be taken out of the few functions it acts in while
# everything else stays as BASE has it; the gates' gate-native.sh then says whether those functions were the fault.
#   NAME      begins with "spx": such a module is never kept, gated as a build or timed
#   SWITCHES  must give the same functions as BASE's (the same DOLRECOMP_PARTITION_AT and closed roots), and a
#             switch must act inside the chunks named: one that changes what other chunks call or import
#             (DOLRECOMP_INLINE_CALLEES, the closed set) cannot be tried this way
#   TOOL      the recompiler's folder under <work>/work (default dolrecomp-port): the one BASE was built with
#   GENERATE  the --profile-generate file of an instrumented BASE, as Windows names it (profile.sh's:
#             <work>\scratch\pgo-NAME\gx-%m.profraw); left out for a BASE without instrumentation
# Takes NATIVE-BUILD.lock and a CPU window itself, after the busy check. Writes <work>/work/cg-NAME, mod-cg-NAME.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd); root=$(cd "$here/../.." && pwd); cd "$root"
. "$root/port/setup/work-folder.env"; W=${PORT_WORK:-$PORT_WORK_DEFAULT}; DW=${PORT_WORK_WIN:-$(wslpath -w "$W")}
[ $# -eq 4 ] || { sed -n 2,15p "$0"; exit 1; }
N=$1; B=$2; SW=$3; ONLY=$4; G=$W/work/cg-$N; E=$W/work/cg-${N}e; EW="$DW\\work\\cg-${N}e"; TOOL=${TOOL:-dolrecomp-port}
case "$N" in spx*) ;; *) echo "hybrid-module: the name must begin with spx" >&2; exit 1;; esac
[ -f "$W/work/cg-$B/generated/generated.c" ] || { echo "hybrid-module: no build $B in $W/work" >&2; exit 1; }
[ -f "$SW" ] || { echo "hybrid-module: no switches file $SW" >&2; exit 1; }
. "$root/port/setup/lib.sh"; BUSY=$(busy_check "$W")   # none: no busy check
[ "$BUSY" = none ] || "$BUSY" > /dev/null || { echo "hybrid-module: the busy check says wait" >&2; exit 3; }
pc_sharing "$BUSY"   # this PC's locks, where it has them (pins.tsv PC_SCRIPTS_DEFAULT)
pc_take native-build-lock.sh "hybrid test module $N (3 min)" 6
pc_take cpu-window.sh "hybrid test module $N (3 min)" 6
rm -rf "$G" "$G-mod.log" "$W/work/mod-cg-$N" "$E" "$E.log"; mkdir -p "$E" "$G/generated/chunks"
cp "$W/work/cg-$B/main-combo.dol" "$E/"
ENVS=$(sed 's/^env: //' "$SW"); GEN=""; [ -z "${GENERATE:-}" ] || GEN="--profile-generate $GENERATE"
( cd "$W" && cmd.exe /c "set DOLRECOMP_LLVM_CACHE=off&& set DOLRECOMP_MEM2_BASE=0xA0000000&& ${ENVS}set DOLRECOMP_LLVM_ONLY=${ONLY}&& start /wait /b $DW\\work\\$TOOL\\dolrecomp.exe -j16 --backend=llvm --native-abi off $GEN --cpu gekko --gamecube $EW\\main-combo.dol $EW > $EW.log 2>&1" ) || true
ls "$E/generated/chunks"/*.o > /dev/null 2>&1 || { echo "hybrid-module: no chunk was emitted (see $E.log)" >&2; exit 1; }
for f in "$W/work/cg-$B/generated"/*; do [ -d "$f" ] || cp "$f" "$G/generated/"; done
cp "$W/work/cg-$B/generated/chunks"/*.o "$G/generated/chunks/"
n=0
for o in "$E/generated/chunks"/*.o; do
  b=$(basename "$o")
  [ -f "$G/generated/chunks/$b" ] || { echo "hybrid-module: $B has no chunk $b: the switch lists give other functions" >&2; exit 1; }
  cp "$o" "$G/generated/chunks/$b"; n=$((n + 1))
done
( [ -n "${TOOL_LLVM_WIN:-}" ] || find_tools
  _port_cmake_cmd "$W" "cmake -DSTEP=compile -DW=${DW//\\//} -DN=$N -DLLVM=${TOOL_LLVM_WIN//\\//} -DJOBS=16 -P \"$(wslpath -w "$root/port/build/cmake/PortModule.cmake")\"" > /dev/null )   # the module's compile, as port/build/cmake/PortModule.cmake's
rm -rf "$E" "$E.log"
[ -f "$W/work/mod-cg-$N/gGFZE01_recomp.dll" ] || { echo "hybrid-module: no module was linked (see $G-mod.log)" >&2; exit 1; }
echo "hybrid-module: $N is $B with $n chunks emitted again; SHA-1 $(sha1sum "$W/work/mod-cg-$N/gGFZE01_recomp.dll" | cut -c1-8). A test module: gate it, then delete it"
