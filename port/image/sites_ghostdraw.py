"""`seat.look = ghost`, as Time Attack draws its ghosts, and the mode part GHOST_GAP, Time Attack's live gap (piconet-docs
research/game-modes/vocabulary.md section 5). Drawing only: nothing a step reads is written here. sites.py calls add().

Our machines stay in the race's array, so the game's own ghost task (fn_1_E9770's GhostCalc and GhostDisp, which bring
the block .bss 0x7B180 with them) is not used. Time Attack's order is copied instead:

| Site | Where | What the game did | Now, with a mode |
|---|---|---|---|
| `ghost_make` | fn_1_36330 0x36A30, `bl fn_1_E9770` (r4 the draw tasks' set) | the ghosts' tasks, outside our races none | after the stock call, under the window's flag GHOST_TASK (a mode whose code asks the look ghost or GHOST_GAP): one task of ours in that set, at priority 0x1A, as fn_1_E9770 makes GhostDisp (0xE98EC to 0xE9904). Every game of the race makes it at the same point of the set-up, from the package alone |
| `ghost_disp` | that task | | each seat `mode_look` set aside this frame: fn_1_EE530's two passes, below. Then the gap, as fn_1_ED540 0xEDF40 to 0xEE148 draws it |
| `ghost_voice` | fn_1_A3D5C 0xA3DFC, `fcmpo cr0, f31, f0`: the machine r27 beyond hearing? | sound by distance | a seat in the window's silent mask takes the path of a machine past 300 units: no volume, as Time Attack's ghosts, which no voice is ever given to (`seat.ghost_sound = on` leaves the seat out of the mask). Other sounds (a mine, a hit) are not voices and stay |
| `ghost_pilot` | fn_1_84860 0x84DB8, `bl fn_1_A7854` (r29 the machine): the pilot, drawn at CarDisp at alpha 1 | the pilot | no call for a seat drawn as a ghost or invisible: Time Attack's draw path has no pilot (fn_1_EE530 calls neither fn_1_A7854 nor fn_1_A8270). fn_1_A77DC, the pose, stays, so it is current if the look changes back. The only other caller, fn_1_7C2A0 0x7C594, is in car_test.c, the developers' CAR_TEST screen: no reference to it from any race, left alone |
| `ghost_shadow` | fn_1_568F8's entry, CarShadowPreRend (r3 the record, +0x48 the machine) | the shadow's picture | not drawn for a seat this game draws as a ghost. Time Attack's draw path has no shadow call [inf] |

The two passes (fn_1_EE530, read [code]; `mode_look` in sites_modehud.py sets the seats aside and draws nothing at CarDisp):
1. The depth pass: the position pushed 0.05 away from the camera, the matrix scaled (d + 0.05) / d, alpha 0.01, the ordinary
   blend, through fn_1_875B4, so that only the front skin shows. fn_1_56298 and the tint first, as the game does.
2. The visible pass: alpha min(0.5, 2 x clamp(d2 - 15, 0, 50) / 35) of the squared distance d2 from the camera, the blend
   fn_80074A60(4, 1), additive; the tint; fn_1_875EC, which draws at once where fn_1_875B4 below alpha 1 queues; the
   resets. That alpha reaches only the moving parts (fn_1_875EC) and a custom machine (fn_1_14E5E4): a stock body (fn_1_556B8)
   draws at the global alpha, 1, additive, at any distance (ta-ghost-pilot.md section 4). A custom machine (car +0x390 bit 0x04000000) through fn_1_14E5E4 with the part's record +0x3A0 in both.

The first 60 frames fade in, x clock / 60 (fn_1_ED540 0xEDD2C): from the race clock, a function of the steps, where the game
has a counter of its own; so before GO a ghost is not seen, as in Time Attack.

The tint: the window's word for the seat (MODE_HUD_TINTS, `seat.ghost_tint`), 0 Time Attack's (1, 1, 0.7).
Not done: Time Attack's level of detail by the ghosts' count, the "G" and number cells of its map (our map's dots, map_hide and
map_mark, are a separate choice) and the 120-frame lap form of the gap from 0xEE158. The look is for rounds by eye."""
import memmap

