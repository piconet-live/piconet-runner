"""Gecko code writing every seat's machine directly, from a list we choose, instead of the game's fill and shuffle:
seats.py LIST   LIST = 30 comma-separated machine ids (seat 0 first), or "same:N" (every seat machine N)
It inserts code at the start of fn_1_3181C (main.rel .text 0x3181C), which builds the 30 seat entries
(.bss 0x25E9C, 0x20 each) from the machine list (.bss 0x2625C, one byte per seat, 0x3C00 + 0x2265C) that the
game has just filled (humans' picks, then CPUs shuffled with the DOL generator): the code first copies LIST over
that list, then runs the instruction it replaced. Slot 'FZSE': +4 enabled, +8 the 30 machine ids (32 bytes).
seats.py --params: the ids come from the race block (memmap.RACE + RACE_MACHINES, written by the Dolphin fork from
the match file) instead, so the code is the same for every race."""
import os, sys
PARAMS = "--params" in sys.argv
sys.argv = [a for a in sys.argv if a != "--params"]
if PARAMS:
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import memmap
from keystone import Ks, KS_ARCH_PPC, KS_MODE_PPC32, KS_MODE_BIG_ENDIAN
arg = sys.argv[1] if len(sys.argv) > 1 else "same:0"
ids = [int(arg[5:])] * 30 if arg.startswith("same:") else [int(v, 0) for v in arg.split(",")]
assert len(ids) == 30 and all(0 <= v < 0x29 for v in ids), "30 stock machine ids (0..0x28)"
words = [int.from_bytes(bytes(ids + [0, 0])[i:i + 4], "big") for i in range(0, 32, 4)]
asm = """
    mflr 11
    bl here
here:
    mflr 12
    mtlr 11
    b over
slot:
    .long 0x465A5345
    .long 1
""" + "".join(f"    .long {w}\n" for w in words) + """
over:
    lwz 0, 16(12)
    cmpwi 0, 0
    beq done
    lis 11, 0x8000
    lwz 11, 0x30C8(11)
    addis 11, 11, 0x1C
    addi 11, 11, -0x5FC0
    addis 11, 11, 2
    addi 11, 11, 0x625B
""" + (f"""    lis 12, {memmap.hi(memmap.RACE + memmap.RACE_MACHINES - 1)}
    ori 12, 12, {memmap.lo(memmap.RACE + memmap.RACE_MACHINES - 1)}
""" if PARAMS else """    addi 12, 12, 19
""") + """    li 0, 30
    mtctr 0
copy:
    lbzu 0, 1(12)
    stbu 0, 1(11)
    bdnz copy
done:
    stwu 1, -0x50(1)
"""
code = bytes(Ks(KS_ARCH_PPC, KS_MODE_PPC32 + KS_MODE_BIG_ENDIAN).asm(asm)[0])
assert code[20:24] == b"FZSE", code.hex()      # r12 = here (offset 8): slot at here + 12, enabled at here + 16, the ids at here + 20
assert 0x3C00 + 0x2265C == 0x2625C and 0x2625C - 1 == 0x20000 + 0x625B
code += bytes.fromhex("6000000000000000" if len(code) % 8 == 0 else "00000000")
w = [code[i:i + 4].hex().upper() for i in range(0, len(code), 4)]
print("\n".join(["48000000 800030C8", "DE000000 80008180", f"D2{0xDC + 0x3181C:06X} {len(w) // 2:08X}"] +
                [f"{w[i]} {w[i + 1]}" for i in range(0, len(w), 2)] + ["E0000000 80008000"]))
