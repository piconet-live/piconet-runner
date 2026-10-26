# lib.sh: what apply.sh and check.sh share. Sourced, never run.
FORKS=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
. "$FORKS/../port/setup/work-folder.env"   # PORT_WORK_DEFAULT: the work folder when PORT_WORK names none
PINS=$FORKS/upstream.tsv
PINS_HEADER=$'tree\turl\tbase\thead\ttree_id\twork\tomits'

die() { echo "forks: $*" >&2; exit 1; }

trees() { awk -F'\t' 'NR > 1 && $1 != "" { print $1 }' "$PINS"; }

# pin TREE: the row of upstream.tsv, as URL (our fork), BASE (the upstream commit our commit is on; COMMIT:FOLDER
# when the tree is one folder of another tree, OMITS then naming what of that folder it leaves out), HEAD (our
# commit, the submodule forks/TREE's), TREE_ID (HEAD's tree) and WORK (the tree's place in the work folder).
pin() {
    local row
    row=$(awk -F'\t' -v t="$1" 'NR > 1 && $1 == t' "$PINS")
    [ -n "$row" ] || die "upstream.tsv has no tree '$1'"
    IFS=$'\t' read -r _ URL BASE HEAD TREE_ID WORK OMITS <<<"$row"
}

# g DIR ...: git in DIR, without the index refresh a status would otherwise write there. A checkout on a Windows
# drive is read with git.exe; gv strips the carriage returns from what it prints, for values.
g() {
    local d=$1
    shift
    if [[ $d == /mnt/[a-z]/* ]]; then
        git.exe --no-optional-locks -C "$(wslpath -w "$d")" "$@"
    else
        git --no-optional-locks -C "$d" "$@"
    fi
}
gv() { g "$@" | tr -d '\r'; }
