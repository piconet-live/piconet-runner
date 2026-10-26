"""A rival's effects follow its drawn body (sites.py calls add() before its entry table). Native form only: the
fork's image does not have it.

The effects are records of 0xE8 bytes in two pools (190 at [.bss 0x6C848], 200 at [.bss 0x6C84C]); +0xC is the
kind, +0x18 the machine an effect belongs to (the per-machine effects copy it from the machine's own effect record,
kind 1), +0x3C its place. A kind's update (task list 11, fn_1_58694) and its draw (fn_1_58994, the draw manager's
list 22, after the frame's steps) come from tables at .data 0x1D2EC and 0x1D2EC + 0x114. Most of them place what
they draw from where the race has the machine: its position (fn_1_862D4: block +0x7C), the point behind and above
it (+0x4A0), its hover corners (+0x244 on), or a place taken from those when the effect was made, in the race's
own steps as well (sparks of a contact, fn_1_68B6C; of a wall, fn_1_694A4). A rival smooth.py draws beside that
after a rollback was parted from its sparks, boost and energy effects by the offset.

Every draw works in the view: it starts from the view matrix (the executable's mtxB, 0xE0000030, three rows of
four floats; mtxA = mtxB x T(place), 0x8006D9D8) and, for a streak, from the view the struct at [0x801A66CC]
holds at its start (its +0, the same rows). The four calls of a draw in fn_1_58994 (0x58A60, 0x58AB4: the two
pools; 0x58B70, 0x58C28: the same in its other branch; each `bctrl`, r3 the record) become a call of
`effect_draw`: for an effect of a machine drawn with an offset in this frame (its drawn transform, sites_drawn.py)
both views are moved for the call by that offset (the translation column t becomes t + R o, so a world place p is
drawn where p + o would be), then every word moved is given back as it was, unless the draw wrote it itself. So
everything the effect draws, wherever its place came from, moves with the body; its record, the machine and the
race are only read. The views are the executable's memory of the drawing (the locked cache's matrices and the
camera's struct), set again for every picture before the effects are drawn; no step runs between the move and the
giving back, and the game's thread is the only one there.

The kinds moved (FOLLOWED; the ones a machine makes, from the spawners in effect_car.c): 2 the hover corners'
sparks (a corner that strikes the road: sliding, landing, scraping; fn_1_6AF70), 5 sparks (a machine's contact,
fn_1_68B6C; a wall, fn_1_694A4; and two more of the machine's own), 12 the boost's (fn_1_6BFB8, machine flag 0x20,
which the boost and the dash plates set), 13 the low-energy smoke (fn_1_6C87C; and only when +0xB2 is 0: Sand
Ocean's course makes kind 13 for itself at fn_1_D6C10 with +0x18 0 and +0xB2 14), 14 and 17 the energy coming back
in the pit (fn_1_6D6A8, fn_1_6D488), 15 debris (fn_1_6CF3C), 16 and 37 a restore's (fn_1_6D7BC, fn_1_71D0C,
fn_1_72318), 20, 24, 27 and 39 the road's dust and water (fn_1_6DD68, fn_1_6B8A4, fn_1_6E62C, fn_1_6ED40,
fn_1_723E0), 28 the spin and side attacks (fn_1_6F288), 29, 31, 33, 34 and 35 a wreck's (fn_1_6F90C's spawners,
fn_1_6FFA0), 38 a wall hit (fn_1_694A4). Not moved: 6 and 9 (the boost's glow from the burner, fn_1_988DC, and its
sparkles), which take the drawn matrix itself (fn_1_8645C) and are with the body already; 22, which no machine
owns (+0x18 never written); 19 (Lightning's course, +0x18 0); 1 and 4 (each machine's own records: their draws are
empty); 25 (the boost's view effect for the machine the camera follows: drawn by lists 0 and 27, not here); and the
kinds nothing in main.rel makes. The burner's flames take the drawn matrix too (fn_1_9895C); the shadow has its own
site (sites_shadow.py).

Tests' counters (only in a game told to keep the logs, memmap.IMAGE_LOGS), at memmap.EFFECTS: for every draw of a
moved kind whose machine has an offset, how far the effect would have stood from its body (the offset) and how far
it does (what the move leaves: the view's translation taken back to the world, less the offset), each as the
largest square and a count in 16 classes of size (class k from 1 on: 2^(k-7) up to 2^(k-6) units; 0 below
1/64, 15 from 256 up); the draws moved and the draws left, and a count a kind. Control block +456 bit 8 (tests):
nothing is moved, and the counters then say how far the effects were drawn from their bodies before."""
import memmap

