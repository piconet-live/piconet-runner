"""The rejoin hand-back's 3, 2, 1 on the start's own floating countdown monitors, for the mod image (nametags.py's
part: `wrap` puts a call to it before the name tags, at the HUD's call in fn_1_37AA8's per-viewport loop, which runs
before the draw manager's tasks with the viewport's camera and projection set by fn_1_5948). Picture only: it
writes its record and state in the data window (memmap.MONITORS), the screen models' texture objects (model data
no step reads) and the GX pipe; it takes no random value, runs none of the game's countdown tasks and sets no rule
flag.

When: a hand-back is on (memmap.HANDBACK, the host's: steps left until control comes back; while it is not 0 the
step of the hand-back, control block +124 plus it, is kept at MONITORS +0, so that GO stays up after it reaches 0),
the pictures are read (+4 = 2, sites_monitors.py), the start's movie is freed (.bss 0x85280 +0x248 = 2: the start
countdown is over, its texture objects are free to take), viewport 0. The countdown c = steps left + 59, the
start's own count (start_countdown, fn_1_34924) with control coming back where the start's GO is (count 59): drawn
while 0 < c <= 420, the start's 420 steps, so GO stays up 59 steps after control is back. A hand-back of 240 steps
starts at c = 299: the monitors are in place with the blank screen, then 3, 2, 1 from 180 steps left. A hand-back
step within SAME steps of the last one is the same hand-back (the sounds and the picture go on).

Where (Starter, fn_1_1049E0, and fn_1_104730, with the start's first layout always; .rodata 0x78D0): from the
followed racer's (fn_1_6B48) drawn matrix (block +0x14C, which smooth.py moves through a rollback's correction),
eased every drawn frame (monitors_ease.py: the frame follows the machine softly, about 0.2 s behind, and snaps where
the picture is not continuous): forward f (the eased frame's back vector, negated), up u (its up), the point p (its
position); the key points p + 1040f + 9.5u, p + 40f + 9.5u, p + 140f + 109.5u. Between the start's thresholds 420, 360, 300,
40, 0: from the first key point to the second with a spin (yaw 600 a step, left to 0 at 360); a tilt from -20 to
15 degrees; held; off to the third key point, spinning away. The record's matrix is the eased frame's turn (right,
up, back; no translation), so the monitors keep their place in front of the racer through bends.

How (StarterDisp, fn_1_1050D0, with one viewport): fn_1_7BEAC(0) and its GX state, then three passes over the
common model list's monitor models (.bss 0x38460 +8, +8): the spinning parts (+0x08 to +0x28, turned by the frame
counter 0x801A66A0) and the screen (+0x30) as they are, the same mirrored (scale x -1, cull flipped), and the back
(+0x90, +0x38). The screens' texture objects (the texture entries of models +0x30 and +0x38 with byte +0xD = 1,
which fn_1_10688C bound to the movie's buffer) are bound to our texture each drawn frame.

StarterDisp's box of six faces is left out (drawn, it shows as a flickering black patch beside the monitors).
Its state, through the executable's cached GX wrappers (cache at [0x801A6D38]): GXSetVtxDesc position only
(0x8007245C(0x200)), GXSetNumTevStages(1) (0x80073678, fn_800377C8), GXSetTevOrder(0, none, none, colour none)
(0x800734A8), GXSetTevOp(0, PASSCLR) (0x80072EDC), GXSetBlendMode(BLEND, ZERO, ONE) (0x800728A8, fn_80037B14),
GXSetZMode(on, ALWAYS, update) (0x80074918, fn_80037BC0), GXSetCullMode(FRONT) (0x80072864, fn_80034890), then six
quads of positions (GXBegin 0x8003462C(0x80, 0, 4), .data 0x3F28C / 0x3F2EC), cull back to NONE, and the models' z
mode LESS (fn_1_56000(1, 1, 1): the model drawer's z mode at .bss 0x6C7A4 +0xC) until LEQUAL again after them. The
box adds nothing to the colour (source x 0 + destination x 1) and writes its far faces' depth whatever is there: a
depth-only box. In the game StarterDisp is a draw-manager task of priority 0x1A (main.rel 0x39BB8), run after
StageDisp (fn_1_78EBC, priority 7) and the scene's other lower priorities (fn_1_407C runs them upward), so the box
pushes the scene's depth back behind the monitors and the track never cuts them. Ours runs at the HUD's call, before
fn_1_407C runs the draw manager's tasks for the viewport (fn_1_37AA8, main.rel 0x37C74 then 0x37C78), on the depth
buffer the frame's copy has just cleared (GXCopyDisp with clear, 0x8006FDA4): there the box puts its far depth in
front of everything drawn after it, so inside its outline every part of the track, sky and machines further away
than its back fails the depth test and the EFB keeps its clear colour, the black patch; as the monitors move and
spin and the scene behind changes depth, which parts pass changes frame to frame, the flicker [inferred from the
code; not watched]. Without the box the models write their own depth into the cleared buffer and the scene drawn
after them is tested against it: only what is really in front of a monitor (a wall in a bend) covers it, which the
game's box would have prevented. The box's state calls are kept but for the front culling (the cache ends at cull
NONE as in the game), so the models start from the state they had with the box.

The screen's picture: the movie frames 66 (blank), 90 (3), 120 (2), 150 (1), 180 (GO) of jpeg/compo.arc, read from
the disc before the race (sites_monitors.py) into a block of the main heap; the one c asks for (c >= 240 blank, then
one a 60 steps) is decoded by the game's JPEG decoder (fn_1_151AF0, as the movie's thread calls it) into the block's
128 x 128 RGBA8 texture when it changes, and the texture flushed from the data cache. The block is main memory, which
a rollback puts back: a restore to a step before a decode would leave the last picture in the texture, so sixteen
of its words are kept with the decode (+44) and the picture is decoded again whenever they differ.

The sounds: the start's own ids through fn_1_A2D84 (the start's sound gate, then the sound queue), from the drawing
only, each once, when c passes its count since the last drawn frame: 360 0xA9091800, 239 0xA9091A00 + 0xA9060000
("three"), 179 + 0xA9060100, 119 + 0xA9060200, 59 + 0xA9060300 ("go"), 40 0xA9091900 (the take-off)."""
import json, os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import memmap, monitors_ease

