"""package_refusals.py: what a player copy must never hold, for port/build/package.py: anything of the game or of a
player. A disc image, a file or a folder of the extracted game, a `data` folder, a save, a replay, a dump, a log, a
picture (the emulator's own, checked against its source, are the exception: package.py marks them `own`). A file
is told by its name, by its ending, by its size and by its first bytes, so a disc image under another name is one
still. Refused is what every refusal of a copy is raised as: why, and the offending path."""
import re

DISC_BYTES = 1459978240               # a GameCube disc image

# What is the game's or a player's, by its name. Lower case; a folder anywhere in the path counts.
FOLDERS = {"data": "a data folder", "game": "an extracted game folder", "gfze01": "an extracted game folder",
           "recomp-game": "the prepared game", "recomp-user": "a player's game settings and saves",
           "dolphin-user": "a player's emulator folder", "replays": "a replay folder", "race": "a race's match file",
           "statesaves": "saved states", "screenshots": "pictures of the game", "dump": "dumps"}
NAMES = {name: "a file of the extracted game" for name in
         ("main.dol", "main.dol.orig", "boot.bin", "bi2.bin", "apploader.img", "fst.bin", "opening.bnr")}
NAMES.update({"settings.json": "a player's settings", "prepared.json": "the prepared game's record",
              "match.json": "a race's match file"})
KINDS = (("a disc image", (".iso", ".gcm", ".rvz", ".gcz", ".wia", ".wbfs", ".ciso", ".tgc", ".nkit")),
         ("a file of the game", (".dol", ".rel", ".lz", ".gma", ".tpl", ".arc", ".adx", ".afs", ".sfd", ".thp", ".bnr")),
         ("a save", (".gci", ".gcs", ".gcp", ".raw", ".sav", ".dtm")), ("a replay", (".fzrp",)),
         ("a dump or a log", (".dmp", ".mdmp", ".dump", ".dff", ".profdata", ".log")),
         ("a picture, a video or a sound", (".png", ".jpg", ".jpeg", ".bmp", ".gif", ".tga", ".dds", ".webp", ".avi",
                                           ".mp4", ".mkv", ".webm", ".wav", ".ogg", ".mp3")))
PICTURES = KINDS[-1][0]


class Refused(Exception):
    """Something the copy must not be made from or must not hold: why, and the offending path."""
    def __init__(self, why, path=None):
        super().__init__(f"{why}: {path}" if path else why)


def game_data(name, file):
    """Why a file is the game's or a player's and cannot be in a copy, or None."""
    *folders, last = name.lower().split("/")
    for folder in folders:
        if folder in FOLDERS:
            return FOLDERS[folder]
    if last in NAMES:
        return NAMES[last]
    for kind, endings in KINDS:
        if last.endswith(endings) and not (file["own"] and kind == PICTURES):
            return kind
    if re.search(r"\.s\d\d$", last):
        return "a saved state"
    head = file["head"]
    if file["bytes"] == DISC_BYTES or head[28:32] == b"\xc2\x33\x9f\x3d" or head[24:28] == b"\x5d\x1c\x9e\xa3" \
            or head[:4] in (b"RVZ\x01", b"WIA\x01", b"CISO", b"WBFS", b"\x01\xc0\x0b\xb1"):
        return "a disc image"
    if head[:4] in (b"GFZE", b"GFZJ", b"GFZP"):
        return "a file that begins with the game's id (a disc image or a save)"
    if head[:4] == b"FZRP":
        return "a replay"
    return None


def refuse_game_data(plan):
    for name, file in sorted(plan.files.items()):
        why = game_data(name, file)
        if why:
            raise Refused(f"{why} must not be in a copy ({file['part']})", file["path"] or name)
