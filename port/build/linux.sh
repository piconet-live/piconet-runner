#!/bin/bash
# linux.sh STEP [ARGS]: the port built for Linux, in LB (LINUX_BUILD, default ~/linux-build: a folder of the Linux
# file system, never the work folder on the Windows drive). The tools come from port/setup/linux-tools.sh.
#   trees            LB/work: the five source trees by port/setup/trees.sh, as the Windows ones are made
#   recompiler       LB/out/dolrecomp/dolrecomp (clang 20.1.8, libc++ and lld: the LLVM release's libraries)
#   runner           LB/out/mg/moderngekko-run, from nothing (the emulator, the port's core and adapter; no X11,
#                    no Wayland: the game runs headless, --headless --graphics Null)
#   module NAME FROM LB/gen/cg-NAME (the recompiler's output) and LB/gen/mod-cg-NAME/gGFZE01_recomp.so, from what
#                    the Windows module FROM was built from: <work>/work/cg-FROM/main-combo.dol, its switches
#                    (<work>/work/cg-FROM.env) and PROFILE (default <work>/scratch/pgo-FROM/a.profdata); the
#                    recompiler's object cache is LB/llvm-cache
#   game NAME        LB/run/game-NAME and LB/run/bundle-NAME, copied from <work>/port (the gates' runs/linux-playback.sh)
# Each build step runs after the busy check, under NATIVE-BUILD.lock and in a CPU window (the gates', where they are), with
# PORT_AGENT naming the request; -j is JOBS (default 8: WSL's memory is capped and shared). Its output is
# LB/STEP.log. Measured at -j8: runner 4 to 5 minutes, recompiler about a minute, module 8 minutes
# (the recompiler 7.9, the link 5 s); WSL's memory peaked about 2 GB above what it held before.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd); root=$(cd "$here/../.." && pwd)
. "$root/port/setup/app.sh"; protocol_generated=$root/$APP_PROTOCOL/generated   # the generated protocol code (the protocol submodule, pins.tsv REPO_PROTOCOL)
. "$root/port/setup/lib.sh"; W=${PORT_WORK:-$PORT_WORK_DEFAULT}
LB=${LINUX_BUILD:-$HOME/linux-build}; T=$LB/tools; L=$T/LLVM-20.1.8-Linux-X64; J=${JOBS:-8}
step=${1:?linux.sh trees|recompiler|runner|module NAME FROM}; shift
[ -x "$L/bin/clang" ] || [ "$step" = trees ] || { echo "linux: no tools in $T (port/setup/linux-tools.sh)" >&2; exit 1; }
export PATH=$T/ldlld:$L/bin:$HOME/.local/bin:$PATH
CC=(-DCMAKE_C_COMPILER="$L/bin/clang" -DCMAKE_CXX_COMPILER="$L/bin/clang++" -DCMAKE_AR="$L/bin/llvm-ar"
    -DCMAKE_RANLIB="$L/bin/llvm-ranlib")
# heavy WHAT MINUTES CMD...: the busy check, the PC's locks (where they are: port/setup/lib.sh, pc_take), then CMD
# with its output in LB/$step.log
heavy() {
  local what=$1 min=$2 check; check=$(busy_check "$W"); shift 2
  pc_sharing "$check"
  [ "$check" = none ] || [ ! -e "$check" ] || "$check" > /dev/null || { echo "linux: the busy check says wait" >&2; exit 3; }
  ( pc_take native-build-lock.sh "linux: $what" "$min"
    pc_take cpu-window.sh "linux: $what" "$min"
    "$@" ) > "$LB/$step.log" 2>&1 || { echo "linux: $what failed; see $LB/$step.log" >&2; grep -n "error\|FAILED" "$LB/$step.log" | head -20 >&2; exit 1; }
}
case $step in
trees) "$root/port/setup/trees.sh" "$LB/work" ;;
recompiler)
  S=$T/sysroot/usr
  cmake -S "$LB/work/DolRecomp" -B "$LB/out/dolrecomp" -G Ninja -DCMAKE_BUILD_TYPE=Release -DDOLRECOMP_ENABLE_LLVM=ON \
    -DLLVM_DIR="$L/lib/cmake/llvm" "${CC[@]}" -DCMAKE_PREFIX_PATH="$S" -DZLIB_INCLUDE_DIR="$S/include" \
    -DZLIB_LIBRARY="$S/lib/x86_64-linux-gnu/libz.a" -DCMAKE_CXX_FLAGS=-stdlib=libc++ \
    "-DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld -Wl,--thinlto-jobs=$J -stdlib=libc++ -static-libstdc++ -L$L/lib/x86_64-unknown-linux-gnu -l:libc++abi.a" \
    > "$LB/recompiler-configure.log"
  grep -o "Codegen source digest: [0-9a-f]* ([0-9]* files)" "$LB/recompiler-configure.log"
  heavy "the recompiler" 5 cmake --build "$LB/out/dolrecomp" --target dolrecomp -j"$J"
  echo "linux: $LB/out/dolrecomp/dolrecomp" ;;
