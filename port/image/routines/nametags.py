"""Floating name tags, for the mod image (modbuild.py): called in place of the `bl fn_1_C7224` (the HUD) in
fn_1_37AA8's per-viewport loop (main.rel text 0x37C74) by the image's tag hook, before the HUD itself. The view
matrix of the viewport is in the locked cache's mtxB there (fn_1_5948 put it there). Picture only: it reads the
race and writes nothing a step reads.

On in a network session (control block +116; a replay played as the race is one) or when control block +76 is set
(tests). Viewport 0 only. Only player seats get a tag, and only a few, close ones:
- a seat is a candidate when it is a player seat (the slot 'FZSI' +40, bit i = seat i), has a player name
  (memmap.NAMES, 32 bytes a seat, written by the core from the match: a CPU seat's is empty), is not driven by the
  AI in the last step run (its input row, memmap.INPUTS at step +124 less 1, has steering byte 0x80, the "AI
  drives" record the relay sends for a seat whose player left; looked at only while running, +120), is not the
  racer the HUD follows (fn_1_6B48), has not finished (flag 0x10000) and is not out by the game's own verdict
  (outtest.py: never under the Restore rule, a breakdown only at rest), is within FAR of the followed machine, and
  its tag point projects in front of the camera and on screen;
- distances are between the machines' drawn positions (the drawn matrix's translation +0x158/+0x168/+0x178, which
  smooth.py moves through a rollback's correction: a corrected rival's tag slides with its body), squared (no
  square root on the GameCube's FPU), so the fade is 1 at NEAR, falling as (FAR^2 - d^2) / (FAR^2 - NEAR^2) to 0
  at FAR, and the text's size 0.6 + 0.4 x that;
- SHOWN tags at most: the candidates with the smallest score, where a shown tag's score is its squared distance
  (0 while it has been shown for less than HOLD drawn frames) and any other's the squared distance times
  1 / (1 - MARGIN)^2: a challenger takes a shown tag's place only when it is MARGIN closer, and never within HOLD
  frames of that tag's arrival;
- the shown tags are placed in order of squared distance, a tag that was left out last frame counted MARGIN
  further: one whose screen position is within 70 x 16 pixels of a tag already placed is left out this frame
  (within 1.25 times that box when it was left out last frame), so in a pack the tag that is up stays up;
- each seat's tag fades in (shown and placed) or out (otherwise) as tagsfade.py says (in about 200 ms, eased;
  out in about 150 ms) and is drawn with alpha ease(fade) x distance fade: a tag never pops in or out. A seat off
  screen, finished, out or driven by the AI stays a candidate that is never chosen while its tag fades out where it
  is (score larger than any; seat word +0x200, and +0x400 for one that left the race, which fades out faster).
The point TAG_HEIGHT above the machine (drawn translation plus the up vector +0x1BC times the height) is projected
with the game's fn_8006F78C (mtxA = view, lbl_8006DCA4; tan(fovy/2) from fn_1_A71AC; out: 640x480 screen x, y and
view z), and the name drawn there with the HUD's text routines (font 5, the ASCII 18x18 the ghost names use, upper
case, centred above the point, with a drop shadow), depth-tested against the scene like the Time Attack ghost names
(fn_1_12A0E0: sort depth 90000.1 + 9000 / view z for fn_1_4954C, and fn_1_12A080 as the text's draw set-up, an
ortho projection with z compare), so a wall in front hides the tag.

State (memmap.TAGS, in the data window: no rollback puts it back, the checksum does not cover it, no step reads
it): +0 the drawn frames counted (one a call), +4/+8/+12 what tagsfade.fresh compares, then 16 bytes a seat from +0x10: +0 the fade (float), +4 the frame
it was last chosen shown, +8 flags (1 shown, 2 left out by the overlap rule), +12 not used. Not the control block's
+80/+84/+88 and +0x200 for the distances and the names: nothing writes those for the tags (and other words of the
block use them), so the tags would never be on in a players' game."""
import json, os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import memmap, outtest, tagsfade
from tagsfade import FADE_IN, FADE_OUT, FADE_LEAVE

