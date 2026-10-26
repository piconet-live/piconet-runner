#!/bin/bash
# check.sh: this folder's checks, for scripts/check.sh: the tests of port/setup and port/build (main.rel's unpacking,
# the on-demand modules' link, the tracked build scripts byte for byte, install.sh, what makes a manifest complete,
# a player copy and its refusals) and the tables' shape. No disc, no work folder, no Windows tool.
set -euo pipefail
cd "$(dirname "$0")/../.."
# (without the run this check is a step of: a script a test starts is no step of it, scripts/progress.py)
out=$(env -u PROGRESS_RUN python3 -W error -m unittest discover -s port/setup/tests 2>&1) || { echo "$out" | tail -30 >&2; exit 1; }
echo "$out" | grep '^Ran '
awk -F'\t' 'NF != 3 { print FILENAME ": line " FNR " has " NF " cells (3 expected)"; bad = 1 } END { exit bad }' port/setup/pins.tsv port/setup/kept.tsv
