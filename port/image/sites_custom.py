"""The seats' custom machines (garage-built: three parts, their colours and a pilot): the words of the patch list that make the game read a seat's parts from the race block's table
(memmap.RACE_CUSTOM, written by the host from the match start or a replay's header) instead of a pad port's garage
record. Native form only: the fork's image does not have it. sites.py calls add() before its entry table.

The game keeps four garage records (0x81C0 bytes, .bss 0x5480), one a pad port, and finds a racing machine's record
by its port (no machine of ours has one), or by its id less 0x32. A custom seat races id 0x32 (memmap.CUSTOM_ID: every
race test for "custom" is id >= 0x29, and the CPU's personality is id mod 4, the same in every game), and each site
below takes the seat's own entry where the game took a record:

| Site | Where | What the game read | Now |
|---|---|---|---|
| `custom_seat` | fn_1_3181C 0x319EC | the seat entry's three part ids and its parts flag (the 3D model's loader follows them, fn_1_14D728), for a seat with a port only | the seat's parts, port or not |
| `custom_result` | fn_1_3181C 0x31BFC | the per-racer record's parts (+0x7C) and pilot (+0x80; the results page, fn_1_12C930), for a seat with a port only | the seat's parts and pilot |
| `custom_create` | fn_1_83070 0x8335C | the stats at creation (inertia, hover and wall corners, energy: fn_1_2277C), port 0's record for a portless seat | the sum of the seat's parts |
| `custom_name` | fn_1_83070 0x833B0 | the name (racer +0x3C, fn_1_14FEAC), port 0's record | the seat's parts' name |
| `custom_step` | fn_1_331EC 0x3364C | the stats of every step (fn_1_82EDC); for a portless seat an uninitialised stack block | the sum of the seat's parts; a stock machine's seat's row where it is on (sites_seats.py) |
| `custom_paint` | fn_1_84860 0x84DBC | the paint's record, id - 0x32 | the seat's entry |
| `custom_colour` | fn_1_151C9C 0x151CD4 | a part's colour from the record (its flags at +0, the colour at +0x81A8) | the entry's colour; 0 the part's own |
| `custom_record` | fn_1_8472C 0x84740 | the results page's record of the local machine (its paint and name), id - 0x32 | the seat's entry |

A record's address made up for an entry (`CUSTOM_RECORD`, entry + 4 - 0x81A4) has the entry's part entries where a
record has its own (+0x81A4, +0x81AC, +0x81B4, each colour 4 bytes on), so the game's routines that read a record
read the entry; its flags word (+0) is no entry's, and the one reader of it here (`custom_colour`) knows such an
address by its range. The table is written once before the race and never in it: a restart or retry builds the seats
again from the same entries. Emblems stay out: they are found by port, and no machine of ours has one."""
import memmap
import sites_seats

SEATS = 30
ENTRY, ID = memmap.CUSTOM_ENTRY, memmap.CUSTOM_ID
TABLE = memmap.RACE + memmap.RACE_CUSTOM
RECORD = memmap.CUSTOM_RECORD                  # entry - RECORD: the record's address made up for an entry
FAKE_LOW = TABLE - RECORD                      # the first entry's
BODY, COCKPIT, BOOSTER, PILOT = 4, 12, 20, 28  # an entry's bytes
assert RECORD == 0x81A0 and ENTRY == 32 and SEATS * ENTRY <= 0x400 and TABLE + SEATS * ENTRY == memmap.RACE + memmap.RACE_SIZE


def lookup(seat, none):
    """r11 = the seat's entry (the seat in register `seat`), or a branch to `none` when the seat has none. Uses r11,
    r12, cr0."""
    return f"""    cmplwi {seat}, {SEATS}
    bge {none}
    mulli 11, {seat}, {ENTRY}
    lis 12, {memmap.hi(TABLE) - 0x10000}
    ori 12, 12, {memmap.lo(TABLE)}
    add 11, 11, 12
    lwz 12, 0(11)
    cmpwi 12, 0
    beq {none}
"""


# r11 the entry: r3 = the record's address made up for it (entry - 0x81A0)
RECORD_OF = f"""    addis 3, 11, -1
    addi 3, 3, {0x10000 - RECORD}
"""
PARTS = f"""    lbz 3, {BODY}(11)
    lbz 4, {COCKPIT}(11)
    lbz 5, {BOOSTER}(11)
"""


