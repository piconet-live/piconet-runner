"""The seats' boost trail colours: the words of the patch list that give a seat's flame, and what a boost throws off,
the colours of the race block's table (memmap.RACE_TRAILS, written by the core from the match start or a replay's
header) in place of the game's. Native form only: the fork's image does not have it. sites.py calls add() before its entry table.

The textures are grey and tinted per draw, so this is colour and nothing else: no copies, the same cost for any
number of different trails.

| Site | Where | What the game had | Now |
|---|---|---|---|
| `trail_record` | fn_1_9885C 0x98880, a burner record's creation | +0x28 and +0x34, the nozzle's idle and boost colours (3 floats 0..255, the machine's .fmi by fn_1_983CC) | the seat's words over them, before the game copies +0x28 to the colour it blends (+0x468) |
| `trail_streak` | fn_1_6BFB8 0x6C3C8, kind 12's spawn | f1, f3, f4: the streak's and its blobs' colour (0..1), from a jump table by machine id | the seat's sparks word |
| `trail_bolts` | fn_1_6A7A4 0x6AAA0, kind 5's spawn for a boost (not a contact's or a wall's) | +0x1C..+0x24: the bolts' colour (128, 224, 255 x f5, f5 from the effects' generator) | the seat's sparks word x f5 |
| `trail_sparkles` | fn_1_988DC 0x98B18, kind 6's spawn, which kind 9 inherits: the followed machine only | +0x1C..+0x24: the nozzle's idle colour / 255 | the seat's sparks word / 255 |

The seat: a record's +0x48 is its machine's block (fn_1_983CC's r3), whose +4 is the racer index; an effect's +0x18
is the same index, and the burner's task holds the block in r30. Burners made for anything else (the ghost's, the ending's, 0xECEF4, 0xED6EC, 0xF26FC) read a +4
of their own: the bound keeps it inside the table, and a stray hit is only a colour.

A picture matter by construction: +0x28 to +0x3B are written once at creation, and their readers (fn_1_9885C,
fn_1_988DC, fn_1_98F28) write only the record and effect records, none in the checksum; rolled back as ever, they
hold the same value. An effect's colour is read only by its own update and draw. No site draws a random number. A word of 0 and a plain start do exactly what the game
did, so a race with no colours has the stock picture."""
import memmap

SEATS = 30
ENTRY = memmap.TRAIL_ENTRY
TABLE = memmap.RACE + memmap.RACE_TRAILS
NORMAL, BOOST, SPARKS = 0, 4, 8           # an entry's words: 0x01RRGGBB; 0 the game's own
OVER_255 = 0x3B808081                     # 1/255, a float
assert ENTRY == 12 and memmap.RACE_TRAILS + SEATS * ENTRY == memmap.RACE_SIZE


def lookup(seat, none):
    """r11 = the seat's entry (the seat in register `seat`), or a branch to `none`. Uses r11, r12, cr0."""
    return f"""    cmplwi {seat}, {SEATS}
    bge {none}
    mulli 11, {seat}, {ENTRY}
    lis 12, {memmap.hi(TABLE) - 0x10000}
    ori 12, 12, {memmap.lo(TABLE)}
    add 11, 11, 12
"""


def part(shift, out):
    """Float register `out` = the byte of r12 at `shift`: a whole number's double less the double of 0 in f6."""
    return f"""    rlwinm 0, 12, {32 - shift & 31}, 24, 31
    stw 0, 12(1)
    lfd {out}, 8(1)
    fsub {out}, {out}, 6
    frsp {out}, {out}
"""