MAKE, MAKE_KNOWN = 0x36A30, 0x480B2D41         # fn_1_36330: bl fn_1_E9770
GHOSTS_FN, TASK_SET, TASK_NEW = 0xE9770, 0x435C, 0x3F8C  # fn_1_435C(set) gives the set before; fn_1_3F8C(name, fn, 0, priority)
PRIORITY = 0x1A                                # GhostDisp's
TASK_NAME = 0x3E358 + 0x2E8                    # main.rel .data: the name fn_1_E9770 gives GhostDisp
VOICE, VOICE_KNOWN = 0xA3DFC, 0xFC1F0040       # fn_1_A3D5C: fcmpo cr0, f31, f0; 0xA3E04 the path of a machine out of hearing
VOICE_FAR = 0xA3E04
SHADOW, SHADOW_KNOWN = 0x568F8, 0x9421FED0     # fn_1_568F8: stwu r1, -0x130(r1)
PILOT, PILOT_KNOWN, PILOT_DRAW = 0x84DB8, 0x48022A9D, 0xA7854  # fn_1_84860: bl fn_1_A7854
TINT, RESET_STATE, DEPTH_STATE, DEPTH_ALPHA = 0x5621C, 0x56000, 0x56298, 0x55FF0
MODEL, COLOUR_TAB, DRAW_QUEUED, DRAW_NOW, DRAW_CUSTOM = 0x556B8, 0x87610, 0x875B4, 0x875EC, 0x14E5E4
VIEWPORT, CAMERA_AT = 0x5910, 0x681C           # fn_1_5910: the viewport; fn_1_681C(it, out): the camera's place
BLEND_ON, BLEND_OFF, LOAD_MATRIX = 0x80074A60, 0x80074A6C, 0x80072558
SQRT, NORMALISE, VIEW, TRANSLATE, CONCAT, SCALE, PUSH, POP = (0x8006D0B4, 0x8006D668, 0x8006DCA4, 0x8006E0A4, 0x8006DFC4,
                                                               0x8006E14C, 0x8006DAEC, 0x8006DC20)
TEXT_BEGIN, TEXT_FONT, TEXT_MODE, TEXT_SPACE, TEXT_FLAGS, TEXT_COLOUR, TEXT_SCALE, TEXT_Y, TEXT_X, TEXT_AT, TEXT_PRINT = (
    0x49410, 0x495FC, 0x495C8, 0x494DC, 0x495B0, 0x49514, 0x4955C, 0x51AC0, 0x519FC, 0x496FC, 0x4AE0C)
GAP_FORMAT = 0x3E358 + 0x3F8                   # main.rel .data: fn_1_ED540's r30 + 0x3F8, "%c%02d<%02d=%03d"
GAP_X, GAP_Y = 0x441A4000, 0x43870000          # 617.0, 270.0 (.rodata 0x6D18, 0x6D1C)
HALF, ONE, ZERO = 0x3F000000, 0x3F800000, 0
SEVEN_TENTHS, PUSHED, FAINT = 0x3F333333, 0x3D4CCCCD, 0x3C23D70A  # 0.7, 0.05, 0.01 (.rodata 0x6C18, 0x6CFC, 0x6D00)
FIFTEEN, FIFTY, THIRTY_FIVE, TWO, SIXTY = 0x41700000, 0x42480000, 0x420C0000, 0x40000000, 0x42700000
BYTE_SCALE = 0x3B808081
CUSTOM_BIT = 0x0400                            # car +0x390's bit 0x04000000, its upper half
FADE = 60


