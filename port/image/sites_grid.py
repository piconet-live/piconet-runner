"""The start grid (Online.Server StartGrid): the word of the patch list that makes the
game's grid the coordinator's, from the race block (memmap.RACE_GRID_ON, RACE_GRID), which the core writes from the
match start's `setup.grid`. Native form only: the fork's image does not have it. sites.py calls add() after
sites_seats.add().

The grid is decided in fn_1_85934 (called at the race's start by fn_1_8616C and fn_1_86194): it fills a list of
halfwords on its stack (sp + 8) with 0..n-1 (outside the replay mode; in it, the lineup's grid bytes), sorts it with
the executable's qsort (0x8008279C) and the comparator fn_1_858A8, and then places machine list[k] in slot k (the
loop at 0x85B88: fn_1_85C58 for the slot, fn_1_85F70 for the machine; the rank tables 0x6D820 +0x21C, +0x23C from
the list). The comparator: in a Grand Prix's later races (its race number, race block +0xA7, above 0) the previous
race's rank, the worse first; otherwise the machines' pad ports (+0x474), the lower first, so the computer racers
(port -1) start ahead of seat 0, the only seat with a port at the race's start in a race of ours: before this site
seat 0 started last and seats 1 to 29 ahead of it in seat order (every recording's starting ranks, its lineup).
(Race block +0xE[30] is not the grid: it is the machines by rank, written again by the ranking every frame.)

| Site | Where | What the game did | Now, in a race or replay of ours with a grid |
|---|---|---|---|
| `grid_sort` | fn_1_85934 0x85B1C | `bl 0x8008279C`: qsort(sp + 8, n, 2, fn_1_858A8) | the list is the race block's grid, slot by slot (list[k] = RACE_GRID[k]); no sort |
| `grid_place` | fn_1_85934 0x85BF4 | `mr r3, r28`: slot k for fn_1_85C58 | under `start_gap 0` (the mode window's flag SAME_SPOT, written at the boot): slot 0 for every k, so every machine takes slot 0's place. fn_1_85C58 only computes a matrix from its slot (a course's row and column), and fn_1_23500 places the machine from it |
| `grid_spread` | fn_1_85934 0x85C0C | `bl fn_1_85F70`: the machine placed on slot k's matrix (fn_1_23500) | under `start_gap` above 0 or `even` (the window's flag SPREAD, the gap word at MODE_HUD + 0x290): the game's placing, then the story's own mid-course placement (story action 13, story_mode.c) at lap distance d0 - k x gap (gap at most the lap over the machines) and lane k mod 3, below |

`grid_place` is the whole of `start_gap 0`'s placing: the list and the tables 0x6D820 +0x21C, +0x23C are written from
k as ever. They do not last, in any race: fn_1_8B880 at 0x85C2C writes both tables 0..n-1 and ranks by course distance,
ties keeping that order, so racers on one spot start ranked by seat, not by the list, until they first differ.

`grid_spread` moves each machine after the game has placed it, with the routines the story's action 13 runs for a
racer put at a course distance (fn_1_214C4, fn_1_17920, fn_1_207DC, fn_1_85F90): d0 is the lap distance of slot 0's
point (0, 0, 19), which fn_1_85C58 projects with fn_1_20994 (its base 19.0 in every race of ours; the other rows of its
table are story courses), as the progress pass computes a distance (segment +0x40 + t x (+0x44 - +0x40)); d = d0 -
k x gap: every machine behind slot 0, going back from the line, and the whole spread within one lap, since the gap
is at most the lap length [header +0x90] over the machines (0x6D820 +0x2A), `even`'s gap: a larger one is taken as that
at the grid (the compiler cannot know a lap's length). fn_1_214C4 turns d into a checkpoint and fraction (it wraps a d
below 0, on a lap too short for the machines' 19 m, to the lap's end, still behind the line); the lane is k mod 3: -0.25, 0,
+0.25 of the road's width (fn_1_207DC's offset is a fraction of the frame's width +0x68, on a cylinder or pipe round
it); fn_1_85F90 then finds the spot's checkpoint, puts the machine on the floor through the restore's spot finder
fn_1_892FC and calls fn_1_23500, whose fn_1_22F90 (M, 1) clears the track caches M+0x488..+0x49C and the progress
record (P+0x11C = -1, P+0x118 = 0, as on the grid). That first progress pass finds its checkpoint with fn_1_20994, the
first checkpoint from the line whose slab holds the point: right on the grid, but away from the line often a stretch
beside it (Drift Highway, game test 2026-11-06: 17 of 30 seats got the start straight's, distance = -z). The in-race
search then found the right one two steps on, a forward jump the skip limit (fn_1_8A1B4 0x8A398, gain above 1000 m)
destroyed four machines for, and a CPU turned round. So the routine then seeds the record as the restore does,
from the spot's own checkpoint: P+0x140 its checkpoint, +0x144 its fraction, +0x148 the machine's position M+0x7C
(what fn_1_85F90 and fn_1_892FC write before the clear), fn_1_23640 (P, 0), the light reset (clears P+0..+0x116 only:
the laps and the spot stay; P+0x80 = 0 and the branch's checkpoint -1, so the pass seeds from P+0x140, 0x8A2DC), and
fn_1_8A190 (M, 1), the pass in its seeding form (no skip test). Then the flags as story action 13 leaves them: those
after the game's placing, with the move's 0x400. The gap word 0.0 is `even`. Every machine so starts as a grid
machine does, behind the line: P+0x11C -1, P+0x118 0, P+0xF8 = its lap distance less a lap, so its first crossing of
the line is its start and counts no lap. Nothing on a course without
laps (header +0x7D) or without a course header: those keep the game's grid. The machine and its start speed come in
r3 and f1 (the loop's r27 and f30), k is the loop's r28 (kept across the calls by every callee); the routine keeps its
values in a frame of its own and uses r0, r3, r4, r11, r12, f0 to f4 and cr0 beyond the calls, all dead after a call
(r3 and r4 as after the replaced call: undefined, and fn_1_8B880, the next call, takes no argument), f5.

The site is a call: r3 the list, r4 the count n (the machines, 0x6D820 +0x2A). The routine uses r0, r3 (walked
along the list), r11 and r12, which the code after the call writes before it reads (liveness.py proves it). A race
block without a grid (its word 0: a practice, a match file, every recording made before the grid), a count above 30,
or a grid entry that is no machine of the race (n or above; the core refuses such a grid before) takes the game's
own sort: those races start as they did, byte for byte. Nothing here is written in a race but the list, which the
game's own code fills at the same moment: the grid is placed once, at the race's start, from the host's block."""
import memmap