TEXT = 0xDC
RODATA = 0x15F710 + 0x78D0          # main.rel .rodata 0x78D0 from the module's header
BSS = 0x1BA040
FRAMES = (66, 90, 120, 150, 180)    # the movie's frames: blank, 3, 2, 1, GO
TEXTURE, WORK, JPEGS, JPEG_SLOT = 0, 0x10000, 0x14000, 0x1000
assert JPEGS + len(FRAMES) * JPEG_SLOT == memmap.MONITORS_BLOCK
SOUNDS = ((360, (0xA9091800,)), (239, (0xA9091A00, 0xA9060000)), (179, (0xA9091A00, 0xA9060100)),
          (119, (0xA9091A00, 0xA9060200)), (59, (0xA9091A00, 0xA9060300)), (40, (0xA9091900,)))
END, STATE, BLOCK, SEEN, SHOWN, LAST, CHECK, RECORD = 0, 4, 8, 32, 36, 40, 44, 0x40
FRAME, GPRS = 0x100, 0xB0
SAME = 30                           # steps: a hand-back step this close to the last one is the same hand-back
assert GPRS + 18 * 4 <= FRAME and RECORD + 0x88 <= 0x100


def rel(fn):             # call main.rel text function fn (r29 = main.rel's header)
    a = TEXT + fn
    hi, lo = (a + 0x8000) >> 16, a & 0xFFFF
    lo = lo - 0x10000 if lo >= 0x8000 else lo
    return f"    addis 12, 29, {hi}\n    addi 12, 12, {lo}\n    mtctr 12\n    bctrl\n"


def dol(a):              # call an absolute main.dol address
    return f"    lis 12, {a >> 16}\n    ori 12, 12, {a & 0xFFFF}\n    mtctr 12\n    bctrl\n"


def at(reg, base, offset):   # reg = base + offset (any 32-bit offset)
    hi, lo = (offset + 0x8000) >> 16, offset & 0xFFFF
    lo = lo - 0x10000 if lo >= 0x8000 else lo
    return f"    addis {reg}, {base}, {hi}\n    addi {reg}, {reg}, {lo}\n"


def tofloat(fr, reg):    # fr = (float) reg, signed (the magic double at .rodata 0x78D0 +8)
    return (f"    xoris 0, {reg}, 0x8000\n    stw 0, 0x2C(1)\n    lis 0, 0x4330\n    stw 0, 0x28(1)\n"
            f"    lfd {fr}, 0x28(1)\n    lfd 13, 8(30)\n    fsub {fr}, {fr}, 13\n    frsp {fr}, {fr}\n")


