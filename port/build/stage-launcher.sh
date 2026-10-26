#!/bin/bash
# stage-launcher.sh [OUT]: a development copy of the launcher with the recompiled game beside it, where the launcher
# looks for it (Launcher.Core RecompLaunch.Resolve): OUT/recomp/moderngekko-run.exe with its Sys folder, the native
# module, OUT/recomp/game, the extracted disc, OUT/recomp/fzgx, the mod bundle a race needs, and OUT/recomp/Fonts,
# the launcher's own font files, which the game window's strip is drawn in. Default OUT:
# <work>/port/launcher-dev, the copy the checks use. <work>/port/launcher is the copy played from: stage it
# only when it is to get a new version, by naming it. The module and the game are game data: they are linked
# or copied from the work folder and never leave it.
#   MODULE=<work/mod-* name>   the module to stage (cg-NAME). Always named: there is no default
#   BUNDLE=<folder>            the mod bundle. Always named: there is no default.
#                              A native bundle (port/image/build.py: mod.bin, patches.json) goes with a module
#                              built from it and with GAME, the game folder that has the patched executable
#   RUNNER=<file>              the runner to stage (default <work>/ModernGekko/build/moderngekko-run.exe; a runner one
#                              user keeps in a folder of its own while another's build stands in ModernGekko/build)
#   GAME=<folder>              the game folder to link (default <work>/extracted/GFZE01; the native form's is made by
#                              port/build/native-game.sh: <work>/port/game-native)
#   STACK=NAME STACK_ADDRESS=wss://HOST/PATH KIND=test|public   the server stack the copy's launcher is made for
#                              (app.sh app_stamp): its official server is that address, and a test build says TEST
#                              BUILD. All three or none; name them for a copy somebody plays from. Without them the
#                              copy names no stack and has no official server: the checks' copies are made so (their
#                              launch files name a server of their own)
# Run it inside a CPU window (about a minute of all cores for the publish).
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"
. "port/setup/work-folder.env"; W=${PORT_WORK:-$PORT_WORK_DEFAULT}; WW=${PORT_WORK_WIN:-$(wslpath -w "$W")}
M=${MODULE:?stage-launcher: name the module, MODULE=cg-NAME (with BUNDLE and GAME: the three parts of one image)}
B=${BUNDLE:?stage-launcher: name the bundle, BUNDLE=<folder> (port/image/build.py made it; there is no default bundle)}
OUT=${1:-$W/port/launcher-dev}
G=${GAME:-$W/extracted/GFZE01}
case "$G" in "$W"/*) ;; *) echo "stage-launcher: GAME must be under $W" >&2; exit 1;; esac
GW="$WW\\$(echo "${G#"$W"/}" | sed 's|/|\\|g')"
case "$OUT" in "$W"/*) ;; *) echo "stage-launcher: OUT must be under $W (it holds game data)" >&2; exit 1;; esac
OUTW="$WW\\$(echo "${OUT#"$W"/}" | sed 's|/|\\|g')"
# OUT is deleted first: a part named inside it (a launcher folder's own bundle) would go with it
for part in "$B" "$G" "${RUNNER:-}"; do case "$part/" in "$OUT"/*) echo "stage-launcher: $part is inside OUT, which is deleted first: name a copy of it elsewhere" >&2; exit 1;; esac; done
case "$OUTW" in *" "*) echo "stage-launcher: no spaces in the work folder's path, please" >&2; exit 1;; esac
# the stack first: a name that is none stops here, before OUT is deleted
source port/setup/app.sh
stack=()
if [ -n "${STACK:-}${STACK_ADDRESS:-}${KIND:-}" ]; then
  props=$(app_stamp "${STACK:?stage-launcher: name STACK, STACK_ADDRESS and KIND together}" "${STACK_ADDRESS:?stage-launcher: name STACK, STACK_ADDRESS and KIND together}" "${KIND:?stage-launcher: name STACK, STACK_ADDRESS and KIND together}")
  mapfile -t stack <<< "$props"
  echo "stage-launcher: made for stack $STACK ($KIND): $STACK_ADDRESS"
else
  echo "stage-launcher: no stack named: the copy's launcher has no official server (STACK=NAME STACK_ADDRESS=wss://HOST/PATH KIND=test makes a copy to play from)"
fi
win() { (cd "$W" && cmd.exe /c "$@" > /dev/null 2>&1); }   # cmd.exe cannot start in a WSL folder
# junctions first: removing the folder must never follow them into the build or the game
for j in Sys game; do [ -e "$OUT/recomp/$j" ] && win rmdir "$OUTW\\recomp\\$j" || true; done
rm -rf "$OUT"; mkdir -p "$OUT/recomp"
dotnet publish "$APP_LAUNCHER" -c Release -r win-x64 --self-contained true -p:DebugType=none "${stack[@]}" -o "$OUT" -v q -nologo
cp "${RUNNER:-$W/ModernGekko/build/moderngekko-run.exe}" "$OUT/recomp/moderngekko-run.exe"
cp "$W/work/mod-$M/gGFZE01_recomp.dll" "$OUT/recomp/"
# the game window's strip is drawn in the launcher's type (port/core/include/fzgx/strip.h names the three files):
# the same files the launcher carries, with their licence; without them the runner uses the system's font
mkdir "$OUT/recomp/Fonts"
for f in SairaCondensed-ExtraBold.ttf SairaCondensed-SemiBold.ttf SairaSemiCondensed-Regular.ttf OFL.txt; do cp "$APP_FONTS/$f" "$OUT/recomp/Fonts/"; done
mkdir "$OUT/recomp/fzgx"; cp "$B/mod.bin" "$B/manifest.json" "$OUT/recomp/fzgx/"
# the fork's bundle has its Gecko codes, the native form's its patch list
for f in codes.txt patches.json symbols.json; do [ ! -f "$B/$f" ] || cp "$B/$f" "$OUT/recomp/fzgx/"; done
# the images that are the same race as the bundle's (a replay recorded with one of them is played): the table
cp port/image/same-race.json "$OUT/recomp/fzgx/"
# a released build whose replays this image would refuse (no row of the table from its image): said here, refused in
# a player copy (port/build/package.py). Prove them on the copy (replay-check.sh) and add the rows
lost=$(python3 port/build/same_race.py refused "$B/mod.bin") && s=0 || s=$?
case $s in
  0) ;;
  1) echo "stage-launcher: NOTE: this copy refuses the replays of $(echo "$lost" | sed 's/ \(........\).*/ (image \1)/' | paste -sd, - | sed 's/,/, /g'): port/image/same-race.json has no row for its image" ;;
  *) echo "stage-launcher: port/image/same-race.json cannot be read" >&2; exit 1 ;;