SITE, QSORT = 0x85B1C, 0x8008279C
PLACE, PLACE_KNOWN = 0x85BF4, 0x7F83E378          # fn_1_85934: mr r3, r28, the slot for fn_1_85C58
SPREAD = 0x85C0C                                   # fn_1_85934: bl fn_1_85F70, the machine placed (fn_1_23500)
PLACE_CAR, SLOT_POINT, AT_DISTANCE = 0x85F70, 0x20994, 0x214C4     # fn_1_85F70; fn_1_20994; fn_1_214C4
COURSE_FRAME, WORK, ON_ROAD, PUT_RACER = 0x17920, 0x14F04, 0x207DC, 0x85F90   # fn_1_17920, fn_1_14F04, fn_1_207DC, fn_1_85F90
LIGHT_RESET, SEED = 0x23640, 0x8A190              # fn_1_23640 (P, 0): the progress record but its laps and spot; fn_1_8A190 (M, 1)
PROGRESS, SPOT = 0x49C, 0x140                      # M+0x49C the progress record P; P+0x140 checkpoint, +0x144 fraction, +0x148 position
HEADER = memmap.REL_BSS + 0x3BE0                   # the loaded course's header (lbl_1_bss_3BE0)
MACHINES = memmap.REL_BSS + 0x6D820 + 0x2A         # the race's machines (a byte), fn_1_85934's count
SLOT_0_Z = 0x41980000                              # 19.0: fn_1_85C58's base, slot 0's point (0, 0, 19)
LANES = (0xBE800000, 0, 0x3E800000)                # -0.25, 0, +0.25 of the road's width, by k mod 3
TWO_23 = 0x4B000000                                # 2^23: a whole number as a float, by its bits
SEATS = 30
assert memmap.RACE_GRID == memmap.RACE_GRID_ON + 4