NEAR, FAR, TAG_HEIGHT = 15.0, 40.0, 2.5   # game units (1 unit = 1 m: a machine at 216 km/h moves 1 a step)
SHOWN, HOLD, MARGIN = 3, 30, 0.2           # tags, drawn frames, share of the distance
PLAYERS = memmap.IMAGE + 0x10 + 40        # the slot 'FZSI' +40: the player seats
TEXT = 0xDC
# constants at r23 (written to the stack on every call): 0 one, 4 255, 8 x min, 12 x max, 16 y max, 20 zero,
# 24/28 the ghost names' sort-depth constants, 32/36 size slope and base, 40/44 the overlap box (pixels), 48 NEAR^2,
# 52 FAR^2, 56 1 / (FAR^2 - NEAR^2), 60 a challenger's factor, 64 the fade's step, 68 TAG_HEIGHT, 72 the box's growth
# for a tag left out, 76 larger than any score, 80 the fade-out step, 84 the leaving fade-out step, 88 two
CONSTS = (1.0, 255.0, -100.0, 740.0, 520.0, 0.0, 90000.1, 9000.0, 0.4, 0.6, 70.0, 16.0, NEAR * NEAR, FAR * FAR,
          1.0 / (FAR * FAR - NEAR * NEAR), 1.0 / (1.0 - MARGIN) ** 2, 1.0 / FADE_IN, TAG_HEIGHT, 1.25, 1.0e30,
          1.0 / FADE_OUT, 1.0 / FADE_LEAVE, 2.0)
# stack: 0x10 world point, 0x20 projected x, y, view z, 0x30 scratch, 0x38 colour, 0x40 candidates (32 bytes: x, y,
# view z, distance fade, seat (| 0x100 once chosen), score, d^2, order key, then drawn alpha), 0x400 placed tags
# (8 bytes: x, y), 0x420 the constants, 0x480 r19-r31, 0x4B8 f26-f31
CAND, PLACED, K, GPRS, FPRS, FRAME = 0x40, 0x400, 0x420, 0x480, 0x4B8, 0x4F0
assert CAND + 30 * 32 <= PLACED and PLACED + SHOWN * 8 <= K and K + 4 * len(CONSTS) <= GPRS
assert GPRS + 13 * 4 <= FPRS and FPRS + 6 * 8 <= FRAME and memmap.TAGS + 0x10 + 30 * 16 <= memmap.RACE


def rel(fn):             # call main.rel text function fn (r29 = main.rel's header)
    a = TEXT + fn
    hi, lo = (a + 0x8000) >> 16, a & 0xFFFF
    lo = lo - 0x10000 if lo >= 0x8000 else lo
    return f"    addis 12, 29, {hi}\n    addi 12, 12, {lo}\n    mtctr 12\n    bctrl\n"


def dol(a):              # call an absolute main.dol address
    return f"    lis 12, {a >> 16}\n    ori 12, 12, {a & 0xFFFF}\n    mtctr 12\n    bctrl\n"


def addr(fn):            # r3 = address of main.rel text function fn
    a = TEXT + fn
    hi, lo = (a + 0x8000) >> 16, a & 0xFFFF
    lo = lo - 0x10000 if lo >= 0x8000 else lo
    return f"    addis 3, 29, {hi}\n    addi 3, 3, {lo}\n"


def const(i, v):
    w = struct.unpack(">I", struct.pack(">f", v))[0]
    return f"    lis 0, {w >> 16}\n    ori 0, 0, {w & 0xFFFF}\n    stw 0, {4 * i}(23)\n"


def cand(reg, index):    # reg = the candidate `index`'s record on the stack
    return f"    slwi {reg}, {index}, 5\n    addi {reg}, {reg}, {CAND}\n    add {reg}, {reg}, 1\n"


def entry(reg, seat):    # reg = seat's state record (seat a register, may be reg)
    return f"    slwi {reg}, {seat}, 4\n    addi {reg}, {reg}, 16\n    add {reg}, {reg}, 22\n"