esac
[ -f "$OUT/recomp/fzgx/codes.txt" ] || [ -f "$OUT/recomp/fzgx/patches.json" ] || { echo "stage-launcher: $B is no mod bundle" >&2; exit 1; }
win mklink /J "$OUTW\\recomp\\Sys" "$WW\\ModernGekko\\build\\Sys" || true
win mklink /J "$OUTW\\recomp\\game" "$GW" || true
if [ -f "$B/patches.json" ]; then
  # the native form's three parts are made from one image: the bundle, the module compiled from it, and the game
  # folder whose executable holds its words. A part made from another image stops the game at its first frames.
  sha() { python3 -c "import json,sys; print(json.load(open(sys.argv[1]))['image_sha1'])" "$1"; }
  [ -f "$W/work/cg-${M#cg-}/manifest.json" ] && [ "$(sha "$W/work/cg-${M#cg-}/manifest.json")" = "$(sha "$B/manifest.json")" ] \
    || { echo "stage-launcher: module $M was not built from this bundle's image (port/build/native-module.sh)" >&2; exit 1; }
  python3 port/image/gameinput.py --dol "$G/sys/main.dol" --rel "$W/derived/main.rel" --bundle "$B" --patched-dol "$OUT/recomp/.exe-check" > /dev/null \
    && cmp -s "$OUT/recomp/.exe-check" "$G/sys/main.dol" \
    || { echo "stage-launcher: $G/sys/main.dol does not hold this bundle's words (port/build/native-game.sh)" >&2; exit 1; }
  rm -f "$OUT/recomp/.exe-check"
fi
[ -f "$OUT/recomp/Sys/GameSettings/GFZ.ini" ] && [ -f "$OUT/recomp/game/sys/main.dol" ] || { echo "stage-launcher: the links did not take" >&2; exit 1; }
echo "staged: $OUTW\\Launcher.App.exe (module $M, bundle $([ -f "$B/patches.json" ] && echo native || echo "the fork's"), game ${G#"$W"/}, launcher $(git rev-parse --short HEAD)$(git diff --quiet HEAD || echo +changes))"
# what the staged module and runner were built from: a part without a complete manifest is a test build
python3 port/build/manifest.py check "$W/work/mod-$M/build-manifest.json" | head -1 || true
python3 port/build/manifest.py check "$W/ModernGekko/build/build-manifest.json" | head -1 || true
