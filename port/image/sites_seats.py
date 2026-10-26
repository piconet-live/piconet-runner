"""A stat pack a seat (handbook/stat-pack-constants.md, "A pack a seat"): the words of the
patch list that make a machine take its seat's own stats and constants from the seats' block (memmap.SEAT_BLOCK),
which the core writes for every race and replay. Native form only: the fork's image does not have them. sites.py
calls add() after sites_custom.add(); the dash plate's own site is sites_dash.py, and the stats of every step are
custom_step's (sites_custom.py), whose stock path takes the seat's row here (`row_copy`).

| Site | Where | What the game did | Now, in a race or replay of ours |
|---|---|---|---|
| `seat_row_create` | fn_1_83070 0x8324C | `mulli r3, r0, 0xb4`: a stock machine's block copied from the loaded car data into the stack (sp + 0x2C) at racer creation | for a seat whose row is on, its row copied there instead, then on at 0x83398 as the custom path does |
| `seat_energy` | fn_1_26F34 0x27420 | `lfs f1, 0x260(r31)`: a boost frame's energy use, .rodata 0xAA0 | the seat's own (+SEAT_ENERGY_USE) |
| `seat_speed_cap` | fn_1_294D0 0x29570 | `lfd f0, 0x2e0(r31)`: the speed cap, .rodata 0xB20 | the seat's own (+SEAT_SPEED_CAP) |

The seat is the machine's racer index (its block +4, a halfword: port/facts/machine-state.csv), the index custom_step
takes too; in fn_1_26F34 and fn_1_294D0 the machine's block is r30, at racer creation the seat is r24 (custom_create's).
An index outside the 30 seats (none is) takes the game's own instruction. A seat with no pack of its own has the
race's pack's values, or the game's own: one pack for all is the game as before, the same values read from another
place. Nothing here is written in a race: the block is the host's, before the race, and no step writes it."""
import memmap

SEATS = 30
FLAGS = memmap.SEAT_BLOCK
CONSTANTS = memmap.SEAT_BLOCK + memmap.SEAT_CONSTANTS
ROWS = memmap.SEAT_BLOCK + memmap.SEAT_ROWS
WORDS = memmap.SEAT_ROW // 4
assert memmap.SEAT_ROW == 0xB4 and memmap.SEAT_CONSTANT < 0x8000


def seat_constants(block, none):
    """r12 = the seat's constants (the machine's block in register `block`), or a branch to `none` for an index outside
    the seats. Uses r11, r12, cr0."""
    return f"""    lha 11, 4({block})
    cmplwi 11, {SEATS}
    bge {none}
    mulli 11, 11, {memmap.SEAT_CONSTANT}
    lis 12, {memmap.hi(CONSTANTS) - 0x10000}
    ori 12, 12, {memmap.lo(CONSTANTS)}
    add 12, 12, 11
"""


def row_copy(seat, dest, none, label, offset=0):
    """For a seat whose row is on (the seat in register `seat`, not r11/r12): its row copied to `dest` + `offset`
    (`dest` a register, not r0/r11/r12, kept), else a branch to `none` with r0 and ctr as they were. Uses r0, r11,
    r12, ctr, cr0."""
    return f"""    cmplwi {seat}, {SEATS}
    bge {none}
    lis 12, {memmap.hi(FLAGS) - 0x10000}
    ori 12, 12, {memmap.lo(FLAGS)}
    lbzx 11, 12, {seat}
    cmpwi 11, 0
    beq {none}
    mulli 11, {seat}, {memmap.SEAT_ROW}
    lis 12, {memmap.hi(ROWS) - 0x10000}
    ori 12, 12, {memmap.lo(ROWS)}
    add 12, 12, 11
    addi 12, 12, -4
    addi 11, {dest}, {offset - 4}
    li 0, {WORDS}
    mtctr 0
{label}:
    lwzu 0, 4(12)
    stwu 0, 4(11)
    bdnz {label}
"""


def add(k, r, patch, stock, test, middle):
    """Adds the routines to `r` and an entry for each site through `patch`, as sites.add does; `test` is the mode
    test's text."""
    TEXT = k.TEXT

    # racer creation's copy of a stock machine's block (the seat r24; the copy's place sp + 0x2C). The row path goes on
    # where the custom path goes on, after a call: r0, r3 to r12 and ctr hold nothing there.
    at = TEXT + 0x8324C
    word = stock(at, 0x1C6000B4)
    r.add("seat_row_create", test + """    beq src_stock
""" + row_copy(24, 1, "src_stock", "src_copy", 0x2C) + f"""    @b {TEXT + 0x83398:#x}
src_stock:
    .long {word:#010x}
    @b {at + 4:#x}
""")
    patch(at, word, 0, "seat_row_create")
    middle.append((at, ["r11", "r12", "cr0"], "seat_row_create"))

    # a boost frame's energy use and the speed cap: the machine's block r30
    for name, offset, known, load in (("seat_energy", 0x27420, 0xC03F0260, f"lfs 1, {memmap.SEAT_ENERGY_USE}(12)"),
                                      ("seat_speed_cap", 0x29570, 0xC81F02E0, f"lfd 0, {memmap.SEAT_SPEED_CAP}(12)")):
        at = TEXT + offset
        word = stock(at, known)
        r.add(name, test + f"""    beq {name}_stock
""" + seat_constants(30, f"{name}_stock") + f"""    {load}
    @b {at + 4:#x}
{name}_stock:
    .long {word:#010x}
    @b {at + 4:#x}
""")
        patch(at, word, 0, name)
        middle.append((at, ["r11", "r12", "cr0"], name))
