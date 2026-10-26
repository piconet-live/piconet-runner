#!/usr/bin/env bash
# The facts table's checks, run by port/check.sh: the generated files are current, and every generated value equals
# the constant in today's sources that it will replace (tests/: places.tsv says where each stands). Fast: no game,
# no build. After a file with places was split or a constant moved: tests/places.py prints what it no longer finds,
# and tests/places.py --sources rewrites the table's source lines.
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

need() { [[ -e $1 ]] || { echo "check: $1 is missing (moved? correct port/facts/check.sh)" >&2; exit 1; }; }
need port/facts/generate.py
need port/facts/game.tsv
need port/facts/tests
# a test program that does not end is stopped; each test has a limit of its own.
# scripts/ (limit.sh, unittest_limited.py) is the monorepo's: a checkout without it (the runner's) uses `timeout` and
# Python's own unittest for the same runs
[[ ! -f scripts/limit.sh ]] || source scripts/limit.sh
declare -F limit > /dev/null || limit() { timeout --foreground -k 10 "$1" "${@:3}"; }
unittest=scripts/unittest_limited.py; [[ -f $unittest ]] || unittest="-m unittest"
echo "check: the facts table"
python3 port/facts/generate.py --check
limit 900 "the facts table's tests" python3 -W error $unittest discover -s port/facts/tests -q
