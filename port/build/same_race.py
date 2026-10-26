#!/usr/bin/env python3
"""same_race.py: which released builds' replays a game with a given image refuses, by the table of same races
(port/image/same-race.json, which the launcher reads: RecompLaunch.RecordedWithThisImage).

The launcher plays a replay only on the image it was recorded with or on one the table's `rows` name for it, so a
new image refuses every released build's replays until a run proves them and adds the row (the table's own
`what`). The table's `released` list names the images that were handed to players. This is asked where a build is
made: stage-launcher.sh says it for a copy, package.py refuses a player copy for it (package.sh
OLD_REPLAYS_REFUSED=1: said on purpose for a build that is not to play them, as a play-breaking build may be).

    same_race.py refused IMAGE_FILE [TABLE]
prints one line for each released build whose replays are refused, "VERSION IMAGE"; status 0 when there is none,
1 when there is one, 2 when the table cannot be read."""
import hashlib, json, os, sys

TABLE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "image", "same-race.json")


def load(path=TABLE):
    """The table: its rows as a set of (recorded, plays_on) and its released builds as (version, image), SHA-1s in
    small letters. A table without `rows` or `released`, or with a member of another kind, is refused (ValueError)."""
    with open(path, encoding="utf-8") as f:
        table = json.load(f)
    rows, released = table.get("rows"), table.get("released")
    if not isinstance(rows, list) or not isinstance(released, list):
        raise ValueError(f"{path}: no `rows` or no `released` list")
    pairs = set()
    for row in rows:
        if not (isinstance(row, dict) and isinstance(row.get("recorded"), str) and isinstance(row.get("plays_on"), str)):
            raise ValueError(f"{path}: a row without `recorded` and `plays_on`: {row!r}")
        pairs.add((row["recorded"].lower(), row["plays_on"].lower()))
    builds = []
    for build in released:
        if not (isinstance(build, dict) and isinstance(build.get("version"), str) and isinstance(build.get("image"), str)):
            raise ValueError(f"{path}: a released build without `version` and `image`: {build!r}")
        builds.append((build["version"], build["image"].lower()))
    return pairs, builds


def refused(pairs, builds, image):
    """The released builds (version, image) whose replays a game with this image (SHA-1) does not play: neither the
    same image nor a row from theirs to it."""
    image = image.lower()
    return [(version, theirs) for version, theirs in builds if theirs != image and (theirs, image) not in pairs]


def sha1_of(path):
    with open(path, "rb") as f:
        return hashlib.sha1(f.read()).hexdigest()


def main(argv):
    if len(argv) not in (3, 4) or argv[1] != "refused":
        print(__doc__.split("\n\n")[-1], file=sys.stderr)
        return 2
    try:
        pairs, builds = load(argv[3] if len(argv) == 4 else TABLE)
        image = sha1_of(argv[2])
    except (OSError, ValueError) as why:
        print(f"same_race: {why}", file=sys.stderr)
        return 2
    found = refused(pairs, builds, image)
    for version, theirs in found:
        print(f"{version} {theirs}")
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
