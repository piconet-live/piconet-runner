"""The frame routine's entry and its frame (netframe.py assembles the parts in order; its docstring lists the control
block's words). From the routine's first instruction: what is set on entry of every displayed frame, the race-start
gate's wait (`gate_mode`, `g_wait`), the test park (`spin`), the spectator camera request (`cam_req`), the frame's
first step (`call1`) and its extra steps (`extra`), and what runs after the steps (`done`), up to the routine's
return (`done_ret`). The spectator's routines are frame_spectate.py's: its controls are read on entry
(`speccontrols`, before the first network hook), its HUD is set after the steps (`spechud`). The text starts with
the empty line the whole text started with.

The race's end (memmap.END_OFFICIAL and the words after it; standin_race.py `over`): `endtest` (frame_replay.py) is
asked after the network hook of the frame's first step, when a rollback has put the memory back, and again before
every extra step (`endtest_extra`: the race clock is counted for the step just run only below, at `extra_on`, so
it is one behind there); the answer of the time before is kept at 32(r1). When the step just run made the race look over
(not over before it, over after it), `extra_cut` runs none of the frame's remaining steps: the game then stands
before the same step in every game when the race-over test, which runs once a displayed frame, says "over". The steps
left out count as never simulated (+492), so their sounds play when they run. A frame whose first step is past that
point is not cut: only the step that makes the race over stops a frame.

The local racer's finish (frame_hud.py `finwatch`) is looked at on every step boundary: after the network hook of
the first step and of every extra step, and after the frame's last step, before the HUD pass. After the HUD pass,
`results_watch` (follow.py) moves the results' cameras to the winner when the local wreck they watch goes.

Before every extra step (`extra_on`, after the race-over cut and before the step's network hook) the game's
delayed-free pass runs: main.rel fn_1_4928 (alloc.c; the module's header + 0x4A04, its text at +0xDC), which the
main loop calls once a pass, after the step. An effect's buffer freed "a frame later" (effect.c line 6101, kind
25's destroy; a list of 32 at .bss 0xDCC) is released between two steps as in a straight run, where a pass is a
step. Without it a rollback's re-simulation, the scrubber's front steps or a catch-up held every such free of the
frame's steps at once and the game stopped ("Not enought [free delay elem]", crash-202550). A frame of one step
never comes here: straight runs are unchanged.

A step that stood (a mode's pause: frame_replay.py `manager`, memmap.STOOD 1) does not count the race clock: `extra_on`
leaves it, and `done_ret`, after a frame whose last step stood, takes one off it in the race phase below the clock's top,
so that the game's frame function, which counts it once after the routine returns (main.rel 0x37BAC: in the race
phase, up to CLOCK_CAP), puts it back where it stood. Nothing reads the clock between the two."""
import memmap


def asm():
    return (_TEXT.replace("STOOD_AT", str(memmap.STOOD - memmap.CONTROL))
            .replace("CLOCK_CAP_HI", str(CLOCK_CAP >> 16)).replace("CLOCK_CAP_LO", str(CLOCK_CAP & 0xFFFF)))


