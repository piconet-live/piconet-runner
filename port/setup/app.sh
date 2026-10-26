# app.sh (sourced): the application's side, for the port's scripts: where the test server and the simulated players
# are once built, what builds them, what a staged launcher is published from, the stamp of a launcher made for a server
# stack, the protocol's folder. The only file under port/ that names net/ (scripts/structure_check.py --explain 14).
# Each part is found in a checkout of its own: port/setup/pins.tsv's rows REPO_SERVICE, REPO_LAUNCHER and
# REPO_PROTOCOL, relative to this repository's root. Today they are all ".", this repository; when the folders are
# cut into repositories, only those rows change. A path below is relative to this root, as before, unless it says.
_app=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)                      # this repository's root
_app_pin() { awk -F'\t' -v n="$1" '$1 == n { print $2; f = 1 } END { exit !f }' "$_app/port/setup/pins.tsv"; }
_app_service=$(_app_pin REPO_SERVICE) && _app_launcher=$(_app_pin REPO_LAUNCHER) \
  && _app_protocol=$(_app_pin REPO_PROTOCOL) \
  || { echo "app.sh: port/setup/pins.tsv has no row REPO_SERVICE, REPO_LAUNCHER or REPO_PROTOCOL" >&2; return 1; }
_app_in() { [ "$1" = . ] && printf %s "$2" || printf %s "$1/$2"; }      # PART's checkout's PATH, relative to the root
# the test server and the simulated players: absolute paths
APP_SERVER=$_app/$(_app_in "$_app_service" net/src/Online.Server/bin/Debug/net10.0/Online.Server.dll)
APP_BOTS=$_app/$(_app_in "$_app_service" net/src/Online.Bots/bin/Debug/net10.0/Online.Bots.dll)
APP_BUILD="dotnet build $(_app_in "$_app_service" net/net.slnx)"                     # builds both (scripts/check.sh does it)
# each part's own plain check, run from this root (gates.sh row checks): this repository's, the protocol's, the
# launcher's and the service's suites. Each suite from its own net/ folder: dotnet test takes its test runner
# (Microsoft.Testing.Platform, which knows --solution) from the global.json of the folder it is started in, and from
# this root, which has none, it refuses --solution (MSB1001: Unknown switch)
_app_test() { printf '( cd %s && dotnet test --solution net.slnx )' "$(_app_in "$1" net)"; }
APP_CHECKS="port/check.sh && $(_app_in "$_app_protocol" protocol/check.sh) && $(_app_test "$_app_protocol") && $(_app_test "$_app_launcher") && $(_app_test "$_app_service") && echo 'check: ok'"
APP_LAUNCHER=$(_app_in "$_app_launcher" net/src/Launcher.App)                         # what stage-launcher.sh publishes
APP_FONTS=$(_app_in "$_app_launcher" net/src/Launcher.App/Assets/Fonts)               # the launcher's fonts, for the game window's strip
APP_ICON=$(_app_in "$_app_launcher" net/src/Launcher.App/Assets/Icon/app.ico)         # the app's icon, for the runner's program file
APP_INSTALL=$(_app_in "$_app_launcher" net/src/Launcher.Core/Files/Install.cs)        # names a player copy's folder and marker
APP_SDK=$(_app_in "$_app_launcher" net/global.json)                                   # names the .NET SDK
APP_PROTOCOL=$(_app_in "$_app_protocol" protocol)                                     # the protocol's schema and generated files
# app_stamp NAME ADDRESS KIND: the publish properties of a launcher made for a server stack (net/src/Launcher.Core/
# StackStamp.cs, which the launcher's build checks): the stack's name, its server's address (wss://HOST/PATH, as the
# launcher's official server) and whether the copy is a test build or the public one. The values are the caller's:
# whoever runs a server names it (the private release path takes them from the service's deploy/fly/stacks.py).
app_stamp() {
  [[ $1 =~ ^[A-Za-z0-9][A-Za-z0-9._-]*$ ]] || { echo "app_stamp: a stack's name is letters, digits, '.', '_' and '-': '$1'" >&2; return 1; }
  [[ $2 =~ ^wss?://[A-Za-z0-9]([A-Za-z0-9.-]*[A-Za-z0-9])?(:[0-9]{1,5})?/[A-Za-z0-9._/-]+$ ]] || { echo "app_stamp: the server's address is wss://HOST/PATH: '$2'" >&2; return 1; }
  [[ $3 =~ ^(test|public)$ ]] || { echo "app_stamp: KIND is test or public: '$3'" >&2; return 1; }
  printf '%s\n' "-p:ServerStack=$1" "-p:StackServer=$2" "-p:StackKind=$3"
}
unset -f _app_pin _app_in _app_test; unset _app_service _app_launcher _app_protocol
