"""work-folder.env as Python reads it: where the port's work folder and the PC's lock files are by default.

The file beside this one is the one place that says so; shell scripts source it, and a Python program imports this
module (sys.path gets port/setup) and takes PORT_WORK from its environment first, WORK_DEFAULT otherwise. By default
both folders are build/work in this checkout (WORK_CHECKOUT); a PC's own folders are in LOCAL
(~/.config/piconet/work-folder.env, outside every checkout), read over them as the file's last line has the shell
do. Nothing here reads the environment: a program does that at its entry (scripts/structure_check.py --explain 12)."""
import os
import re

FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "work-folder.env")
LOCAL = os.path.expanduser("~/.config/piconet/work-folder.env")
NAMES = ["PORT_LOCKS_DEFAULT", "PORT_WORK_DEFAULT"]
LINE = re.compile(r"(PORT_(?:WORK|LOCKS)_DEFAULT)=(/mnt/[a-z](?:/[\w.-]+)+)")
# the tracked file's own lines, in this order: the checkout's build folder, both defaults set to it, LOCAL read over
CHECKOUT_LINES = ['PORT_WORK_CHECKOUT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)/build/work',
                  "PORT_WORK_DEFAULT=$PORT_WORK_CHECKOUT", "PORT_LOCKS_DEFAULT=$PORT_WORK_CHECKOUT",
                  '[ ! -f "${HOME:-}/.config/piconet/work-folder.env" ] || '
                  '. "${HOME:-}/.config/piconet/work-folder.env"']


def checkout_folder(path=FILE):
    """build/work in the checkout whose port/setup holds PATH."""
    return os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(path)))), "build", "work")


def _values(path):
    """The NAME=/mnt/<drive>/path lines of PATH (a PC's own file, or a copy install.sh wrote) by name. Anything else
    is an error: the file is sourced by shell scripts, the busy check among them."""
    values = {}
    with open(path, encoding="utf-8") as f:
        for number, line in enumerate(f, 1):
            line = line.rstrip("\n")
            if not line or line.startswith("#"):
                continue
            found = LINE.fullmatch(line)
            if not found or found[1] in values:
                raise ValueError(f"{path}:{number}: not a line NAME=/mnt/<drive>/path of its own name: {line}")
            values[found[1]] = found[2]
    return values


def read(path=FILE, local=LOCAL):
    """The defaults by name: the tracked file's (this checkout's build folder; PATH is that file) or, for a file of
    NAME=/path lines, its own; LOCAL's (when it is there; it may name one or both) over them."""
    with open(path, encoding="utf-8") as f:
        lines = [line.rstrip("\n") for line in f if line.strip() and not line.startswith("#")]
    if lines == CHECKOUT_LINES:
        values = dict.fromkeys(NAMES, checkout_folder(path))
    else:
        values = _values(path)
        if sorted(values) != NAMES:
            raise ValueError(f"{path}: the checkout's four lines, or PORT_WORK_DEFAULT and PORT_LOCKS_DEFAULT one "
                             f"line each (found: {sorted(values)})")
    if local and os.path.isfile(local):
        values.update(_values(local))
    return values


WORK_CHECKOUT = checkout_folder()
_values_read = read()
WORK_DEFAULT, LOCKS_DEFAULT = _values_read["PORT_WORK_DEFAULT"], _values_read["PORT_LOCKS_DEFAULT"]
