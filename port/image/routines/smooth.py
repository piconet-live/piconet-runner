"""Rivals drawn smoothly through rollbacks (for the frame routine, netframe.py, which includes this code).

A rollback puts a rival whose inputs were guessed wrong where it really is: the race is right, but the machine is drawn
jumping there. The machine is drawn from its drawn matrix (+0x14C, translation +0x158/+0x168/+0x178), which the race
rebuilds every step (fn_1_29A0C) and which only the restore and breakdown code read in the race (machine-state map);
the presentation tasks (effects, list 11; per machine, list 22) place the engine flames and boost glow from it while
they run (moved after the step, the body rose and its flames stayed on the road). So in the frame's last step (the one
with presentation, netframe.py), between the race's task lists (2, 3, 7, 9) and the presentation ones (10 on),
`smooth_draw` adds an offset to the translation of every machine but the local racer's; `smooth_restore`, on the next
frame's entry (before the network hook and any step), puts the race's own translation back: the race, its checksums
and the fork's undo log only ever see the race's values. The hook: the task manager's loop (fn_1_407C, main.rel
0x40AC: `li r0, 1` at the top of each list) gets a `bl smooth_hook` (installed by `smooth_install` once per main.rel,
control block +464, while the site still holds its word; main.rel's code is outside the undo log), which runs
`smooth_draw` once at list 10 after the frame routine has set +460 (the last step is about to run) and clears it.
The same hook, at list 20 (the cameras') of a step the spectator's routine has marked (memmap.SPEC_STEP), calls
frame_specwatch.py's `specwatch`: the one place that is inside a step after the race's tasks and before the
cameras'.

The offset (per seat, at memmap.SMOOTH, outside the rolled-back memory): on a frame with a rollback (the fork's count,
control block +136, changed), the offset makes the machine drawn where it was expected without the correction: last
frame's drawn position moved on by the machine's last step (position +0x7C minus its start-of-step copy +0x88) once per
step the frame advanced (+124); on other frames it shrinks to 0.85 of itself (a correction fades out in about 6-10
frames). It snaps to 0 when larger than 50 units (a respawn) or below 0.01. Only in a network session's race phase
(0x29) while running; a frame advancing more than 30 steps starts over without an offset. A frame that advances
no step (held by the time sync: the memory goes back one step and the step is run again) is a frame like any
other with the machine expected where it was drawn last: it stays there, or goes on fading. (Were such a frame to
start over, every rival still sliding would snap to where it is: 13 and 14 frames of 2,825 in
run cam3-c.)

A machine on the road is drawn at its true height above it (a machine stays on the track unless it is in the air,
so a vertical correction is unnecessary). The offset is a straight
line in world space from where the machine is to where it was expected, and on a curved or banked road a straight
line between two points of the surface cuts through it: rivals dipped into the ground in curves, further the
later their inputs came. So for a machine that is not in the air (flags +0x0 bit 0x2 clear. From the game's code:
fn_1_289B4, the machine's corners' routine, clears the bit at its start in every step (0x28A18: the flags and
0xBFA3FFE9), runs the four hover corners, and sets it at 0x28BCC only when every corner's own flags (+0x244,
0x5C apart) have their bit 0x2, which fn_1_2ABD4 sets at 0x2ADF0 for a corner that finds no road within its
reach and clears at 0x2AE2C for one that does) the offset's part along the machine's up vector is taken out in every
frame (+0x1BC, the road's normal under the machine as the race keeps it: roads bank, loop and wrap round pipes,
so not the world's up): only the part along the road is smoothed. A machine in the air keeps the whole offset.
The offset kept for the next frame is the cut one, so the drawn position stays on the road while it fades.

A seat's offset (+12) with its byte "written" (+36) is the machine's drawn transform: sites_drawn.py is its one
reader, for every picture that must stay with the body and is placed from where the race has the machine. A
machine's shadow is placed at its contact point on the road (block +0x1D4), where the machine really is; the
shadows' task moves it with the body (sites_shadow.py, which writes the log's last field). The effects (sparks,
boost, energy) are drawn with their machine's offset (sites_effects.py, whose counters are at memmap.EFFECTS).

An offset that is not a number snaps to 0 like one too large (no compare is true of it, so neither snap test
caught it, and the rival was then never drawn again in that game), and an up vector that is not a number cuts
nothing.

Tests' log (memmap.SMOOTH_LOG, 8 bytes at frame & 2047, written at the end of every `smooth_draw` in a game told to
keep the logs, memmap.IMAGE_LOGS; a players' game writes nothing): the step word's
low half, the seat whose offset had the largest part along its up vector among the machines on the road in this
frame, the frame's flags (1 it advanced 0 to 30 steps, 2 a rollback, 4 no smoothing, 8 the part is left in, 16 the
shadows are left where they were, 32 the effects are left where the game draws them: the four bits of +456), that
part's size (the upper half of a float): what was
cut, or with flag 8 what is drawn. On a frame with a rollback it is the height by which the fresh correction would
have moved the machine off the road. Then the upper half of a float: the square of the largest distance along the
road between a rival's drawn body and its shadow in this frame (sites_shadow.py).

Per seat (48 bytes): +0 the race's translation, +12 offset, +24 drawn position, +36 written (put back next entry),
+37 has history. Globals at SMOOTH + 0x600: +0 last step, +4 last rollback count, +8 frames, +12 frames x rivals drawn
jumping (more than 3 units from where they were expected), +20 the same without smoothing (the race's own positions),
+24.. constants, +48 and +52 the frame's largest part cut and its seat, +56 the square of the frame's largest
distance from a body to its shadow. Control block: +452 tests: seat + 1 (31: every rival) drawn 5 units up its own
up axis (what follows the drawn matrix); +456 tests, measurement only: bit 1 no smoothing, 2 a machine on the road
keeps the offset's part along its up vector (the earlier drawing), 4 the shadows stay at the contact
point (the same), 8 the effects stay where the game draws them (the earlier drawing)."""
import memmap

