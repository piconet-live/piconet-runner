#!/usr/bin/env bash
# The port's checks, run by scripts/check.sh, and the runner's plain check (build and ordinary tests; no structure
# guards): the facts table's, the image's, the core's, its scripts' (the gates' judges), then the setup's and the
# build's. A part whose check.sh is missing fails; the gates are optional (private, piconet-gates): without their checkout (port/setup/pins.tsv REPO_GATES) their checks are left
# out, and said. Needs nothing of scripts/ (the monorepo's).
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

[[ ! -f scripts/limit.sh ]] || source scripts/limit.sh   # tries: at a landing a part that fails is run once more, alone
declare -F tries > /dev/null || tries() { shift; "$@"; }
[[ ! -f scripts/progress.sh ]] || source scripts/progress.sh      # the full check's steps (scripts/check.sh began the run)
declare -F progress_step > /dev/null || progress_step() { :; }
gates=$(awk -F'\t' '$1 == "REPO_GATES" { print $2 }' port/setup/pins.tsv 2> /dev/null || :)
gates=${gates:-port/gates}   # the gates' checkout (pins.tsv REPO_GATES)
for part in port/facts port/image port/core "$gates" port/setup; do
    progress_step "${part/\//-}"
    if [[ $part == "$gates" && ! -d $gates ]]; then echo "check: no gates here ($gates, piconet-gates): their checks are left out"; continue; fi
    [[ -f $part/check.sh ]] || { echo "check: $part/check.sh is missing, so the checks of $part/ did not run" >&2; exit 1; }
    tries "the checks of $part/" bash "$part/check.sh"
done