def add(k, r, patch, stock, test, la, middle):
    """Adds the routines to `r` and the sites' entries through `patch`, as sites.add does."""
    TEXT = k.TEXT
    FN = lambda offset: f"{TEXT + offset:#x}"

    def bits(reg, value):
        """Loads a float through the stack word 0x8(r1)."""
        return f"    lis 0, {value >> 16:#x}\n    ori 0, 0, {value & 0xFFFF:#x}\n    stw 0, 0x8(1)\n    lfs {reg}, 0x8(1)\n"

    def tint(label):
        """The seat's tint (r26 the window's word, 0 Time Attack's) as fn_1_5621C's f1 to f4, and the call."""
        return f"""    cmplwi 26, 0
    bne {label}_own
{bits("1", ONE)}    fmr 2, 1
{bits("3", SEVEN_TENTHS)}    fmr 4, 1
    b {label}_set
{label}_own:
    lis 0, 0x4330
    stw 0, 0x70(1)
    stw 0, 0x78(1)
    li 0, 0
    stw 0, 0x74(1)
    lfd 5, 0x70(1)
{bits("6", BYTE_SCALE)}    rlwinm 0, 26, 8, 24, 31
    stw 0, 0x7C(1)
    lfd 1, 0x78(1)
    rlwinm 0, 26, 16, 24, 31
    stw 0, 0x7C(1)
    lfd 2, 0x78(1)
    rlwinm 0, 26, 24, 24, 31
    stw 0, 0x7C(1)
    lfd 3, 0x78(1)
    fsub 1, 1, 5
    fsub 2, 2, 5
    fsub 3, 3, 5
    fmuls 1, 1, 6
    fmuls 2, 2, 6
    fmuls 3, 3, 6
{bits("4", ONE)}{label}_set:
    @bl {FN(TINT)}
"""

    def draw(label, alpha, fn):
        """The machine (r29 its car, r27 the level of detail) at the alpha in float register `alpha`, as fn_1_EE530."""
        return f"""    lhz 0, 0x390(29)
    andi. 0, 0, {CUSTOM_BIT}
    beq {label}_stock
    lwz 3, 0x3A0(29)
    cmplwi 3, 0
    beq {label}_done
    fmr 1, {alpha}
    mr 4, 27
    @bl {FN(DRAW_CUSTOM)}
    b {label}_done
{label}_stock:
    mulli 3, 27, 0xC
    add 3, 3, 29
    lwz 3, 0x334(3)
    lwz 3, 0(3)
    @bl {FN(MODEL)}
    @bl {PUSH:#x}
    mr 3, 29
    mr 4, 29
    @bl {FN(COLOUR_TAB)}
    fmr 1, {alpha}
    mr 3, 29
    mr 4, 29
    mr 5, 27
    @bl {FN(fn)}
    @bl {POP:#x}
{label}_done:
"""

    def stage_matrix(place):
        """The matrix from the turn on the stack and the place at `place`(r1), as fn_1_EE530 builds it."""
        return f"""    @bl {VIEW:#x}
    addi 3, 1, {place:#x}
    @bl {TRANSLATE:#x}
    addi 3, 1, 0x40
    @bl {CONCAT:#x}
"""

    # the task, once a picture (the frame's draw half): every seat set aside this frame, then the gap
    colour_at = memmap.MODE_HUD_GAP
    r.add("ghost_disp", f"""    stwu 1, -0x100(1)
    mflr 0
    stw 0, 0x104(1)
    stfd 31, 0xF0(1)
    stfd 30, 0xE8(1)
    stw 31, 0xE0(1)
    stw 30, 0xDC(1)
    stw 29, 0xD8(1)
    stw 28, 0xD4(1)
    stw 27, 0xD0(1)
    stw 26, 0xCC(1)
""" + la(30, memmap.MODE_HUD_GHOSTS) + """    li 31, 0
gd_seat:
    lwz 29, 0(30)
    cmplwi 29, 0
    beq gd_next
    li 0, 0
    stw 0, 0(30)
    lbz 0, 4(30)
    extsb 27, 0
    sth 27, 0x3BA(29)
    lwz 28, 0x32C(29)
    slwi 0, 31, 2
""" + la(12, memmap.MODE_HUD_TINTS) + f"""    lwzx 26, 12, 0
    lfs 0, 0x158(28)
    stfs 0, 0x10(1)
    lfs 0, 0x168(28)
    stfs 0, 0x14(1)
    lfs 0, 0x178(28)
    stfs 0, 0x18(1)
    li 0, 12
    mtctr 0
    addi 11, 28, 0x14C - 4
    addi 12, 1, 0x40 - 4
gd_turn:
    lwzu 0, 4(11)
    stwu 0, 4(12)
    bdnz gd_turn
    li 0, 0
    stw 0, 0x4C(1)
    stw 0, 0x5C(1)
    stw 0, 0x6C(1)
{bits("1", ZERO)}    fmr 2, 1
    fmr 3, 1
    fmr 4, 1
    @bl {FN(DEPTH_STATE)}
{bits("1", ONE)}    fmr 2, 1
    fmr 3, 1
    fmr 4, 1
    @bl {FN(TINT)}
    @bl {FN(VIEWPORT)}
    clrlwi 3, 3, 24
    addi 4, 1, 0x20
    @bl {FN(CAMERA_AT)}
    lfs 1, 0x10(1)
    lfs 0, 0x20(1)
    fsubs 1, 1, 0
    stfs 1, 0x20(1)
    lfs 2, 0x14(1)
    lfs 0, 0x24(1)
    fsubs 2, 2, 0
    stfs 2, 0x24(1)
    lfs 3, 0x18(1)
    lfs 0, 0x28(1)
    fsubs 3, 3, 0
    stfs 3, 0x28(1)
    fmuls 0, 1, 1
    fmadds 0, 2, 2, 0
    fmadds 0, 3, 3, 0
    stfs 0, 0x38(1)
    fmr 1, 0
    @bl {SQRT:#x}
    fmr 30, 1
    addi 3, 1, 0x20
    @bl {NORMALISE:#x}
{bits("3", PUSHED)}    lfs 0, 0x20(1)
    fmuls 0, 0, 3
    lfs 1, 0x10(1)
    fadds 0, 0, 1
    stfs 0, 0x20(1)
    lfs 0, 0x24(1)
    fmuls 0, 0, 3
    lfs 1, 0x14(1)
    fadds 0, 0, 1
    stfs 0, 0x24(1)
    lfs 0, 0x28(1)
    fmuls 0, 0, 3
    lfs 1, 0x18(1)
    fadds 0, 0, 1
    stfs 0, 0x28(1)
    li 3, 1
    li 4, 3
    li 5, 1
    @bl {FN(RESET_STATE)}
""" + stage_matrix(0x20) + f"""{bits("0", PUSHED)}    fadds 1, 0, 30
    fdivs 1, 1, 30
    @bl {SCALE:#x}
    @bl {LOAD_MATRIX:#x}
{bits("1", FAINT)}    fmr 31, 1
    @bl {FN(DEPTH_ALPHA)}
""" + tint("gd_t1") + draw("gd_d1", "31", DRAW_QUEUED) + f"""{bits("1", ZERO)}    fmr 2, 1
    fmr 3, 1
    fmr 4, 1
    @bl {FN(TINT)}
    li 3, 1
    li 4, 3
    li 5, 1
    @bl {FN(RESET_STATE)}
    lfs 0, 0x38(1)
{bits("1", FIFTEEN)}    fsubs 0, 0, 1
{bits("1", ZERO)}    fcmpu 0, 0, 1
    bge gd_low
    fmr 0, 1
gd_low:
{bits("1", FIFTY)}    fcmpu 0, 0, 1
    ble gd_high
    fmr 0, 1
gd_high:
{bits("1", THIRTY_FIVE)}    fdivs 0, 0, 1
{bits("1", TWO)}    fmuls 31, 0, 1
{bits("1", HALF)}    fcmpu 0, 31, 1
    ble gd_most
    fmr 31, 1
gd_most:
""" + la(12, memmap._facts.RACE_CLOCK_AT) + f"""    lwz 12, 0(12)
    cmpwi 12, {FADE}
    bge gd_faded
    cmpwi 12, 0
    bge gd_clock
    li 12, 0
gd_clock:
    lis 0, 0x4330
    stw 0, 0x70(1)
    stw 0, 0x78(1)
    li 0, 0
    stw 0, 0x74(1)
    stw 12, 0x7C(1)
    lfd 0, 0x70(1)
    lfd 1, 0x78(1)
    fsub 1, 1, 0
    frsp 1, 1
{bits("0", SIXTY)}    fdivs 1, 1, 0
    fmuls 31, 31, 1
gd_faded:
""" + stage_matrix(0x10) + f"""    @bl {LOAD_MATRIX:#x}
    li 3, 4
    li 4, 1
    @bl {BLEND_ON:#x}
""" + tint("gd_t2") + draw("gd_d2", "31", DRAW_NOW) + f"""    @bl {BLEND_OFF:#x}
{bits("1", ONE)}    fmr 2, 1
    fmr 3, 1
    fmr 4, 1
    @bl {FN(TINT)}
    li 3, 1
    li 4, 3
    li 5, 1
    @bl {FN(RESET_STATE)}
gd_next:
    addi 30, 30, 8
    addi 31, 31, 1
    cmpwi 31, 30
    blt gd_seat
""" + la(31, colour_at) + f"""    lwz 0, 0(31)
    cmplwi 0, 0
    beq gd_out
    @bl {FN(TEXT_BEGIN)}
    @bl {FN(TEXT_FONT)}
    li 3, 2
    @bl {FN(TEXT_MODE)}
    li 3, 0xA
    @bl {FN(TEXT_SPACE)}
    lis 3, 0x8000
    @bl {FN(TEXT_FLAGS)}
    lwz 0, 0(31)
    stw 0, 0x10(1)
    addi 3, 1, 0x10
    @bl {FN(TEXT_COLOUR)}
{bits("1", HALF)}    fmr 2, 1
    @bl {FN(TEXT_SCALE)}
{bits("1", GAP_Y)}    @bl {FN(TEXT_Y)}
    fmr 30, 1
{bits("1", GAP_X)}    @bl {FN(TEXT_X)}
    fmr 2, 30
    @bl {FN(TEXT_AT)}
""" + la(3, memmap._facts.REL_DATA_AT + GAP_FORMAT) + f"""    lwz 4, 4(31)
    lwz 5, 8(31)
    lwz 6, 0xC(31)
    lwz 7, 0x10(31)
    crxor 6, 6, 6
    @bl {FN(TEXT_PRINT)}
gd_out:
    lwz 26, 0xCC(1)
    lwz 27, 0xD0(1)
    lwz 28, 0xD4(1)
    lwz 29, 0xD8(1)
    lwz 30, 0xDC(1)
    lwz 31, 0xE0(1)
    lfd 30, 0xE8(1)
    lfd 31, 0xF0(1)
    lwz 0, 0x104(1)
    mtlr 0
    addi 1, 1, 0x100
    blr
""")

    # the task's making: the game's call first, whatever it did; then ours in the draw tasks' set (r4 at the call)
    site = TEXT + MAKE
    word = stock(site, MAKE_KNOWN)
    r.add("ghost_make", la(12, memmap.MODE_HUD_FLAGS) + f"""    lwz 12, 0(12)
    andi. 12, 12, {memmap.MODE_GHOST_TASK}
    bne gm_ours
    @bl {FN(GHOSTS_FN)}
    @b {site + 4:#x}
gm_ours:
    stwu 1, -0x20(1)
    stw 31, 0x1C(1)
    stw 30, 0x18(1)
    mr 31, 4
    @bl {FN(GHOSTS_FN)}
    mr 3, 31
    @bl {FN(TASK_SET)}
    mr 30, 3
""" + la(3, memmap._facts.REL_DATA_AT + TASK_NAME) + f"""    bl gm_after
    b ghost_disp
gm_after:
    mflr 4
    li 5, 0
    li 6, {PRIORITY:#x}
    @bl {FN(TASK_NEW)}
    mr 3, 30
    @bl {FN(TASK_SET)}
    lwz 30, 0x18(1)
    lwz 31, 0x1C(1)
    addi 1, 1, 0x20
    @b {site + 4:#x}
""")
    patch(site, word, 0, "ghost_make")
    middle.append((site, ["r12", "cr0", "lr"], "ghost_make"))

    # a silent seat is out of hearing (r27 the machine's index)
    site = TEXT + VOICE
    word = stock(site, VOICE_KNOWN)
    r.add("ghost_voice", test + """    beq gv_stock
""" + la(12, memmap.MODE_HUD_SILENT) + f"""    lwz 12, 0(12)
    srw 12, 12, 27
    andi. 12, 12, 1
    beq gv_stock
    @b {TEXT + VOICE_FAR:#x}
gv_stock:
    .long {word:#010x}
    @b {site + 4:#x}
""")
    patch(site, word, 0, "ghost_voice")
    middle.append((site, ["r12", "cr0"], "ghost_voice"))

    # no pilot for a seat drawn as a ghost or invisible (r29 the machine), as Time Attack's draw path has none
    site = TEXT + PILOT
    word = stock(site, PILOT_KNOWN)
    r.add("ghost_pilot", test + """    beq gp_stock
""" + la(12, memmap.MODES) + """    lwz 12, 4(12)
    cmplwi 12, 0
    beq gp_stock
    lha 11, 4(29)
    cmplwi 11, 30
    bge gp_stock
""" + la(12, memmap.MODE_HUD_LOOKS) + f"""    lbzx 11, 12, 11
    cmpwi 11, 1
    beq gp_none
    cmpwi 11, 2
    beq gp_none
gp_stock:
    @bl {FN(PILOT_DRAW)}
gp_none:
    @b {site + 4:#x}
""")
    patch(site, word, 0, "ghost_pilot")
    middle.append((site, ["r11", "r12", "cr0", "lr"], "ghost_pilot"))

    # no shadow's picture for a seat drawn as a ghost
    site = TEXT + SHADOW
    word = stock(site, SHADOW_KNOWN)
    r.add("ghost_shadow", test + """    beq gs_stock
""" + la(12, memmap.MODES) + f"""    lwz 12, 4(12)
    cmplwi 12, 0
    beq gs_stock
    lwz 12, 0x48(3)
    cmplwi 12, 0
    beq gs_stock
    lha 12, 4(12)
    cmplwi 12, 30
    bge gs_stock
""" + la(11, memmap.MODE_HUD_LOOKS) + f"""    lbzx 12, 11, 12
    cmpwi 12, 1
    beqlr
gs_stock:
    .long {word:#010x}
    @b {site + 4:#x}
""")
    patch(site, word, 0, "ghost_shadow")
    middle.append((site, ["r11", "r12", "cr0"], "ghost_shadow"))
