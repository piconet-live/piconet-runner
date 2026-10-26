"""Player names on the results screens (for the frame routine, netframe.py, which includes this code and calls
`playernames_install` on entry every frame).

The results screens' racer rows (fn_1_144F40 builds one per place on the stack: row +0x4C the racer, from the places
table [.bss 0x8E428]; row +0x24 the pilot, from the racer's record .bss 0x3C30 + 0xD4 + racer * 0xA0, +0x80) show,
with pilot names on (.bss 0x8E52C; Z toggles machine names), the pilot's name from the English name table
(.data 0x212A8, read at fn_1_147508 0x14791C / 0x147958 (row r27) and fn_1_147A04 0x1483C4 / 0x148404 (row r28):
`lwzx r3, r3, r0`, the table and the pilot index x 4). Here the row's racer's player name (memmap.NAMES, 32 bytes a
seat in seat order, written by the race's Gecko codes in the first, archived experiment) takes its place when it is not
empty; a seat without one (a CPU racer) keeps its pilot's name. Control block +532: the main.rel address the sites
were patched for (follow.installer)."""
import memmap
from follow import installer

GUARD = 532
SITES = [(0x14791C, 0x7C63002E, 0xFFFFFFFF, "r27"),
         (0x147958, 0x7C63002E, 0xFFFFFFFF, "r27"),
         (0x1483C4, 0x7C63002E, 0xFFFFFFFF, "r28"),
         (0x148404, 0x7C63002E, 0xFFFFFFFF, "r28")]


def lookup(label, row):
    """r3 the name table, r0 the pilot index x 4, `row` the row: r3 = the racer's player name or the pilot's."""
    return f"""    lwz 12, 0x4C({row})
    cmplwi 12, 30
    bge {label}
    slwi 12, 12, 5
    addis 12, 12, {memmap.hi(memmap.NAMES)}
    addi 12, 12, {memmap.lo(memmap.NAMES)}
    lbz 11, 0(12)
    cmpwi 11, 0
    beq {label}
    mr 3, 12
    blr
{label}:
    lwzx 3, 3, 0
    blr
"""


assert memmap.NAME_SIZE == 32 and memmap.lo(memmap.NAMES) < 0x8000
HELPERS = {"r27": lookup("pn_pilot27", 27), "r28": lookup("pn_pilot28", 28)}


def asm():
    """The helpers and `playernames_install` (r31 = the control block; called with bl; uses r0, r3-r12 and 32(r1))."""
    return installer("playernames_install", "pn", SITES, HELPERS, GUARD)
