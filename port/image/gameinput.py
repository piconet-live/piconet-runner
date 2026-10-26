#!/usr/bin/env python3
"""The game's code as the native form needs it, made from the stock game's files and the bundle's patch list
(port/image/build.py). Two outputs, both game data: they stay in the work folder.

  gameinput.py --dol MAIN.DOL --rel MAIN.REL --bundle DIR [--patched-dol OUT] [--combined OUT]
    --patched-dol OUT   the executable with the list's words of the executable in: the copy that is booted
                        (never the disc's). main.rel's words are written by the host when main.rel is linked.
    --combined OUT      the recompiler's input: that executable, with two more text sections: main.rel's .text as
                        it is in memory (linked at memmap.REL, relink.py) with the list's words in, and the mod image
                        at memmap.IMAGE. The native module is compiled from exactly the code the game then holds.
    --extra ADDR:FILE   with --combined, repeatable: one more text section each (a DOL has seven), the code of an
                        on-demand module as it stands in memory (port/setup/ondemand.py)
  --dol: the stock executable, or one that already has some of the list's words (the extracted copy has the pin).
Every word is checked before it is replaced: it must hold the list's original (or already the new word)."""
import argparse, json, os, struct, sys

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, "routines"))
sys.path.insert(0, here)
import memmap
import relink

TEXT_OFFSET, TEXT_SIZE = 0xDC, 0x15F62C        # main.rel's .text: its offset from the header, its size (GFZE01)


def read(path, mode="rb"):
    with open(path, mode) as f:
        return f.read()


def write(path, data):
    with open(path, "wb") as f:
        f.write(data)


def load_patches(bundle):
    listed = json.loads(read(os.path.join(bundle, "patches.json"), "r"))
    if not listed.get("complete"):
        sys.exit("gameinput.py: the bundle's list is incomplete (built without the game's files)")
    return [(int(p["address"], 16), int(p["original"], 16), int(p["new"], 16), p["what"]) for p in listed["patches"]]


def put(data, offset, address, original, new, what):
    found = struct.unpack_from(">I", data, offset)[0]
    if found not in (original, new):
        sys.exit(f"gameinput.py: {address:08X} ({what}) holds {found:08X}, neither the stock {original:08X} nor {new:08X}")
    struct.pack_into(">I", data, offset, new)


def sections(dol):
    h = struct.unpack_from(">57I", dol, 0)
    return [(h[18 + i], h[i], h[36 + i]) for i in range(18) if h[36 + i]]     # address, file offset, size


def patched_dol(dol, patches):
    """The executable with the list's words of the executable (addresses below main.rel) in."""
    out = bytearray(dol)
    for address, original, new, what in patches:
        if address >= memmap.REL:
            continue
        offset = next((o + address - a for a, o, size in sections(dol) if a <= address < a + size), None)
        if offset is None:
            sys.exit(f"gameinput.py: {address:08X} ({what}) is in no section of the executable")
        put(out, offset, address, original, new, what)
    return bytes(out)


def patched_text(rel, patches):
    """main.rel's .text as the game holds it once the host has written the list: linked, then the list's words."""
    linked, _ = relink.relocate(rel, memmap.REL, memmap.REL_BSS)
    text = bytearray(linked[TEXT_OFFSET:TEXT_OFFSET + TEXT_SIZE])
    for address, original, new, what in patches:
        if address < memmap.REL:
            continue
        offset = address - memmap.REL_TEXT
        if not 0 <= offset < TEXT_SIZE:
            sys.exit(f"gameinput.py: {address:08X} ({what}) is outside main.rel's .text")
        put(text, offset, address, original, new, what)
    return bytes(text)


def append_text(dol, address, blob):
    """One more text section (a DOL has seven text slots)."""
    out = bytearray(dol)
    header = list(struct.unpack_from(">64I", out, 0))
    slot = next(i for i in range(7) if header[36 + i] == 0)
    while len(out) % 32:
        out.append(0)
    header[slot], header[18 + slot], header[36 + slot] = len(out), address, len(blob)
    out += blob
    struct.pack_into(">64I", out, 0, *header)
    return bytes(out)


def combined(dol, rel, image, patches, extra=()):
    """`extra`: (address, bytes) of further code as it stands in memory (the game's on-demand modules, port/setup/ondemand.py)."""
    out = append_text(patched_dol(dol, patches), memmap.REL_TEXT, patched_text(rel, patches))
    image = image + b"\0" * (-len(image) % 4)
    out = append_text(out, memmap.IMAGE, image)
    for address, blob in extra:
        if address % 4 or len(blob) % 4:
            sys.exit(f"gameinput.py: the code at {address:08X} is not whole instructions")
        out = append_text(out, address, blob)
    return out


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--dol", required=True)
    parser.add_argument("--rel", required=True)
    parser.add_argument("--bundle", required=True)
    parser.add_argument("--patched-dol")
    parser.add_argument("--combined")
    parser.add_argument("--extra", action="append", default=[], metavar="ADDRESS:FILE",
                        help="with --combined: one more text section, code as it stands in memory (port/setup/ondemand.py)")
    a = parser.parse_args()
    patches = load_patches(a.bundle)
    dol = read(a.dol)
    if a.patched_dol:
        write(a.patched_dol, patched_dol(dol, patches))
        print(f"patched executable: {a.patched_dol} ({sum(p[0] < memmap.REL for p in patches)} words)")
    if a.combined:
        extra = [(int(e.split(":", 1)[0], 16), read(e.split(":", 1)[1])) for e in a.extra]
        out = combined(dol, read(a.rel), read(os.path.join(a.bundle, "mod.bin")), patches, extra)
        write(a.combined, out)
        print("recompiler input:", a.combined, "text sections", [(f"{x:08X}", f"{s:X}") for x, _, s in sections(out)[:7]])