SITE, ORIG = 0x40AC, 0x38000001     # fn_1_407C: `li r0, 1` at the top of each list
C = memmap.hi(memmap.CONTROL)

SMOOTH = memmap.SMOOTH
H = memmap.hi(SMOOTH) - 0x10000 if memmap.hi(SMOOTH) >= 0x8000 else memmap.hi(SMOOTH)
L = memmap.lo(SMOOTH)
G = L + 0x600
FRAMES = 0x600 + 8        # from memmap.SMOOTH: the frames counted, which is the log's place of the last one
assert L + 0x600 + 0x40 <= 0x10000 and 30 * 48 <= 0x600
S = memmap.hi(0x81800014) - 0x10000   # slot 'FZSI' +4: the local seat


def asm():
    """`smooth_install` and `smooth_restore` (frame entry; r31 = the control block, r30 = main.rel .bss; use r0,
    r3-r12, f0-f12 and ctr) and the hook. The installer finds the hook from its own address (`bl` to the next
    instruction); the distance is worked out here (keystone takes no label differences as operands)."""
    from keystone import Ks, KS_ARCH_PPC, KS_MODE_PPC32, KS_MODE_BIG_ENDIAN
    ks = Ks(KS_ARCH_PPC, KS_MODE_PPC32 + KS_MODE_BIG_ENDIAN)
    enter = """    stwu 1, -32(1)
    mflr 0
    stw 0, 36(1)
    stw 30, 8(1)
    stw 31, 12(1)
    mr 31, 12
    lis 12, 0x8000
    lwz 12, 0x30C8(12)
    addis 30, 12, 0x1C
    addi 30, 30, -0x5FC0
"""
    hook = f"""    cmpwi 27, 10
    beq sh_draw
    cmpwi 27, 20
    bne sh_ret
    lis 12, {C}
    lwz 0, {memmap.SPEC_STEP - memmap.CONTROL}(12)
    cmpwi 0, 0
    beq sh_ret
    li 0, 0
    stw 0, {memmap.SPEC_STEP - memmap.CONTROL}(12)
{enter}    bl specwatch
    b sh_leave
sh_draw:
    lis 12, {C}
    lwz 0, 460(12)
    cmpwi 0, 0
    beq sh_ret
    li 0, 0
    stw 0, 460(12)
{enter}    bl smooth_draw
sh_leave:
    lwz 30, 8(1)
    lwz 31, 12(1)
    lwz 0, 36(1)
    mtlr 0
    addi 1, 1, 32
sh_ret:
    li 0, 1
    blr
"""
    install = f"""    lis 12, 0x8000
    lwz 12, 0x30C8(12)
    lwz 11, 464(31)
    cmpw 11, 12
    beqlr
    stw 12, 464(31)
    addi 9, 12, {0xDC + SITE}
    lwz 10, 0(9)
    lis 8, {ORIG >> 16}
    ori 8, 8, {ORIG & 0xFFFF}
    cmpw 10, 8
    bnelr
    mflr 0
    bl si_pc
si_pc:
    mflr 8
    mtlr 0
    addi 8, 8, -HOOKDIST
    subf 10, 9, 8
    rlwinm 10, 10, 0, 6, 29
    oris 10, 10, 0x4800
    ori 10, 10, 1
    stw 10, 0(9)
    dcbst 0, 9
    sync
    icbi 0, 9
    isync
    blr
"""
    alone = hook.replace("bl smooth_draw", "nop").replace("bl specwatch", "nop")     # the size only
    pc = len(bytes(ks.asm(alone)[0])) + 14 * 4                                       # si_pc: 14 instructions in
    import native
    if native.ON:
        # The native form: the site is a static patch. The hook needs no mode test: outside a session neither +460
        # nor the spectator's mark is ever set, and it then does only what the replaced instruction did
        # (`li r0, 1`), having used r12 and CR field 0.
        native.SITES.append({"text": SITE, "original": native.original(SITE, ORIG)[0], "complete": True,
                             "label": "smooth_hook", "helper": "smooth_hook", "field": 0})
    return "smooth_hook:\n" + hook + "smooth_install:\n" + install.replace("HOOKDIST", str(pc)) + _draw()


