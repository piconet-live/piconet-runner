"""parts.py: where the other parts' checkouts are, for the runner's Python: port/setup/pins.tsv's rows REPO_SERVICE,
REPO_LAUNCHER and REPO_PROTOCOL, each relative to this repository's root ("." while every part is in this repository;
on the split only those rows change). port/setup/app.sh
reads the same rows for the shell. Standard library only.

  checkout("REPO_LAUNCHER")                  the launcher's checkout, an absolute path
  path_in("REPO_PROTOCOL", "handbook/...")   a file of that checkout"""
import os

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
PARTS = ("REPO_SERVICE", "REPO_LAUNCHER", "REPO_PROTOCOL")


def checkout(part, root=ROOT):
    """PART's checkout (one of PARTS) as an absolute path; ValueError when pins.tsv cannot be read or has no row."""
    if part not in PARTS:
        raise ValueError(f"{part} is not one of the parts' rows ({', '.join(PARTS)})")
    try:
        with open(os.path.join(root, "port", "setup", "pins.tsv"), encoding="utf-8") as f:
            rows = [line.rstrip("\n").split("\t") for line in f]
    except OSError as e:
        raise ValueError(f"the parts' checkouts cannot be read from port/setup/pins.tsv ({e.strerror})") from e
    found = [cells[1] for cells in rows if len(cells) >= 2 and cells[0] == part]
    if not found:
        raise ValueError(f"port/setup/pins.tsv has no row {part}")
    return os.path.normpath(os.path.join(root, found[0]))


def path_in(part, path, root=ROOT):
    """PATH (with /) in PART's checkout."""
    return os.path.join(checkout(part, root), *path.split("/"))
