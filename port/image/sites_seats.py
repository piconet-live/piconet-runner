"""A stat pack a seat (handbook/stat-pack-constants.md, "A pack a seat"): the words of the
patch list that make a machine take its seat's own stats and constants from the seats' block (memmap.SEAT_BLOCK),
which the core writes for every race and replay. Native form only: the fork's image does not have them. sites.py
calls add() after sites_custom.add(); the dash plate's own site is sites_dash.py, and the stats of every step are
custom_step's (sites_custom.py), whose stock path takes the seat's row here (`row_copy`).

| Site | Where | What the game did | Now, in a race or replay of ours |
|---|---|---|---|
| `seat_row_create` | fn_1_83070 0x8324C | `mulli r3, r0, 0xb4`: a stock machine's block copied from the loaded car data into the stack (sp + 0x2C) at racer creation | for a seat whose row is on, its row copied there instead, then on at 0x83398 as the custom path does |
| `seat_energy` | fn_1_26F34 0x27420 | `lfs f1, 0x260(r31)`: a boost frame's energy use, .rodata 0xAA0 | the seat's own (+SEAT_ENERGY_USE); with a game mode whose seat.stat.boost_energy wrote the seat (its stat record's bit BOOST_ENERGY_BIT), the mode's value on the stats page |
| `seat_speed_cap` | fn_1_294D0 0x29570 | `lfd f0, 0x2e0(r31)`: the speed cap, .rodata 0xB20 | the seat's own (+SEAT_SPEED_CAP) |

The seat is the machine's racer index (its block +4, a halfword: port/facts/machine-state.csv), the index custom_step
takes too; in fn_1_26F34 and fn_1_294D0 the machine's block is r30, at racer creation the seat is r24 (custom_create's).
An index outside the 30 seats (none is) takes the game's own instruction. A seat with no pack of its own has the
race's pack's values, or the game's own: one pack for all is the game as before, the same values read from another
place. Nothing here is written in a race: the block is the host's, before the race, and no step writes it. A game mode's
boost energy (seat.stat.boost_energy, port/core/src/mode.h STAT_BOOST_ENERGY) is the mode block's, which the undo log and the
checksum cover: the site reads it there, and the seat's record stays the pack's (the mode's base)."""
import memmap

SEATS = 30
FLAGS = memmap.SEAT_BLOCK
CONSTANTS = memmap.SEAT_BLOCK + memmap.SEAT_CONSTANTS
ROWS = memmap.SEAT_BLOCK + memmap.SEAT_ROWS
WORDS = memmap.SEAT_ROW // 4
assert memmap.SEAT_ROW == 0xB4 and memmap.SEAT_CONSTANT < 0x8000
# seat.stat.boost_energy: its bit in a seat's stat record (mode.h STAT_BOOST_ENERGY) and its value's place on the stats
# page, reached from the block + MODE_STATS_REACH as the stand-in's wrapper reaches the page
BOOST_ENERGY_BIT = 18
BOOST_ENERGY_AT = memmap.MODE_STATS_TWO - memmap.MODE_STATS_REACH + 4 * (BOOST_ENERGY_BIT - memmap.MODE_STATS_FIRST)
assert -0x8000 <= BOOST_ENERGY_AT < 0x8000 and memmap.MODE_STATS < 0x8000 and 4 * (BOOST_ENERGY_BIT - memmap.MODE_STATS_FIRST) < 0x40


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

    # a boost frame's energy use and the speed cap: the machine's block r30. The energy use first asks the race's mode
    # (memmap.MODES +4, its block; 0 none): a seat whose stat record has the boost energy bit loads the mode's value.
    mode_energy = f"""    lis 12, {memmap.hi(memmap.MODES)}
    lwz 12, {memmap.lo(memmap.MODES) + 4}(12)
    cmplwi 12, 0
    beq seat_energy_pack
    lha 11, 4(30)
    cmplwi 11, {SEATS}
    bge seat_energy_stock
    mulli 11, 11, {memmap.MODE_STAT}
    add 11, 11, 12
    lwz 11, {memmap.MODE_STATS:#x}(11)
    rlwinm. 11, 11, 0, {31 - BOOST_ENERGY_BIT}, {31 - BOOST_ENERGY_BIT}
    beq seat_energy_pack
    lha 11, 4(30)
    slwi 11, 11, {memmap.MODE_STAT_TWO_SHIFT}
    add 12, 12, 11
    addis 12, 12, {memmap.MODE_STATS_REACH >> 16}
    lfs 1, {BOOST_ENERGY_AT}(12)
    @b {TEXT + 0x27420 + 4:#x}
seat_energy_pack:
"""
    for name, offset, known, load in (("seat_energy", 0x27420, 0xC03F0260, f"lfs 1, {memmap.SEAT_ENERGY_USE}(12)"),
                                      ("seat_speed_cap", 0x29570, 0xC81F02E0, f"lfd 0, {memmap.SEAT_SPEED_CAP}(12)")):
        at = TEXT + offset
        word = stock(at, known)
        r.add(name, test + f"""    beq {name}_stock
""" + (mode_energy if name == "seat_energy" else "") + seat_constants(30, f"{name}_stock") + f"""    {load}
    @b {at + 4:#x}
{name}_stock:
    .long {word:#010x}
    @b {at + 4:#x}
""")
        patch(at, word, 0, name)
        middle.append((at, ["r11", "r12", "cr0"], name))
