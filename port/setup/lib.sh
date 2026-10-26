# lib.sh (sourced by the scripts of port/setup and port/build): reads pins.tsv, the table of repository paths, tools
# and the hashes of what the disc gives, into shell variables of the rows' names. The source trees' pins are not
# here: forks/upstream.tsv is their one table. Where the shared work folder is, is not here either:
# work-folder.env is its one place (PORT_WORK_DEFAULT, PORT_LOCKS_DEFAULT), read here for the scripts that refuse it.
# pins.tsv names no tool's place and no disc: a PC's own rows (a tool kept off PATH, the disc image) are in
# ~/.config/piconet/pins.tsv, a table of this form outside every checkout, read over pins.tsv's when it is there.
_pins=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/pins.tsv
. "${_pins%/*}/work-folder.env"
for _table in "$_pins" "${HOME:-}/.config/piconet/pins.tsv"; do
  [ -f "$_table" ] || continue
  # (cut at each tab by hand: `IFS=$'\t' read` takes two tabs in a row as one, so a row with an empty value,
  # DISC_DEFAULT's or PC_SCRIPTS_DEFAULT's, would get its note as the value)
  while IFS= read -r _line; do
    _name=${_line%%$'\t'*}
    case "$_name" in name|"#"*|"") continue;; esac
    case "$_line" in *$'\t'*) _value=${_line#*$'\t'}; _value=${_value%%$'\t'*};; *) _value="";; esac
    printf -v "$_name" '%s' "$_value"
  done < "$_table"
done
# The disc image is the user's own setting, with no default (pins.tsv's row is empty): DISC_HOW says how to set it.
DISC_HOW='set DISC_DEFAULT to your own F-Zero GX (USA) ISO: a row "DISC_DEFAULT<tab>/mnt/c/.../F-Zero GX (USA).iso" in ~/.config/piconet/pins.tsv'
: "${DISC_DEFAULT:=}"
# disc_lacks DISC: nothing when DISC is a file; else one phrase, why not and how to set it
disc_lacks() {
  if [ -z "$1" ]; then printf 'no disc image set: %s' "$DISC_HOW"
  elif [ ! -f "$1" ]; then printf 'no disc image at %s: %s' "$1" "$DISC_HOW"; fi
}
# windows_folder WORK WHO: whether the Windows build can work in WORK (on a Windows drive, no spaces), else one line
# of how to have one. pc_shared_work WORK: whether WORK is the work folder this PC names for everyone
# (~/.config/piconet/work-folder.env), which only the scripts that say so write (install.sh --shared).
windows_folder() {
  local how="clone this repository onto a Windows drive (e.g. under /mnt/c/), or name a work folder there: setup.sh WORK and PORT_WORK=WORK for the build, or PORT_WORK_DEFAULT=/mnt/<drive>/... in ~/.config/piconet/work-folder.env for both"
  case "$1" in
    *" "*) echo "$2: the work folder $1 has a space, which the Windows build scripts cannot take: $how" >&2; return 1;;
    /mnt/?/*) ;;
    *) echo "$2: the work folder $1 is not on a Windows drive: the compilers are Windows programs, and cmd.exe cannot work in WSL's own filesystem: $how" >&2; return 1;;
  esac
}
pc_shared_work() { [ "$PORT_WORK_DEFAULT" != "$PORT_WORK_CHECKOUT" ] && [ "$(realpath -m "$1")" = "$PORT_WORK_DEFAULT" ]; }
# find_tools: TOOL_VCVARS_WIN, TOOL_LLVM_WIN, TOOL_PYTHON_WIN and TOOL_GIT as port/setup/tools.py finds them (vswhere,
# PATH, the py launcher; a row of ~/.config/piconet/pins.tsv over that). Status 1, with one line for each tool that is
# missing or of another version than pins.tsv's VERSION_ rows. Only the scripts that write or run the Windows build
# call it (it starts Windows programs).
find_tools() {
  local found
  found=$(python3 "$_pc_setup/tools.py" --shell) || return 1
  eval "$found"
}
# The gates (REPO_GATES: the private piconet-gates, checked out in this repository at that path; optional): where
# they are, the harness is. GATES_DIR is that folder whether it is there or not.
_pc_setup=${_pins%/*}
GATES_DIR=${_pins%/port/setup/*}/$REPO_GATES
# The harness that starts every test game: the gates' copy (vendor/harness), or a recomp-kit checkout's
# tools/harness when RECOMP_KIT names one. Neither is there in a checkout without the gates: no build needs it.
HARNESS_DIR=${RECOMP_KIT:+$RECOMP_KIT/tools/harness}; HARNESS_DIR=${HARNESS_DIR:-$GATES_DIR/vendor/harness}
unset _pins _table _name _value _line

# This PC's machine sharing: the busy check (CHECK: the work folder's port/scratch/rp/chk.sh, which install.sh puts
# there), NATIVE-BUILD.lock and the CPU window (native-build-lock.sh, cpu-window.sh). They are the PC's, not the
# build's: no repository holds them. A PC that has such scripts names their
# folder: PC_SCRIPTS, by default PC_SCRIPTS_DEFAULT (empty here; a row of ~/.config/piconet/pins.tsv). They are used
# where they are, and a checkout or a work folder without them builds all the same.
#   pc_sharing CHECK   says which of them are not here, once for a command and the scripts it starts
#   busy_check WORK    the busy check's file: BUSY_CHECK, or WORK's port/scratch/rp/chk.sh (install.sh puts it
#                      there); BUSY_CHECK=none leaves the check out on purpose (said once by pc_sharing). An empty
#                      BUSY_CHECK is the default, as an unset one
#   pc_has SCRIPT      whether the PC's SCRIPT is here (PC_SCRIPTS/SCRIPT)
#   pc_take SCRIPT WHAT [MINUTES]   sources PC_SCRIPTS/SCRIPT when it is here, in the calling shell (the lock or the
#                      request is given back when that shell exits); nothing when it is not
#   pc_release         gives back now what this shell took through pc_take (an exit trap of the caller's own, which
#                      replaces the sourced script's); nothing when nothing was taken
: "${PC_SCRIPTS_DEFAULT:=}"
PC_SCRIPTS=${PC_SCRIPTS:-$PC_SCRIPTS_DEFAULT}
pc_has() { [ -n "$PC_SCRIPTS" ] && [ -f "$PC_SCRIPTS/$1" ]; }
pc_take() { pc_has "$1" || return 0; local script=$PC_SCRIPTS/$1; shift; . "$script" "$@"; }
pc_release() { ! declare -F _lk_release > /dev/null || _lk_release; }
pc_sharing() {
  [ -z "${PC_SHARING_SAID:-}" ] || return 0
  export PC_SHARING_SAID=1
  local gone=""
  pc_has native-build-lock.sh || gone="$gone; the build lock (native-build-lock.sh)"
  pc_has cpu-window.sh || gone="$gone; the CPU window (cpu-window.sh)"
  [ -z "$gone" ] || gone="$gone, this PC's own scripts (PC_SCRIPTS: ${PC_SCRIPTS:-none named})"
  [ -z "${1:-}" ] || [ "$1" = none ] || [ -e "$1" ] || gone="$gone; the busy check ($1)"
  [ -z "$gone" ] || echo "note: left out, as this PC's machine sharing is not here: ${gone#; }"
  [ "${1:-}" != none ] || echo "note: no busy check: BUSY_CHECK=none"
}
busy_check() { printf '%s' "${BUSY_CHECK:-$1/port/scratch/rp/chk.sh}"; }

# pc_busy_check WORK: sets BUSY, the busy check a test game asks first (busy_check WORK); `true` when the file is
# not there or BUSY_CHECK=none, so a game starts (pc_sharing says so on standard error, once for the command and
# the scripts it starts).
pc_busy_check() {
  BUSY=$(busy_check "$1")
  pc_sharing "$BUSY" >&2
  [ "$BUSY" != none ] && [ -e "$BUSY" ] || BUSY=true
}

# wait_while_busy CHECK: go on when the busy check says "none"; while a game or a launcher of anyone's runs, look
# again every five seconds, for at most 40 minutes (then status 3). The check itself is run as it is. Without a
# check (no file CHECK, or CHECK none: pc_sharing says so) it goes on at once.
wait_while_busy() {
  local waited=0
  [ "$1" != none ] && [ -e "$1" ] || return 0
  until "$1" > /dev/null; do
    [ $waited = 0 ] && echo "waiting: the busy check says a game or a launcher is running"
    sleep 5; waited=$((waited + 5))
    [ $waited -lt 2400 ] || { echo "the busy check still says wait after 40 minutes" >&2; return 3; }
  done
}

# The build is CMake's: the superbuild at the checkout's root (CMakeLists.txt) with a preset per platform
# (CMakePresets.json; port/build/README.md). The port's scripts run it in WSL through cmd.exe, in a Developer
# environment made as a contributor's is: vcvars64.bat (Visual Studio), then Python and its Scripts in front of PATH
# (cmake and ninja may be pip's), with the tools as port/setup/tools.py finds them. Its build folder is in the work
# folder (a checkout may be on WSL's own filesystem, where cmd.exe cannot work): WORK/cmake/<checkout>-<hash>.
#   port_cmake WORK [-DNAME=VALUE ...]   configures it (the preset windows, with this PC's tools and WORK); status 1
#                                        with the log's last lines when it fails
#   port_cmake_build WORK TARGET LOG     builds TARGET, its output into LOG (a path in WSL)
port_cmake_dir() {
  local root; root=$(cd "$_pc_setup/../.." && pwd)
  printf '%s/cmake/%s-%s' "$1" "$(basename "$root")" "$(printf %s "$root" | sha1sum | cut -c1-8)"
}
_port_cmake_cmd() {   # WORK COMMAND: COMMAND in the Developer environment, through a batch file in WORK/cmake (cmd.exe
  # needs a Windows folder, and quotes inside cmd.exe's own command line do not survive WSL's hand-over)
  [ -n "${TOOL_VCVARS_WIN:-}" ] && [ -n "${TOOL_PYTHON_WIN:-}" ] || find_tools || { echo "build: the tools above are missing (port/setup/tools.py --say)" >&2; return 1; }
  local bat status; mkdir -p "$1/cmake"; bat=$(mktemp "$1/cmake/run-XXXXXX.cmd")
  printf '@echo off\r\ncall "%s" >nul || exit /b 1\r\nset "PATH=%s\\Scripts;%s;%%PATH%%"\r\n%s\r\nexit /b %%errorlevel%%\r\n' \
    "$TOOL_VCVARS_WIN" "$TOOL_PYTHON_WIN" "$TOOL_PYTHON_WIN" "${2//%/%%}" > "$bat"   # (a % of the command: %%)
  local run=$bat priority; priority=$(port_priority) || { rm -f "$bat"; return 1; }
  if [ "$priority" = low ]; then   # the batch file through another, which starts it below normal and waits for
    run=${bat%.cmd}-low.cmd                 # it: start /wait gives its status back; the compilers it starts inherit it
    printf '@echo off\r\nstart "" /belownormal /b /wait cmd /c "%s"\r\nexit /b %%errorlevel%%\r\n' "$(wslpath -w "$bat")" > "$run"
  fi
  (cd "$1" && cmd.exe /c "$(wslpath -w "$run")"); status=$?
  rm -f "$bat" "${bat%.cmd}-low.cmd"; return $status
}
# PORT_PRIORITY=low: the build's Windows compile steps (every batch file above: the configure and each target) run
# at below-normal priority, and the WSL side of a script that sources this file at nice 10, so that a PC that is
# also used for something else keeps answering. Unset or normal: as before, normal priority.
port_priority() {
  case "${PORT_PRIORITY:-normal}" in normal|low) printf '%s' "${PORT_PRIORITY:-normal}";;
    *) echo "build: PORT_PRIORITY is low or normal (unset: normal), not '$PORT_PRIORITY'" >&2; return 1;; esac
}
if [ "${PORT_PRIORITY:-}" = low ] && [ "$(nice)" -lt 10 ]; then renice -n 10 -p $$ > /dev/null; fi
port_cmake() {
  local work=$1 root bin log; shift
  root=$(cd "$_pc_setup/../.." && pwd); bin=$(port_cmake_dir "$work"); mkdir -p "$bin"; log=$bin/configure.log
  [ -n "${TOOL_LLVM_WIN:-}" ] || find_tools || return 1
  local args=(-S "$(wslpath -w "$root")" --preset windows -B "$(wslpath -m "$bin")" "-DPORT_WORK=$(wslpath -m "$work")"
              "-DPORT_LLVM_ROOT=$TOOL_LLVM_WIN" "-DPORT_SOURCE_COMMIT=$(git -C "$root" describe --always --dirty)" "$@")
  _port_cmake_cmd "$work" "cmake $(printf '"%s" ' "${args[@]}") > \"$(wslpath -w "$log")\" 2>&1" \
    || { echo "build: the configure failed; see $log" >&2; tail -15 "$log" >&2; return 1; }
}
port_cmake_build() {
  local bin; bin=$(port_cmake_dir "$1")
  _port_cmake_cmd "$1" "cmake --build \"$(wslpath -m "$bin")\" --target $2 > \"$(wslpath -w "$3")\" 2>&1" \
    || { echo "build: $2 failed; see $3" >&2; grep -n "error\|FAILED\|CMake Error" "$3" | head -30 >&2; return 1; }
}
