#!/usr/bin/env python3
"""ondemand.py FILES MAIN.REL OUT_FOLDER: the code of the story's movie player as it stands in memory, made from the
disc's files alone (no game is run).

The native module also holds two modules the game loads on demand (fze.movie.rel, fze.movie_module.rel): their
code is compiled in at the addresses where the game links them. The two
images are what a memory dump of a running game holds of them: the
files' text sections relocated as the game's OSLinkFixed does it, and that is computed here: the module at its
load address, its .bss behind the part of the file that stays (the header's fixSize, rounded up to the .bss's
alignment), main.rel where the pins put it. The result equals the dump's cut byte for byte (SHA-1 in
port/setup/layout.env), and the same disc gives the same bytes every time.

The load addresses are facts about the game, seen in every start: the heap for on-demand modules begins at
0x80596000 and is empty when a mode's first module comes; disc.sh checks the output against the pins SHA1_MOVIE
and SHA1_MOVIE_MODULE.
The output is game data: it stays in the work folder."""
import os, struct, sys

MAIN_REL = (0x801BA660, 0x803746A0)                 # main.rel's load address and its .bss, with the two pins
MODULES = [                                         # file, load address, output name
    ("fze.movie.rel", 0x80596000, "loaded-05-movie.bin"),
    ("fze.movie_module.rel", 0x8059EE80, "loaded-0c-movie_module.bin"),
]


def sections(rel: bytes):
    count, table = struct.unpack_from(">II", rel, 0xC)
    return [(offset & ~1, size, offset & 1) for offset, size in
            (struct.unpack_from(">II", rel, table + 8 * i) for i in range(count))]


def placed(rel: bytes, base: int, bss: int):
    """section number -> address, for a module loaded at base with its .bss at bss."""
    return [bss if offset == 0 and size else base + offset for offset, size, _ in sections(rel)]


def fixed_bss(rel: bytes, base: int) -> int:
    """Where OSLinkFixed puts the .bss: behind the kept part of the file, at the .bss's alignment."""
    align, fix_size = struct.unpack_from(">I", rel, 0x44)[0], struct.unpack_from(">I", rel, 0x48)[0]
    return (base + fix_size + align - 1) // align * align


def link(rel: bytes, base: int, bss: int, others: dict) -> bytearray:
    """The module's bytes after OSLink. others: module id -> section addresses (placed) of the modules linked
    before it; module 0 is the executable, whose symbols are absolute."""
    out, ident = bytearray(rel), struct.unpack_from(">I", rel, 0)[0]
    table, table_size = struct.unpack_from(">II", rel, 0x28)
    where_is, place = dict(others), sections(rel)
    where_is[ident] = placed(rel, base, bss)
    word = lambda at: struct.unpack_from(">I", out, at)[0]
    for i in range(table_size // 8):
        target, at = struct.unpack_from(">II", rel, table + 8 * i)
        where = 0
        while True:
            skip, kind, section, addend = struct.unpack_from(">HBBI", rel, at)
            at += 8
            if kind == 203:                                   # end of this module's list
                break
            where += skip
            if kind == 202:                                   # go to a section
                where = place[section][0]
                continue
            if kind in (0, 201):
                continue
            if target and target not in where_is:
                raise ValueError(f"a symbol of module {target}, which is not linked")
            symbol = addend if target == 0 else where_is[target][section] + addend
            here = base + where
            if kind == 1: struct.pack_into(">I", out, where, symbol & 0xFFFFFFFF)
            elif kind == 2: struct.pack_into(">I", out, where, (word(where) & ~0x03FFFFFC) | (symbol & 0x03FFFFFC))
            elif kind in (3, 4): struct.pack_into(">H", out, where, symbol & 0xFFFF)
            elif kind == 5: struct.pack_into(">H", out, where, (symbol >> 16) & 0xFFFF)
            elif kind == 6: struct.pack_into(">H", out, where, ((symbol + 0x8000) >> 16) & 0xFFFF)
            elif kind in (7, 8, 9): struct.pack_into(">I", out, where, (word(where) & ~0xFFFC) | (symbol & 0xFFFC))
            elif kind == 10: struct.pack_into(">I", out, where, (word(where) & ~0x03FFFFFC) | ((symbol - here) & 0x03FFFFFC))
            elif kind in (11, 12, 13): struct.pack_into(">I", out, where, (word(where) & ~0xFFFC) | ((symbol - here) & 0xFFFC))
            else: raise ValueError(f"relocation type {kind} at {where:#x}")
    return out


def text(rel: bytes, base: int, others: dict):
    """(address, bytes) of the module's one executable section after the link."""
    code = [(offset, size) for offset, size, executable in sections(rel) if executable and size]
    if len(code) != 1:
        raise ValueError(f"{len(code)} executable sections (one expected)")
    offset, size = code[0]
    return base + offset, bytes(link(rel, base, fixed_bss(rel, base), others)[offset:offset + size])


if __name__ == "__main__":
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    files, main_rel, out = sys.argv[1:]
    with open(main_rel, "rb") as f:
        main = f.read()
    others = {struct.unpack_from(">I", main, 0)[0]: placed(main, *MAIN_REL)}
    for name, base, to in MODULES:
        with open(os.path.join(files, name), "rb") as f:
            address, code = text(f.read(), base, others)
        with open(os.path.join(out, to), "wb") as f:
            f.write(code)
        print(f"{address:08X}:{os.path.join(out, to)}")
