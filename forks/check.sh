#!/bin/bash
# check.sh: what a commit check can verify about forks/.
#   The pins are well formed; every submodule forks/TREE is a row of upstream.tsv and every row a submodule.
#   Each row's head is the commit the submodule is pinned at in this repository (its gitlink), and when the
#   submodule is checked out, that commit's tree is the row's tree id.
# check.sh --trees [WORK]: the trees of the work folder (PORT_WORK, by default port/setup/work-folder.env's) are the
#   pinned ones: the pinned tree id, and nothing uncommitted. Read-only; run before a build.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
bad=0
fail() { echo "forks/check: $*" >&2; bad=1; }
hex='^[0-9a-f]{40}$'

if [ "${1:-}" = --trees ]; then
    work=${2:-${PORT_WORK:-$PORT_WORK_DEFAULT}}
    for t in $(trees); do
        pin "$t"
        d=$work/$WORK
        [ -e "$d/.git" ] || { fail "$t: no checkout at $d (port/setup/trees.sh)"; continue; }
        got=$(gv "$d" rev-parse 'HEAD^{tree}')
        at=$(gv "$d" rev-parse HEAD)
        [ "$at" = "$HEAD" ] || echo "$t: $d is at ${at:0:7}, the pin is ${HEAD:0:7} (the tree decides)"
        [ -z "$(gv "$d" status --porcelain --ignore-submodules=all)" ] || fail "$t: $d has uncommitted changes"
        [ "$got" = "$TREE_ID" ] || fail "$t: $d has tree $got, upstream.tsv pins $TREE_ID"
        echo "$t  $got"
    done
    [ $bad -eq 0 ] || exit 1
    echo "forks/check: the work folder's trees are the pinned ones"
    exit 0
fi

[ "$(head -1 "$PINS")" = "$PINS_HEADER" ] || fail "upstream.tsv:1 is not the header"
root=$(git -C "$FORKS" rev-parse --show-toplevel)
subs=$(git -C "$root" ls-files -s -- forks | awk '$1 == "160000" { sub("^forks/", "", $4); print $4 }')
for s in $subs; do trees | grep -qx "$s" || fail "the submodule forks/$s has no row in upstream.tsv"; done
for t in $(trees); do
    pin "$t"
    [[ $URL == https://* ]] || fail "$t: the url is not a public https address"
    [[ ${BASE%%:*} =~ $hex && $TREE_ID =~ $hex && $HEAD =~ $hex ]] || fail "$t: a pin is not a full id"
    [[ $OMITS == - || ($OMITS != '' && $BASE == *:*) ]] || fail "$t: omits is '-', or paths of a COMMIT:FOLDER base"
    link=$(git -C "$root" ls-files -s -- "forks/$t" | awk '$1 == "160000" { print $2 }')
    [ -n "$link" ] || { fail "$t: no submodule forks/$t"; continue; }
    [ "$link" = "$HEAD" ] || fail "$t: the submodule is pinned at ${link:0:7}, upstream.tsv's head is ${HEAD:0:7}"
    if [ -e "$FORKS/$t/.git" ] && git -C "$FORKS/$t" cat-file -e "$HEAD^{tree}" 2>/dev/null; then
        got=$(git -C "$FORKS/$t" rev-parse "$HEAD^{tree}")
        [ "$got" = "$TREE_ID" ] || fail "$t: commit ${HEAD:0:7} has tree $got, upstream.tsv pins $TREE_ID"
    fi
done
[ $bad -eq 0 ] || exit 1
echo "forks/check: $(trees | wc -l) trees, each pinned as its submodule"
