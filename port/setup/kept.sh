#!/bin/bash
# kept.sh WORK [--from REFERENCE]: are the kept files of kept.tsv in the work folder, each with its recorded SHA-1?
# These are inputs of the gates that no recipe makes again: the recordings the gates play back (made on the retired
# fork, or with a fault that is fixed), the JIT reference's match race and the gate scenes' two saved states
# (kept.tsv's first line says why each). Without --from it reads only.
#   --from REFERENCE   a work folder that has them (the shared one, which work-folder.env names): a file WORK lacks is
#                      copied from there when it has the recorded SHA-1. Nothing in WORK is overwritten, REFERENCE is
#                      only read. This is how a new work folder (setup.sh) gets the inputs of the gate rows batch,
#                      match, fork, forced, control and crash.
# Exit status: 0 all there and equal, 1 otherwise.
set -uo pipefail
here=$(cd "$(dirname "$0")" && pwd); WORK=${1:?kept.sh WORK [--from REFERENCE]}; bad=0; FROM=""
[ "${2:-}" != --from ] || FROM=${3:?kept.sh WORK --from REFERENCE}
sha() { sha1sum "$1" | cut -c1-40; }
while IFS=$'\t' read -r name want why; do
  case "$name" in name|"#"*|"") continue;; esac
  if [ ! -f "$WORK/$name" ] && [ -n "$FROM" ] && [ -f "$FROM/$name" ] && [ "$(sha "$FROM/$name")" = "$want" ]; then
    mkdir -p "$(dirname "$WORK/$name")" && cp "$FROM/$name" "$WORK/$name" && echo "kept: copied    $name  (from $FROM)"
  fi
  if [ ! -f "$WORK/$name" ]; then echo "kept: MISSING   $name  ($why)"; bad=1
  elif [ "$(sha "$WORK/$name")" = "$want" ]; then echo "kept: equal     $name"
  else echo "kept: DIFFERENT $name  ($why)"; bad=1; fi
done < "$here/kept.tsv"
exit $bad