runner)
  S=$LB/work/port/src; mkdir -p "$S/facts" "$S/app"   # the port's sources as port/moderngekko/build.sh copies them
  rsync -rc --delete --exclude __pycache__ --exclude tests --exclude patches "$root/port/core" "$root/port/moderngekko" "$S/"
  rsync -rc --delete "$root/port/facts/generated" "$S/facts/"
  mkdir -p "$S/protocol" && rsync -rc --delete "$protocol_generated" "$S/protocol/"
  . "$root/port/setup/app.sh"; rsync -c "$root/$APP_ICON" "$S/app/"
  rm -rf "$LB/out/mg"
  cmake -S "$LB/work/ModernGekko" -B "$LB/out/mg" -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF "${CC[@]}" \
    -DPKG_CONFIG_EXECUTABLE="$T/pkg-config" -DENABLE_X11=OFF -DMODERNGEKKO_FZGX_PORT_DIR="$S" > "$LB/runner-configure.log"
  heavy "the runner (about 5 min)" 10 cmake --build "$LB/out/mg" --target moderngekko-port -j"$J"
  echo "linux: $LB/out/mg/moderngekko-run ($(sha1sum "$LB/out/mg/moderngekko-run" | cut -c1-8))" ;;
module)
  N=${1:?linux.sh module NAME FROM}; F=${2:?linux.sh module NAME FROM}; G=$LB/gen/cg-$N; M=$LB/gen/mod-cg-$N
  P=${PROFILE:-$W/scratch/pgo-$F/a.profdata}; V=$LB/work/ModernGekko/vendor/dolphin
  [ ! -e "$G" ] && [ ! -e "$M" ] || { echo "linux: $G or $M exists; a build is never overwritten" >&2; exit 1; }
  for f in "$W/work/cg-$F/main-combo.dol" "$W/work/cg-$F.env" "$P"; do [ -f "$f" ] || { echo "linux: no $f" >&2; exit 1; }; done
  mkdir -p "$G" "$LB/llvm-cache"; cp "$W/work/cg-$F/main-combo.dol" "$P" "$G/"; cp "$W/work/cg-$F.env" "$G/switches.env"
  # the switches as native-module.sh hands them to the recompiler and to fastlookup.py
  mapfile -t SW < <(sed 's/^env: //; s/&& */\n/g; s/set //g' "$G/switches.env" | grep .)
  heavy "native module $N for Linux (about 8 min)" 12 env "${SW[@]}" DOLRECOMP_LLVM_CACHE="$LB/llvm-cache" \
    DOLRECOMP_MEM2_BASE=0xA0000000 "$LB/out/dolrecomp/dolrecomp" -j"$J" --backend=llvm --native-abi off \
    --profile-use "$G/$(basename "$P")" --cpu gekko --gamecube "$G/main-combo.dol" "$G"
  env "${SW[@]}" python3 "$LB/work/DolRecomp/tools/fastlookup.py" "$G/generated/generated.h"
  cp "$G/main-combo.dol" "$G/generated/main.dol"
  CMAKE_NINJA_FORCE_RESPONSE_FILE=1 cmake -S "$LB/work/work/module-template" -B "$M" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    "${CC[@]}" -DGAME_ID=GFZE01 -DGENERATED_DIR="$G/generated" -DGXRUNTIME_DIR="$V/GXRuntime" \
    -DCHASSIS_ABI_DIR="$V/Source/Core/Core/PowerPC/StaticRecomp" > "$LB/module-configure.log"
  grep -q -- "-flto=thin" "$M/build.ninja" || { echo "linux: the module's ThinLTO is off (see $LB/module-configure.log)" >&2; exit 1; }
  step=module-link; heavy "native module $N for Linux: the link" 3 cmake --build "$M" -j"$J"
  echo "linux: $M/gGFZE01_recomp.so ($(sha1sum "$M/gGFZE01_recomp.so" | cut -c1-8)); hash mismatches $(grep -c 'hash mismatch' "$LB/module.log" || true)" ;;
game)
  # LB/run/game-NAME and bundle-NAME: copies of the work folder's (game data: never committed), on Linux's disk
  F=${1:?linux.sh game NAME}; R=$LB/run; mkdir -p "$R/game-$F/sys" "$R/bundle-$F"
  cp "$W/port/game-$F/sys/"* "$R/game-$F/sys/"; cp -r "$W/port/bundle-$F/." "$R/bundle-$F/"
  [ -d "$R/game-$F/files" ] || cp -r "$(readlink -f "$W/port/game-$F/files")" "$R/game-$F/files"
  echo "linux: $R/game-$F, $R/bundle-$F" ;;
*) echo "linux: unknown step $step" >&2; exit 2 ;;
esac
