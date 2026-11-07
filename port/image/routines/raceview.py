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
  and every step of a frame would see the same press. Bits from the countdown on (phase 0x28, the race start, as a
  console takes the button there: the cameras' tasks run in every step from that phase on, frame_tasks.py
  `rt_cam`), none before it (the course intro, whose cameras are the course's), for a row whose steering byte says
  the AI drives, or while this game spectates (the cameras are the spectator's then: frame_spectate.py);
- 0xAD3C, `li r31, 0` (the view's lowest): left to the game in the native form, so view 0, the first person view, is a
  racer's to choose as on a console. The fork's image gives 1 (`low`): it has none of the reads below.

View 0 and who the game takes for the camera's machine. No test of a human stands in fn_1_ABAC or the chase camera
fn_1_90E0: they work on any racer the record follows. What ties view 0 to a player is the machine's viewport slot
(+0x475): fn_1_83070 gives it to the first R+0x9 (humans) machines, so 0 to seat 0 in every game of ours and -1 to the
others; it is in the checksum, so it stays. The readers: the car's near-camera fade fn_1_86A2C (which hides the own
machine in views 0 and 1), the burner's fade fn_1_9A0A8, and the effects' tests "is this machine's camera in view 0?".
With only the view opened, a seat but 0 would look through its own machine from inside and seat 0's car would take the
effects. The sites below, in the native form, make those reads say 0 for the racer the cameras follow (rankicons.followed:
the local seat while racing, the racer watched by a spectator or a replay's viewer) and 0xFF for the others. With
sites_camera.py `camera_view` (a machine with no slot is in no view 0), that is the stock game's behaviour for the
machine actually viewed. Nothing else in the game asks for the view: the only readers are fn_1_725C's callers and the
chase camera itself, none of the HUD; fn_1_715C's change of the engine's sound class on entering and leaving view 0
(0xA5000000, class 8; `.bss 0xF78`) is the game's own and left. Readers of +0x475 not about the view stay seat 0's
(study: versus-race-flow.md).

Where the reads' answers go [code]: in drawings (fn_1_59EE4 kind 5, fn_1_86A2C, fn_1_9A0A8 from fn_1_98F28) and in the
effects' step code, to an effect record's numbers (fn_1_5D378, fn_1_5D500, fn_1_5F654, fn_1_64FB8, fn_1_6A7A4) or to
whether an effect is made (fn_1_6BFB8 kind 12's sparks, the streak fn_1_6E140 of fn_1_68B6C, fn_1_694A4, fn_1_69BCC).
None of those branches reaches the executable's generator 0x801A63C0 (the effect kinds that draw from it are 30 and 38,
and the spawners fn_1_6D7BC, fn_1_6F90C, fn_1_6FFA0, fn_1_702A0, fn_1_71D0C: never behind a view test; kinds 12 and 22
do not draw from it). They do change how often the effects' generator .data 0x1D628 is drawn, which differs between two
games at every step already and is read by no step of a machine. The helpers write no memory and draw nothing. A
machine's block is not written: the race does not depend on them, and a replay of a race in view 0 played in another
view must and does give the same checksums.

The view stays where the game keeps it (the camera record's +0xA4, in memory a rollback puts back; the checksum does
not cover it). Nothing here writes a machine's block: +0x474 and +0x475 are in the checksum and stay as they are.
The view a race starts in is the player's last (the launch file's "match.camera"): the core writes it into the
game's options record before the go, where the race start's camera set-up takes it (port/core/src/boot.cpp
WriteStartView), and tells the launcher each view chosen (board_race.cpp CameraView).

`bits` needs two registers besides its result: it keeps r3 and r4 in the control block (memmap.VIEW_SCRATCH) and
puts them back, since the second site's own instruction reads them in a plain start. Control block +0x3BC: the
main.rel address the sites were patched for (follow.installer)."""
import memmap
import native
import rankicons
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
    cmpwi 4, 0x28
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


# --- the first person view's reads, in the native form. (text offset, machine's register, register read into), each
# `lbz rt, 0x475(ra)`; 0x5D41C and 0x5D570 the `extsb r29, r3` after a call of fn_1_86634, where r30 is the effect
# (+0x18 its machine's number).
SLOT_READS = [(0x59F98, 3, 3), (0x59FB4, 23, 3), (0x5F75C, 29, 3), (0x5F778, 29, 3), (0x65088, 29, 3), (0x650A4, 29, 3),
              (0x693A8, 29, 3), (0x693B8, 30, 3), (0x69B74, 3, 3), (0x6A644, 3, 3), (0x6AC08, 28, 3), (0x6AC24, 28, 3),
              (0x6C41C, 28, 3), (0x6C438, 28, 3), (0x6C6D0, 28, 3), (0x6C6EC, 28, 3), (0x86A54, 30, 3), (0x86A7C, 30, 4),
              (0x86A9C, 30, 3), (0x86B00, 30, 3), (0x86C50, 30, 3), (0x9A0D0, 30, 3), (0x9A0E8, 30, 4)]
SLOT_CALLS = (0x5D41C, 0x5D570)
SLOT_CALL_STOCK = 0x7C7D0774     # extsb r29, r3


def slot_word(ra, rt):
    return 0x88000475 | rt << 21 | ra << 16


def slot_of(seat_read, rt, label):
    """rt = 0 for the racer the cameras follow, 0xFF for another; `seat_read` the machine's number into r0. Uses r12 and
    CR0, which the build proves dead after a site; r0 it keeps on the stack, as it cannot be proven dead across the calls
    that follow most reads (and a word of the control block would not be safe from another context's drawing)."""
    return f"""    stwu 1, -16(1)
    stw 0, 8(1)
""" + rankicons.followed(label) + f"""{seat_read}    cmpw 0, 12
    li {rt}, 0xFF
    bne {label}other
    li {rt}, 0
{label}other:
    lwz 0, 8(1)
    addi 1, 1, 16
    blr
"""


def first_person():
    """The sites and helpers of the native form: the view's lowest left to the game, the reads above added."""
    sites = [s for s in SITES if s[3] != "low"]
    helpers = {k: v for k, v in HELPERS.items() if k != "low"}
    for off, ra, rt in SLOT_READS:
        name = f"s{ra}_{rt}"
        sites.append((off, slot_word(ra, rt), 0xFFFFFFFF, name))
        helpers.setdefault(name, slot_of(f"    lha 0, 4({ra})\n", rt, f"vs{ra}_{rt}_"))
    for off in SLOT_CALLS:
        sites.append((off, SLOT_CALL_STOCK, 0xFFFFFFFF, "effect"))
    helpers["effect"] = slot_of("    lha 0, 0x18(30)\n", 29, "vse_")
    return sites, helpers


def asm():
    """The helpers and `raceview_install` (r31 = the control block; called with bl; uses r0, r3-r12 and 32(r1))."""
    if native.ON:
        sites, helpers = first_person()
        return installer("raceview_install", "v", sites, helpers, GUARD)
    return installer("raceview_install", "v", SITES, HELPERS, GUARD)