def asm():
    names = f"    lis 9, {memmap.hi(memmap.NAMES)}\n    ori 9, 9, {memmap.lo(memmap.NAMES)}\n"
    return f"""
    stwu 1, -{FRAME:#x}(1)
    mflr 0
    stw 0, {FRAME + 4:#x}(1)
    stmw 19, {GPRS:#x}(1)
""" + "".join(f"    stfd {26 + i}, {FPRS + 8 * i:#x}(1)\n" for i in range(6)) + f"""
    lis 31, {memmap.hi(memmap.CONTROL)}
    lwz 0, 116(31)
    lwz 9, 76(31)
    or. 0, 0, 9
    beq out
    addi 23, 1, {K:#x}
""" + "".join(const(i, v) for i, v in enumerate(CONSTS)) + """
    lis 12, 0x8000
    lwz 29, 0x30C8(12)
    addis 30, 29, 0x1C
    addi 30, 30, -0x5FC0
""" + rel(0x5910) + """
    cmpwi 3, 0
    bne out
    li 3, 0
""" + rel(0x6B48) + """
    mr 25, 3
""" + dol(0x8006DCA4) + rel(0xA71AC) + f"""
    fmr 31, 1
    addis 9, 30, 7
    lwz 28, -0x27C8(9)
    lbz 26, 0x3C39(30)
    cmplw 25, 26
    bge out
    mulli 9, 25, 0x620
    add 9, 9, 28
    lfs 28, 0x158(9)
    lfs 29, 0x168(9)
    lfs 30, 0x178(9)
    lis 22, {memmap.hi(memmap.TAGS)}
    ori 22, 22, {memmap.lo(memmap.TAGS)}
    lwz 9, 0(22)
    addi 9, 9, 1
    stw 9, 0(22)
""" + tagsfade.fresh(22, 25) + f"""    li 27, 0
    li 21, 0
collect:
    cmpw 27, 26
    bge select
""" + entry(19, 27) + f"""
    li 20, 0
    cmpw 27, 25
    beq c_not
    slwi 3, 27, 5
{names}    lbzx 0, 3, 9
    cmpwi 0, 0
    beq c_not
    lis 9, {memmap.hi(PLAYERS)}
    lwz 0, {memmap.lo(PLAYERS)}(9)
    srw 0, 0, 27
    andi. 0, 0, 1
    beq c_not
    lwz 0, 120(31)
    cmpwi 0, 0
    beq c_row
    lwz 9, 124(31)
    addi 9, 9, -1
    andi. 9, 9, 1023
    mulli 9, 9, 30
    add 9, 9, 27
    slwi 9, 9, 3
    lis 10, {memmap.hi(memmap.INPUTS)}
    add 9, 9, 10
    lbz 0, 1(9)
    cmpwi 0, 0x80
    bne c_row
    li 20, 2
c_row:
    mulli 24, 27, 0x620
    add 24, 24, 28
    lwz 0, 0(24)
    andis. 9, 0, 0x0001
    bne c_leave
""" + outtest.clause(24, 30, 0, 9, "c_leave", "c_tag") + """c_leave:
    li 20, 2
c_tag:
    lfs 1, 0x158(24)
    fsubs 1, 1, 28
    fmuls 4, 1, 1
    lfs 1, 0x168(24)
    fsubs 1, 1, 29
    fmadds 4, 1, 1, 4
    lfs 1, 0x178(24)
    fsubs 1, 1, 30
    fmadds 4, 1, 1, 4
    lfs 0, 52(23)
    fcmpu 0, 4, 0
    bge c_not
    stfs 4, 0x30(1)
    lfs 0, 68(23)
    lfs 1, 0x158(24)
    lfs 2, 0x1BC(24)
    fmadds 1, 2, 0, 1
    stfs 1, 0x10(1)
    lfs 1, 0x168(24)
    lfs 2, 0x1C0(24)
    fmadds 1, 2, 0, 1
    stfs 1, 0x14(1)
    lfs 1, 0x178(24)
    lfs 2, 0x1C4(24)
    fmadds 1, 2, 0, 1
    stfs 1, 0x18(1)
    addi 3, 1, 0x10
    addi 4, 1, 0x20
    fmr 1, 31
""" + dol(0x8006F78C) + f"""
    lfs 1, 0x28(1)
    fneg 1, 1
    lfs 0, 0(23)
    fcmpu 0, 1, 0
    blt c_not
    lfs 3, 0x20(1)
    lfs 4, 0x24(1)
    lfs 0, 8(23)
    fcmpu 0, 3, 0
    blt c_off
    lfs 0, 12(23)
    fcmpu 0, 3, 0
    bgt c_off
    lfs 0, 20(23)
    fcmpu 0, 4, 0
    blt c_off
    lfs 0, 16(23)
    fcmpu 0, 4, 0
    ble c_on
c_off:
    cmpwi 20, 0
    bne c_on
    li 20, 1
c_on:
    lfs 5, 0x30(1)
    lfs 0, 52(23)
    fsubs 2, 0, 5
    lfs 0, 56(23)
    fmuls 2, 2, 0
    lfs 0, 0(23)
    fcmpu 0, 2, 0
    ble c_fade
    fmr 2, 0
c_fade:
""" + tagsfade.gone(20, 19, 23) + cand(9, 21) + f"""
    stfs 3, 0(9)
    stfs 4, 4(9)
    stfs 1, 8(9)
    stfs 2, 12(9)
    stw 27, 16(9)
    stfs 5, 24(9)
""" + tagsfade.leaving(20, 27, 9, 23) + f"""    lwz 0, 8(19)
    fmr 6, 5
    andi. 10, 0, 2
    beq c_key
    lfs 0, 60(23)
    fmuls 6, 5, 0
c_key:
    stfs 6, 28(9)
    andi. 10, 0, 1
    bne c_held
    lfs 0, 60(23)
    fmuls 6, 5, 0
    b c_score
c_held:
    fmr 6, 5
    lwz 10, 0(22)
    lwz 11, 4(19)
    subf 10, 11, 10
    cmpwi 10, {HOLD}
    bge c_score
    lfs 6, 20(23)
c_score:
    stfs 6, 20(9)
    addi 21, 21, 1
    b c_next
c_not:
    li 0, 0
    stw 0, 0(19)
    stw 0, 8(19)
c_next:
    addi 27, 27, 1
    b collect
select:
    li 20, 0
s_pick:
    cmpwi 20, {SHOWN}
    bge overlap
    li 19, -1
    li 27, 0
    lfs 26, 76(23)
s_scan:
    cmpw 27, 21
    bge s_scanned
""" + cand(9, 27) + """
    lfs 0, 20(9)
    fcmpu 0, 0, 26
    bge s_skip
    fmr 26, 0
    mr 19, 27
s_skip:
    addi 27, 27, 1
    b s_scan
s_scanned:
    cmpwi 19, 0
    blt overlap
""" + cand(9, 19) + """
    lfs 0, 76(23)
    stfs 0, 20(9)
    lwz 0, 16(9)
    ori 0, 0, 0x100
    stw 0, 16(9)
    addi 20, 20, 1
    b s_pick
overlap:
    li 20, 0
o_pick:
    li 19, -1
    li 27, 0
    lfs 26, 76(23)
o_scan:
    cmpw 27, 21
    bge o_scanned
""" + cand(9, 27) + """
    lwz 0, 16(9)
    andi. 0, 0, 0x100
    beq o_skip
    lfs 0, 28(9)
    fcmpu 0, 0, 26
    bge o_skip
    fmr 26, 0
    mr 19, 27
o_skip:
    addi 27, 27, 1
    b o_scan
o_scanned:
    cmpwi 19, 0
    blt draw
""" + cand(24, 19) + """
    lfs 0, 76(23)
    stfs 0, 28(24)
    lwz 9, 16(24)
    andi. 9, 9, 0xFF
""" + entry(19, 9) + f"""
    lfs 5, 40(23)
    lfs 6, 44(23)
    lwz 0, 8(19)
    andi. 0, 0, 2
    beq o_box
    lfs 0, 72(23)
    fmuls 5, 5, 0
    fmuls 6, 6, 0
o_box:
    lfs 1, 0(24)
    lfs 2, 4(24)
    li 27, 0
o_ovl:
    cmpw 27, 20
    bge o_free
    slwi 9, 27, 3
    addi 9, 9, {PLACED:#x}
    add 9, 9, 1
    lfs 3, 0(9)
    fsubs 3, 1, 3
    fabs 3, 3
    fcmpu 0, 3, 5
    bge o_ovl_next
    lfs 3, 4(9)
    fsubs 3, 2, 3
    fabs 3, 3
    fcmpu 0, 3, 6
    blt o_blocked
o_ovl_next:
    addi 27, 27, 1
    b o_ovl
o_blocked:
    lwz 0, 8(19)
    ori 0, 0, 2
    stw 0, 8(19)
    b o_pick
o_free:
    lwz 0, 8(19)
    andi. 0, 0, 1
    stw 0, 8(19)
    slwi 9, 20, 3
    addi 9, 9, {PLACED:#x}
    add 9, 9, 1
    stfs 1, 0(9)
    stfs 2, 4(9)
    addi 20, 20, 1
    b o_pick
draw:
    li 27, 0
d_loop:
    cmpw 27, 21
    bge out
""" + cand(24, 27) + """
    lwz 9, 16(24)
    andi. 10, 9, 0xFF
""" + entry(19, 10) + """
    lwz 0, 8(19)
    lfs 1, 0(19)
    lfs 2, 64(23)
    andi. 11, 9, 0x100
    beq d_down
    andi. 10, 0, 1
    bne d_since
    lwz 10, 0(22)
    stw 10, 4(19)
d_since:
    andi. 10, 0, 2
    bne d_left_out
    li 0, 1
    stw 0, 8(19)
    fadds 1, 1, 2
    lfs 0, 0(23)
    fcmpu 0, 1, 0
    ble d_store
    fmr 1, 0
    b d_store
d_left_out:
    li 0, 3
    stw 0, 8(19)
    b d_fall
d_down:
    li 0, 0
    stw 0, 8(19)
d_fall:
""" + tagsfade.down(2, 9, 11, 23, 80, 84) + """
    fsubs 1, 1, 2
    lfs 0, 20(23)
    fcmpu 0, 1, 0
    bge d_store
    fmr 1, 0
d_store:
    stfs 1, 0(19)
    lfs 0, 20(23)
    fcmpu 0, 1, 0
    ble d_next
""" + tagsfade.ease(1, 0, 23, 88) + """
    lfs 0, 12(24)
    fmuls 1, 1, 0
    lfs 0, 4(23)
    fmuls 0, 1, 0
    fctiwz 0, 0
    stfd 0, 0x30(1)
    lwz 0, 0x34(1)
    lis 9, -1
    ori 9, 9, 0xFF00
    or 0, 0, 9
    stw 0, 0x38(1)
""" + rel(0x49410) + """
    li 3, 5
""" + rel(0x494DC) + rel(0x4962C) + """
    li 3, 0x11
""" + rel(0x495C8) + """
    addi 3, 1, 0x38
""" + rel(0x49514) + rel(0x495FC) + """
    lfs 1, 28(23)
    lfs 2, 8(24)
    fdivs 1, 1, 2
    lfs 0, 24(23)
    fsubs 1, 0, 1
""" + rel(0x4954C) + addr(0x12A080) + rel(0x49738) + """
    lfs 0, 12(24)
    lfs 1, 32(23)
    lfs 2, 36(23)
    fmadds 1, 0, 1, 2
    fmr 2, 1
""" + rel(0x4955C) + """
    lfs 1, 0(24)
    lfs 2, 4(24)
""" + rel(0x496FC) + f"""
    lwz 3, 16(24)
    andi. 3, 3, 0xFF
    slwi 3, 3, 5
{names}    add 3, 3, 9
""" + rel(0x4A0D8) + f"""
d_next:
    addi 27, 27, 1
    b d_loop
out:
""" + "".join(f"    lfd {26 + i}, {FPRS + 8 * i:#x}(1)\n" for i in range(6)) + f"""
    lmw 19, {GPRS:#x}(1)
    lwz 0, {FRAME + 4:#x}(1)
    mtlr 0
    addi 1, 1, {FRAME:#x}
    blr
"""


if __name__ == "__main__":
    from keystone import Ks, KS_ARCH_PPC, KS_MODE_PPC32, KS_MODE_BIG_ENDIAN
    import monitors          # the image's tag part: the hand-back's monitors first (monitors.py), then the tags
    code = bytes(Ks(KS_ARCH_PPC, KS_MODE_PPC32 + KS_MODE_BIG_ENDIAN).asm(monitors.wrap(asm()))[0])
    print(json.dumps({"image": code.hex()}))
