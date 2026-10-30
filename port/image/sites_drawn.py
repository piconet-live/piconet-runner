"""A machine's drawn transform: where it is drawn against where the race has it (sites.py calls add() before the
routines that read it). Native form only: the fork's image does not have it.

One record a seat, and one reader of it, so that every picture of a machine that must stay with its body (the
shadow, sites_shadow.py; the effects, sites_effects.py) moves by the same amount. Today the record is smooth.py's
entry of the seat (memmap.SMOOTH, 48 bytes a seat): +12 the offset from the race's place to the drawn one (world
units, three floats), +36 not 0 when smooth.py has drawn the machine with that offset in this frame (written at
list 10 of the frame's last step, cleared on the next frame's entry). A machine smooth.py has not moved (this
game's own, every machine outside a race of ours, a frame that started over) has no record, and its followers
stay where the game puts them. A later drawing that moves bodies too (a frame drawn between two steps) adds its
own part to the same record, and every follower takes it from here.

`drawn_entry`: r9 the machine's index (0 to 29, as fn_1_862D4 and the effects take it). Out: r10 the seat's entry
(memmap.SMOOTH + 48 x index) when the machine is drawn with an offset in this frame, else 0. Uses r0, r10, cr0.
`drawn_machine`: r9 a machine's block (the race's machines, 0x620 bytes apart). Out: as `drawn_entry`. Uses r0, r9,
r10, r11, ctr, cr0. Neither tests the mode word: their callers do. Neither writes memory."""
import memmap

SEATS, BLOCK, ENTRY = 30, memmap.MACHINE_SIZE, 48
OFFSET, WRITTEN = 12, 36           # the entry's offset (three floats) and its byte "drawn with it in this frame"


def add(r):
    """Adds the two readers to `r` (build.Routines)."""
    assert SEATS * ENTRY <= 0x600 and memmap.lo(memmap.SMOOTH) == 0x2000   # the table, below smooth.py's globals
    r.add("drawn_machine", f"""    lis 11, 0x8000
    lwz 11, 0x30C8(11)
    addis 11, 11, 0x1C
    addi 11, 11, -0x5FC0
    addis 11, 11, 7
    lwz 11, -0x27C8(11)
    cmpwi 11, 0
    beq dm_none
    subf 9, 11, 9
    li 0, {SEATS}
    mtctr 0
    li 10, 0
dm_seat:
    cmpwi 9, 0
    beq dm_found
    addi 9, 9, -{BLOCK}
    addi 10, 10, 1
    bdnz dm_seat
dm_none:
    li 10, 0
    blr
dm_found:
    mr 9, 10
    b drawn_entry
""")
    r.add("drawn_entry", f"""    cmplwi 9, {SEATS}
    bge de_none
    mulli 10, 9, {ENTRY}
    lis 0, {memmap.hi(memmap.SMOOTH) - 0x10000}
    ori 0, 0, {memmap.lo(memmap.SMOOTH)}
    add 10, 10, 0
    lbz 0, {WRITTEN}(10)
    cmpwi 0, 0
    bnelr
de_none:
    li 10, 0
    blr
""")
