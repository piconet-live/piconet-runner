#!/bin/bash
# recompiler.sh WORK [--dry-run]: the recompiler (dolrecomp.exe, with the LLVM backend) from WORK/DolRecomp into
# WORK/work/dolrecomp-port, the tool every module build uses: the CMake superbuild's target `recompiler`
# (CMakeLists.txt at the checkout's root, the preset windows; port/setup/lib.sh port_cmake). The log is
# WORK/work/dolrecomp-port.log. A configure of the recompiler prints "Codegen source digest": the recompiler's own
# hash of the 72 source files that decide what it emits, which is part of every key of its object cache; n6's tool
# had 14fc1ecfb571b14c... Afterwards build-manifest.json is written beside the tool (manifest.py); a tool whose
# manifest is not complete is a test build, and the last line says so. Run it after the busy check (this PC's
# machine sharing, where it is there). Under a minute at 16 jobs (LLVM itself is not built).
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
. "$here/../setup/lib.sh"
WORK=$(cd "${1:?recompiler.sh WORK [--dry-run]}" && pwd)
LOG=$WORK/work/dolrecomp-port.log
if [ "${2:-}" = --dry-run ]; then
  echo "cmake -S <this checkout> --preset windows -B $(port_cmake_dir "$WORK") -DPORT_WORK=$WORK ...    (port_cmake, in the Developer environment)"
  echo "cmake --build $(port_cmake_dir "$WORK") --target recompiler > $LOG"
  echo "python3 $here/manifest.py recompiler $WORK"; exit 0
fi
[ -f "$WORK/DolRecomp/CMakeLists.txt" ] || { echo "recompiler: no source tree at $WORK/DolRecomp (port/setup/trees.sh)" >&2; exit 1; }
pc_sharing; pc_take native-build-lock.sh "the recompiler" 6   # one native build at a time on the PC
mkdir -p "$WORK/work"
port_cmake "$WORK"
port_cmake_build "$WORK" recompiler "$LOG"
# (a tool that was up to date is not configured again, so its log has no digest: "no work to do")
grep -o "Codegen source digest: [0-9a-f]*" "$LOG" | tail -1 || echo "recompiler: up to date (no new configure, no digest in the log)"
echo "recompiler: $WORK/work/dolrecomp-port/dolrecomp.exe ($(sha1sum "$WORK/work/dolrecomp-port/dolrecomp.exe" | cut -c1-8))"
python3 "$here/manifest.py" recompiler "$WORK" || echo "recompiler: a test build only (its manifest is not complete)"