def add(k, r, patch, stock, test, middle, complete):
    """Adds the routines to `r` and an entry for each site through `patch`, as sites.add does; `test` is the mode
    test's text."""
    TEXT, branch, branch_target = k.TEXT, k.branch, k.branch_target
    FN = lambda offset: f"{TEXT + offset:#x}"
    SUM, NAME, SEAT_PARTS, STATS = TEXT + 0x14F6F8, TEXT + 0x833DC, TEXT + 0x14F38C, TEXT + 0x82EDC

    def site(name, offset, known, text, dead):
        at = TEXT + offset
        word = stock(at, known)
        r.add(name, text.replace("WORD", f"    .long {word:#010x}\n"))
        patch(at, word, 0, name)
        middle.append((at, dead, name))
        return word

    # the seat entry (r29; the seat r21): its parts and parts flag, which the model's loader follows
    # (r4 to r10 kept over the call: the call after the site counts as reading them)
    site("custom_seat", 0x319EC, 0x88180008, test + f"""    beq cs_stock
""" + lookup(21, "cs_stock") + f"""    lha 0, 2(29)
    cmpwi 0, 0x28
    ble cs_stock
    stwu 1, -0x30(1)
    stw 4, 8(1)
    stw 5, 12(1)
    stw 6, 16(1)
    stw 7, 20(1)
    stw 8, 24(1)
    stw 9, 28(1)
    stw 10, 32(1)
    addis 4, 11, -1
    addi 4, 4, {0x10000 - RECORD}
    mr 3, 29
    @bl {SEAT_PARTS:#x}
    lwz 4, 8(1)
    lwz 5, 12(1)
    lwz 6, 16(1)
    lwz 7, 20(1)
    lwz 8, 24(1)
    lwz 9, 28(1)
    lwz 10, 32(1)
    addi 1, 1, 0x30
cs_stock:
WORD    @b {TEXT + 0x319F0:#x}
""", ["r3", "r11", "r12", "cr0", "cr1", "cr5", "cr6", "cr7", "ctr", "lr"])

    # the per-racer record (r20; the racer r22): parts and pilot, as the game sets them for a seat with a port
    site("custom_result", 0x31BFC, 0x3B3900A0, test + f"""    beq cr_stock
""" + lookup(22, "cr_stock") + f"""    lbz 0, {BODY}(11)
    stb 0, 0x7C(20)
    lbz 0, {COCKPIT}(11)
    stb 0, 0x7D(20)
    lbz 0, {BOOSTER}(11)
    stb 0, 0x7E(20)
    lbz 0, {PILOT}(11)
    stb 0, 0x80(20)
cr_stock:
WORD    @b {TEXT + 0x31C00:#x}
""", ["r0", "r11", "r12", "cr0"])

    # creation's stats (the racer r24; reached for an id of 0x29 or more): the sum into the block at sp + 0x2C
    site("custom_create", 0x8335C, 0x7EC00734, test + f"""    beq cc_stock
""" + lookup(24, "cc_stock") + PARTS + f"""    addi 6, 1, 0x2C
    @bl {SUM:#x}
    @b {FN(0x83398)}
cc_stock:
WORD    @b {TEXT + 0x83360:#x}
""", ["r11", "r12", "cr0"])

    # creation's name (the racer r24): fn_1_14FEAC(body, cockpit, booster, sp + 0xC, language 0), as the game calls it
    site("custom_name", 0x833B0, 0x3C600001, test + f"""    beq cn_stock
""" + lookup(24, "cn_stock") + PARTS + f"""    addi 6, 1, 0xC
    li 7, 0
    @b {NAME:#x}
cn_stock:
WORD    @b {TEXT + 0x833B4:#x}
""", ["r11", "r12", "cr0"])

    # every step's stats (the racer r27, its index +0x4): the sum into the block at sp + 8, in place of fn_1_82EDC
    step = TEXT + 0x3364C
    step_word = stock(step, branch(step, STATS, True))
    if complete:
        assert branch_target(step_word, step) == STATS
    # (a stock machine whose seat's row is on takes the row, as fn_1_82EDC would have copied the table's block to
    # r5 and returned it in r3: sites_seats.py; r0, ctr and cr0 are what fn_1_82EDC itself writes first)
    r.add("custom_step", test + f"""    beq cst_stock
    lha 11, 4(27)
""" + lookup(11, "cst_row") + PARTS + f"""    addi 6, 1, 8
    @bl {SUM:#x}
    @b {step + 4:#x}
cst_row:
    lha 0, 4(27)
""" + sites_seats.row_copy(0, 5, "cst_stock", "cst_copy") + f"""    mr 3, 5
    @b {step + 4:#x}
cst_stock:
    @bl {STATS:#x}
    @b {step + 4:#x}
""")
    patch(step, step_word, 0, "custom_step")
    middle.append((step, ["r11", "r12", "cr0"], "custom_step"))

    # the paint's record (the racer r29, its index +0x4): the entry's made-up record; r28 as the game leaves it (id - 0x32)
    site("custom_paint", 0x84DBC, 0xA87D0006, test + f"""    beq cp_stock
    lha 11, 4(29)
""" + lookup(11, "cp_stock") + RECORD_OF + f"""    li 28, 0
    @b {FN(0x84E00)}
cp_stock:
WORD    @b {TEXT + 0x84DC0:#x}
""", ["r11", "r12"])

    # a part's colour (r29 the record, r30 the record + 8 x the part): a made-up record's colour; 0, the part's own
    site("custom_colour", 0x151CD4, 0x281D0000, test + f"""    beq cco_stock
    lis 12, {memmap.hi(FAKE_LOW) - 0x10000}
    ori 12, 12, {memmap.lo(FAKE_LOW)}
    subf 11, 12, 29
    cmplwi 11, {SEATS * ENTRY}
    bge cco_stock
    addis 3, 30, 1
    lwz 3, -0x7E58(3)
    cmpwi 3, 0
    beq cco_own
    @b {FN(0x151D08)}
cco_own:
    @b {FN(0x151CF0)}
cco_stock:
WORD    @b {TEXT + 0x151CD8:#x}
""", ["r11", "r12"])

    # the results page's record of a machine (r3 its racer, index +0x4)
    site("custom_record", 0x84740, 0xA8630006, test + f"""    beq cre_stock
    lha 11, 4(3)
""" + lookup(11, "cre_stock") + RECORD_OF + f"""    @b {FN(0x84780)}
cre_stock:
WORD    @b {TEXT + 0x84744:#x}
""", ["r11", "r12"])