def model(offset):       # draw the common list's model at `offset`
    return f"    lwz 3, 8(31)\n    lwz 3, 8(3)\n    lwz 3, {offset:#x}(3)\n" + rel(0x556B8)


MATRIX = (dol(0x8006DCA4) + "    mr 3, 28\n" + dol(0x8006E0A4) + "    lha 3, 0x80(28)\n" + dol(0x8006E398) +
          "    addi 3, 28, 0x48\n" + dol(0x8006DFC4))
TILT = ("    lfs 1, 4(30)\n    lfs 0, 0x78(28)\n    fmuls 0, 1, 0\n    fctiwz 0, 0\n    stfd 0, 0x38(1)\n"
        "    lwz 3, 0x3C(1)\n" + dol(0x8006E324))
LIGHT = "    li 3, 0\n    lis 4, 0x801A\n    lwz 4, 0x6D00(4)\n" + rel(0x560F0)


def spin(magic, period, negate):   # mtxA from the stack's top, turned about x by the frame counter mod period
    return dol(0x8006DBE4) + f"""    lis 4, 0x801A
    lwz 4, 0x66A0(4)
    lis 3, {magic >> 16}
    ori 3, 3, {magic & 0xFFFF}
    mulhwu 0, 3, 4
    srwi 0, 0, 5
    mulli 0, 0, {period}
    subf 0, 0, 4
    slwi 0, 0, 16
""" + ("    neg 0, 0\n" if negate else "") + f"""    mulhwu 0, 3, 0
    srwi 0, 0, 5
    extsh 3, 0
""" + dol(0x8006E324)


def signature():         # r10 = sixteen words of the texture mixed (r21 its block): what a decode left there
    out = "    li 10, 0\n    addis 11, 21, 1\n"
    for k in range(16):
        base, off = (21, k * 0x1000) if k < 8 else (11, (k - 16) * 0x1000)
        out += f"    lwz 0, {off:#x}({base})\n    rotlwi 10, 10, 1\n    add 10, 10, 0\n"
    return out


def sounds():
    out = ""
    for i, (count, ids) in enumerate(SOUNDS):
        out += f"    cmpwi 22, {count}\n    ble mn_snd{i}\n    cmpwi 25, {count}\n    bgt mn_snd{i}\n"
        for sid in ids:
            out += f"    lis 3, {sid >> 16}\n    ori 3, 3, {sid & 0xFFFF}\n" + rel(0xA2D84)
        out += f"mn_snd{i}:\n"
    return out


