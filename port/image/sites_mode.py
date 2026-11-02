"""The game mode's sites (piconet-docs research/game-modes/: the design, its vocabulary; the race map's hook table):
the mode's block, the mode point, the speed's hold and a seat's own Restore. With no mode in the race (memmap.MODES
+4 is 0: a match start without one, every recording made before modes) every site does what the game does, and
nothing is taken from the heap. Native form only: the fork's image does not have it. sites.py calls add().

| Site | Where | What the game did | Now, with a mode |
|---|---|---|---|
| `mode_preload` | the frame routine, on the black frame before the start gate (sites.py's `frame`, with mines_preload) | — | the mode's block (memmap.MODE_BLOCK bytes on whole pages of 4 KiB, zeroed: MODE_ALLOC taken) from the top of the main heap's largest free block, as mines_preload takes its block, when the host asked for one (MODES +0 = 4). Only the block: the machines do not exist yet, so what the mode says about them waits for `mode_begin` |
| `mode_begin` | fn_1_34924's entry, the countdown task (`stwu r1, -0x20(r1)`) | — | while the block's run count is 0, the host call HOST_MODE: `game_start`, the mode's first pass. fn_1_395CC makes the machines and registers this task at the frame's start, so its first entry is in the step before the first picture with machines in it, before the count's holds and before any machine task has run: every setup word is in force for the first picture and the first contact pass. A task of flag 1: every step run again runs it, and the block's count, which the undo log puts back with the block, says whether it has run |
| `mode_point` | fn_1_850C4 0x8548C, after the race pass's callbacks (`lwz r3, 0x15C(r31)`, r31 the car manager) | — | the host call HOST_MODE (the interpreter, port/core/src/mode.cpp: the rules run, their actions go into the block and the machines), then for each seat the game's own damage where the run asked for it (`damage`: fn_1_24F10, which calls nothing, so it draws no generator and plays no sound), its explosion, and for every seat whose speed is held this step the hold's rescale and the read-out M+0x17C, as the step's start computes it (the vocabulary's "rescale R") |
| `mode_rolls` | fn_1_34924 0x34988, the count after its decrement (`lhz r0, 0(r29)`) | the machines' tasks wait for the count below 240 | under `rolling_start` (window flag ROLLING) the mask is lifted from the count's first step: the machines and the mode run through the whole intro; GO's step and the count are the game's |
| `mode_monitors` | fn_1_1049E0 0x104BFC, the start monitors' placing (`li r4, 0`) | keeps the first step's turn | a rolling start: 1, the machine's turn every step |
| `mode_speed` | fn_1_236CC 0x23854, after the air drag (`bl fn_1_294D0`) and before the position moves by P / W (`lfs f1, 0x14(r31)`, r30 the machine) | — | for a seat whose speed is held: P scaled to the held speed, keeping its direction (rebuilt along the nose when the drag left it 0), and the base speed M+0x224 the same (the vocabulary's "site L") |
| `respawns` | fn_1_32600 0x32628, the game's verdict (`rlwinm. r0, r0, 0, 16, 16`: the rule word's Restore bit, r30 the machine) | Restore when the rule says so | Restore when the rule says so or the machine has flag 0x1 (its own Restore: a mode's `respawns`, story chapter 8's), as the image's verdict text says (routines/outtest.py). Not gated on a mode: flag 0x1 on an unfinished machine occurs only under the rule, which already exempts it (the open words' note, section 1) |

A seat's record in the block (memmap.MODE_SEAT bytes at +16 x seat, written by the host at every mode point): +0
flags (1 pedals locked, 2 boost off, 4 speed held this step), +4 the accelerator and +8 the brake the pedals hold
(floats; the stand-in routine applies them, routines/standin_inputs.py), +12 the held speed in km/h (a float, 0 to
9,000). A seat frozen (0x8000), broken down (0x80), out of the course (0x800), finished (0x10000), retired
(0x08000000), destroyed (0x40000000) or restoring (M+0x214 not 0) is never held: a broken machine held at speed would
never come to rest, and the out verdict at rest would never come.

The hold, `mode_hold` (r3 the machine, r4 its record, r5 not 0: and the read-out): d = v / 216.0 in double (v as the
float, exact in double; 1 m a step = 216 km/h), the base speed M+0x224 = (float) d, T = (float)(d x W) with W =
M+0x8 (the weight the step uses), s2 = Px Px + Py Py + Pz Pz as the game sums it (fmuls, fmadds, fmadds); P x= T /
sqrt(s2) with the game's own square root (main.dol 0x8006D0B4, as fn_1_294D0 and fn_1_289B4 call it: the same bits
on every host), or, when s2 is 0, P = -T x (M+0xF4, M+0x104, M+0x114), the nose (the orientation's column 2). The
read-out is fn_1_289B4's: M+0x17C = (float)(216.0 x (float)(sqrt(s2) / W)). Every word it writes is the machine's
(SIM, in the checksum); it reads only the machine and its record, so the per-machine rollback compares the record
with the machine (port/moderngekko/src/lanes_partial.cpp).

Registers: the routines save every register they use but r12 and cr0 (the mode test), which are proven dead after
each site (build.py, from the site's next word); the floating-point registers they use are the volatile ones, saved
too. The verdict's `respawns` writes r0 (the original's) and r3: r3 is written again at 0x32630 and 0x326D8 before
any read."""
import memmap

