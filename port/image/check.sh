#!/usr/bin/env bash
# The image's checks, run by port/check.sh. Every Gecko code and the mod image are built, so the assertions inside
# the builders run. They need keystone, from PORT_PYTHON or else the .venv of the main checkout (premerge checks
# commits in a worktree beside it, without its own .venv). A generator or the tests folder that is missing fails, with or without keystone:
# when one moves, correct its path here.
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

# a test program that does not end is stopped; each test has a limit of its own.
# scripts/ (limit.sh, unittest_limited.py) is the monorepo's: a checkout without it (the runner's) uses `timeout` and
# Python's own unittest for the same runs
[[ ! -f scripts/limit.sh ]] || source scripts/limit.sh
declare -F limit > /dev/null || limit() { timeout --foreground -k 10 "$1" "${@:3}"; }
unittest=scripts/unittest_limited.py; [[ -f $unittest ]] || unittest="-m unittest"
main=$(cd "$(git rev-parse --git-common-dir)/.." && pwd)
py=${PORT_PYTHON:-$main/.venv/bin/python}   # a Python with keystone, as build.sh takes it
have=0
if command -v "$py" > /dev/null && "$py" -c "import keystone" 2>/dev/null; then have=1; fi
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
need() { [[ -e $1 ]] || { echo "check: $1 is missing (moved? correct port/image/check.sh)" >&2; exit 1; }; }
build() { local f=$1; shift; need "$f"; ((have)) || return 0; "$py" "$f" "$@" >"$tmp/out" 2>"$tmp/err" || { cat "$tmp/err" >&2; echo "check: $f failed" >&2; exit 1; }; }

if ((have)); then echo "check: Gecko codes and the mod image build"; fi
build port/image/routines/direct.py 1 0 29 3
build port/image/routines/setup.py
build port/image/routines/seats.py same:0
build port/image/routines/standin.py 0 0 1 0x8001 0
build port/image/routines/netframe.py
build port/image/routines/mute.py
build port/image/routines/modbuild.py "$tmp/mod.bin" 0 0 1 0x8001 0
need port/image/tests
if ((have)); then
    echo "check: the native form's image and patch list"
    limit 900 "the image's tests" "$py" -W error $unittest discover -s port/image/tests -q \
        2>"$tmp/err" >"$tmp/out" || { cat "$tmp/err" >&2; exit 1; }
else
    echo "check: skipped the code builds (no .venv with keystone in $main; see README)"
fi