def _draw():
    """`smooth_restore` and `smooth_draw`."""
    return f"""smooth_restore:
    lis 4, {H}
    ori 4, 4, {L}
    addis 10, 30, 7
    lwz 3, -0x27C8(10)
    cmpwi 3, 0
    beqlr
    lbz 6, -0x27B6(10)
    li 5, 0
sr_loop:
    cmpw 5, 6
    bgelr
    lbz 9, 36(4)
    cmpwi 9, 0
    beq sr_next
    li 9, 0
    stb 9, 36(4)
    lwz 9, 0(4)
    stw 9, 0x158(3)
    lwz 9, 4(4)
    stw 9, 0x168(3)
    lwz 9, 8(4)
    stw 9, 0x178(3)
sr_next:
    addi 3, 3, 0x{memmap.MACHINE_SIZE:X}
    addi 4, 4, 48
    addi 5, 5, 1
    b sr_loop
smooth_draw:
    lis 11, {H}
    ori 11, 11, {G}
    lwz 9, 116(31)
    cmpwi 9, 0
    beq sd_off
    lwz 9, 120(31)
    cmpwi 9, 0
    beq sd_off
    lha 9, 0x3C2A(30)
    cmpwi 9, 0x29
    bne sd_off
    addis 10, 30, 7
    lwz 3, -0x27C8(10)
    cmpwi 3, 0
    beq sd_off
    lbz 6, -0x27B6(10)
    lwz 9, 124(31)
    lwz 12, 0(11)
    stw 9, 0(11)
    subf 12, 12, 9
    li 8, 0
    cmplwi 12, 31
    bge sd_rb
    ori 8, 8, 1
sd_rb:
    lwz 9, 136(31)
    lwz 10, 4(11)
    stw 9, 4(11)
    cmpw 9, 10
    beq sd_dis
    ori 8, 8, 2
sd_dis:
    lwz 9, 456(31)
    rlwinm 9, 9, 2, 26, 29
    or 8, 8, 9
    lis 9, 0x3F59
    ori 9, 9, 0x999A
    stw 9, 24(11)
    lis 9, 0x451C
    ori 9, 9, 0x4000
    stw 9, 28(11)
    lis 9, 0x38D1
    ori 9, 9, 0xB717
    stw 9, 32(11)
    lis 9, 0x4110
    stw 9, 36(11)
    lis 9, 0x40A0
    stw 9, 40(11)
    li 9, 0
    stw 9, 44(11)
    stw 9, 48(11)
    stw 9, 52(11)
    stw 9, 56(11)
    lwz 9, 8(11)
    addi 9, 9, 1
    stw 9, 8(11)
    lis 7, {S}
    lwz 7, 0x14(7)
    lis 4, {H}
    ori 4, 4, {L}
    li 5, 0
sd_loop:
    cmpw 5, 6
    bge sd_log
    cmpw 5, 7
    beq sd_next
    li 10, 0
    andi. 9, 8, 1
    beq sd_reset
    lbz 9, 37(4)
    cmpwi 9, 0
    beq sd_reset
    li 10, 1
    lfs 1, 0x{memmap.MACHINE_POSITION_X:X}(3)
    lfs 0, 0x88(3)
    fsubs 1, 1, 0
    lfs 2, 0x{memmap.MACHINE_POSITION_Y:X}(3)
    lfs 0, 0x8C(3)
    fsubs 2, 2, 0
    lfs 3, 0x{memmap.MACHINE_POSITION_Z:X}(3)
    lfs 0, 0x90(3)
    fsubs 3, 3, 0
    lfs 7, 24(4)
    lfs 8, 28(4)
    lfs 9, 32(4)
    cmpwi 12, 0
    beq sd_held
    mtctr 12
sd_ext:
    fadds 7, 7, 1
    fadds 8, 8, 2
    fadds 9, 9, 3
    bdnz sd_ext
sd_held:
    lfs 1, 0x158(3)
    lfs 2, 0x168(3)
    lfs 3, 0x178(3)
    lfs 4, 12(4)
    lfs 5, 16(4)
    lfs 6, 20(4)
    fsubs 10, 1, 7
    fadds 10, 10, 4
    fmuls 11, 10, 10
    fsubs 10, 2, 8
    fadds 10, 10, 5
    fmadds 11, 10, 10, 11
    fsubs 10, 3, 9
    fadds 10, 10, 6
    fmadds 11, 10, 10, 11
    lfs 12, 36(11)
    fcmpu 0, 11, 12
    ble sd_new
    lwz 9, 20(11)
    addi 9, 9, 1
    stw 9, 20(11)
sd_new:
    andi. 9, 8, 2
    beq sd_decay
    fsubs 4, 7, 1
    fsubs 5, 8, 2
    fsubs 6, 9, 3
    b sd_size
sd_decay:
    lfs 12, 24(11)
    fmuls 4, 4, 12
    fmuls 5, 5, 12
    fmuls 6, 6, 12
sd_size:
    andi. 9, 8, 4
    bne sd_zero
    fmuls 11, 4, 4
    fmadds 11, 5, 5, 11
    fmadds 11, 6, 6, 11
    fcmpu 0, 11, 11
    bne sd_zero
    lfs 12, 28(11)
    fcmpu 0, 11, 12
    bgt sd_zero
    lfs 12, 32(11)
    fcmpu 0, 11, 12
    blt sd_zero
    b sd_test
sd_reset:
    lfs 1, 0x158(3)
    lfs 2, 0x168(3)
    lfs 3, 0x178(3)
sd_zero:
    lfs 4, 44(11)
    fmr 5, 4
    fmr 6, 4
sd_test:
    lwz 9, 0(3)
    andi. 9, 9, 2
    bne sd_flying
    lfs 10, 0x{memmap.MACHINE_UP_X:X}(3)
    lfs 11, 0x{memmap.MACHINE_UP_X + 4:X}(3)
    lfs 12, 0x{memmap.MACHINE_UP_X + 8:X}(3)
    fmuls 0, 4, 10
    fmadds 0, 5, 11, 0
    fmadds 0, 6, 12, 0
    fcmpu 0, 0, 0
    bne sd_flying
    andi. 9, 8, 8
    bne sd_part
    fnmsubs 4, 0, 10, 4
    fnmsubs 5, 0, 11, 5
    fnmsubs 6, 0, 12, 6
sd_part:
    fabs 10, 0
    lfs 12, 48(11)
    fcmpu 0, 10, 12
    ble sd_flying
    stfs 10, 48(11)
    stw 5, 52(11)
sd_flying:
    lwz 9, 452(31)
    cmpwi 9, 31
    beq sd_up
    addi 9, 9, -1
    cmpw 9, 5
    bne sd_write
sd_up:
    lfs 12, 40(11)
    lfs 4, 0x150(3)
    fmuls 4, 4, 12
    lfs 5, 0x160(3)
    fmuls 5, 5, 12
    lfs 6, 0x170(3)
    fmuls 6, 6, 12
sd_write:
    cmpwi 10, 0
    beq sd_store
    fadds 10, 1, 4
    fsubs 10, 10, 7
    fmuls 11, 10, 10
    fadds 10, 2, 5
    fsubs 10, 10, 8
    fmadds 11, 10, 10, 11
    fadds 10, 3, 6
    fsubs 10, 10, 9
    fmadds 11, 10, 10, 11
    lfs 12, 36(11)
    fcmpu 0, 11, 12
    ble sd_store
    lwz 9, 12(11)
    addi 9, 9, 1
    stw 9, 12(11)
sd_store:
    fadds 7, 1, 4
    fadds 8, 2, 5
    fadds 9, 3, 6
    stfs 1, 0(4)
    stfs 2, 4(4)
    stfs 3, 8(4)
    stfs 4, 12(4)
    stfs 5, 16(4)
    stfs 6, 20(4)
    stfs 7, 24(4)
    stfs 8, 28(4)
    stfs 9, 32(4)
    li 9, 1
    stb 9, 36(4)
    stb 9, 37(4)
    stfs 7, 0x158(3)
    stfs 8, 0x168(3)
    stfs 9, 0x178(3)
sd_next:
    addi 3, 3, 0x{memmap.MACHINE_SIZE:X}
    addi 4, 4, 48
    addi 5, 5, 1
    b sd_loop
sd_log:
    lwz 9, SLOG_ON(31)
    cmpwi 9, 0
    beqlr
    lwz 9, 8(11)
    andi. 9, 9, SLOG_MASK
    slwi 9, 9, 3
    lis 10, SLOG_HI
    ori 10, 10, SLOG_LO
    add 10, 10, 9
    lwz 9, 124(31)
    sth 9, 0(10)
    lwz 9, 52(11)
    stb 9, 2(10)
    stb 8, 3(10)
    lhz 9, 48(11)
    sth 9, 4(10)
    li 9, 0
    sth 9, 6(10)
    blr
sd_off:
    lis 9, 0x7FFF
    stw 9, 0(11)
    blr
""".replace("SLOG_ON", str(memmap.IMAGE_LOGS - memmap.CONTROL)).replace("SLOG_MASK", str(memmap.SMOOTH_LOG_FRAMES - 1)).replace("SLOG_HI", str(memmap.hi(memmap.SMOOTH_LOG))).replace(
        "SLOG_LO", str(memmap.lo(memmap.SMOOTH_LOG)))