MODES = memmap.MODES
SEAT, SEATS = memmap.MODE_SEAT, memmap.MODE_SEATS
PEDALS, BOOST_OFF, HELD, BOOST, EXPLODE, HAND, ROLL, DAMAGE = 1, 2, 4, 8, 16, 32, 64, 128   # a seat record's flags
WANTED, TAKEN, NONE = 4, 1, 3                     # MODES +0: the host asks; the block taken; none
WANTED_STATS = 5                                  # the host asks, with the stats page (memmap.MODE_STATS_PAGE more)
BEGIN, BEGIN_KNOWN = 0x34924, 0x9421FFE0          # fn_1_34924's first word: stwu r1, -0x20(r1)
ROLLS, ROLLS_KNOWN = 0x34988, 0xA01D0000          # fn_1_34924: lhz r0, 0(r29), the count after its decrement
MONITORS_TURN, MONITORS_TURN_KNOWN = 0x104BFC, 0x38800000   # fn_1_1049E0: li r4, 0, the stock fn_1_104730 call's "oriented"
TASK_MASK = 0x4310                                # fn_1_4310(mask)
POINT, POINT_KNOWN = 0x8548C, 0x807F015C          # fn_1_850C4: lwz r3, 0x15c(r31)
SPEED, SPEED_KNOWN = 0x23854, 0xC03F0014          # fn_1_236CC: lfs f1, 0x14(r31)
VERDICT, VERDICT_KNOWN = 0x32628, 0x54000421      # fn_1_32600: rlwinm. r0, r0, 0, 16, 16
PAIR, PAIR_CALL = 0x8514C, 0x239C4                # fn_1_850C4: bl fn_1_239C4 (a pair's contact; r28 i, r27 j)
PAIR_KNOWN = 0x4BF9E879
TIMER, TIMER_SKIP = 0x3A36C, 0x3A448              # fn_1_3A330: beq on the rule word's timed bit (cr0)
TIMER_KNOWN = 0x418200DC                          # beq +0xDC
DESTROY = 0x25004                                 # fn_1_25004: the game's explosion of a machine (r3)
HURT = 0x24F10                                    # fn_1_24F10: the game's damage (r3 the machine, f1 the amount; calls nothing)
SQRT = 0x8006D0B4                                 # main.dol: the game's square root (f1 -> f1)
NOT_HELD = 0x8000 | 0x80 | 0x800 | 0x10000 | 0x08000000 | 0x40000000
SEATS_ROWS = 30                                   # the block's no-contact rows
RESTORING = 0x214                                 # M+0x214: the restore's countdown (u16)
assert SEATS + 30 * SEAT <= memmap.MODE_END and memmap.MODE_END + 8 <= memmap.MODE_BLOCK
assert memmap.MACHINE_FLAG_RESTORE == 1

