#!/usr/bin/env bash
# The core's checks, run by port/check.sh. The recomp port's host-side core (port/core): its
# tests run against plain memory (no game, no emulator). Its addresses are the facts table's (port/facts/check.sh).
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

need() { [[ -e $1 ]] || { echo "check: $1 is missing (moved? correct port/core/check.sh)" >&2; exit 1; }; }
need port/core/tests/run.sh
if command -v g++ >/dev/null; then
    echo "check: port core tests"
    # the output goes to a file in a folder of this run's own, not through $(...): a pipe stays open while any process
    # the tests started holds it, so the check would wait for such a child after the tests had ended
    tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
    status=0; port/core/tests/run.sh >"$tmp/out" 2>&1 || status=$?
    [[ $status -eq 0 ]] || { tail -40 "$tmp/out" >&2; exit 1; }   # the failing checks' lines, or the compiler's
    grep -m1 '^core tests: ' "$tmp/out" || true   # whether the test program was compiled or is the one kept (run.sh)
    tail -1 "$tmp/out"
else
    echo "check: skipped the port core tests (no g++)"
fi