def add(k, r, patch, stock, test, middle, complete):
    """Adds the routine to `r` and the site's entry through `patch`, as sites.add does; `test` is the mode test's
    text."""
    at = k.TEXT + SITE
    word = stock(at, k.branch(at, QSORT, True))
    if complete:
        assert k.branch_target(word, at) == QSORT, f"{at:08X}"
    grid_on = memmap.RACE + memmap.RACE_GRID_ON
    r.add("grid_sort", test + f"""    beq grid_stock
    cmplwi 4, {SEATS}
    bgt grid_stock
    lis 12, {memmap.hi(grid_on) - 0x10000}
    ori 12, 12, {memmap.lo(grid_on)}
    lwz 11, 0(12)
    cmpwi 11, 0
    beq grid_stock
    addi 12, 12, 4
    li 11, 0
grid_check:
    cmpw 11, 4
    bge grid_checked
    lbzx 0, 12, 11
    cmpw 0, 4
    bge grid_stock
    addi 11, 11, 1
    b grid_check
grid_checked:
    li 11, 0
grid_copy:
    cmpw 11, 4
    bge grid_done
    lbzx 0, 12, 11
    sth 0, 0(3)
    addi 3, 3, 2
    addi 11, 11, 1
    b grid_copy
grid_done:
    @b {at + 4:#x}
grid_stock:
    @bl {QSORT:#x}
    @b {at + 4:#x}
""")
    patch(at, word, 0, "grid_sort")
    middle.append((at, ["r0", "r3", "r11", "r12", "cr0"], "grid_sort"))

    at = k.TEXT + PLACE
    word = stock(at, PLACE_KNOWN)
    flags = memmap.MODE_HUD_FLAGS
    r.add("grid_place", test + f"""    beq place_stock
    lis 12, {memmap.hi(flags) - 0x10000}
    ori 12, 12, {memmap.lo(flags)}
    lwz 12, 0(12)
    andi. 12, 12, {memmap.MODE_SAME_SPOT}
    beq place_stock
    li 3, 0
    @b {at + 4:#x}
place_stock:
    .long {word:#010x}
    @b {at + 4:#x}
""")
    patch(at, word, 0, "grid_place")
    middle.append((at, ["r12", "cr0"], "grid_place"))

    at = k.TEXT + SPREAD
    word = stock(at, k.branch(at, k.TEXT + PLACE_CAR, True))
    if complete:
        assert k.branch_target(word, at) == k.TEXT + PLACE_CAR, f"{at:08X}"
    FN = lambda offset: f"{k.TEXT + offset:#x}"
    la = lambda reg, address: (f"    lis {reg}, {memmap.hi(address) - (0x10000 if address & 0x80000000 else 0)}\n"
                               f"    ori {reg}, {reg}, {memmap.lo(address)}\n")
    lanes = "".join(f"    cmpwi 11, {i}\n    bne spread_lane{i}\n    lis 0, {(v >> 16) - (0x10000 if v & 0x80000000 else 0)}\nspread_lane{i}:\n" for i, v in enumerate(LANES))
    r.add("grid_spread", test + f"""    beq spread_stock
""" + la(12, memmap.MODE_HUD_FLAGS) + f"""    lwz 12, 0(12)
    andi. 12, 12, {memmap.MODE_SPREAD}
    beq spread_stock
""" + la(12, HEADER) + f"""    lwz 12, 0(12)
    cmplwi 12, 0
    beq spread_stock
    lbz 0, 0x7D(12)
    cmpwi 0, 0
    bne spread_stock
    lwz 0, 8(12)
    cmpwi 0, 0
    ble spread_stock
    stwu 1, -0x40(1)
    stw 3, 0x20(1)
    stfs 1, 0x24(1)
    @bl {FN(PLACE_CAR)}
    lwz 12, 0x20(1)
    lwz 0, 0(12)
    stw 0, 0x38(1)
    li 0, 0
    stw 0, 0x10(1)
    stw 0, 0x14(1)
    lis 0, {SLOT_0_Z >> 16:#x}
    stw 0, 0x18(1)
    addi 3, 1, 0x10
    addi 4, 1, 0x1C
    @bl {FN(SLOT_POINT)}
    stw 3, 0x28(1)
""" + la(12, HEADER) + f"""    lwz 12, 0(12)
    lwz 11, 0xC(12)
    mulli 0, 3, 12
    add 11, 11, 0
    lwz 11, 4(11)
    lfs 2, 0x40(11)
    lfs 0, 0x44(11)
    fsubs 0, 0, 2
    lfs 1, 0x1C(1)
    fmuls 0, 1, 0
    fadds 1, 2, 0
    lis 0, {TWO_23 >> 16:#x}
    stw 0, 0x30(1)
    lwz 11, 0x90(12)
    lfs 5, 0(11)
""" + la(11, MACHINES) + f"""    lbz 0, 0(11)
    oris 0, 0, {TWO_23 >> 16:#x}
    stw 0, 0x34(1)
    lfs 4, 0x34(1)
    lfs 0, 0x30(1)
    fsubs 4, 4, 0
    fdivs 5, 5, 4
""" + la(11, memmap.MODE_HUD_START_GAP) + f"""    lfs 3, 0(11)
    lwz 0, 0(11)
    cmpwi 0, 0
    beq spread_even
    fcmpu 0, 3, 5
    ble spread_gap
spread_even:
    fmr 3, 5
spread_gap:
    oris 0, 28, {TWO_23 >> 16:#x}
    stw 0, 0x34(1)
    lfs 4, 0x34(1)
    lfs 0, 0x30(1)
    fsubs 4, 4, 0
    fmuls 4, 4, 3
    fsubs 1, 1, 4
    addi 3, 1, 0x1C
    lwz 4, 0x28(1)
    @bl {FN(AT_DISTANCE)}
    stw 3, 0x28(1)
    lfs 1, 0x1C(1)
    li 4, 0
    @bl {FN(COURSE_FRAME)}
    @bl {FN(WORK)}
    addi 3, 3, 0x400
    mr 11, 28
spread_mod:
    cmpwi 11, 3
    blt spread_modded
    addi 11, 11, -3
    b spread_mod
spread_modded:
    li 0, 0
""" + lanes + f"""    stw 0, 0x2C(1)
    lfs 1, 0x2C(1)
    addi 4, 1, 0x10
    @bl {FN(ON_ROAD)}
    lwz 3, 0x20(1)
    lwz 4, 0x28(1)
    lfs 1, 0x24(1)
    lfs 2, 0x10(1)
    lfs 3, 0x14(1)
    lfs 4, 0x18(1)
    @bl {FN(PUT_RACER)}
    lwz 12, 0x20(1)
    lwz 3, {PROGRESS:#x}(12)
    cmpwi 3, 0
    beq spread_flags
    lwz 0, 0x28(1)
    stw 0, {SPOT:#x}(3)
    lwz 0, 0x1C(1)
    stw 0, {SPOT + 4:#x}(3)
    lwz 0, 0x7C(12)
    stw 0, {SPOT + 8:#x}(3)
    lwz 0, 0x80(12)
    stw 0, {SPOT + 12:#x}(3)
    lwz 0, 0x84(12)
    stw 0, {SPOT + 16:#x}(3)
    li 4, 0
    @bl {FN(LIGHT_RESET)}
    lwz 3, 0x20(1)
    li 4, 1
    @bl {FN(SEED)}
spread_flags:
    lwz 12, 0x20(1)
    lwz 0, 0(12)
    rlwinm 0, 0, 0, 21, 21
    lwz 11, 0x38(1)
    or 0, 0, 11
    stw 0, 0(12)
    addi 1, 1, 0x40
    @b {at + 4:#x}
spread_stock:
    @bl {FN(PLACE_CAR)}
    @b {at + 4:#x}
""")
    patch(at, word, 0, "grid_spread")
    # (r3 and r4 are left out: the replaced call leaves them undefined as well, and the one reader the proof sees is
    # the call fn_1_8B880 at 0x85C2C, which takes no argument: it sets its own r3 for fn_1_8B8A4)
    middle.append((at, ["r0", "r11", "r12", "f0", "f1", "f2", "f3", "f4", "f5", "cr0", "lr"], "grid_spread"))
