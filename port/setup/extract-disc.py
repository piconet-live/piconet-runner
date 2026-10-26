#!/usr/bin/env python3
"""Extract a GameCube ISO into sys/ + files/ (Dolphin DirectoryBlob layout)."""
import os, struct, sys
iso, out = sys.argv[1], sys.argv[2]
f = open(iso, 'rb')
def rd(off, n): f.seek(off); return f.read(n)
def u32(b, o=0): return struct.unpack_from('>I', b, o)[0]
boot = rd(0, 0x440); bi2 = rd(0x440, 0x2000)
dol_off, fst_off, fst_size = u32(boot, 0x420), u32(boot, 0x424), u32(boot, 0x428)
os.makedirs(f'{out}/sys', exist_ok=True)
open(f'{out}/sys/boot.bin', 'wb').write(boot)
open(f'{out}/sys/bi2.bin', 'wb').write(bi2)
al = rd(0x2440, 0x20); al_size = u32(al, 0x14) + u32(al, 0x18)
open(f'{out}/sys/apploader.img', 'wb').write(rd(0x2440, 0x20 + al_size))
hdr = rd(dol_off, 0x100)
end = max(u32(hdr, i*4) + u32(hdr, 0x90 + i*4) for i in range(18))
open(f'{out}/sys/main.dol', 'wb').write(rd(dol_off, end))
fst = rd(fst_off, fst_size)
open(f'{out}/sys/fst.bin', 'wb').write(fst)
n = u32(fst, 8); strtab = n * 12
def name(i):
    o = strtab + (u32(fst, i*12) & 0xFFFFFF); e = fst.index(b'\0', o)
    return fst[o:e].decode('shift_jis')
def walk(start, end, path):
    i = start
    while i < end:
        flags = fst[i*12]
        nm = name(i)
        if flags:
            nxt = u32(fst, i*12 + 8)
            os.makedirs(f'{path}/{nm}', exist_ok=True)
            walk(i + 1, nxt, f'{path}/{nm}'); i = nxt
        else:
            off, size = u32(fst, i*12 + 4), u32(fst, i*12 + 8)
            open(f'{path}/{nm}', 'wb').write(rd(off, size)); i += 1
os.makedirs(f'{out}/files', exist_ok=True)
walk(1, n, f'{out}/files')
print('ok', n, 'entries')
