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
| `grid_place` | fn_1_85934 0x85BF4 | `mr r3, r28`: slot k for fn_1_85C58 | under `start_together` (the mode window's flag TOGETHER, written at the boot): slot 0 for every k, so every machine takes slot 0's place. fn_1_85C58 only computes a matrix from its slot (a course's row and column), and fn_1_23500 places the machine from it |

`grid_place` is the whole of the word's placing: the list and the tables 0x6D820 +0x21C, +0x23C are written from k as
ever. They do not last, in any race: fn_1_8B880 at 0x85C2C writes both tables 0..n-1 and ranks by course distance, ties
keeping that order, so racers on one spot start ranked by seat, not by the list, until they first differ.

The site is a call: r3 the list, r4 the count n (the machines, 0x6D820 +0x2A). The routine uses r0, r3 (walked
along the list), r11 and r12, which the code after the call writes before it reads (liveness.py proves it). A race
block without a grid (its word 0: a practice, a match file, every recording made before the grid), a count above 30,
or a grid entry that is no machine of the race (n or above; the core refuses such a grid before) takes the game's
own sort: those races start as they did, byte for byte. Nothing here is written in a race but the list, which the
game's own code fills at the same moment: the grid is placed once, at the race's start, from the host's block."""
import memmap

SITE, QSORT = 0x85B1C, 0x8008279C
PLACE, PLACE_KNOWN = 0x85BF4, 0x7F83E378          # fn_1_85934: mr r3, r28, the slot for fn_1_85C58
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
    andi. 12, 12, {memmap.MODE_TOGETHER}
    beq place_stock
    li 3, 0
    @b {at + 4:#x}
place_stock:
    .long {word:#010x}
    @b {at + 4:#x}
""")
    patch(at, word, 0, "grid_place")
    middle.append((at, ["r12", "cr0"], "grid_place"))
