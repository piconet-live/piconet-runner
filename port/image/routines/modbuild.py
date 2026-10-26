"""Builds the mod image and its hooks. The game side has outgrown Dolphin's cheat-code area (0x80001800-0x80003000,
2,880 bytes of it the code handler), so the routines live in an image in free RAM and the cheat list keeps only small
hooks that check the image's magic word before using it. The test harness writes the image (modload.py); the Dolphin
fork will place it at boot.
  modbuild.py OUT_BIN SEAT OFF UNIFORM REFMASK DELAY   -> writes the image to OUT_BIN, prints the hook codes
Image at memmap.IMAGE (0x81800000, the fork's mod memory): +0 'FZMD', version, size, +0x0C the network hook (`blr`;
the fork's network layer runs its handler at this address); +0x10 the seat routines (standin.py: per-machine hook, race-over
test, rubber-band references; data slot 'FZSI' first); then 'FZFR' and the frame routine (netframe.py), then 'FZNT' and the name tags (nametags.py, after a call to the hand-back's
monitors, monitors.py)."""
import json, os, subprocess, struct, sys
from keystone import Ks, KS_ARCH_PPC, KS_MODE_PPC32, KS_MODE_BIG_ENDIAN
here = os.path.dirname(os.path.abspath(__file__)); py = sys.executable
sys.path.insert(0, here)
import memmap
BASE = memmap.IMAGE; LIMIT = memmap.CONTROL       # the mod's memory above the game's 24 MiB
out, seat, off, uniform, refmask, delay = sys.argv[1:7]
# the native form and the clean main.rel (native.py), as port/image/build.py sets them: read here, handed to netframe.py
NATIVE, CLEAN_REL = os.environ.get("FZGX_NATIVE") == "1", os.environ.get("FZGX_CLEAN_REL")
seats = json.loads(subprocess.run([py, os.path.join(here, "standin.py"), seat, off, uniform, refmask, delay, "--parts"],
                                  capture_output=True, text=True, check=True).stdout)
frame = json.loads(subprocess.run([py, os.path.join(here, "netframe.py")] + (["--native"] + [CLEAN_REL] * bool(CLEAN_REL) if NATIVE else []),
                                  capture_output=True, text=True, check=True).stdout)
S = BASE + 0x10
img = bytearray(bytes.fromhex(seats["image"]))
while len(img) % 8: img += b"\x60\x00\x00\x00"
F = S + len(img) + 4
img += b"FZFR" + bytes.fromhex(frame["image"])
tags = json.loads(subprocess.run([py, os.path.join(here, "nametags.py")], capture_output=True, text=True, check=True).stdout)
while len(img) % 4: img += b"\x00"
T = S + len(img) + 4
img += b"FZNT" + bytes.fromhex(tags["image"])
image = struct.pack(">4sIII", b"FZMD", 1, 0x10 + len(img), 0x4E800020) + bytes(img)   # +0x0C: the network hook, a blr
assert BASE + len(image) <= min(LIMIT, memmap.SMOOTH_LOG), hex(len(image))   # the tests' log has the upper half
open(out, "wb").write(image)
if NATIVE:
    # The native form (port/image/build.py): where the routines are, and the frame routine's installer sites with
    # the address of each one's entry.
    with open(out + ".layout.json", "w") as f:
        json.dump({"slot": S, "routine": S + seats["routine"], "over": S + seats["over"], "refs": S + seats["refs"],
                   "frame": F, "tags": T, "sites": [dict(site, address=F + site["at"]) for site in frame["sites"]]}, f)
ks = Ks(KS_ARCH_PPC, KS_MODE_PPC32 + KS_MODE_BIG_ENDIAN)
def words(code):
    code = bytes(code); w = [code[i:i + 4].hex().upper() for i in range(0, len(code), 4)]
    return [f"{w[i]} {w[i + 1]}" for i in range(0, len(w), 2)]
