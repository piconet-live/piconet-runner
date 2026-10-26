#!/bin/bash
# native-game.sh [OUT]: the game folder the native form boots: the extracted
# game with the executable's words of the bundle's patch list written into a copy of main.dol. Default OUT:
# <work>/port/game-native. `sys` is copied (four small files and the executable), `files` is a link to the extracted
# game's: the shared extracted copy, which build 19, the gate scenes and other builds read, is never changed.
#   BUNDLE=<folder>   the native bundle (default <work>/port/bundle-native, made by port/image/build.py)
# Needs the main checkout's .venv python only for the bundle; this script uses plain python3. Everything it writes is
# game data and stays in the work folder.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
. "$here/../setup/work-folder.env"; W=${PORT_WORK:-$PORT_WORK_DEFAULT}; WW=${PORT_WORK_WIN:-$(wslpath -w "$W")}
OUT=${1:-$W/port/game-native}; B=${BUNDLE:-$W/port/bundle-native}; G=$W/extracted/GFZE01
case "$OUT" in "$W"/*) ;; *) echo "native-game: OUT must be under $W (it holds game data)" >&2; exit 1;; esac
OUTW="$WW\\$(echo "${OUT#"$W"/}" | sed 's|/|\\|g')"
win() { (cd "$W" && cmd.exe /c "$@" > /dev/null 2>&1); }
# the link first: removing the folder must never follow it into the extracted game
[ -e "$OUT/files" ] && win rmdir "$OUTW\\files" || true
rm -rf "$OUT"; mkdir -p "$OUT/sys"
cp "$G"/sys/apploader.img "$G"/sys/bi2.bin "$G"/sys/boot.bin "$G"/sys/fst.bin "$OUT/sys/"
python3 "$here/../image/gameinput.py" --dol "$G/sys/main.dol.orig" --rel "$W/derived/main.rel" --bundle "$B" \
    --patched-dol "$OUT/sys/main.dol"
win mklink /J "$OUTW\\files" "$WW\\extracted\\GFZE01\\files" || true
[ -f "$OUT/files/main.rel" ] || [ -n "$(ls "$OUT/files" 2> /dev/null | head -1)" ] || { echo "native-game: the link to the game's files did not take" >&2; exit 1; }
echo "native game folder: $OUTW (bundle $(python3 -c "import json,sys; print(json.load(open(sys.argv[1]))['commit'])" "$B/manifest.json"))"
