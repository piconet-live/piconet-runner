#!/bin/bash
# trees.sh WORK [--no-externals]: the five source trees of forks/upstream.tsv, each checked out by forks/apply.sh
# at its pinned commit (the submodule forks/TREE's; it fails unless the checkout has the pinned tree id), at its
# place in the work folder WORK (a folder under /mnt/<drive>, for the Windows compilers):
#   WORK/DolRecomp                              the recompiler the module is built with
#   WORK/ModernGekko                            the runtime
#   WORK/ModernGekko/vendor/dolphin             the emulator core (RecompCore), and its Externals
#   WORK/ModernGekko/vendor/dolphin/DolRecomp   the recompiler as the runner's build reads it
#   WORK/work/module-template                   the module's CMake project
# The submodules are checked out here first (git submodule update --init forks/...), from their URLs in
# .gitmodules, or wherever this clone's submodule.<name>.url points (a local mirror).
# Who makes the checkouts: Linux git, as apply.sh runs it (core.autocrlf=false, core.eol=lf), straight into the
# Windows drive. So every file has the line ends the repository stores, on every PC, and Git for Windows' settings
# decide nothing (DolRecomp's build hashes its own sources into its object cache: other line ends, other keys).
# The Windows compilers read LF sources as they are. Each checkout gets core.fileMode=false, because git.exe, which
# forks/check.sh reads a Windows drive with, sees no file modes there.
# A tree that is there already is moved to the pinned commit (refused while it holds uncommitted changes); the last
# step, forks/check.sh --trees WORK, says whether every tree is the pinned one.
#   --no-externals   leaves out RecompCore's Externals (34 submodules and three of theirs, needed only to build
#                    the runner). They are upstream's, at the commits RecompCore records: no change of ours.
# Needs the network (github.com) and, for the last step, Git for Windows. Writes only under WORK and forks/.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd); root=$(cd "$here/../.." && pwd)
. "$here/lib.sh"
WORK=${1:?trees.sh WORK [--no-externals]}; EXT=1; [ "${2:-}" = --no-externals ] && EXT=""
mkdir -p "$WORK/work"; WORK=$(cd "$WORK" && pwd)
forks=$root/$REPO_FORKS
V=ModernGekko/vendor/dolphin

git -C "$root" submodule update -q --init -- "$REPO_FORKS"
# sorted by place: a tree comes before the trees inside it (the place may be an empty folder, a submodule's in the
# tree above it; apply.sh checks out into it)
while IFS=$'\t' read -r place name; do "$forks/apply.sh" "$name" "$WORK/$place"; done \
    < <(awk -F'\t' 'NR > 1 && $1 != "" { print $6 "\t" $1 }' "$forks/upstream.tsv" | LC_ALL=C sort)

if [ -n "$EXT" ]; then
  # RecompCore's own submodules but DolRecomp (made above, our commit): upstream's, at the recorded commits.
  # --recursive: cubeb and libadrenotools have submodules of their own; the runner's configure step stops without
  git -C "$WORK/$V" -c submodule.DolRecomp.update=none -c core.autocrlf=false \
      submodule update -q --init --recursive --depth 1 --jobs 8
  echo "trees: $V: $(git -C "$WORK/$V" submodule status --recursive | grep -vc '^-') submodules checked out"
fi
"$forks/check.sh" --trees "$WORK"