hi = lambda a: (a >> 16) & 0xFFFF
# 1. Seat installer (C0, every frame): with the image present, install its routines as the game's hooks.
inst = bytes(ks.asm(f"""
    lis 9, {hi(S)}
    ori 9, 9, {S & 0xFFFF}
    lwz 10, 0(9)
    lis 11, 0x465A
    ori 11, 11, 0x5349
    cmpw 10, 11
    .long 0x40820000
""")[0])
off_bne = len(inst) - 4                       # 'bne' to the final blr, patched below
inst += bytes.fromhex(seats["installer"]) + bytes.fromhex("4E800020")
fix = bytearray(inst); fix[off_bne:off_bne + 4] = struct.pack(">I", 0x40820000 | (len(inst) - 4 - off_bne))
inst = bytes(fix)
if len(inst) % 8: inst += bytes.fromhex("60000000")
codes = [f"C0000000 {len(inst) // 8:08X}"] + words(inst)
# 2. Frame trampoline (D2 at main.rel 0x37C14, the `bl fn_1_407C`): the image's frame routine, or the manager.
tr = bytes(ks.asm(f"""
    lis 12, {hi(F)}
    ori 12, 12, {F & 0xFFFF}
    lwz 11, -4(12)
    lis 10, 0x465A
    ori 10, 10, 0x4652
    cmpw 11, 10
    bne direct
    mtctr 12
    bctrl
    b out
direct:
    lis 12, 0x8000
    lwz 12, 0x30C8(12)
    addi 12, 12, 0x4158
    mtctr 12
    bctrl
out:
""")[0])
tr += bytes.fromhex("6000000000000000" if len(tr) % 8 == 0 else "00000000")
codes += ["48000000 800030C8", "DE000000 80008180", f"D2037C14 {len(tr) // 8:08X}"] + words(tr) + ["E0000000 80008000"]
# 3. Name-tag hook (D2 at main.rel text 0x37C74, the per-viewport `bl fn_1_C7224`, the HUD): the image's name tags
# (nametags.py), then the HUD.
nt = bytes(ks.asm(f"""
    stwu 1, -16(1)
    stw 3, 8(1)
    lis 12, {hi(T)}
    ori 12, 12, {T & 0xFFFF}
    lwz 11, -4(12)
    lis 10, 0x465A
    ori 10, 10, 0x4E54
    cmpw 11, 10
    bne hud
    mtctr 12
    bctrl
hud:
    lwz 3, 8(1)
    addi 1, 1, 16
    lis 12, 0x8000
    lwz 12, 0x30C8(12)
    addis 12, 12, 0xC
    addi 12, 12, 0x7300
    mtctr 12
    bctrl
""")[0])
assert 0xDC + 0xC7224 == 0xC7300 and 0xDC + 0x37C74 == 0x37D50
nt += bytes.fromhex("6000000000000000" if len(nt) % 8 == 0 else "00000000")
codes += ["48000000 800030C8", "DE000000 80008180", f"D2037D50 {len(nt) // 8:08X}"] + words(nt) + ["E0000000 80008000"]
# 4. The camera follows any seat: the chase camera (fn_1_AA54 0xAA84, fn_1_ABAC 0xAC08) indexes its record by the
# followed machine's pad port (fn_1_86634); a seat without one (every seat but 0 online) gives -1, read as 255, and the
# game writes outside its four camera records every frame (into whatever follows them; a desync now and then).
# Each game has one screen: record 0 (as the game itself uses in replays, fn_1_ABAC 0xABE4).
for at in (0xAA84, 0xAC08):
    codes += ["48000000 800030C8", "DE000000 80008180", f"14{0xDC + at:06X} 38600000", "E0000000 80008000"]
# 5. Mute while re-simulating.
codes += subprocess.run([py, os.path.join(here, "mute.py")], capture_output=True, text=True, check=True).stdout.split("\n")
print("\n".join(c for c in codes if c.strip()))
print(f"image {len(image)} bytes at {BASE:#x}: seats {S:#x}, frame routine {F:#x}, name tags {T:#x}", file=sys.stderr)
