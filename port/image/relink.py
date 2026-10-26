#!/usr/bin/env python3
"""relink.py MAIN.REL BASE BSS [OUT]: a REL module (Nintendo's relocatable module format, as OSLink reads it) relocated
as OSLink would for a load at guest address BASE with its .bss at BSS; references to the executable (module 0) are
absolute; another module's symbols stay as the file has them (OSLink resolves them when that module is linked).
Writes the module's bytes from its header on: the layout of a dump of guest memory from BASE. For F-Zero GX's main.rel
(BASE 801BA660, BSS 803746A0) the text equals such a dump of the running game word for word.
The output is game data: it stays in the work folder."""
import struct, sys

def relocate(data: bytes, base: int, bss: int):
    b = bytearray(data)
    u32 = lambda o: struct.unpack_from(">I", b, o)[0]
    (mod_id, _, _, nsec, sec_off, _, _, version, bss_size, rel_off, imp_off, imp_size) = struct.unpack_from(">12I", b, 0)
    secs = []
    for i in range(nsec):
        off, size = struct.unpack_from(">II", b, sec_off + i * 8)
        secs.append([off & ~1, size, off & 1])
    addr = lambda s: (bss if secs[s][0] == 0 and secs[s][1] else base + secs[s][0])
    for i in range(imp_size // 8):
        target_mod, off = struct.unpack_from(">II", b, imp_off + i * 8)
        where = 0
        while True:
            skip, rtype, sec, addend = struct.unpack_from(">HBBI", b, off); off += 8
            if rtype == 203: break                       # R_DOLPHIN_END
            where += skip
            if rtype == 201: continue                    # R_DOLPHIN_NOP
            if rtype == 202: where = secs[sec][0]; continue   # R_DOLPHIN_SECTION (file offset = offset from the header)
            if rtype == 0: continue
            if target_mod not in (0, mod_id): continue          # another module's symbol: resolved when that module links
            sym = addend if target_mod == 0 else addr(sec) + addend
            P = base + where
            if rtype == 1: struct.pack_into(">I", b, where, sym & 0xFFFFFFFF)                       # ADDR32
            elif rtype == 2: struct.pack_into(">I", b, where, (u32(where) & ~0x03FFFFFC) | (sym & 0x03FFFFFC))   # ADDR24
            elif rtype == 3: struct.pack_into(">H", b, where, sym & 0xFFFF)                         # ADDR16
            elif rtype == 4: struct.pack_into(">H", b, where, sym & 0xFFFF)                         # ADDR16_LO
            elif rtype == 5: struct.pack_into(">H", b, where, (sym >> 16) & 0xFFFF)                 # ADDR16_HI
            elif rtype == 6: struct.pack_into(">H", b, where, ((sym + 0x8000) >> 16) & 0xFFFF)      # ADDR16_HA
            elif rtype in (7, 8, 9): struct.pack_into(">I", b, where, (u32(where) & ~0xFFFC) | (sym & 0xFFFC))  # ADDR14
            elif rtype == 10: struct.pack_into(">I", b, where, (u32(where) & ~0x03FFFFFC) | ((sym - P) & 0x03FFFFFC))  # REL24
            elif rtype in (11, 12, 13): struct.pack_into(">I", b, where, (u32(where) & ~0xFFFC) | ((sym - P) & 0xFFFC))  # REL14
            else: raise ValueError(f"relocation type {rtype} at {where:#x}")
    return b, secs

if __name__ == "__main__":
    with open(sys.argv[1], "rb") as f:
        data = f.read()
    base, bss = int(sys.argv[2], 16), int(sys.argv[3], 16)
    out, secs = relocate(data, base, bss)
    print("sections (offset, size, code):", [(hex(o), hex(s), x) for o, s, x in secs if s])
    if len(sys.argv) > 4:
        with open(sys.argv[4], "wb") as f:
            f.write(out)
