"""Gecko code booting straight into a Practice race: no title, no select menus, no START YOUR ENGINE screen.
direct.py [course] [machine] [opponents] [laps] [race type]
Inserted at the start of main.rel's mode machine (fn_1_ECC, main thread, every frame): in the title's first frame
(mode .bss 0x960 == MD_TITLE, nothing requested; at boot and when a race is left; running it before the title has
set up crashed), it does on the main thread what the select module would do between the mode screen and the race,
then requests MD_GAME:
- the menu state (.bss 0x8B3A0) the race reads: entry 3 (Practice), 1 human, CPU level 3 (Master; +0x8, which
  fn_1_C3A0C copies to the CPU block .bss 0x7AC64), port 0's machine, the course, CPU opponents, laps;
- START YOUR ENGINE's set-up (sel fn_10_4568): fn_1_34E08 (the 30-seat lineup and the machine preload) and the
  resource group 0x8B (fn_1_47F74), which the select module's exit leaves loaded;
- the select module's race set-up for Practice (sel fn_10_D2C): race type 3 (free run) or 6 (laps) with rule flag
  0x04000000 set and 0x20000 cleared for laps (fn_1_3EF8C, fn_1_3FCB0, fn_1_3FCD4), the setup block (fn_1_3ED8C(0, laps, 1, course, 1, racers, 0)) and the four ports' entries (12 bytes
  each: machine, menu +0x14, the profile's +0x7/+0x8 bytes, the name "AV   ", menu +0x9F; fn_1_3EC88(1, entries)).
Grand Prix rules where Practice differs: Practice also sets rule flag 0x10 (boosting on lap 1: fn_1_331EC clears a
machine's boost request while +0x49C->+0x118 laps < 1 unless it is set; the HUD and the CPUs test it too) and copies
the menu's Restore option (+0x136) into rule flag 0x8000 (a crashed or fallen machine is not out, fn_1_32600); this
code leaves 0x10 clear and always clears 0x8000.
The screen is kept black (VISetBlack, main.dol 0x8001BDF0, which the game never calls) from the first frame until
MD_GAME has loaded the course (its sub-mode stops being -1), so neither the Nintendo/SEGA logos nor the frame left on
screen while loading show; the player sees black, then the course intro. Slot: 0 before, 1 black, 2 released.
--params: course, machine, opponents, laps, race type, CPU level and restore (rule flag 0x8000 set instead of
cleared) are read from the race block (memmap.RACE, which the Dolphin
fork writes from the match file) instead of the arguments, so the code is the same for every race."""
import os, sys
PARAMS = "--params" in sys.argv
sys.argv = [a for a in sys.argv if a != "--params"]
from keystone import Ks, KS_ARCH_PPC, KS_MODE_PPC32, KS_MODE_BIG_ENDIAN
course = int(sys.argv[1]) if len(sys.argv) > 1 else 1
machine = int(sys.argv[2]) if len(sys.argv) > 2 else 0
opponents = int(sys.argv[3]) if len(sys.argv) > 3 else 29
laps = int(sys.argv[4]) if len(sys.argv) > 4 else 3
racetype = int(sys.argv[5], 0) if len(sys.argv) > 5 else (6 if laps else 3)   # experiments: 2 = Grand Prix
if PARAMS:
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import memmap
TEXT = 0xDC


def value(reg, name, imm, plus=0):
    """Loads a race value into reg: the argument, or (--params) the race block's word (r24 = the block)."""
    if not PARAMS:
        return f"    li {reg}, {imm + plus}\n"
    return f"    lwz {reg}, {getattr(memmap, 'RACE_' + name)}(24)\n" + (f"    addi {reg}, {reg}, {plus}\n" if plus else "")


race_base = (f"    lis 24, {memmap.hi(memmap.RACE)}\n    ori 24, 24, {memmap.lo(memmap.RACE)}\n" if PARAMS else "")
def call(fn):            # r31 = main.rel's header
    a = TEXT + fn
    hi, lo = (a + 0x8000) >> 16, a & 0xFFFF
    lo = lo - 0x10000 if lo >= 0x8000 else lo
    return f"    addis 12, 31, {hi}\n    addi 12, 12, {lo}\n    mtctr 12\n    bctrl\n"