def asm():
    hb = f"    lis 27, {memmap.hi(memmap.MONITORS)}\n    ori 27, 27, {memmap.lo(memmap.MONITORS)}\n"
    return f"""monitors:
    stwu 1, -{FRAME:#x}(1)
    mflr 0
    stw 0, {FRAME + 4:#x}(1)
    stmw 14, {GPRS:#x}(1)
{hb}    lwz 0, {STATE}(27)
    cmpwi 0, 2
    bne mn_out
    lis 12, 0x8000
    lwz 29, 0x30C8(12)
""" + at(23, 29, BSS) + at(30, 29, RODATA) + at(9, 23, 0x85280 + 0x248) + f"""    lwz 0, 0(9)
    cmpwi 0, 2
    bne mn_out
""" + rel(0x5910) + f"""    cmpwi 3, 0
    bne mn_out
    lis 9, {memmap.hi(memmap.CONTROL)}
    lwz 10, 124(9)
    lwz 0, {memmap.HANDBACK - memmap.CONTROL}(9)
    cmpwi 0, 0
    beq mn_kept
    add 26, 10, 0
    stw 26, {END}(27)
    b mn_count
mn_kept:
    lwz 26, {END}(27)
    cmpwi 26, 0
    beq mn_out
mn_count:
    subf 25, 10, 26
    addi 25, 25, 59
    cmpwi 25, 0
    ble mn_out
    cmpwi 25, 420
    bgt mn_out
    lwz 0, {SEEN}(27)
    subf 0, 0, 26
    cmpwi 0, -{SAME}
    blt mn_new
    cmpwi 0, {SAME}
    ble mn_same
mn_new:
    stw 26, {SEEN}(27)
    li 0, 0
    stw 0, {SHOWN}(27)
    stw 0, {monitors_ease.FOLLOW}(27)
    addi 0, 25, 1
    stw 0, {LAST}(27)
mn_same:
    li 3, 0
""" + rel(0x6B48) + f"""    lbz 0, 0x3C39(23)
    cmplw 3, 0
    bge mn_out
    addi 22, 3, 1
    mulli 24, 3, 0x620
""" + at(9, 23, 0x6D838) + f"""    lwz 9, 0(9)
    add 24, 24, 9
    addi 28, 27, {RECORD}
""" + monitors_ease.asm() + f"""    lfs 7, 0xE4(30)
    lfs 8, 0xE0(30)
    lfs 9, 0x18(30)
    lfs 10, 0x10(30)
    lfs 11, 0x108(30)
    li 21, 0
    cmpwi 25, 360
    ble mn_s2
    addi 0, 25, -360
    mulli 21, 0, 600
    lfs 1, 0xDC(30)
    lfs 2, 0xE0(30)
    b mn_lerp
mn_s2:
    cmpwi 25, 300
    ble mn_s4
    addi 0, 25, -300
""" + tofloat(4, 0) + f"""    lfs 5, 0(30)
    fdivs 4, 4, 5
    fsubs 5, 10, 4
    lfs 6, 0xB4(30)
    fmuls 11, 11, 5
    fmadds 11, 6, 4, 11
    b mn_place
mn_s4:
    cmpwi 25, 40
    bgt mn_place
    subfic 0, 25, 40
    mulli 21, 0, 600
    fmr 1, 7
    fmr 2, 8
    lfs 7, 0xEC(30)
    lfs 8, 0xF0(30)
    mr 0, 25
""" + tofloat(9, 0) + "    li 0, 40\n" + tofloat(5, 0) + f"""    fdivs 9, 9, 5
    b mn_mix
mn_lerp:
    addi 0, 25, -360
""" + tofloat(9, 0) + f"""    lfs 5, 0(30)
    fdivs 9, 9, 5
    lfs 11, 0xB4(30)
mn_mix:
    fsubs 5, 1, 7
    fmadds 7, 5, 9, 7
    fsubs 5, 2, 8
    fmadds 8, 5, 9, 8
mn_place:
    stfs 11, 0x78(28)
    sth 21, 0x80(28)
    li 9, 0
mn_point:
    slwi 10, 9, 4
    add 10, 10, 28
    slwi 11, 9, 2
    add 11, 11, 28
    lfs 0, {monitors_ease.POS - RECORD}(11)
    lfs 1, 0x50(10)
    fnmsubs 0, 1, 7, 0
    lfs 1, 0x4C(10)
    fmadds 0, 1, 8, 0
    stfs 0, 0(11)
    addi 9, 9, 1
    cmpwi 9, 3
    blt mn_point
    lwz 22, {LAST}(27)
    stw 25, {LAST}(27)
""" + sounds() + f"""    li 20, 0
    cmpwi 25, 240
    bge mn_pick
    li 0, 60
    divw 0, 25, 0
    subfic 20, 0, 4
mn_pick:
    lwz 21, {BLOCK}(27)
""" + signature() + f"""    lwz 0, {SHOWN}(27)
    addi 9, 20, 1
    cmpw 0, 9
    bne mn_decode
    lwz 0, {CHECK}(27)
    cmpw 0, 10
    beq mn_bind
mn_decode:
    stw 9, {SHOWN}(27)
    slwi 9, 20, 12
""" + at(3, 21, JPEGS) + f"""    add 3, 3, 9
    li 4, 0
    addi 5, 21, {TEXTURE}
    li 6, 0
    li 7, 0
""" + at(8, 21, WORK) + rel(0x151AF0) + f"""    addi 3, 21, {TEXTURE}
    lis 4, 1
""" + dol(0x8000B654) + signature() + f"""    stw 10, {CHECK}(27)
mn_bind:
""" + at(31, 23, 0x38460) + f"""    lwz 9, 8(31)
    cmpwi 9, 0
    beq mn_out
    lwz 9, 8(9)
    li 19, 0x30
mn_models:
    lwz 9, 8(31)
    lwz 9, 8(9)
    lwzx 18, 9, 19
    cmpwi 18, 0
    beq mn_model_done
    lhz 17, 0x18(18)
    addi 18, 18, 0x40
mn_entry:
    cmpwi 17, 0
    ble mn_model_done
    lbz 0, 0xD(18)
    cmpwi 0, 1
    bne mn_entry_next
    lwz 3, 8(18)
    mr 4, 21
    li 5, 0x80
    li 6, 0x80
    li 7, 6
    li 8, 0
    li 9, 0
    li 10, 0
""" + dol(0x80035C50) + f"""mn_entry_next:
    addi 18, 18, 0x20
    addi 17, 17, -1
    b mn_entry
mn_model_done:
    addi 19, 19, 8
    cmpwi 19, 0x38
    ble mn_models
    li 3, 0
""" + rel(0x7BEAC) + MATRIX + dol(0x80072558) + dol(0x800724C8) + "    li 3, 0x200\n" + dol(0x8007245C) + \
        "    li 3, 1\n" + dol(0x80073678) + "    li 3, 0\n    li 4, 0xFF\n    li 5, 0xFF\n    li 6, 0xFF\n" + \
        dol(0x800734A8) + "    li 3, 0\n    li 4, 4\n" + dol(0x80072EDC) + \
        "    li 3, 1\n    li 4, 0\n    li 5, 1\n    li 6, 0\n" + dol(0x800728A8) + \
        "    li 3, 1\n    li 4, 7\n    li 5, 1\n" + dol(0x80074918) + "    li 3, 0\n" + dol(0x80072864) + \
        "    li 3, 1\n    li 4, 1\n    li 5, 1\n" + rel(0x56000) + f"""    li 18, 0
mn_pass:
""" + MATRIX + f"""    cmpwi 18, 1
    bne mn_pushed
    lfs 2, 0x10(30)
    lfs 1, 0x24(30)
    fmr 3, 2
""" + dol(0x8006E15C) + "    li 3, 1\n" + dol(0x80077BBC) + "mn_pushed:\n" + dol(0x8006DAEC) + f"""    cmpwi 18, 2
    beq mn_screen
""" + spin(0x1B4E81B5, 300, False) + dol(0x80072558) + model(0x08) + \
        spin(0x1B4E81B5, 300, True) + dol(0x80072558) + model(0x10) + \
        spin(0x51EB851F, 100, True) + dol(0x80072558) + model(0x18) + \
        spin(0x88888889, 60, False) + dol(0x80072558) + model(0x20) + \
        dol(0x8006DBE4) + dol(0x80072558) + model(0x28) + f"""mn_screen:
    cmpwi 18, 0
    bne mn_back
""" + dol(0x8006D758) + LIGHT + dol(0x8006DBE4) + TILT + dol(0x80072558) + model(0x30) + f"""    b mn_pop
mn_back:
    cmpwi 18, 2
    bne mn_mirror
""" + dol(0x8006D758) + LIGHT + dol(0x8006DBE4) + TILT + dol(0x80072558) + model(0x90) + \
        dol(0x8006DBE4) + TILT + dol(0x80072558) + model(0x38) + f"""    b mn_pop
mn_mirror:
    lfs 2, 0x18(30)
    lfs 1, 0x10(30)
    fmr 3, 2
""" + dol(0x8006D7F4) + f"""    lfs 2, 0x10(30)
    lfs 1, 0x24(30)
    fmr 3, 2
""" + dol(0x8006E15C) + LIGHT + dol(0x8006DBE4) + TILT + dol(0x80072558) + model(0x30) + f"""mn_pop:
""" + dol(0x8006DB30) + f"""    cmpwi 18, 1
    bne mn_next
    li 3, 0
""" + dol(0x80077BBC) + f"""mn_next:
    addi 18, 18, 1
    cmpwi 18, 2
    ble mn_pass
    li 3, 1
    li 4, 3
    li 5, 1
""" + rel(0x56000) + f"""mn_out:
    lmw 14, {GPRS:#x}(1)
    lwz 0, {FRAME + 4:#x}(1)
    mtlr 0
    addi 1, 1, {FRAME:#x}
    blr
"""


def wrap(tags):
    """The image's tag part: a call to the monitors, then the name tags' own text (entered at its first word, as the
    hook entered it before), then the monitors."""
    return """    stwu 1, -16(1)
    mflr 0
    stw 0, 20(1)
    bl monitors
    lwz 0, 20(1)
    mtlr 0
    addi 1, 1, 16
""" + tags + asm()


if __name__ == "__main__":
    from keystone import Ks, KS_ARCH_PPC, KS_MODE_PPC32, KS_MODE_BIG_ENDIAN
    code = bytes(Ks(KS_ARCH_PPC, KS_MODE_PPC32 + KS_MODE_BIG_ENDIAN).asm(asm())[0])
    print(json.dumps({"image": code.hex()}))