FOLLOWED = (2, 5, 12, 13, 14, 15, 16, 17, 20, 24, 27, 28, 29, 31, 33, 34, 35, 37, 38, 39)
KINDS = 40                          # the kinds the tables hold before their filler
LOW_ENERGY, COURSE_MARK = 13, 0xB2  # kind 13 is a machine's only when its +0xB2 is 0
VIEW = 0xE0000030                   # the executable's mtxB
CAMERA_STRUCT = 0x801A66CC          # the executable's pointer to the camera's struct: a view matrix at its +0
STAY = 8                            # control block +456: the effects stay where the game draws them (tests)
# EFFECTS + : the counters (words, floats where said)
MOVED, LARGEST_BEFORE, LARGEST_AFTER, LEFT, BEFORE, AFTER, BY_KIND = 0, 4, 8, 12, 16, 80, 144
CLASSES = 16
assert AFTER == BEFORE + 4 * CLASSES and BY_KIND == AFTER + 4 * CLASSES and BY_KIND + 4 * KINDS <= 0x400


def masks():
    """The followed kinds as two words, kind k at bit 0x80000000 >> (k mod 32)."""
    low = sum(0x80000000 >> k for k in FOLLOWED if k < 32)
    high = sum(0x80000000 >> (k - 32) for k in FOLLOWED if k >= 32)
    return low, high