asm = f"""
    stwu 1, -0xD0(1)
    mflr 0
    stw 0, 0xD4(1)
    stmw 3, 0x50(1)
    lis 31, 0x8000
    lwz 31, 0x30C8(31)
    addis 30, 31, 0x1C
    addi 30, 30, -0x5FC0
""" + race_base + """    bl after_slot
slot:
    .long 0
after_slot:
    mflr 25
    lwz 0, 0(25)
    cmpwi 0, 0
    bne not_first
    li 3, 1
    lis 12, 0x8001
    ori 12, 12, 0xBDF0
    mtctr 12
    bctrl
    li 0, 1
    stw 0, 0(25)
not_first:
    lwz 0, 0(25)
    cmpwi 0, 1
    bne setup_check
    lha 0, 0x960(30)
    cmpwi 0, 2
    bne setup_check
    lha 0, 0x962(30)
    cmpwi 0, -1
    beq setup_check
    li 3, 0
    lis 12, 0x8001
    ori 12, 12, 0xBDF0
    mtctr 12
    bctrl
    li 0, 2
    stw 0, 0(25)
setup_check:
    lha 0, 0x960(30)
    cmpwi 0, 10
    bne out
    lha 0, 0x968(30)
    cmpwi 0, -1
    bne out
    addis 29, 30, 9
    addi 29, 29, -0x4C60
    li 0, 3
    sth 0, 0(29)
    li 0, 1
    sth 0, 4(29)
""" + value(0, "MACHINE0", machine) + """    sth 0, 0xE(29)
""" + value(0, "COURSE", course) + """    sth 0, 0x8C(29)
    sth 0, 0x90(29)
""" + value(0, "OPPONENTS", opponents) + """    sth 0, 0x12C(29)
""" + value(0, "LAPS", laps) + """    sth 0, 0x13A(29)
""" + value(0, "CPULEVEL", 3) + """    sth 0, 8(29)
""" + call(0x34E08) + """
    li 3, 0x8B
""" + call(0x47F74) + value(3, "TYPE", racetype) + call(0x3EF8C) + (("""
    lwz 0, 16(24)
    cmpwi 0, 0
    beq no_laps
""" if PARAMS else "") + """
    lis 3, 0x0400
""" + call(0x3FCB0) + """
    lis 3, 2
""" + call(0x3FCD4) + ("no_laps:\n" if PARAMS else "") if laps or PARAMS else "") + """
    li 3, 0x7FFF
    addi 3, 3, 1
""" + (("""    lwz 0, %d(24)
    cmpwi 0, 0
    beq no_restore
""" % memmap.RACE_RESTORE + call(0x3FCB0) + """    b restore_done
no_restore:
    li 3, 0x7FFF
    addi 3, 3, 1
""" + call(0x3FCD4) + "restore_done:\n") if PARAMS else call(0x3FCD4)) + f"""
    li 3, 0
""" + value(4, "LAPS", laps) + """    li 5, 1
""" + value(6, "COURSE", course) + """    li 7, 1
""" + value(8, "OPPONENTS", opponents, 1) + """    li 9, 0
""" + call(0x3ED8C) + call(0x36AD0) + """
    mr 28, 3
    addi 27, 1, 0x10
    li 26, 0
port_loop:
    li 0, 0
    stb 0, 0(27)
    stb 0, 5(27)
    slwi 9, 26, 5
    add 9, 9, 29
    lha 0, 0xE(9)
    stb 0, 1(27)
    lbz 0, 0x14(9)
    stb 0, 2(27)
    li 9, 0x40E0
    slwi 9, 9, 1
    mullw 9, 9, 26
    add 9, 9, 28
    lbz 0, 7(9)
    stb 0, 3(27)
    lbz 0, 8(9)
    stb 0, 4(27)
    lis 0, 0x4156
    ori 0, 0, 0x2020
    stw 0, 6(27)
    li 0, 0x20
    stb 0, 10(27)
    add 9, 29, 26
    lbz 0, 0x9F(9)
    stb 0, 11(27)
    addi 27, 27, 12
    addi 26, 26, 1
    cmpwi 26, 4
    blt port_loop
    li 3, 1
    addi 4, 1, 0x10
""" + call(0x3EC88) + """
    li 0, 2
    sth 0, 0x968(30)
out:
    lmw 3, 0x50(1)
    lwz 0, 0xD4(1)
    mtlr 0
    addi 1, 1, 0xD0
    stwu 1, -0x10(1)
"""
assert 0x50 + 29 * 4 <= 0xD0                              # r3..r31 saved inside the frame
code = bytes(Ks(KS_ARCH_PPC, KS_MODE_PPC32 + KS_MODE_BIG_ENDIAN).asm(asm)[0])
assert 0x8B3A0 == 0x90000 - 0x4C60 and 0x1BA040 == 0x1C0000 - 0x5FC0
code += bytes.fromhex("6000000000000000" if len(code) % 8 == 0 else "00000000")
w = [code[i:i + 4].hex().upper() for i in range(0, len(code), 4)]
print("\n".join(["48000000 800030C8", "DE000000 80008180", f"D2{TEXT + 0xECC:06X} {len(w) // 2:08X}"] +
                [f"{w[i]} {w[i + 1]}" for i in range(0, len(w), 2)] + ["E0000000 80008000"]))