# The race clock's top: the game's frame function counts it up to this (100 minutes) and no further (race-map.md 2.1).
CLOCK_CAP = 0x57E40
_TEXT = """
    stwu 1, -48(1)
    mflr 0
    stw 0, 52(1)
    stw 31, 44(1)
    stw 30, 40(1)
    lis 31, CONTROL_HI
    lis 12, 0x8000
    lwz 12, 0x30C8(12)
    addis 30, 12, 0x1C
    addi 30, 30, -0x5FC0
    bl follow_install
    bl banners_install
    bl effects_install
    bl raceview_install
    bl othersound_install
    bl seatport_install
    bl playernames_install
    bl smooth_install
    bl smooth_restore
    bl untracked_write
    lwz 0, 0xA34(31)
    cmpwi 0, 0
    beq no_press
    li 0, 0
    stw 0, 0xA34(31)
    lhz 9, 0x9F8(30)
    ori 9, 9, 0x100
    sth 9, 0x9F8(30)
    lhz 9, 0xA00(30)
    ori 9, 9, 0x100
    sth 9, 0xA00(30)
no_press:
    bl speccontrols
    bl racetasks
    lwz 9, 0xDAC(30)
    lwz 0, 0x14(9)
    stw 0, 8(1)
    lwz 0, 0x10(9)
    stw 0, 24(1)
    addis 9, 30, -1
    lwz 0, -0x70A4(9)
    stw 0, 28(1)
    lwz 0, 48(31)
    cmpwi 0, 0
    beq no_gate
    lha 9, 0x960(30)
    cmpwi 9, 2
    bne no_gate
gate_mode:
    lha 9, 0x3C2A(30)
    cmpwi 9, 0x27
    bne no_gate
    addis 9, 30, 2
    lwz 9, 0x6C28(9)
    cmpwi 9, 1
    bne no_gate
    lwz 9, 40(31)
    cmpwi 9, 0
    bne no_gate
    li 9, 1
    stw 9, 44(31)
g_wait:
    bl netsync
    lwz 9, 40(31)
    cmpwi 9, 0
    beq g_wait
no_gate:
    lwz 0, 0(31)
    cmpwi 0, 0
    beq cam_req
    lwz 9, 0x50A4(30)
    cmpw 9, 0
    bne cam_req
    stw 9, 4(31)
spin:
    lwz 0, 0(31)
    cmpw 9, 0
    beq spin
cam_req:
    lwz 4, 52(31)
    cmpwi 4, 0
    beq call1
    li 0, 0
    stw 0, 52(31)
    addi 4, 4, -1
    li 3, 1
    lis 12, 0x8000
    lwz 12, 0x30C8(12)
    addi 12, 12, 0x544C
    mtctr 12
    bctrl
call1:
    bl netsync
    bl finwatch
    bl endtest
    stw 3, 32(1)
    bl stepclock
    bl filters
    bl replay
    bl manager
    bl genlog
    bl checksum
    bl net_step
    lwz 9, 8(31)
    cmpwi 9, 0
    beq done
    li 9, 1
    stw 9, 12(31)
    mftb 9
    stw 9, 28(31)
extra:
    bl endtest_extra
    lwz 9, 32(1)
    stw 3, 32(1)
    cmpwi 3, 0
    beq extra_on
    cmpwi 9, 0
    beq extra_cut
extra_on:
    lis 12, 0x8000
    lwz 12, 0x30C8(12)
    addi 12, 12, 0x4A04
    mtctr 12
    bctrl
    lha 9, 0x3C2A(30)
    cmpwi 9, 0x29
    blt extra_clock
    lwz 0, STOOD_AT(31)
    cmpwi 0, 0
    bne extra_clock
    lwz 9, 0x50A4(30)
    addi 9, 9, 1
    stw 9, 0x50A4(30)
extra_clock:
    lwz 9, 8(31)
    cmpwi 9, 1
    bne extra_sync
    bl filters
    li 9, 1
    stw 9, 460(31)
extra_sync:
    bl netsync
    bl finwatch
    bl stepclock
    bl manager
    bl genlog
    bl checksum
    bl net_step
    lwz 9, 16(31)
    addi 9, 9, 1
    stw 9, 16(31)
    lwz 9, 8(31)
    addi 9, 9, -1
    stw 9, 8(31)
    cmpwi 9, 0
    bne extra
extra_end:
    li 9, 0
    stw 9, 12(31)
    mftb 9
    lwz 0, 28(31)
    subf 9, 0, 9
    stw 9, 28(31)
    bl filters
done:
    bl spectate
    bl finwatch
    bl hudstate
    bl results_watch
    bl leave
    li 0, 0
    stw 0, 460(31)
    bl spechud
done_ret:
    lwz 0, STOOD_AT(31)
    cmpwi 0, 0
    beq dr_clock
    lha 9, 0x3C2A(30)
    cmpwi 9, 0x29
    blt dr_clock
    lwz 9, 0x50A4(30)
    lis 0, CLOCK_CAP_HI
    ori 0, 0, CLOCK_CAP_LO
    cmplw 9, 0
    bge dr_clock
    addi 9, 9, -1
    stw 9, 0x50A4(30)
dr_clock:
    lwz 30, 40(1)
    lwz 31, 44(1)
    lwz 0, 52(1)
    mtlr 0
    addi 1, 1, 48
    blr
extra_cut:
    li 9, 0
    stw 9, 8(31)
    lwz 9, 124(31)
    stw 9, 492(31)
    b extra_end
"""
