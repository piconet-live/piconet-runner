#!/bin/bash
# disc.sh WORK [DISC]: everything a build takes from the player's disc image, into the work folder. No game is run,
# the image is only read, and the same image gives the same bytes every time.
#   WORK/extracted/GFZE01          the disc as a folder (sys/, files/), laid out as the gates' saved states expect:
#                                  sys/main.dol.orig and files/fze.sample.rel.orig are the disc's files, and the
#                                  two files beside them carry the pins (one word each; recomp/manifest.toml, [[pin]])
#   WORK/derived/main.rel          main.rel, decrypted and unpacked (mainrel.py)
#   WORK/derived/loaded-05-movie.bin, loaded-0c-movie_module.bin   the movie player's code as linked (ondemand.py)
# Every result is checked against its SHA-1 in pins.tsv; another disc (another region, a bad dump) stops here.
# Everything written is game data: it stays in the work folder.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd); root=$(cd "$here/../.." && pwd)
. "$here/lib.sh"
WORK=${1:?disc.sh WORK [DISC]}; DISC=${2:-$DISC_DEFAULT}
[ -f "$DISC" ] || { echo "disc: $(disc_lacks "$DISC") (or give it: disc.sh WORK DISC)" >&2; exit 1; }
G=$WORK/extracted/GFZE01; D=$WORK/derived
is() { [ "$(sha1sum "$1" | cut -c1-40)" = "$2" ] || { echo "disc: $1 is not the file the build was made from (SHA-1 $(sha1sum "$1" | cut -c1-40), expected $2)" >&2; exit 1; }; }
pin() {  # file, offset, stock word, new word (hex): one word of a copy; the stock word must be there
  python3 - "$@" <<'PY'
import sys
path, offset, was, becomes = sys.argv[1], int(sys.argv[2], 16), bytes.fromhex(sys.argv[3]), bytes.fromhex(sys.argv[4])
with open(path, "r+b") as f:
    f.seek(offset); have = f.read(4)
    if have not in (was, becomes):
        sys.exit(f"disc: {path} holds {have.hex()} at {offset:#x}, neither the stock word nor the pin")
    f.seek(offset); f.write(becomes)
PY
}
if [ ! -f "$G/sys/main.dol.orig" ]; then
  [ ! -e "$G" ] || { echo "disc: $G exists without main.dol.orig; remove it first" >&2; exit 1; }
  mkdir -p "$G"
  python3 "$here/extract-disc.py" "$DISC" "$G"
  cp "$G/sys/main.dol" "$G/sys/main.dol.orig"
  cp "$G/files/fze.sample.rel" "$G/files/fze.sample.rel.orig"
fi
is "$G/sys/main.dol.orig" "$SHA1_MAIN_DOL"; is "$G/files/fze.sample.rel.orig" "$SHA1_SAMPLE_REL"
pin "$G/sys/main.dol" 2940 4800BAE5 38600000              # pin sample-alloc-pad: `bl OSGetTick` -> `li r3,0`
pin "$G/files/fze.sample.rel" 104 7C051838 38A00000       # pin mainrel-pad: `and r5,r0,r3` -> `li r5,0`
mkdir -p "$D"
python3 "$here/mainrel.py" "$G/files/enemy_line/line__.bin" "$D/main.rel" > /dev/null
is "$D/main.rel" "$SHA1_MAIN_REL"
python3 "$here/ondemand.py" "$G/files" "$D/main.rel" "$D" > /dev/null
is "$D/loaded-05-movie.bin" "$SHA1_MOVIE"; is "$D/loaded-0c-movie_module.bin" "$SHA1_MOVIE_MODULE"
echo "disc: $G ($(find "$G" -type f | wc -l) files), main.rel and the movie player's two images: all as pinned"