# the volatile registers the hold's call to the game's square root may change, kept on the routines' stack
SAVE_GPR = "    stmw 3, 0x10(1)\n"           # r3 to r31: 29 words, 0x10 to 0x84
LOAD_GPR = "    lmw 3, 0x10(1)\n"
SAVE_FPR = "".join(f"    stfd {n}, {0x88 + 8 * n:#x}(1)\n" for n in range(14))   # f0 to f13: 0x88 to 0xF8
LOAD_FPR = SAVE_FPR.replace("stfd", "lfd")
FRAME = 0x100


def add(k, r, patch, stock, test, la, D, middle):
    """Adds the routines to `r` and the sites' entries through `patch`, as sites.add does; `test` is the mode test's
    text, `la` loads an address, `D` gives a main.rel .data address."""
    TEXT, MAIN_HEAP, HEAP_TABLE, RESERVE = k.TEXT, k.MAIN_HEAP, k.HEAP_TABLE, k.RESULTS_RESERVE
    FN = lambda offset: f"{TEXT + offset:#x}"
    SIZE, ALLOC = memmap.MODE_BLOCK, memmap.MODE_ALLOC
    FILE_NAME = D(0x1A368)                       # results_preload's allocation tag

    # the block, once, on the black frame before the start gate, when the host asked for one (its state 4); the
    # frame and the test as mines_preload's. No room: state 3, and the host's interpreter says it (a mode error)
    r.add("mode_preload", la(12, MODES) + f"""    lwz 0, 0(12)
    li 10, 0
    cmpwi 0, {WANTED}
    beq mpre_asked
    li 10, {memmap.MODE_STATS_PAGE}
    cmpwi 0, {WANTED_STATS}
    bnelr
mpre_asked:
""" + la(11, memmap.REL_BSS) + f"""    lha 9, 0x960(11)
    cmpwi 9, 2
    bnelr
    lha 9, 0x3C2A(11)
    cmpwi 9, 0x27
    bnelr
    addis 9, 11, 2
    lwz 9, 0x6C28(9)
    cmpwi 9, 1
    bnelr
    li 0, {NONE}
    stw 0, 0(12)
    stwu 1, -0x30(1)
    mflr 0
    stw 0, 0x34(1)
    stw 24, 0x10(1)
    stw 25, 0x14(1)
    stw 26, 0x18(1)
    stw 27, 0x1C(1)
    stw 31, 0x20(1)
    stw 30, 0x24(1)
    mr 30, 10
""" + la(31, MODES) + la(9, MAIN_HEAP) + "    lwz 27, 0(9)\n" + la(9, HEAP_TABLE) + """    lwz 9, 0(9)
    mulli 0, 27, 12
    add 9, 9, 0
    lwz 9, 4(9)
    li 26, 0
mpre_walk:
    cmplwi 9, 0
    beq mpre_walked
    lwz 0, 8(9)
    cmplw 0, 26
    ble mpre_next
    mr 26, 0
mpre_next:
    lwz 9, 4(9)
    b mpre_walk
mpre_walked:
""" + la(0, ALLOC + RESERVE + 0x60) + """    add 0, 0, 30
    cmplw 26, 0
    blt mpre_out
""" + la(0, ALLOC + 0x60) + """    add 0, 0, 30
    subf 4, 0, 26
    mr 3, 27
""" + la(5, FILE_NAME) + f"""    li 6, 0x7211
    @bl {FN(0x45D0)}
    mr. 25, 3
    beq mpre_out
    mr 3, 27
""" + la(4, ALLOC) + "    add 4, 4, 30\n" + la(5, FILE_NAME) + f"""    li 6, 0x7212
    @bl {FN(0x45D0)}
    mr 24, 3
    mr 3, 27
    mr 4, 25
""" + la(5, FILE_NAME) + f"""    li 6, 0x7213
    @bl {FN(0x46B4)}
    cmplwi 24, 0
    beq mpre_out
    addi 24, 24, 0xFFF
    rlwinm 24, 24, 0, 0, 19
    li 0, 0
""" + la(9, SIZE // 4) + """    srwi 10, 30, 2
    add 9, 9, 10
    mtctr 9
    addi 9, 24, -4
mpre_zero:
    stwu 0, 4(9)
    bdnz mpre_zero
    stw 24, 4(31)
    li 0, 1
    stw 0, 0(31)
mpre_out:
    lwz 30, 0x24(1)
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

    # game_start: the host's first pass, at the countdown task's first entry. r3 is the task's argument; nothing else
    # is live at a function's entry but LR
    begin = TEXT + BEGIN
    word = stock(begin, BEGIN_KNOWN)
    r.add("mode_begin", test + """    beq mbg_stock
""" + la(12, MODES) + f"""    lwz 12, 4(12)
    cmplwi 12, 0
    beq mbg_stock
    lwz 0, {memmap.MODE_RUNS:#x}(12)
    cmpwi 0, 0
    bne mbg_stock
    stwu 1, -0x20(1)
    mflr 0
    stw 0, 0x24(1)
    stw 3, 8(1)
    @bl host_mode
    lwz 3, 8(1)
    lwz 0, 0x24(1)
    mtlr 0
    addi 1, 1, 0x20
mbg_stock:
    .long {word:#010x}
    @b {begin + 4:#x}
""")
    patch(begin, word, 0, "mode_begin")

    # the hold: r3 the machine, r4 its record, r5 not 0 for the read-out too. Changes the volatile registers.
    r.add("mode_hold", f"""    stwu 1, -0x40(1)
    mflr 0
    stw 0, 0x44(1)
    stw 31, 0x3C(1)
    stw 30, 0x38(1)
    stw 29, 0x34(1)
    mr 31, 3
    mr 30, 4
    mr 29, 5
    lis 0, 0x406B
    stw 0, 0x10(1)
    li 0, 0
    stw 0, 0x14(1)
    stw 0, 0x20(1)
    lfd 2, 0x10(1)
    lfs 1, 12(30)
    fdiv 3, 1, 2
    frsp 4, 3
    stfs 4, 0x224(31)
    lfs 5, 8(31)
    fmul 6, 3, 5
    frsp 6, 6
    stfs 6, 0x18(1)
    lfs 0, 0x94(31)
    lfs 2, 0x98(31)
    fmuls 1, 0, 0
    lfs 0, 0x9C(31)
    fmadds 1, 2, 2, 1
    fmadds 1, 0, 0, 1
    lfs 7, 0x20(1)
    fcmpu 0, 1, 7
    beq mhold_nose
    @bl {SQRT:#x}
    lfs 6, 0x18(1)
    fdivs 0, 6, 1
    lfs 1, 0x94(31)
    fmuls 1, 1, 0
    stfs 1, 0x94(31)
    lfs 1, 0x98(31)
    fmuls 1, 1, 0
    stfs 1, 0x98(31)
    lfs 1, 0x9C(31)
    fmuls 1, 1, 0
    stfs 1, 0x9C(31)
    b mhold_held
mhold_nose:
    lfs 6, 0x18(1)
    fneg 6, 6
    lfs 1, 0xF4(31)
    fmuls 1, 1, 6
    stfs 1, 0x94(31)
    lfs 1, 0x104(31)
    fmuls 1, 1, 6
    stfs 1, 0x98(31)
    lfs 1, 0x114(31)
    fmuls 1, 1, 6
    stfs 1, 0x9C(31)
mhold_held:
    cmpwi 29, 0
    beq mhold_out
    lfs 0, 0x94(31)
    lfs 2, 0x98(31)
    fmuls 1, 0, 0
    lfs 0, 0x9C(31)
    fmadds 1, 2, 2, 1
    fmadds 1, 0, 0, 1
    @bl {SQRT:#x}
    lfs 0, 8(31)
    lfd 2, 0x10(1)
    fdivs 0, 1, 0
    fmul 0, 2, 0
    frsp 0, 0
    stfs 0, 0x17C(31)
mhold_out:
    lwz 29, 0x34(1)
    lwz 30, 0x38(1)
    lwz 31, 0x3C(1)
    lwz 0, 0x44(1)
    mtlr 0
    addi 1, 1, 0x40
    blr
""")

    # whether a seat is held: r3 the machine, r4 its record -> r3 1 or 0; changes r0 and r3
    r.add("mode_held", f"""    lwz 0, 0(4)
    andi. 0, 0, {HELD}
    beq mheld_no
    lwz 0, 0(3)
    andis. 3, 0, {NOT_HELD >> 16:#x}
    bne mheld_no
    andi. 3, 0, {NOT_HELD & 0xFFFF:#x}
    bne mheld_no
    li 3, 1
    blr
mheld_no:
    li 3, 0
    blr
""")

    # explode: story action 16, the game's own explosion (fn_1_25004: flags 0x800 | 0x80 | 0x00800000 | 0x40000000,
    # +0x58C 0x10, energy and motion 0, the effect) with the motion written back as it was: momentum P and +0xA0 to
    # +0xA8, the base speed +0x224, the read-out +0x17C (the wreck slides on). r3 the machine; changes the volatile
    # registers. A second explode is the game's no-op (fn_1_25004 returns at +0x58C 0x10).
    r.add("mode_explode", f"""    stwu 1, -0x40(1)
    mflr 0
    stw 0, 0x44(1)
    stw 31, 0x3C(1)
    mr 31, 3
    lwz 0, 0x94(31)
    stw 0, 0x10(1)
    lwz 0, 0x98(31)
    stw 0, 0x14(1)
    lwz 0, 0x9C(31)
    stw 0, 0x18(1)
    lwz 0, 0xA0(31)
    stw 0, 0x1C(1)
    lwz 0, 0xA4(31)
    stw 0, 0x20(1)
    lwz 0, 0xA8(31)
    stw 0, 0x24(1)
    lwz 0, 0x224(31)
    stw 0, 0x28(1)
    lwz 0, 0x17C(31)
    stw 0, 0x2C(1)
    @bl {FN(DESTROY)}
    lwz 0, 0x10(1)
    stw 0, 0x94(31)
    lwz 0, 0x14(1)
    stw 0, 0x98(31)
    lwz 0, 0x18(1)
    stw 0, 0x9C(31)
    lwz 0, 0x1C(1)
    stw 0, 0xA0(31)
    lwz 0, 0x20(1)
    stw 0, 0xA4(31)
    lwz 0, 0x24(1)
    stw 0, 0xA8(31)
    lwz 0, 0x28(1)
    stw 0, 0x224(31)
    lwz 0, 0x2C(1)
    stw 0, 0x17C(31)
    lwz 31, 0x3C(1)
    lwz 0, 0x44(1)
    mtlr 0
    addi 1, 1, 0x40
    blr
""")

    # a rolling start: the start's monitors take the followed machine's turn every step, as the game's own branch for
    # rule flag 0x100 in phase 0x28 does; the stock call keeps the first step's turn, so on a loop they part from it
    site = TEXT + MONITORS_TURN
    word = stock(site, MONITORS_TURN_KNOWN)
    r.add("mode_monitors", test + """    beq mmt_stock
""" + la(12, memmap.MODE_HUD_FLAGS) + f"""    lwz 12, 0(12)
    andi. 12, 12, {memmap.MODE_ROLLING}
    beq mmt_stock
    li 4, 1
    @b {site + 4:#x}
mmt_stock:
    .long {word:#010x}
    @b {site + 4:#x}
""")
    patch(site, word, 0, "mode_monitors")
    middle.append((site, ["r12", "cr0"], "mode_monitors"))

    # a rolling start: what the count does below 240, lift the machines' task mask, from its first step
    site = TEXT + ROLLS
    word = stock(site, ROLLS_KNOWN)
    r.add("mode_rolls", test + """    beq mrl_stock
""" + la(12, memmap.MODE_HUD_FLAGS) + f"""    lwz 12, 0(12)
    andi. 12, 12, {memmap.MODE_ROLLING}
    beq mrl_stock
    stwu 1, -0x20(1)
    mflr 0
    stw 0, 0x24(1)
    stw 3, 8(1)
    stw 4, 0xC(1)
    li 3, 0
    @bl {TEXT + TASK_MASK:#x}
    lwz 3, 8(1)
    lwz 4, 0xC(1)
    lwz 0, 0x24(1)
    mtlr 0
    addi 1, 1, 0x20
mrl_stock:
    .long {word:#010x}
    @b {site + 4:#x}
""")
    patch(site, word, 0, "mode_rolls")
    middle.append((site, ["r12", "cr0"], "mode_rolls"))

    # the mode point: the interpreter, then the rescale and read-out of every held seat (r31 the car manager)
    point = TEXT + POINT
    word = stock(point, POINT_KNOWN)
    r.add("mode_point", test + """    beq mpt_stock
""" + la(12, MODES) + f"""    lwz 12, 4(12)
    cmplwi 12, 0
    beq mpt_stock
    stwu 1, -{FRAME:#x}(1)
    mflr 0
    stw 0, {FRAME + 4:#x}(1)
    mfcr 0
    stw 0, 0x8(1)
    mfctr 0
    stw 0, 0xC(1)
{SAVE_GPR}{SAVE_FPR}    @bl host_mode
    bl mode_banners
""" + la(28, MODES) + f"""    lwz 28, 4(28)
    lwz 27, 0x18(31)
    lbz 26, 0x2A(31)
    extsb. 26, 26
    ble mpt_done
    addi 25, 28, {memmap.MODE_STATS:#x}
mpt_seat:
    lwz 0, 0(28)
    andi. 0, 0, {DAMAGE}
    beq mpt_unhurt
    mr 3, 27
    lfs 1, {memmap.MODE_STAT_DAMAGE:#x}(25)
    @bl {FN(HURT)}
mpt_unhurt:
    lwz 0, 0(28)
    andi. 0, 0, {EXPLODE}
    beq mpt_whole
    mr 3, 27
    bl mode_explode
mpt_whole:
    mr 3, 27
    mr 4, 28
    bl mode_held
    cmpwi 3, 0
    beq mpt_next
    lhz 0, {RESTORING:#x}(27)
    cmpwi 0, 0
    bne mpt_next
    mr 3, 27
    mr 4, 28
    li 5, 1
    bl mode_hold
mpt_next:
    addi 27, 27, 0x{memmap.MACHINE_SIZE:X}
    addi 28, 28, {SEAT}
    addi 25, 25, {memmap.MODE_STAT}
    addic. 26, 26, -1
    bne mpt_seat
mpt_done:
{LOAD_FPR}{LOAD_GPR}    lwz 0, 0xC(1)
    mtctr 0
    lwz 0, 0x8(1)
    mtcr 0
    lwz 0, {FRAME + 4:#x}(1)
    mtlr 0
    addi 1, 1, {FRAME:#x}
mpt_stock:
    .long {word:#010x}
    @b {point + 4:#x}
""")
    patch(point, word, 0, "mode_point")
    middle.append((point, ["r12", "cr0"], "mode_point"))

    # the hold in the step, after the air drag (r30 the machine; the restore gate has skipped a restoring one)
    site = TEXT + SPEED
    word = stock(site, SPEED_KNOWN)
    r.add("mode_speed", test + """    beq mspd_stock
""" + la(12, MODES) + f"""    lwz 12, 4(12)
    cmplwi 12, 0
    beq mspd_stock
    stwu 1, -{FRAME:#x}(1)
    mflr 0
    stw 0, {FRAME + 4:#x}(1)
    mfcr 0
    stw 0, 0x8(1)
    mfctr 0
    stw 0, 0xC(1)
{SAVE_GPR}{SAVE_FPR}    lha 4, 4(30)
    mulli 4, 4, {SEAT}
    add 4, 4, 12
    mr 28, 4
    mr 3, 30
    bl mode_held
    cmpwi 3, 0
    beq mspd_move
    mr 3, 30
    mr 4, 28
    li 5, 0
    bl mode_hold
mspd_move:
{la(12, MODES)}    lwz 12, 4(12)
    lha 4, 4(30)
    mulli 4, 4, {memmap.MODE_MOVE}
    add 4, 4, 12
    addi 4, 4, {memmap.MODE_MOVES:#x}
    lwz 0, 0(4)
    cmpwi 0, 0
    beq mspd_done
    lhz 3, {RESTORING:#x}(30)
    cmpwi 3, 0
    bne mspd_done
    lwz 3, 0(30)
    andis. 5, 3, {NOT_HELD >> 16:#x}
    bne mspd_done
    andi. 5, 3, {NOT_HELD & 0xFFFF:#x}
    bne mspd_done
    cmpwi 0, 1
    bne mspd_set
    lfs 1, 0x94(30)
    lfs 2, 4(4)
    fadds 1, 1, 2
    stfs 1, 0x94(30)
    lfs 1, 0x98(30)
    lfs 2, 8(4)
    fadds 1, 1, 2
    stfs 1, 0x98(30)
    lfs 1, 0x9C(30)
    lfs 2, 12(4)
    fadds 1, 1, 2
    stfs 1, 0x9C(30)
    b mspd_done
mspd_set:
    lwz 0, 4(4)
    stw 0, 0x94(30)
    lwz 0, 8(4)
    stw 0, 0x98(30)
    lwz 0, 12(4)
    stw 0, 0x9C(30)
mspd_done:
{LOAD_FPR}{LOAD_GPR}    lwz 0, 0xC(1)
    mtctr 0
    lwz 0, 0x8(1)
    mtcr 0
    lwz 0, {FRAME + 4:#x}(1)
    mtlr 0
    addi 1, 1, {FRAME:#x}
mspd_stock:
    .long {word:#010x}
    @b {site + 4:#x}
""")
    patch(site, word, 0, "mode_speed")
    middle.append((site, ["r12", "cr0"], "mode_speed"))

    # a seat's own Restore in the game's verdict: the rule's bit or the machine's flag 0x1, in cr0 as the original
    # sets it (r0 the rule word, r30 the machine)
    site = TEXT + VERDICT
    word = stock(site, VERDICT_KNOWN)
    r.add("respawns", test + """    beq resp_stock
    rlwinm 0, 0, 0, 16, 16
    lwz 3, 0(30)
    clrlwi 3, 3, 31
    or. 0, 0, 3
    @b {back:#x}
resp_stock:
    .long {word:#010x}
    @b {back:#x}
""".format(back=site + 4, word=word))
    patch(site, word, 0, "respawns")
    middle.append((site, ["r3", "r12", "cr0"], "respawns"))

    # the pair gate (the contact loop's call of a pair, i = r28, j = r27, r3 = M[i], r4 = M[j]): a pair the mode
    # keeps apart (contact off, ghost, no contact, a team's) is not answered (fn_1_239C4 not called, the answer 0: no
    # push, damage, KO, wreck or sparks) and keeps the game's own immunity bits on both machines' +0x514, so the pair
    # stays immune until it first separates when the mode lets it meet again (ghost-machines.md 2.4); a pair answered
    # (1) goes into the touching rows, both ways. Registers: r0, r11, r12, cr0 and lr (the call's own), r3 the answer.
    site = TEXT + PAIR
    call = TEXT + PAIR_CALL
    word = stock(site, PAIR_KNOWN)
    nc, touch = memmap.MODE_NC, memmap.MODE_TOUCH
    r.add("mode_pair", test + f"""    beq mpair_stock
""" + la(12, MODES) + f"""    lwz 12, 4(12)
    cmplwi 12, 0
    beq mpair_stock
    lwz 0, {memmap.MODE_NC_ANY:#x}(12)
    cmpwi 0, 0
    beq mpair_call
    slwi 11, 28, 2
    add 11, 11, 12
    lwz 11, {nc:#x}(11)
    addi 0, 27, 1
    rlwnm. 0, 11, 0, 31, 31
    bne mpair_skip
    slwi 11, 27, 2
    add 11, 11, 12
    lwz 11, {nc:#x}(11)
    addi 0, 28, 1
    rlwnm. 0, 11, 0, 31, 31
    bne mpair_skip
mpair_call:
    @bl {call:#x}
    cmpwi 3, 0
    beq mpair_back
""" + la(12, MODES) + f"""    lwz 12, 4(12)
    slwi 11, 28, 2
    add 11, 11, 12
    lwz 12, {touch:#x}(11)
    lis 0, 0x8000
    srw 0, 0, 27
    or 12, 12, 0
    stw 12, {touch:#x}(11)
""" + la(12, MODES) + f"""    lwz 12, 4(12)
    slwi 11, 27, 2
    add 11, 11, 12
    lwz 12, {touch:#x}(11)
    lis 0, 0x8000
    srw 0, 0, 28
    or 12, 12, 0
    stw 12, {touch:#x}(11)
mpair_back:
    @b {site + 4:#x}
mpair_skip:
    lis 0, 0x8000
    srw 0, 0, 27
    lwz 11, 0x514(3)
    or 11, 11, 0
    stw 11, 0x514(3)
    lis 0, 0x8000
    srw 0, 0, 28
    lwz 11, 0x514(4)
    or 11, 11, 0
    stw 11, 0x514(4)
    li 3, 0
    @b {site + 4:#x}
mpair_stock:
    @bl {call:#x}
    @b {site + 4:#x}
""")
    patch(site, word, 0, "mode_pair")
    middle.append((site, ["r0", "r11", "r12", "cr0", "lr"], "mode_pair"))

    # the timer (fn_1_3A330's countdown of the timed rule, 0x40000: R+0xA4 down a step, the beeps, time over at 0):
    # under a clock limit the mode keeps the timer (from the race clock: mode.cpp) and ends the race itself, so the
    # game's countdown is skipped as it is without the rule. r0 holds the rule word's timed bit on entry (the site's
    # rlwinm.), which the routine tests again after the mode test.
    site = TEXT + TIMER
    word = stock(site, TIMER_KNOWN)
    skip = TEXT + TIMER_SKIP
    r.add("mode_timer", test + """    beq mtim_stock
    cmpwi 0, 0
    beq mtim_skip
""" + la(12, MODES) + f"""    lwz 12, 4(12)
    cmplwi 12, 0
    beq mtim_count
    lwz 12, {memmap.MODE_CLOCK:#x}(12)
    cmpwi 12, 0
    beq mtim_count
mtim_skip:
    @b {skip:#x}
mtim_stock:
    cmpwi 0, 0
    bne mtim_count
    @b {skip:#x}
mtim_count:
    @b {site + 4:#x}
""")
    patch(site, word, 0, "mode_timer")
    middle.append((site, ["r12", "cr0"], "mode_timer"))
