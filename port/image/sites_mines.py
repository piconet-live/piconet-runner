"""Mines that come back: with the host's rule in the race
block (memmap.RACE_MINE_REGEN, N steps, written by the host from the match start or a replay's header), a mine that
went off in step s is armed again for the machines of step s + N. With the rule at 0 (a match start without it,
every recording made before it) every word here does what the game did: a mine stays gone for the rest of the race.
Native form only: the fork's image does not have it. sites.py calls add() before its entry table.

The game's mine is a placed object of the course file (0x40-byte records from [header +0x54], the course header at
.bss 0x3BE0), and its whole state is two bits of its record's first word: 0x20000000, hit in this step (the mine
walk fn_1_163BC sets it in a machine's step, priority 7), and 0x40000000, spent (the walk passes over it; the draw
shows another model). The background task's per-step pass (fn_1_9AD54 -> fn_1_9AF80(objects, count, 0), priority
23) turns the one into the other at 0x9B0DC, for every object with flag 0x00020000; nothing else clears "spent" but
the race's set-up (fn_1_9AD20, the same pass with its third argument 1). That site is patched:

| Site | Where | What the game did | Now, with N not 0 |
|---|---|---|---|
| `mine_regen` | fn_1_9AF80 0x9B0DC | `lwz r3, 0(r28)`: the flags, then "hit" becomes "spent" | a hit sets the mine's countdown to N - 1 and, at 0, clears "spent" at once; a spent mine's countdown goes down by one a step and at 0 "spent" is cleared |

The mine is the record whose collider ([[r28 +8] +0xC], in r5 at the site: the pass's own load at 0x9B090) has flag
0x4000, as the walk tests; its countdown is the walk's index (r26, the pass's object index, less [header +0x50],
where the walk starts), for the first MINE_TABLE mines (the most a shipped course has is 26; a custom course's mines
past the table keep the game's way). The routine draws no random number and reads only the race's memory, the
countdowns and the race block's word, which is the same in every game for the whole race.

The countdowns live in a block of the game's main heap (`mines_preload`: memmap.MINES names it), so that the undo log
puts them back with the mines' flags: a step run again after a rollback reads the countdown it read the first time.
It is taken on the black frame before the start gate (the frame and test of results_preload, before it), at the top
of the heap's largest free block as results_preload takes its files (a filler below it is taken first and given
back), so everything the game allocates afterwards lies where it lies without it; only when the rule is not 0 (a
race without the rule takes nothing, and its memory is the stock game's) and the block keeps sites.py's
RESULTS_RESERVE beyond it. No room: the rule is off for that race, in every game alike (state 3). The block's first
word changes in every step that meets a mine, so the undo log watches its page from the race's first step (a page
first written later is a known open fault of the undo log); its next two count the mines that went off and
came back, which the core logs at the race's end. A recording with the rule plays as the race, as every one does.

A mine that comes back fades in, drawing only: nothing is written. The course objects'
draw (fn_1_9BBE4, r15 the record) reads the record's flags at 0x9C014 and draws the "spent" model in a spent mine's
place, and reads the record's +0x2C at 0x9C03C as the model's fade: the alpha drawn is the draw's own times (1.0 -
fade), so 0 is fully shown and 1.0 not at all (1.0 is the double at .rodata 0x4230; the word is 0 in every course
file). Both reads are patched: for a spent mine of the table whose countdown c is below W = min(N, FADE_STEPS) steps
(a countdown is read, never written) `mine_shown` gives the draw the flags without "spent", so the mine's own model is
drawn, and `mine_fade` gives it the fade c / W. So the mine fades in over the countdown's last FADE_STEPS steps (0.25
s), or over the whole wait when N is shorter (with N = 200 ms: from the step it went off), and it is fully shown
from the step it is armed again: never a mine that can be hit and is not seen. Before that, and for every other
record, both reads are the game's own. Nothing of the race reads either: the draw runs after the steps, and the
per-step pass's reads of the flags (0x9B0DC) are not these."""
import memmap

TABLE, COUNTS, BLOCK = memmap.MINE_TABLE, memmap.MINE_COUNTS, memmap.MINE_BLOCK
SIZE = (BLOCK + 31) & ~31                 # what is taken from the heap
PASSES, WENT_OFF, CAME_BACK = 0, 4, 8     # the block's counters
HEADER = memmap.REL_BSS + 0x3BE0          # the loaded course's header (lbl_1_bss_3BE0)
WALK_START = 0x50                         # header +0x50: the mine walk's first object
SITE, KNOWN, BACK = 0x9B0DC, 0x807C0000, 0x9B0E0
HIT, SPENT, MINE = 0x20000000, 0x40000000, 0x4000
FADE_STEPS = 15                           # a mine fades in over the last 0.25 s of its countdown
SHOWN_SITE, SHOWN_KNOWN = 0x9C014, 0x800F0000     # the draw: lwz r0, 0(r15), the flags (spent: the other model)
FADE_SITE, FADE_KNOWN = 0x9C03C, 0xC00F002C       # the draw: lfs f0, 0x2c(r15), the model's fade
assert COUNTS >= 12 and BLOCK == COUNTS + 2 * TABLE and SIZE <= 0x100 and TABLE * 2 + COUNTS < 0x8000


