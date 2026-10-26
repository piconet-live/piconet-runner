#!/bin/bash
# package.sh: one command makes a player copy: the folder a tester is handed, and its zip, from named parts.
#   <work>/dist/<Name>-<version>-<build>/  and  <Name>-<version>-<build>.zip   (the launcher's version and commit)
# A copy's top level is one program (the application's name, with its icon), the read-me and folders: no batch file.
# The program is the launcher's own host file, written by the publish to run the launcher from the folder beside it
# that holds everything else of the launcher; the marker in that folder tells the launcher it is a copy, so it keeps
# its data in the copy without a command line (Launcher.Core Install, which names the folder and the marker).
# Every part is named; none is "whatever is in the work folder today":
#   STACK=<name>            the server stack the copy belongs to (its name, as the launcher shows it)
#   STACK_ADDRESS=<address> that stack's server, wss://HOST/PATH: the launcher's official server. No default: the
#                           copy connects where whoever makes it points it (the private release path takes the
#                           name and address from the service's deploy/fly/stacks.py)
#   KIND=test|public        a test build (its launcher says TEST BUILD) or the players' build
#   RUNNER=<file>           moderngekko-run.exe, with its build-manifest.json beside it
#   RUNNER_COMMIT=<commit>  the commit of this repository the runner was built from (7 digits or more): its
#                           manifest must name it, and source/forks is taken from it
#   MODULE=cg-NAME          the native module, <work>/work/mod-cg-NAME (or the .dll's path), manifest beside it
#   BUNDLE=<folder>         the image bundle the module was built from (port/image/build.py)
#   SYS=<folder>            the emulator's Sys folder, a real one (<work>/ModernGekko/build/Sys): copied, not linked
#   SYS_SOURCE=<tree>       the emulator's source tree (<work>/ModernGekko/vendor/dolphin): Sys is checked against
#                           it, file for file, at the commit the runner's manifest names
#   VCRT=<folder>           the folder with the four VC runtime files (Visual Studio's redist folder,
#                           VC\Redist\MSVC\<version>\x64\Microsoft.VC143.CRT)
#   REPLACE=1               make a copy again that <work>/dist already holds
#   OLD_REPLAYS_REFUSED=1   says on purpose that this build does not play the replays of a released build that
#                           port/image/same-race.json names no row for (a play-breaking build); without it such a
#                           copy is refused (port/build/same_race.py)
#   COPIES=<folder>         the copy goes to COPIES/dist, a folder under the work folder (default: the work folder's
#                           own dist): a build's test copy and its players' copy carry one name, so each has its
#                           own (scripts/release-build.sh)
#   MORE_IDENTITIES=<file>  more names no copy may hold, a name a line (or kind<TAB>name): a nickname, an older
#                           address (the file is yours and is never tracked; what the PC knows is gathered by itself)
#   WITHOUT_IDENTITIES="windows git:fzgx-tools"   sources of names this PC does not have, left out knowingly. A
#                           source that cannot be asked and is not named here refuses the copy (package_names.py)
# The launcher is published from the commit checked out, which must be clean; the read-me comes from
# port/build/player-copy/, and the fonts of the game window's strip from the launcher's own (recomp/Fonts). What is checked and refused: port/build/package.py (its head). Nothing of the game
# is in a copy, nor anybody's name (package_identities.py: every file is searched for this PC's and the repositories'
# git users' names before a copy is made), and a copy is written under the work folder only: it is copied by hand
# to where players get it.
# It takes the PC for the publish itself (a minute of all cores; it waits while TIMING.lock is there), where this
# PC's CPU window is there (port/setup/lib.sh, pc_take).
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"
. port/setup/lib.sh; W=${PORT_WORK:-$PORT_WORK_DEFAULT}
: "${STACK:?package: name the server stack the copy belongs to, STACK=NAME}"
: "${STACK_ADDRESS:?package: name the stack's server, STACK_ADDRESS=wss://HOST/PATH}"
: "${KIND:?package: say what the copy is, KIND=test or KIND=public}"
: "${RUNNER:?package: name the runner, RUNNER=<moderngekko-run.exe with its build-manifest.json beside it>}"
: "${RUNNER_COMMIT:?package: name the commit the runner was built from, RUNNER_COMMIT=<commit>}"
: "${MODULE:?package: name the module, MODULE=cg-NAME}"
: "${BUNDLE:?package: name the image bundle, BUNDLE=<folder>}"
: "${SYS:?package: name the emulator\'s Sys folder, SYS=<folder>}"
: "${SYS_SOURCE:?package: name the emulator\'s source tree, SYS_SOURCE=<tree>}"
: "${VCRT:?package: name the folder with the four VC runtime files, VCRT=<folder>}"
case "$MODULE" in */*) module=$MODULE ;; *) module=$W/work/mod-$MODULE/gGFZE01_recomp.dll ;; esac
source port/setup/app.sh
name=$(sed -n 's/.*const string DisplayName = "\([^"]*\)".*/\1/p' "$APP_LAUNCHER/Branding.cs")    # the one place that holds it
said() { sed -n "s/.*const string $1 = \"\([^\"]*\)\".*/\1/p" "$2"; }       # a name as the launcher's source has it
program=$(said AppName "$APP_LAUNCHER/Branding.cs"); folder=$(said ProgramFolder "$APP_INSTALL"); marker=$(said MarkerName "$APP_INSTALL")
release_stage=$(said Stage "$APP_LAUNCHER/Branding.cs")     # the release's stage for the read-me ("Beta"; empty: none)
[ -n "$name" ] && [ -n "$program" ] || { echo "package: no name in $APP_LAUNCHER/Branding.cs" >&2; exit 1; }
[ -n "$folder" ] && [ -n "$marker" ] || { echo "package: no folder or marker name in $APP_INSTALL" >&2; exit 1; }
case "${COPIES:=$W}/" in "$W"/*) ;; *) echo "package: COPIES is a folder under the work folder ($W)" >&2; exit 1;; esac
parts=(--name "$name" --stack "$STACK" --repository "$PWD" --work "$COPIES" --runner "$RUNNER" --runner-commit "$RUNNER_COMMIT"
       --module "$module" --bundle "$BUNDLE" --sys "$SYS" --sys-source "$SYS_SOURCE" --vcrt "$VCRT" --fonts "$PWD/$APP_FONTS")
[ -z "${OLD_REPLAYS_REFUSED:-}" ] || parts+=(--old-replays-refused)
# Whose names no copy may hold, beside the checkout's and SYS_SOURCE's git users, which package.py asks itself: the
# users of the work folder's other trees and of the sibling repository the launcher's disc reader is packed in.
for tree in "$W/ModernGekko" "$W/DolRecomp" "$(dirname "$(git rev-parse --path-format=absolute --git-common-dir)")/../fzgx-tools"; do
    parts+=(--identity-repository "$tree")
done
[ -z "${MORE_IDENTITIES:-}" ] || parts+=(--more-identities "$MORE_IDENTITIES")
for source in ${WITHOUT_IDENTITIES:-}; do parts+=(--identity-without "$source"); done

# The parts first: a part that is refused costs no publish.
python3 port/build/package.py "${parts[@]}" --parts-only

# ===================== THE STACK'S ADDRESS: THE ONE PLACE IT IS APPLIED =====================
# A copy's launcher is made for its stack: the stack's address is compiled in as its official server, with the
# stack's name and whether it is a test build (net/src/Launcher.Core/StackStamp.cs), as the caller names them
# (app.sh app_stamp; a value that is no stamp is refused here, before the publish). The read-me and the record name
# the address, and package.py refuses a launcher whose own program does not carry this stack, this address and
# this kind.
props=$(app_stamp "$STACK" "$STACK_ADDRESS" "$KIND")
mapfile -t publish <<< "$props"
into_launcher() { :; }
stack_address=$STACK_ADDRESS
# =============================================================================================

stage=$(mktemp -d "${TMPDIR:-/tmp}/package.XXXXXX")
trap 'rm -rf "$stage"' EXIT
(
  PORT_AGENT=${PORT_AGENT:-package}
  pc_sharing; pc_take cpu-window.sh "package: publish the launcher" 5
  dotnet publish "$APP_LAUNCHER" -c Release -r win-x64 --self-contained true -p:DebugType=none "${publish[@]}" \
      -p:CopyProgram="$stage/program.exe" -p:CopyProgramFolder="$folder" -o "$stage/launcher" -v q -nologo
)
into_launcher "$stage/launcher"
python3 port/build/package.py "${parts[@]}" --launcher "$stage/launcher" --program "$stage/program.exe" \
    --program-name "$program" --program-folder "$folder" --marker "$marker" \
    --stack-address "$stack_address" --stack-kind "$KIND" --stage "$release_stage" ${REPLACE:+--replace}
