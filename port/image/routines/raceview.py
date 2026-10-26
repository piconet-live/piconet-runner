"""A racer changes the camera view in a race of ours, as on a console (for the frame routine, netframe.py, which
includes this code and calls `raceview_install` on entry every frame).

The game changes the view in fn_1_ABAC, which the chase camera's task fn_1_90E0 calls in every step: it takes the
pad port of the machine the camera follows (fn_1_86678, machine +0x474) and reads that port's newly set control bits
(.bss 0xCC0[port]: 0x02 the next view, 0x01 the view before), which the default controller setting gives the D-pad's
up and down. In a race of ours no machine has a port (seats 1 to 29 never had one; seat 0's goes with the hand-over
to a CPU driver in its first step, standin.py), so the routine returned at 0xAC78 for every racer. The bits
themselves travel already: they are in byte 0 of every seat's control record, so in the input rows, the official
rows and the recordings.

Four words of fn_1_ABAC, each a `bl` to a helper here. In a plain start each does what the word did. In a race of
ours (also a replay, which plays as the race):
- 0xABF8, `bl fn_1_86678`: `port`, port 0, so the routine goes on to read the bits;
- 0xAD10 and 0xAD24, `lbzx r0, r3, r4` (the port's newly set bits): `bits`, byte 0 of the followed seat's row for
  the step that is running (control block +124) and-not the row of the step before, as standin_inputs.py makes
  "newly pressed". Every step run again reads the same rows, so a rollback makes the change again by itself, and a
  row corrected by the official inputs corrects the view with it. Not .bss 0xCC0[0]: it is stale after a restore,
  and every step of a frame would see the same press. No bits before the race phase (the cameras' tasks run in
  every step only from phase 0x29 on), for a row whose steering byte says the AI drives, or while this game
  spectates (the cameras are the spectator's then: frame_spectate.py);
- 0xAD3C, `li r31, 0` (the view's lowest): `low`, 1. View 0 is drawn by the pad port's slot (machine +0x475, seat
  0's in every game: the car's own drawing at 0x86A78 and the effects' sites ask "view 0?" for that machine), so on
  any seat but 0 it would hide seat 0's car, not the racer's own: views 1 to 3 only, for every seat alike. A view 0
  a player's save file starts the camera with becomes 1 the first time the routine runs.
The view stays where the game keeps it (the camera record's +0xA4, in memory a rollback puts back; the checksum does
not cover it). Nothing here writes a machine's block: +0x474 and +0x475 are in the checksum and stay as they are.

`bits` needs two registers besides its result: it keeps r3 and r4 in the control block (memmap.VIEW_SCRATCH) and
puts them back, since the second site's own instruction reads them in a plain start. Control block +0x3BC: the
main.rel address the sites were patched for (follow.installer)."""
import memmap
from follow import installer

PORT_SITE = 0xABF8
BITS_SITES = (0xAD10, 0xAD24)
LOW_SITE = 0xAD3C
PORT_STOCK = 0x4807BA81          # bl fn_1_86678
BITS_STOCK = 0x7C0320AE          # lbzx r0, r3, r4
LOW_STOCK = 0x3BE00000           # li r31, 0
GUARD = 0x3BC
assert memmap.VIEW_SCRATCH + 8 == memmap.CONTROL + GUARD
# (text offset, original word, mask, helper)
SITES = [(PORT_SITE, PORT_STOCK, 0xFFFFFFFF, "port"), (BITS_SITES[0], BITS_STOCK, 0xFFFFFFFF, "bits"),
         (BITS_SITES[1], BITS_STOCK, 0xFFFFFFFF, "bits"), (LOW_SITE, LOW_STOCK, 0xFFFFFFFF, "low")]
C = memmap.hi(memmap.CONTROL)
SCRATCH = memmap.VIEW_SCRATCH - memmap.CONTROL
assert 0x1C0000 - 0x23A0 + 0xA == 0x1BA040 + 0x3C2A           # the phase word from the module's address

HELPERS = {
    "port": """    li 3, 0
    blr
""",
    "low": """    li 31, 1
    blr
""",
    # r29 = the camera record (+0x2: the machine it follows); gives r0; uses r12 and CR field 0
    "bits": f"""    lis 12, {C}
    stw 3, {SCRATCH}(12)
    stw 4, {SCRATCH + 4}(12)
    li 0, 0
    lwz 3, 120(12)
    cmpwi 3, 0
    beq vb_out
    lwz 3, 248(12)
    cmpwi 3, 180
    bge vb_out
    lis 4, 0x8000
    lwz 4, 0x30C8(4)
    addis 4, 4, 0x1C
    lha 4, -0x2396(4)
    cmpwi 4, 0x29
    blt vb_out
    lha 4, 2(29)
    cmplwi 4, 30
    bge vb_out
    lwz 3, 124(12)
    lis 12, {memmap.hi(memmap.INPUTS)}
    andi. 0, 3, 1023
    mulli 0, 0, 30
    add 0, 0, 4
    slwi 0, 0, 3
    add 12, 12, 0
    addi 3, 3, -1
    andi. 3, 3, 1023
    mulli 3, 3, 30
    add 3, 3, 4
    slwi 3, 3, 3
    lis 4, {memmap.hi(memmap.INPUTS)}
    lbzx 3, 4, 3
    lbz 4, 1(12)
    li 0, 0
    cmpwi 4, 0x80
    beq vb_out
    lbz 0, 0(12)
    andc 0, 0, 3
vb_out:
    lis 12, {C}
    lwz 3, {SCRATCH}(12)
    lwz 4, {SCRATCH + 4}(12)
    blr
""",
}
assert memmap.lo(memmap.INPUTS) == 0


def asm():
    """The helpers and `raceview_install` (r31 = the control block; called with bl; uses r0, r3-r12 and 32(r1))."""
    return installer("raceview_install", "v", SITES, HELPERS, GUARD)