def add(k, r, patch, stock, test, la, D, middle):
    """Adds the routines to `r` and the site's entry through `patch`, as sites.add does; `test` is the mode test's
    text, `la` loads an address, `D` gives a main.rel .data address."""
    TEXT, MAIN_HEAP, HEAP_TABLE, RESERVE = k.TEXT, k.MAIN_HEAP, k.HEAP_TABLE, k.RESULTS_RESERVE
    FN = lambda offset: f"{TEXT + offset:#x}"
    FILE_NAME = D(0x1A368)                       # results_preload's allocation tag
    RULE = memmap.RACE + memmap.RACE_MINE_REGEN

    # the countdowns' block, once, on the black frame before the start gate (sites.py's frame routine)
    zero = "".join(f"    stw 0, {at}(24)\n" for at in range(0, SIZE, 4))
    r.add("mines_preload", la(12, memmap.MINES) + """    lwz 0, 0(12)
    cmpwi 0, 0
    bnelr
""" + la(11, memmap.REL_BSS) + """    lha 9, 0x960(11)
    cmpwi 9, 2
    bnelr
    lha 9, 0x3C2A(11)
    cmpwi 9, 0x27
    bnelr
    addis 9, 11, 2
    lwz 9, 0x6C28(9)
    cmpwi 9, 1
    bnelr
    li 0, 3
    stw 0, 0(12)
""" + la(11, RULE) + """    lwz 0, 0(11)
    cmpwi 0, 0
    beqlr
    stwu 1, -0x30(1)
    mflr 0
    stw 0, 0x34(1)
    stw 24, 0x10(1)
    stw 25, 0x14(1)
    stw 26, 0x18(1)
    stw 27, 0x1C(1)
    stw 31, 0x20(1)
""" + la(31, memmap.MINES) + la(9, MAIN_HEAP) + "    lwz 27, 0(9)\n" + la(9, HEAP_TABLE) + """    lwz 9, 0(9)
    mulli 0, 27, 12
    add 9, 9, 0
    lwz 9, 4(9)
    li 26, 0
mq_walk:
    cmplwi 9, 0
    beq mq_walked
    lwz 0, 8(9)
    cmplw 0, 26
    ble mq_next
    mr 26, 0
mq_next:
    lwz 9, 4(9)
    b mq_walk
mq_walked:
""" + la(0, SIZE + RESERVE + 0x60) + """    cmplw 26, 0
    blt mq_out
""" + la(0, SIZE + 0x60) + """    subf 4, 0, 26
    mr 3, 27
""" + la(5, FILE_NAME) + f"""    li 6, 0x7201
    @bl {FN(0x45D0)}
    mr. 25, 3
    beq mq_out
    mr 3, 27
    li 4, {SIZE}
""" + la(5, FILE_NAME) + f"""    li 6, 0x7202
    @bl {FN(0x45D0)}
    mr 24, 3
    mr 3, 27
    mr 4, 25
""" + la(5, FILE_NAME) + f"""    li 6, 0x7203
    @bl {FN(0x46B4)}
    cmplwi 24, 0
    beq mq_out
    li 0, 0
""" + zero + """    stw 24, 4(31)
    li 0, 1
    stw 0, 0(31)
mq_out:
    lwz 24, 0x10(1)
    lwz 25, 0x14(1)
    lwz 26, 0x18(1)
    lwz 27, 0x1C(1)
    lwz 31, 0x20(1)
    lwz 0, 0x34(1)
    mtlr 0
    addi 1, 1, 0x30
    blr
""")

    # the per-step pass's site: r28 the object, r26 its index, r5 its collider (or 0); r3 is the flags at the
    # instruction after the site, as the game loaded them (a hit already turned here has no "hit" left in them).
    # (r0 holds the flags and r3 counts: `addi` reads no r0)
    at = TEXT + SITE
    word = stock(at, KNOWN)
    r.add("mine_regen", test + """    beq mg_stock
""" + la(12, RULE) + """    lwz 0, 0(12)
    cmpwi 0, 0
    beq mg_stock
    cmplwi 5, 0
    beq mg_stock
    lwz 3, 0(5)
    andi. 3, 3, 0x4000
    beq mg_stock
""" + la(12, memmap.MINES) + """    lwz 12, 4(12)
    cmplwi 12, 0
    beq mg_stock
""" + la(11, HEADER) + f"""    lwz 11, 0(11)
    lwz 11, {WALK_START}(11)
    subf 11, 11, 26
    cmplwi 11, {TABLE}
    bge mg_stock
    lwz 3, {PASSES}(12)
    addi 3, 3, 1
    stw 3, {PASSES}(12)
    rlwinm 11, 11, 1, 0, 30
    add 11, 11, 12
    lwz 0, 0(28)
    andis. 3, 0, 0x2000
    beq mg_spent
    rlwinm 0, 0, 0, 3, 1
    oris 0, 0, 0x4000
    lwz 3, {WENT_OFF}(12)
    addi 3, 3, 1
    stw 3, {WENT_OFF}(12)
""" + la(12, RULE) + f"""    lwz 3, 0(12)
    addi 3, 3, -1
    sth 3, {COUNTS}(11)
    cmpwi 3, 0
    bne mg_store
    b mg_back
mg_spent:
    andis. 3, 0, 0x4000
    beq mg_left
    lhz 3, {COUNTS}(11)
    cmpwi 3, 0
    beq mg_left
    addi 3, 3, -1
    sth 3, {COUNTS}(11)
    cmpwi 3, 0
    bne mg_left
mg_back:
    rlwinm 0, 0, 0, 2, 0
""" + la(12, memmap.MINES) + f"""    lwz 12, 4(12)
    lwz 3, {CAME_BACK}(12)
    addi 3, 3, 1
    stw 3, {CAME_BACK}(12)
mg_store:
    stw 0, 0(28)
mg_left:
    mr 3, 0
    @b {TEXT + BACK:#x}
mg_stock:
    .long {word:#010x}
    @b {TEXT + BACK:#x}
""")
    patch(at, word, 0, "mine_regen")
    middle.append((at, ["r0", "r3", "r11", "r12", "cr0"], "mine_regen"))
    add_fade(k, r, patch, stock, test, la, middle)


