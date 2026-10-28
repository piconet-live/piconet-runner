#!/bin/bash
# The core's tests: builds the library, tests/test_*.cpp and tests/main.cpp with g++ and runs them. No game, no
# emulator.
#   tests/run.sh [build folder]   (default: a temporary one)
#
# The tests run every time. The test program is not compiled again when nothing it is built from has changed
# (the compile is 22 to 30 s of every full check, and most landings do not touch the core). The built
# program is kept, outside the repository, under a key that is a hash of everything the compile reads:
#   - this script; the flags; the list of sources;
#   - every file the compiler says the sources include (g++ -M: the core's headers, the facts table's, the system's
#     and the standard library's own), by its content;
#   - the compiler: its version text, its target, and the compiler program itself by its content.
# A program is taken from there only under exactly that key, and only when it is still the bytes that were kept
# (its own hash is kept beside it). Whatever differs, a header, a flag, a test file, the compiler, is another key:
# the program is built as ever and kept under the new one. So no program built from other files can run.
#   - The program holds no path of this checkout: the sources are compiled by their names from port/core, and it
#     runs from there. What its tests read while they run (port/configs, the facts' tables, a handbook page, their
#     own cases) they find from where they run, in this checkout, every time.
#   - Two runs at once: each builds its own program and runs that; a kept program appears under its key whole, by
#     one rename, and the second to arrive finds it there and drops its own copy.
#   - A file that changed while the program was being built: the key is taken again after the build, and a program
#     whose key moved meanwhile is run and not kept.
#   FZGX_CORE_TESTS_CACHE   where the programs are kept (default ~/.cache/fzgx-core-tests; the twelve used last
#                           stay). A folder that cannot be made: nothing is kept, and the program is built each time.
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
out=${1:-$(mktemp -d)}; mkdir -p "$out"; out=$(cd "$out" && pwd)
# The test program's temporary folder is one this script makes and removes. The program keeps its files in a folder
# of its own with a random name (TempRoot in harness.h) and removes it when it ends; a run that ends without its
# destructors (a test over its time limit, an abort, a kill) leaves that folder behind. Here it is left inside this
# one, which the trap removes, and not in the caller's temporary folder.
tmp=$(mktemp -d)
if [ -n "$1" ]; then trap 'rm -rf "$tmp"' EXIT; else trap 'rm -rf "$tmp" "$out"' EXIT; fi
cd "$here"
# the other parts' checkouts, from port/core (port/setup/pins.tsv's REPO_PROTOCOL and REPO_LAUNCHER, relative to the
# repository's root: "." today; on the split only those rows change):
# the protocol's generated headers (the status board's, the relay's packets) and the files the tests hold the
# core to (ProtocolFile, LauncherFile in tests/harness.h)
pin() { awk -F'\t' -v n="$1" '$1 == n { print $2; f = 1 } END { exit !f }' ../setup/pins.tsv; }
protocol=../../$(pin REPO_PROTOCOL) && launcher=../../$(pin REPO_LAUNCHER) \
  || { echo "run.sh: port/setup/pins.tsv has no row REPO_PROTOCOL or REPO_LAUNCHER" >&2; exit 1; }
