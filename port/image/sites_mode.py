"""The game mode's sites (piconet-docs research/game-modes/: the design, its vocabulary; the race map's hook table):
the mode's block, the mode point, the speed's hold and a seat's own Restore. With no mode in the race (memmap.MODES
+4 is 0: a match start without one, every recording made before modes) every site does what the game does, and
nothing is taken from the heap. Native form only: the fork's image does not have it. sites.py calls add().

| Site | Where | What the game did | Now, with a mode |
|---|---|---|---|
| `mode_preload` | the frame routine, on the black frame before the start gate (sites.py's `frame`, with mines_preload) | — | the mode's block (memmap.MODE_BLOCK bytes on whole pages of 4 KiB, zeroed: MODE_ALLOC taken) from the top of the main heap's largest free block, as mines_preload takes its block, when the host asked for one (MODES +0 = 4) |
| `mode_point` | fn_1_850C4 0x8548C, after the race pass's callbacks (`lwz r3, 0x15C(r31)`, r31 the car manager) | — | the host call HOST_MODE (the interpreter, port/core/src/mode.cpp: the rules run, their actions go into the block and the machines), then for every seat whose speed is held this step the hold's rescale and the read-out M+0x17C, as the step's start computes it (the vocabulary's "rescale R") |
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
PEDALS, BOOST_OFF, HELD = 1, 2, 4                 # a seat record's flags
WANTED, TAKEN, NONE = 4, 1, 3                     # MODES +0: the host asks; the block taken; none
POINT, POINT_KNOWN = 0x8548C, 0x807F015C          # fn_1_850C4: lwz r3, 0x15c(r31)
SPEED, SPEED_KNOWN = 0x23854, 0xC03F0014          # fn_1_236CC: lfs f1, 0x14(r31)
VERDICT, VERDICT_KNOWN = 0x32628, 0x54000421      # fn_1_32600: rlwinm. r0, r0, 0, 16, 16
SQRT = 0x8006D0B4                                 # main.dol: the game's square root (f1 -> f1)
NOT_HELD = 0x8000 | 0x80 | 0x800 | 0x10000 | 0x08000000 | 0x40000000
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
    cmpwi 0, {WANTED}
    bnelr
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
""" + la(0, ALLOC + RESERVE + 0x60) + """    cmplw 26, 0
    blt mpre_out
""" + la(0, ALLOC + 0x60) + """    subf 4, 0, 26
    mr 3, 27
""" + la(5, FILE_NAME) + f"""    li 6, 0x7211
    @bl {FN(0x45D0)}
    mr. 25, 3
    beq mpre_out
    mr 3, 27
""" + la(4, ALLOC) + la(5, FILE_NAME) + f"""    li 6, 0x7212
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
""" + la(9, SIZE // 4) + """    mtctr 9
    addi 9, 24, -4
mpre_zero:
    stwu 0, 4(9)
    bdnz mpre_zero
    stw 24, 4(31)
    li 0, 1
    stw 0, 0(31)
mpre_out:
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
""" + la(28, MODES) + f"""    lwz 28, 4(28)
    lwz 27, 0x18(31)
    lbz 26, 0x2A(31)
    extsb. 26, 26
    ble mpt_done
mpt_seat:
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
    beq mspd_done
    mr 3, 30
    mr 4, 28
    li 5, 0
    bl mode_hold
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