def add_fade(k, r, patch, stock, test, la, middle):
    """The mine's fade-in: the two reads of the course objects' draw (the docstring's last part). The draw reaches
    0x9C03C only from 0x9C014 (its one way in, .L_9C030, is 0x9C01C's branch, and 0x9C018 falls through to it), and
    the six instructions between write r0, r3 and r20 only and read no float register: so `mine_shown` decides once,
    for both (the mode test included: `mine_fade` has none of its own), and leaves r11 (1: fading, 0: the game's
    own) and, when fading, f0 = c / W, which nothing between them reads or writes. f0, f1 and f2 are dead at
    0x9C014: 0x9C03C, 0x9C038 and 0x9C040 write them before any read. A whole number k below 2^23 is the float
    2^23 + k (its bits 0x4B000000 | k) less 2^23. No test of [header] or of [record +8] against 0: the draw of the
    course's objects reads [header] itself, and a spent record went through the per-step pass, which read [record
    +8] +0xC (0x9B088) before it made it spent. (The image is 32 KB at most: every word counts.)"""
    shown, word = k.TEXT + SHOWN_SITE, stock(k.TEXT + SHOWN_SITE, SHOWN_KNOWN)
    r.add("mine_shown", test + f"""    beq ms_stock
""" + la(12, memmap.MINES) + f"""    lwz 12, 4(12)
    cmplwi 12, 0
    beq ms_stock
    lwz 0, 0(15)
    andis. 0, 0, {SPENT >> 16:#x}
    beq ms_stock
""" + la(11, HEADER) + f"""    lwz 11, 0(11)
    lwz 3, 0x54(11)
    subf 3, 3, 15
    andi. 0, 3, 0x3F
    bne ms_stock
    rlwinm 3, 3, 26, 6, 31
    lwz 0, {WALK_START}(11)
    subf 3, 0, 3
    cmplwi 3, {TABLE}
    bge ms_stock
    lwz 11, 8(15)
    lwz 11, 0xC(11)
    cmplwi 11, 0
    beq ms_stock
    lwz 0, 0(11)
    andi. 0, 0, {MINE:#x}
    beq ms_stock
    rlwinm 3, 3, 1, 0, 30
    add 3, 3, 12
    lhz 3, {COUNTS}(3)
""" + la(12, memmap.RACE + memmap.RACE_MINE_REGEN) + f"""    lwz 11, 0(12)
    cmplwi 11, {FADE_STEPS}
    ble ms_w
    li 11, {FADE_STEPS}
ms_w:
    cmplwi 3, 0
    beq ms_stock
    cmplw 3, 11
    bge ms_stock
    stwu 1, -16(1)
    lis 0, 0x4B00
    stw 0, 4(1)
    oris 3, 3, 0x4B00
    stw 3, 8(1)
    oris 11, 11, 0x4B00
    stw 11, 12(1)
    lfs 1, 4(1)
    lfs 0, 8(1)
    fsubs 0, 0, 1
    lfs 2, 12(1)
    fsubs 2, 2, 1
    fdivs 0, 0, 2
    addi 1, 1, 16
    li 11, 1
    lwz 0, 0(15)
    rlwinm 0, 0, 0, 2, 0
    @b {shown + 4:#x}
ms_stock:
    li 11, 0
    .long {word:#010x}
    @b {shown + 4:#x}
""")
    patch(shown, word, 0, "mine_shown")
    middle.append((shown, ["r0", "r3", "r11", "r12", "cr0"], "mine_shown"))
    fade, word = k.TEXT + FADE_SITE, stock(k.TEXT + FADE_SITE, FADE_KNOWN)
    r.add("mine_fade", f"""    cmpwi 11, 0
    bne mf_fading
    .long {word:#010x}
mf_fading:
    @b {fade + 4:#x}
""")
    patch(fade, word, 0, "mine_fade")
    middle.append((fade, ["cr0"], "mine_fade"))