def add(k, r, patch, stock, test, middle):
    """Adds the routines to `r` and an entry for each site through `patch`, as sites.add does; `test` is the mode
    test's text."""
    TEXT = k.TEXT

    # r12 a word 0x01RRGGBB: f1, f3, f4 = its three parts, 0 to 255. Uses r0 and f6.
    r.add("trail_floats", """    stwu 1, -0x10(1)
    lis 0, 0x4330
    stw 0, 8(1)
    li 0, 0
    stw 0, 12(1)
    lfd 6, 8(1)
""" + part(16, 1) + part(8, 3) + part(0, 4) + """    addi 1, 1, 0x10
    blr
""")

    def colour(offset, at, skip):
        """The entry's word at `offset`, when one is set, into the record's three floats from `at` (r30 the record)."""
        return f"""    lwz 12, {offset}(11)
    cmpwi 12, 0
    beq {skip}
    @bl trail_floats
    stfs 1, {at}(30)
    stfs 3, {at + 4}(30)
    stfs 4, {at + 8}(30)
{skip}:
"""

    # the record's creation (r30 the record, r31 its machine's block; r3 about to be read from the record; the
    # function has saved its lr and calls on from here)
    site = TEXT + 0x98880
    word = stock(site, 0x80630028)
    r.add("trail_record", test + """    beq tr_stock
    lha 11, 4(31)
""" + lookup(11, "tr_stock") + colour(NORMAL, 0x28, "tr_boost") + colour(BOOST, 0x34, "tr_stock") + f"""    .long {word:#010x}
    @b {site + 4:#x}
""")
    patch(site, word, 0, "trail_record")
    middle.append((site, ["r0", "r11", "r12", "cr0", "lr", "f1", "f3", "f4", "f6"], "trail_record"))

    # r11 a seat: when it has a sparks word, f1, f3, f4 = its three parts, 0 to 1, and cr0 not equal; otherwise those
    # as they were and cr0 equal. Uses r0, r11, r12, lr, f6 and f7; f0 and f5 are the sites' own.
    r.add("trail_sparks", """    li 12, 0
""" + lookup(11, "tk_word") + f"""    lwz 12, {SPARKS}(11)
tk_word:
    cmpwi 12, 0
    beq tk_end
    stwu 1, -0x10(1)
    mflr 0
    stw 0, 0x14(1)
    @bl trail_floats
    lis 11, {OVER_255 >> 16}
    ori 11, 11, {OVER_255 & 0xFFFF}
    stw 11, 8(1)
    lfs 7, 8(1)
    fmuls 1, 1, 7
    fmuls 3, 3, 7
    fmuls 4, 4, 7
    lwz 0, 0x14(1)
    mtlr 0
    addi 1, 1, 0x10
tk_end:
    blr
""")

    # the streak's spawn (r31 the effect; f1, f3, f4 its colour, 0 to 1; every machine's jump table entry comes here;
    # f0 its brightness; the function's own lr is saved at its start)
    site = TEXT + 0x6C3C8
    word = stock(site, 0xEC410032)
    r.add("trail_streak", test + """    beq ts_stock
    lha 11, 0x18(31)
    @bl trail_sparks
ts_stock:
""" + f"""    .long {word:#010x}
    @b {site + 4:#x}
""")
    patch(site, word, 0, "trail_streak")
    middle.append((site, ["r0", "r11", "r12", "cr0", "lr", "f6", "f7"], "trail_streak"))

    # a boost's bolts (r25 the machine's effect; the new effect on the stack at 0x38; f5 the bolt's own brightness)
    site = TEXT + 0x6AAA0   # the colour's last store, stfs f0,0x5C(r1) (0x6AA9C is its green, stfs f1,0x58(r1))
    word = stock(site, 0xD001005C)
    r.add("trail_bolts", f"""    .long {word:#010x}
""" + test + """    beq tb_stock
    lha 11, 0x18(25)
    @bl trail_sparks
    beq tb_stock
    fmuls 1, 1, 5
    fmuls 3, 3, 5
    fmuls 4, 4, 5
    stfs 1, 0x54(1)
    stfs 3, 0x58(1)
    stfs 4, 0x5C(1)
tb_stock:
""" + f"""    @b {site + 4:#x}
""")
    patch(site, word, 0, "trail_bolts")
    middle.append((site, ["r0", "r11", "r12", "cr0", "lr", "f1", "f3", "f4", "f6", "f7"], "trail_bolts"))

    # a boost's sparkles (r30 the machine's block; the new effect on the stack at 0x44, r3 points to it, its colour
    # about to be finished)
    site = TEXT + 0x98B18
    word = stock(site, 0xD0010068)
    r.add("trail_sparkles", f"""    .long {word:#010x}
""" + test + """    beq tl_stock
    lha 11, 4(30)
    @bl trail_sparks
    beq tl_stock
    stfs 1, 0x60(1)
    stfs 3, 0x64(1)
    stfs 4, 0x68(1)
tl_stock:
""" + f"""    @b {site + 4:#x}
""")
    patch(site, word, 0, "trail_sparkles")
    middle.append((site, ["r0", "r11", "r12", "cr0", "lr", "f1", "f3", "f4", "f6", "f7"], "trail_sparkles"))
