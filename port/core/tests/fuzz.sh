#!/bin/bash
# Fuzzes the core's readers of other people's data (tests/fuzz_*.cpp; from the client's security audits, of which
# the 0.3.0 audit's packs mode is fuzz_packs.cpp): builds
# the library and the harness with g++ and the address and undefined-behaviour sanitizers (and the check of numbers
# that do not fit the type they are turned into), then runs the replay
# fuzzer on one core and the relay fuzzer on another for the time given. A structured mutation loop, not a
# coverage-guided one (no libFuzzer or AFL here).
#   tests/fuzz.sh [seconds, default 60] [seed, default the clock] [folder for the build and what is found]
#                 ["modes", default "replay relay"; "packs" is the third (fuzz_packs.cpp)]
# A fault stops that process; its input is kept (found-N.*, with the sanitizer's report in found-N.log) and the
# fuzzer goes on from the next seed. Exit status 1 when anything was found. Two cores, never more: raise the build
# request file first when other runs time things on this machine.
set -u
here=$(cd "$(dirname "$0")/.." && pwd)
seconds=${1:-60}
seed=${2:-$(date +%s)}
out=${3:-$(mktemp -d)}
modes=${4:-replay relay}
mkdir -p "$out/obj"
. "$here/../setup/app.sh"; protocol_generated=$here/../../$APP_PROTOCOL/generated   # the status board's header (the protocol submodule, pins.tsv REPO_PROTOCOL)
flags="-std=c++20 -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=undefined,float-cast-overflow -Wall -Wextra -I$here/include -I$here/src -I$here/third_party/picojson -I$protocol_generated"
pids=()
for f in "$here"/src/*.cpp "$here"/tests/fuzz_*.cpp; do
  g++ $flags -c "$f" -o "$out/obj/$(basename "$f" .cpp).o" & pids+=($!)
  # two compilers at a time
  if [ ${#pids[@]} -ge 2 ]; then wait "${pids[0]}" || exit 2; pids=("${pids[@]:1}"); fi
done
for p in "${pids[@]}"; do wait "$p" || exit 2; done
g++ -fsanitize=address,undefined "$out"/obj/*.o -o "$out/fuzz_core" -lpthread || exit 2

one() {  # mode core
  local mode=$1 core=$2 dir="$out/$1" s=$seed found=0 end=$(( $(date +%s) + seconds ))
  mkdir -p "$dir"
  while :; do
    local left=$(( end - $(date +%s) ))
    [ $left -le 0 ] && break
    if ! ASAN_OPTIONS=abort_on_error=0:detect_leaks=0 UBSAN_OPTIONS=print_stacktrace=1 \
         taskset -c "$core" "$out/fuzz_core" "$mode" "$left" "$s" "$dir" > "$dir/run.log" 2>&1; then
      found=$((found + 1))
      for f in "$dir"/current*; do [ -e "$f" ] && cp "$f" "$out/found-$mode-$found.${f##*.}"; done
      { grep -E "^(replay|relay race|packs) " "$dir/run.log" | tail -1; grep -v -E "^(replay|relay race|packs) " "$dir/run.log" | head -60; } > "$out/found-$mode-$found.log"
      echo "fuzz $mode: FOUND (seed $s): $(grep -m1 -E "ERROR|runtime error|hang|terminate" "$out/found-$mode-$found.log")"
    else
      tail -1 "$dir/run.log"
    fi
    s=$((s + 1))
  done
  return $found
}
# the modes, two at a time, one core each
total=0 i=0 running=()
for mode in $modes; do
  one "$mode" $((i % 2)) & running+=($!)
  i=$((i + 1))
  if [ ${#running[@]} -ge 2 ]; then for p in "${running[@]}"; do wait "$p"; total=$((total + $?)); done; running=(); fi
done
for p in "${running[@]}"; do wait "$p"; total=$((total + $?)); done
echo "fuzz: $total found; build and inputs in $out"
[ $total -eq 0 ]