protocol_generated=$protocol/protocol/generated
flags="-std=c++20 -O1 -g -Wall -Wextra -Werror -Iinclude -Isrc -Ithird_party/picojson -I$protocol_generated -DFZGX_PROTOCOL_DIR=\"$protocol\" -DFZGX_LAUNCHER_DIR=\"$launcher\""
sources=(src/*.cpp tests/test_*.cpp tests/main.cpp)
export flags out

# key: the hash of everything the compile reads, or nothing when it cannot be had (then nothing is kept or taken).
key() {
  local deps
  deps=$(printf '%s\0' "${sources[@]}" | xargs -0 -P "$(nproc)" -I{} sh -c 'g++ $flags -M "$1"' _ {} 2> /dev/null) || return 0
  { cat "$here/tests/run.sh"; echo "$flags"; printf '%s\n' "${sources[@]}"
    g++ --version; g++ -dumpmachine; sha256sum "$(g++ -print-prog-name=cc1plus)" "$(command -v g++)" 2> /dev/null | cut -d' ' -f1
    printf '%s\n' "$deps" | tr ' \\' '\n\n' | grep -v ':$' | grep . | sort -u | xargs -d '\n' sha256sum 2> /dev/null
  } | sha256sum | cut -c1-40
}
cache=${FZGX_CORE_TESTS_CACHE:-${XDG_CACHE_HOME:-$HOME/.cache}/fzgx-core-tests}
mkdir -p "$cache" 2> /dev/null || cache=""
k=""; [ -z "$cache" ] || k=$(key)
kept=$cache/$k
if [ -n "$k" ] && [ -f "$kept/test_core" ] && (cd "$kept" && sha256sum --status -c test_core.sha256 2> /dev/null) && cp "$kept/test_core" "$out/test_core" 2> /dev/null; then
  touch "$kept" 2> /dev/null || true
  echo "core tests: the test program is the one built from these same files before (kept as ${k:0:12}): not compiled again"
else
  # One compiler a processor at most. Every file compiled at once was 42 compilers on 16 processors, about
  # 330 MB each at their peak, 11 GB together, enough for WSL to run out of memory. The time
  # is the same: the files would wait for a processor anyway. A file that does not compile fails the run (xargs:
  # 123).
  printf '%s\0' "${sources[@]}" |
    xargs -0 -P "$(nproc)" -I{} sh -c 'g++ $flags -c "$1" -o "$out/$(basename "$1" .cpp).o"' _ {}
  g++ "$out"/*.o -o "$out/test_core" -lpthread -llzma
  # kept for the next run of these same files: only when they are still the files the key was taken from
  if [ -n "$k" ] && [ "$(key)" = "$k" ] && new=$(mktemp -d "$cache/.new.XXXXXX" 2> /dev/null); then
    # (a program kept under this key that is no longer the bytes that were kept goes first: it is never run, and
    # would stand in the way of this one for ever)
    if [ -d "$kept" ] && ! (cd "$kept" && sha256sum --status -c test_core.sha256 2> /dev/null); then rm -rf "$kept"; fi
    if cp "$out/test_core" "$new/test_core" && (cd "$new" && sha256sum test_core > test_core.sha256) && mv -T "$new" "$kept" 2> /dev/null; then
      echo "core tests: compiled (${#sources[@]} files); the test program is kept as ${k:0:12} for the next run of these same files"
      # the twelve used last stay
      ls -1dt "$cache"/[0-9a-f][0-9a-f][0-9a-f][0-9a-f]*/ 2> /dev/null | tail -n +13 | while IFS= read -r old; do
        case "$old" in "$cache"/????????????????????????????????????????/) rm -rf "$old";; esac
      done
    else
      rm -rf "$new"   # another run kept the same program a moment earlier (or the folder is full): this copy goes
      echo "core tests: compiled (${#sources[@]} files)"
    fi
  else
    echo "core tests: compiled (${#sources[@]} files); not kept$([ -z "$k" ] || echo ": a file changed while it was built")"
  fi
fi
# The time limit itself, first: a run that hangs on purpose must be ended by a limit of one second, with status 3 and
# the limit's line (a second). If the limit is broken nothing in the program ends that run, so `timeout` stops it
# after a minute (status 124), and this script fails saying so.
hung=$(FZGX_TEST_TIME_LIMIT_SECONDS=1 TMPDIR="$tmp" timeout -k 5 60 "$out/test_core" --hang) && status=0 || status=$?
case "$status:$hung" in
  3:"TIME LIMIT: "*) ;;
  124:*) echo "the tests' own time limit did not fire: a run that hangs on purpose was still running after 60 s and" \
              "was stopped from outside (status 124)"; exit 1 ;;
  *) echo "the tests' time limit did not end a run that hangs (status $status): $hung"; exit 1 ;;
esac
TMPDIR="$tmp" "$out/test_core"   # (from port/core: its tests find their files from here)
