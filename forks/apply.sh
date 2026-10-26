#!/bin/bash
# apply.sh TREE DEST [SOURCE]: our tree TREE (a row of upstream.tsv) checked out at DEST, at the pinned commit.
#   SOURCE  a repository that holds the commit: by default the submodule forks/TREE (git submodule update --init
#           forks/TREE checks it out), else the row's URL, a mirror or a local clone of our fork
#   DEST    a new or empty folder, which becomes a checkout of the commit; or a checkout made before, which is
#           moved to it (refused while it holds uncommitted changes)
# The commit is fetched from SOURCE (its HEAD when SOURCE is the submodule, else the commit itself), checked out
# detached, and its tree id (git rev-parse HEAD^{tree}) must equal the row's, or the script fails and names both.
# The checkout is Linux git's with core.autocrlf=false and core.eol=lf (every file as the repository stores it) and
# core.fileMode=false (git.exe, which check.sh reads a Windows drive with, sees no file modes there). Submodules of
# the tree are not fetched: it only records their commits.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
[ $# -ge 2 ] && [ $# -le 3 ] || die "usage: apply.sh TREE DEST [SOURCE]"
tree=$1 dest=$2 src=${3:-}
pin "$tree"
ref=$HEAD
if [ -z "$src" ]; then
    src=$FORKS/$tree
    [ -e "$src/.git" ] || die "$tree: the submodule forks/$tree is not checked out (git submodule update --init forks/$tree), or name a SOURCE"
    [ "$(git -C "$src" rev-parse HEAD)" = "$HEAD" ] || die "$tree: forks/$tree is not at the pinned commit ${HEAD:0:7} (git submodule update forks/$tree)"
    ref=HEAD
elif [[ $src != *://* && $src != /* ]]; then   # a folder, named from here: the fetch runs inside DEST
    [ -d "$src" ] || die "no repository at $src (a URL, or a folder that exists)"
    src=$(cd "$src" && pwd)
fi

mkdir -p "$dest"
depth=()
if [ ! -e "$dest/.git" ]; then
    [ -z "$(ls -A "$dest")" ] || die "$dest exists, holds files and is no checkout"
    git init -q "$dest"
    depth=(--depth 1)   # a new checkout needs the commit only; one made before keeps its history whole
else
    [ -z "$(git -C "$dest" status --porcelain --ignore-submodules=all)" ] || die "$tree: $dest has uncommitted changes"
fi
git -C "$dest" config core.autocrlf false
git -C "$dest" config core.eol lf
git -C "$dest" config core.fileMode false
git -C "$dest" -c protocol.file.allow=always fetch -q "${depth[@]}" "$src" "$ref" || die "$tree: $src does not give the commit ${HEAD:0:7}"
got=$(git -C "$dest" rev-parse FETCH_HEAD)
[ "$got" = "$HEAD" ] || die "$tree: $src gives commit ${got:0:7}, upstream.tsv pins ${HEAD:0:7}"
git -C "$dest" checkout -q --detach "$HEAD"
got=$(git -C "$dest" rev-parse 'HEAD^{tree}')
[ "$got" = "$TREE_ID" ] || die "$tree: commit ${HEAD:0:7} has tree $got, upstream.tsv pins $TREE_ID"
echo "$tree: commit ${HEAD:0:7}, tree ${got:0:7}, as pinned, at $dest"
