"""Gecko code: no sounds or rumble from steps simulated before. While a step that already ran once is simulated again
(a rollback's replay; netframe.py sets control block +488 before each step), it already played its sounds the first
time, so:
- the sound command queue (main.dol fn_80067898, which every sound start goes through: fn_1_A2D84, fn_1_A2DC4,
  fn_1_A2E5C) returns -1 at once, as when its 64-entry queue is full, which callers handle;
- rumble requests (main.rel fn_1_F7338) return at once.
Sound commands dropped are counted at control block +32, all sound commands at +36; tests: +516 = 1 logs every
command (LOG).
(The game's own mute byte, .bss 0x6F1D8, is checked only by fn_1_A2D84.) Tests: control block +296 = 1 drops them all
the time."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import memmap
C = memmap.hi(memmap.CONTROL); assert memmap.lo(memmap.CONTROL) == 0
from keystone import Ks, KS_ARCH_PPC, KS_MODE_PPC32, KS_MODE_BIG_ENDIAN
ks = Ks(KS_ARCH_PPC, KS_MODE_PPC32 + KS_MODE_BIG_ENDIAN)
LOG = 0x81B00000   # tests: sound command log (control block +516 = 1): +0 count, 8-byte entries from +0x10 (ring of 8192):
                   # the step (+124; bit 31 set when the step was simulated before, +488, and the command dropped), the id


def body(ret, original, count_all=False, log=False):
    pre = f"    lis 11, {C}\n    lwz 12, 36(11)\n    addi 12, 12, 1\n    stw 12, 36(11)\n" if count_all else ""
    if log:
        pre += f"""    lis 12, {C}
    lwz 11, 516(12)
    cmpwi 11, 0
    beq nolog
    lis 12, {LOG >> 16}
    lwz 11, 0(12)
    addi 0, 11, 1
    stw 0, 0(12)
    rlwinm 0, 11, 3, 16, 28
    add 12, 12, 0
    stw 3, 0x14(12)
    lis 11, {C}
    lwz 0, 124(11)
    lwz 11, 488(11)
    rlwimi 0, 11, 31, 0, 0
    stw 0, 0x10(12)
nolog:
"""
    asm = pre + f"""
    lis 12, {C}
    lwz 11, 296(12)
    lwz 12, 488(12)
    or. 12, 12, 11
    beq go
    {ret}
    blr
go:
    {original}
"""
    code = bytes(ks.asm(asm)[0])
    code += bytes.fromhex("6000000000000000" if len(code) % 8 == 0 else "00000000")
    w = [code[i:i + 4].hex().upper() for i in range(0, len(code), 4)]
    return [f"{w[i]} {w[i + 1]}" for i in range(0, len(w), 2)]
snd = body(f"lis 11, {C}\n    lwz 3, 32(11)\n    addi 3, 3, 1\n    stw 3, 32(11)\n    li 3, -1",
           "stwu 1, -0x10(1)", True, True)  # fn_80067898's first instruction; counts dropped and all commands
rmb = body("nop", "stwu 1, -0x20(1)")           # fn_1_F7338's first instruction
print("\n".join([f"C2067898 {len(snd):08X}"] + snd +
                ["48000000 800030C8", "DE000000 80008180", f"D2{0xDC + 0xF7338:06X} {len(rmb):08X}"] + rmb +
                ["E0000000 80008000"]))