def add(k, r, patch, stock, test, la, middle):
    """Adds `effect_draw`, a routine for each site, and their entries through `patch`, as sites.add does; `test` is
    the mode test's text, `la` loads an address. A site's routine does the replaced call in a plain start and
    `effect_draw` in a race of ours."""
    low, high = masks()
    C = k.C
    S = memmap.EFFECTS
    shift = "".join(f"""    lfs 0, {16 * i}(11)
    fmuls 0, 0, 4
    lfs 1, {16 * i + 4}(11)
    fmadds 0, 1, 5, 0
    lfs 1, {16 * i + 8}(11)
    fmadds 0, 1, 6, 0
    lwz 0, {16 * i + 12}(11)
    stw 0, {4 * i}(12)
    lfs 1, {16 * i + 12}(11)
    fadds 1, 1, 0
    stfs 1, {16 * i + 12}(11)
    stfs 1, {12 + 4 * i}(12)
""" for i in range(3))
    back = "".join(f"""    lwz 0, {16 * i + 12}(11)
    lwz 3, {12 + 4 * i}(12)
    cmpw 0, 3
    bne ep_{i}
    lwz 0, {4 * i}(12)
    stw 0, {16 * i + 12}(11)
ep_{i}:
""" for i in range(3))
    # what the move leaves, in the world: R^T (t' - t) less the offset (R and t from mtxB, as moved in slot A)
    left = "".join(f"""    lfs 0, {36 + 4 * j}(1)
    lfs 1, {24 + 4 * j}(1)
    fsubs {7 + j}, 0, 1
""" for j in range(3)) + "".join(f"""    lfs 0, {j * 4}(12)
    fmuls 0, 0, 7
    lfs 1, {16 + j * 4}(12)
    fmadds 0, 1, 8, 0
    lfs 1, {32 + j * 4}(12)
    fmadds 0, 1, 9, 0
    fsubs {10 + j}, 0, {4 + j}
""" for j in range(3))
    count = lambda at: f"""    lwz 12, {at}(11)
    addi 12, 12, 1
    stw 12, {at}(11)
"""
    classed = lambda freg, largest, table: f"""    lfs 1, {largest}(11)
    fcmpu 0, {freg}, 1
    ble ec_{table}
    stfs {freg}, {largest}(11)
ec_{table}:
    stfs {freg}, 20(1)
    lwz 9, 20(1)
    rlwinm 9, 9, 9, 23, 31
    addi 9, 9, -127
    srawi 9, 9, 1
    addi 9, 9, 7
    cmpwi 9, 0
    bge ec_{table}_low
    li 9, 0
ec_{table}_low:
    cmpwi 9, {CLASSES - 1}
    ble ec_{table}_high
    li 9, {CLASSES - 1}
ec_{table}_high:
    slwi 9, 9, 2
    addi 9, 9, {table}
    lwzx 12, 11, 9
    addi 12, 12, 1
    stwx 12, 11, 9
"""
    r.add("effect_draw", f"""    stwu 1, -112(1)
    mflr 0
    stw 0, 116(1)
    li 0, 0
    stw 0, 48(1)
    stw 0, 76(1)
    lha 12, 0xC(3)
    cmplwi 12, {KINDS}
    bge ed_call
    cmplwi 12, 32
    blt ed_low
    addi 0, 12, -32
    lis 11, {high >> 16:#x}
    ori 11, 11, {high & 0xFFFF:#x}
    b ed_bit
ed_low:
    mr 0, 12
    lis 11, {low >> 16:#x}
    ori 11, 11, {low & 0xFFFF:#x}
ed_bit:
    slw 11, 11, 0
    cmpwi 11, 0
    bge ed_call
    cmpwi 12, {LOW_ENERGY}
    bne ed_owner
    lhz 0, {COURSE_MARK:#x}(3)
    cmpwi 0, 0
    bne ed_call
ed_owner:
    stw 9, 12(1)
    stw 10, 16(1)
    lha 9, 0x18(3)
    bl drawn_entry
    cmpwi 10, 0
    beq ed_back
    lfs 4, 12(10)
    lfs 5, 16(10)
    lfs 6, 20(10)
    lis 11, {C}
    lwz 0, 456(11)
    andi. 0, 0, {STAY}
    bne ed_logs
    lis 11, {memmap.hi(VIEW) - 0x10000}
    ori 11, 11, {memmap.lo(VIEW)}
    addi 12, 1, 24
    bl ed_shift
    lis 11, {memmap.hi(CAMERA_STRUCT) - 0x10000}
    lwz 11, {memmap.lo(CAMERA_STRUCT)}(11)
    cmpwi 11, 0
    beq ed_logs
    addi 12, 1, 52
    bl ed_shift
ed_logs:
    lis 11, {C}
    lwz 0, {memmap.IMAGE_LOGS - memmap.CONTROL}(11)
    cmpwi 0, 0
    beq ed_back
    fmuls 2, 4, 4
    fmadds 2, 5, 5, 2
    fmadds 2, 6, 6, 2
    fmr 3, 2
    lwz 0, 48(1)
    cmpwi 0, 0
    beq ed_count
    mr 12, 0
{left}    fmuls 3, 10, 10
    fmadds 3, 11, 11, 3
    fmadds 3, 12, 12, 3
ed_count:
""" + la(11, S) + f"""    lwz 0, 48(1)
    cmpwi 0, 0
    beq ed_left
{count(MOVED)}    b ed_classes
ed_left:
{count(LEFT)}ed_classes:
    lha 9, 0xC(3)
    slwi 9, 9, 2
    addi 9, 9, {BY_KIND}
    lwzx 12, 11, 9
    addi 12, 12, 1
    stwx 12, 11, 9
{classed(2, LARGEST_BEFORE, BEFORE)}{classed(3, LARGEST_AFTER, AFTER)}ed_back:
    lwz 9, 12(1)
    lwz 10, 16(1)
ed_call:
    bctrl
    addi 12, 1, 24
    bl ed_put_back
    addi 12, 1, 52
    bl ed_put_back
    lwz 0, 116(1)
    mtlr 0
    addi 1, 1, 112
    blr
ed_shift:
{shift}    stw 11, 24(12)
    blr
ed_put_back:
    lwz 11, 24(12)
    cmpwi 11, 0
    beqlr
{back}    blr
""")
    for offset in k.EFFECT_DRAWS:
        site, name = k.TEXT + offset, f"effect_draw_{offset:x}"
        word = stock(site, k.EFFECT_DRAW_WORD)
        r.add(name, test + f"""    bne {name}_on
    .long {word:#010x}
    @b {site + 4:#x}
{name}_on:
    bl effect_draw
    @b {site + 4:#x}
""")
        patch(site, word, 0, name)
        middle.append((site, ["r0", "r3", "r11", "r12", "cr0", "lr"], name))
