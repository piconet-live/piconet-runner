"""The game mode's displays (piconet-docs research/game-modes/vocabulary.md section 5; the core's
port/core/src/mode_hud.h): the sites through which the drawing shows what the mode's display words asked. With no mode
in the race (memmap.MODES +4 is 0: a match start without one, every recording made before modes) each site does what
the game does. Native form only. sites.py calls add().

| Site | Where | What the game did | Now, with a mode |
|---|---|---|---|
| `mode_spare` | fn_1_CA8FC 0xCA920, the spare-machine icon's value (`bl fn_1_3F1D4`, r3 the racer the HUD follows) | R+0x13F6 in a Grand Prix, else -1 (nothing drawn) | the racer's show_number (its record's number + 1 in the block's display page; 0: -1, nothing drawn) |
| `mode_look` | fn_1_84860 0x84F9C, a machine's draw (`bl fn_1_870BC`, r29 the machine, f1 its alpha) | the machine drawn | by the look this game draws the seat with (the window's MODE_HUD_LOOKS, the core's HUD pass): normal, the stock call; ghost, the call between Time Attack's ghost state (fn_1_ED540's trail pass: the tint 0.7, 0.7, 1.0, fn_80074A60(4, 1), an alpha of GHOST_ALPHA at most) and its reset; invisible, no call; coloured (seat.colour on a machine drawn normally), the call between the tint of the seat's MODE_HUD_TINTS word (its red, green and blue bytes / 255) and the tint's reset |
| `mode_points` | fn_1_14A328 0x14A37C, the first results page's A (`rlwinm. r0, r0, 0, 5, 5`: rule 0x04000000, every race of ours, to the times list, page 7) | page 7 | with the window's MODE_POINTS_PAGE (a mode that awards points), the rule's bit read as clear: pages 3 then 4, VS TOTAL RANKING, every record's +0x4 as its points |
| `mode_story` | the frame routine, after mode_preload (sites.py's `frame`) | — | the story's picture group 0xBA (bmp_sto: the big digit, DANGER, the distance's digits, MISSION CLEAR!/FAILED, Time is up...) loaded once, with fn_1_47F74, on the frame the block is taken, when the core says the mode shows one of them (MODE_STORY_PICTURES): every game and playback of the race at the same point, before step 0 |
| `mode_banners` | the mode point (sites_mode.py's `mode_point`, after the host's run) | — | the banner stock_banner asked at this run for the local seat raised by the game's own registration, on port 0's HUD entry (follow.py: port 0's presentation is the local racer's): FINAL_LAP fn_1_15E5E4, BOOST_POWER fn_1_15E540, TIME_BONUS fn_1_15E764 (each refuses a second time in a race by the HUD entry's bit, which is cleared first). In the step, as the game raises them: banners.py's sites make the banner a function of the steps, its sound played once (mute.py) |

| `mode_wrong` | fn_1_CD51C 0xCD560, the wrong-way sign's test of P+0x10C (`lwz r0, 0x10c(r3)`) | the sign made while the machine goes the wrong way | the sign also made once when the core's HUD pass says WRONG_WAY was asked (MODE_HUD +0xC, cleared here); it blinks for 30 frames, the sign's own test then ending it |
| `mode_only` | fn_1_14A328 0x14A368, the first results page's A (`extrwi. r0, r0, 1, 23`; its gate 0x14A358 stays) | the page waits for A | under `results points` as pressed |
| `mode_title` | fn_1_1461C8 0x146304, VS TOTAL RANKING's string (`addi r3, r21, 0x404`) | the game's | the window's title, when there is one |
| `tag_cell`, `tag_cpu` | fn_1_147188 0x1473D8 (`lwz r7, 0x4c(r30)`), 0x14740C (`lis r4, 1`); with follow.py's 0x147390 | row's tag: P1 to P4 by racer, CPU for the others | the local seat's row P1; a row with a name in memmap.NAMES no tag (as a player row, to 0x147494). Every race; drawing only |
| `mode_beep` | fn_1_3A330 0x3A448, where the timed mode's countdown ends on every path (`lwz r0, 0x30(r29)`) | — | under clock_limit (whose timer the game no longer counts) the countdown's own tick: 0xA9092700 when the time left is a whole second below ten, including 0, as the code it skips does. Only a sound: the mute of a step run again (mute.py) as for the game's |

Registers: each routine replaces a call (`mode_spare`, `mode_look`), so the volatile registers are free there; or a
test (`mode_points`), after which only r0 and cr0 (both set again as the original sets them) and r12 are written;
`mode_story` and `mode_banners` are called. Nothing a step reads is written: the drawing's draw state, which every
routine resets as Time Attack does; the results' page choice; the HUD entry's banner (presentation, flagged by
banners.py as the game's own)."""
import memmap

SPARE, SPARE_KNOWN = 0xCA920, 0x4BF748B5    # fn_1_CA8FC: bl fn_1_3F1D4
LOOK, LOOK_KNOWN = 0x84F9C, 0x48002121       # fn_1_84860: bl fn_1_870BC
POINTS, POINTS_KNOWN = 0x14A37C, 0x5400014B  # fn_1_14A328: rlwinm. r0, r0, 0, 5, 5
SPARE_GET, DRAW_MACHINE = 0x3F1D4, 0x870BC
TINT, RESET_STATE = 0x5621C, 0x56000         # fn_1_5621C(r, g, b, a), fn_1_56000(1, 3, 1)
BLEND_ON, BLEND_OFF = 0x80074A60, 0x80074A6C  # main.dol: as Time Attack's ghost trail sets them (fn_1_ED540)
LOAD_GROUP, STORY_GROUP = 0x47F74, 0xBA
FINAL_LAP, BOOST_POWER, TIME_BONUS = 0x15E5E4, 0x15E540, 0x15E764
GAP_POPUP = 0x15E330                          # fn_1_15E330(port, the time, heap): the gap popup, against the leader's line
PLAY_SOUND = 0xA2D84                          # fn_1_A2D84: a sound, as the game's voices start
WRONG_WAY_VOICE, TICK = (-0x56FA, 0x1800), (-0x56F7, 0x2700)  # 0xA9061800 (fn_1_A4BC8's), 0xA9092700 (fn_1_3A330's)
WRONG_SITE, WRONG_KNOWN, WRONG_SIGN = 0xCD560, 0x8003010C, 0xCD578  # fn_1_CD51C: lwz r0, 0x10c(r3); the sign's making
ONLY, ONLY_KNOWN = 0x14A368, 0x5400C7FF        # fn_1_14A328: extrwi. r0, r0, 1, 23
TITLE, TITLE_KNOWN = 0x146304, 0x38750404      # fn_1_1461C8: addi r3, r21, 0x404
TAG_CELL, TAG_CELL_KNOWN = 0x1473D8, 0x80FE004C  # fn_1_147188: lwz r7, 0x4c(r30)
TAG_CPU, TAG_CPU_KNOWN, TAG_DONE = 0x14740C, 0x3C800001, 0x147494  # lis r4, 1; where a player row's tag ends
BEEP_SITE, BEEP_KNOWN = 0x3A448, 0x801D0030   # fn_1_3A330: lwz r0, 0x30(r29), where the timer's code ends on every path
HUD_ENTRIES, BANNER_HEAP = 0x8FDA8, 0x25B88   # .bss: port 0's HUD entry (+0 the banners raised); the banners' heap word
ONCE_BITS = {FINAL_LAP: 0x2, BOOST_POWER: 0x1, TIME_BONUS: 0x0}
GHOST_TINT = 0x3F333333                       # 0.7: Time Attack's (1.0, 1.0, 0.7) other ghosts; (0.7, 0.7, 1.0) blue
ONE = 0x3F800000
BYTE_SCALE = 0x3B808081                       # 1 / 255: seat.colour's bytes to the tint's floats
GHOST_ALPHA = 0x3F000000                      # 0.5 at most (the stock alpha, a near-camera fade, if lower)
DISPLAY, SEATS_AT, SEAT_DISPLAY, NUMBER, BANNER, GAP_TIME = 0x6000, 0x6010, 0x20, 4, 10, 24   # mode_hud.h
assert SEATS_AT + 30 * SEAT_DISPLAY <= memmap.MODE_BLOCK and SEATS_AT + NUMBER < 0x8000
SLOT_SEAT = memmap.IMAGE + 0x14               # the slot 'FZSI' +4: the local seat (frame_hud.py)
# The results pages' reads of R+0x8 (the race's players) where the page is 4, the points page (each `page == 4 ?
# R+0x8 : R+0x9`, the list's rows, its window, its scroll; 0x14B710 and fn_1_14A1AC's 0x14A234 the window's rows as page 4 opens), all
# `lbz rD, 0x8(rA)`: +1 is `lbz rD, 0x9(rA)`, the racers
RANKING_COUNTS = [(at, 0x88000008 | rd << 21 | ra << 16) for at, rd, ra in (
    (0x14A234, 3, 3), (0x14A4C8, 0, 6), (0x14A4F8, 0, 3), (0x14A524, 4, 3), (0x14A6E4, 0, 6), (0x14A714, 0, 3), (0x14A748, 4, 3),
    (0x14A8EC, 0, 6), (0x14A91C, 0, 3), (0x14A950, 4, 3), (0x14AF08, 3, 3), (0x14B0B0, 0, 5), (0x14B0E0, 0, 3),
    (0x14B114, 4, 3), (0x14B244, 3, 3), (0x14B420, 0, 6), (0x14B450, 0, 3), (0x14B484, 4, 3), (0x14B670, 3, 3),
    (0x14B710, 3, 3), (0x14B814, 0, 5), (0x14B844, 0, 3), (0x14B878, 4, 3), (0x14B984, 3, 3), (0x14BB60, 0, 6),
    (0x14BB90, 0, 3), (0x14BBC4, 4, 3), (0x14C898, 3, 3), (0x14CA88, 3, 3))]
SORT = 0x3F2A0                                # fn_1_3F2A0: the results' order R+0x1394 (outside a Grand Prix: R+0x8's)
SORT_CALLS = {0x14B738: 0x4BEF3B69, 0x14B7C4: 0x4BEF3ADD, 0x14B95C: 0x4BEF3945}   # its `bl` as page 4 opens
RACE, RECORDS, RECORD = 0x3C30, 0xD4, 0xA0    # R (.bss); its results records, one a seat


def add(k, r, patch, stock, test, la, middle):
    """Adds the routines to `r` and the sites' entries through `patch`, as sites.add does."""
    TEXT = k.TEXT
    FN = lambda offset: f"{TEXT + offset:#x}"
    REL_BSS = memmap.REL_BSS

    def floats(*pairs):
        """Loads floats f1.. from their bits through the stack word 0x8(r1)."""
        return "".join(f"    lis 0, {bits >> 16:#x}\n    ori 0, 0, {bits & 0xFFFF:#x}\n    stw 0, 0x8(1)\n"
                       f"    lfs {reg}, 0x8(1)\n" for reg, bits in pairs)

    # the spare-machine icon's value: the followed racer's show_number
    site = TEXT + SPARE
    word = stock(site, SPARE_KNOWN)
    r.add("mode_spare", test + """    beq msp_stock
""" + la(12, memmap.MODES) + f"""    lwz 12, 4(12)
    cmplwi 12, 0
    beq msp_stock
    cmplwi 3, 30
    bge msp_none
    mulli 11, 3, {SEAT_DISPLAY}
    add 12, 12, 11
    lwz 3, {SEATS_AT + NUMBER:#x}(12)
    addi 3, 3, -1
    @b {site + 4:#x}
msp_none:
    li 3, -1
    @b {site + 4:#x}
msp_stock:
    @bl {FN(SPARE_GET)}
    @b {site + 4:#x}
""")
    patch(site, word, 0, "mode_spare")
    middle.append((site, ["r11", "r12", "cr0", "lr"], "mode_spare"))

    # a machine's draw by the look this game draws its seat with (r29 the machine; f1 the alpha; r3 to r7 the call's)
    site = TEXT + LOOK
    word = stock(site, LOOK_KNOWN)
    r.add("mode_look", test + """    beq mlk_stock
""" + la(12, memmap.MODES) + """    lwz 12, 4(12)
    cmplwi 12, 0
    beq mlk_stock
    lha 11, 4(29)
    cmplwi 11, 30
    bge mlk_stock
""" + la(12, memmap.MODE_HUD_LOOKS) + f"""    lbzx 11, 12, 11
    cmpwi 11, 1
    beq mlk_ghost
    cmpwi 11, 2
    beq mlk_out
    cmpwi 11, 3
    beq mlk_tint
mlk_stock:
    @bl {FN(DRAW_MACHINE)}
mlk_out:
    @b {site + 4:#x}
mlk_ghost:
    stwu 1, -0x40(1)
    mflr 0
    stw 0, 0x44(1)
    stw 3, 0x10(1)
    stw 4, 0x14(1)
    stw 5, 0x18(1)
    stw 6, 0x1C(1)
    stw 7, 0x20(1)
    stfs 1, 0x24(1)
{floats(("1", GHOST_TINT), ("2", GHOST_TINT), ("3", ONE), ("4", ONE))}    @bl {FN(TINT)}
    li 3, 4
    li 4, 1
    @bl {BLEND_ON:#x}
{floats(("0", GHOST_ALPHA))}    lfs 1, 0x24(1)
    fcmpu 0, 1, 0
    blt mlk_alpha
    fmr 1, 0
mlk_alpha:
    lwz 3, 0x10(1)
    lwz 4, 0x14(1)
    lwz 5, 0x18(1)
    lwz 6, 0x1C(1)
    lwz 7, 0x20(1)
    @bl {FN(DRAW_MACHINE)}
    @bl {BLEND_OFF:#x}
{floats(("1", ONE), ("2", ONE), ("3", ONE), ("4", ONE))}    @bl {FN(TINT)}
    li 3, 1
    li 4, 3
    li 5, 1
    @bl {FN(RESET_STATE)}
    lwz 0, 0x44(1)
    mtlr 0
    addi 1, 1, 0x40
    @b {site + 4:#x}
mlk_tint:
    stwu 1, -0x40(1)
    mflr 0
    stw 0, 0x44(1)
    stw 3, 0x10(1)
    stw 4, 0x14(1)
    stw 5, 0x18(1)
    stw 6, 0x1C(1)
    stw 7, 0x20(1)
    stfs 1, 0x24(1)
    lha 11, 4(29)
    slwi 11, 11, 2
""" + la(12, memmap.MODE_HUD_TINTS) + f"""    lwzx 11, 12, 11
    lis 0, 0x4330
    stw 0, 0x28(1)
    stw 0, 0x30(1)
    li 0, 0
    stw 0, 0x2C(1)
    lfd 5, 0x28(1)
{floats(("6", BYTE_SCALE))}    rlwinm 0, 11, 8, 24, 31
    stw 0, 0x34(1)
    lfd 1, 0x30(1)
    rlwinm 0, 11, 16, 24, 31
    stw 0, 0x34(1)
    lfd 2, 0x30(1)
    rlwinm 0, 11, 24, 24, 31
    stw 0, 0x34(1)
    lfd 3, 0x30(1)
    fsub 1, 1, 5
    fsub 2, 2, 5
    fsub 3, 3, 5
    fmuls 1, 1, 6
    fmuls 2, 2, 6
    fmuls 3, 3, 6
{floats(("4", ONE))}    @bl {FN(TINT)}
    lwz 3, 0x10(1)
    lwz 4, 0x14(1)
    lwz 5, 0x18(1)
    lwz 6, 0x1C(1)
    lwz 7, 0x20(1)
    lfs 1, 0x24(1)
    @bl {FN(DRAW_MACHINE)}
{floats(("1", ONE), ("2", ONE), ("3", ONE), ("4", ONE))}    @bl {FN(TINT)}
    lwz 0, 0x44(1)
    mtlr 0
    addi 1, 1, 0x40
    @b {site + 4:#x}
""")
    patch(site, word, 0, "mode_look")
    middle.append((site, ["r11", "r12", "cr0", "lr"], "mode_look"))

    # the results row's tag: only actual CPUs say CPU, the local seat's row P1 (follow.py's 0x147390 picks the branch)
    site = TEXT + TAG_CELL
    word = stock(site, TAG_CELL_KNOWN)
    r.add("tag_cell", test + """    beq tcl_stock
    li 7, 0
    @b """ + f"""{site + 4:#x}
tcl_stock:
    .long {word:#010x}
    @b {site + 4:#x}
""")
    patch(site, word, 0, "tag_cell")
    middle.append((site, ["r12", "cr0"], "tag_cell"))
    site = TEXT + TAG_CPU
    word = stock(site, TAG_CPU_KNOWN)
    r.add("tag_cpu", test + """    beq tcp_stock
""" + la(12, memmap.NAMES) + f"""    lwz 0, 0x4C(30)
    slwi 0, 0, 5
    lbzx 0, 12, 0
    cmpwi 0, 0
    beq tcp_stock
    @b {TEXT + TAG_DONE:#x}
tcp_stock:
    .long {word:#010x}
    @b {site + 4:#x}
""")
    patch(site, word, 0, "tag_cpu")
    middle.append((site, ["r0", "r12", "cr0"], "tag_cpu"))

    # `results points`: the first page's A as pressed (r0 and cr0 are the test's)
    site = TEXT + ONLY
    word = stock(site, ONLY_KNOWN)
    r.add("mode_only", test + """    beq mon_stock
""" + la(12, memmap.MODE_HUD_FLAGS) + f"""    lwz 12, 0(12)
    andi. 12, 12, {memmap.MODE_POINTS_ONLY}
    beq mon_stock
    li 0, 1
    cmpwi 0, 0
    @b {site + 4:#x}
mon_stock:
    .long {word:#010x}
    @b {site + 4:#x}
""")
    patch(site, word, 0, "mode_only")
    middle.append((site, ["r12", "cr0"], "mode_only"))

    # `results_title NAME`: the window's string for VS TOTAL RANKING
    site = TEXT + TITLE
    word = stock(site, TITLE_KNOWN)
    r.add("mode_title", test + """    beq mtt_stock
""" + la(12, memmap.MODE_HUD_TITLE) + f"""    lbz 12, 0(12)
    cmpwi 12, 0
    beq mtt_stock
""" + la(3, memmap.MODE_HUD_TITLE) + f"""    @b {site + 4:#x}
mtt_stock:
    .long {word:#010x}
    @b {site + 4:#x}
""")
    patch(site, word, 0, "mode_title")
    middle.append((site, ["r12", "cr0"], "mode_title"))

    # the results' points page: rule 0x04000000 read as clear while the core says the mode awards points
    site = TEXT + POINTS
    word = stock(site, POINTS_KNOWN)
    r.add("mode_points", test + """    beq mpts_stock
""" + la(12, memmap.MODE_HUD_FLAGS) + f"""    lwz 12, 0(12)
    andi. 12, 12, {memmap.MODE_POINTS_PAGE}
    beq mpts_stock
    li 0, 0
mpts_stock:
    .long {word:#010x}
    @b {site + 4:#x}
""")
    patch(site, word, 0, "mode_points")
    middle.append((site, ["r12", "cr0"], "mode_points"))

    # the points page's rows: every racer's (RANKING_COUNTS), while the core says the mode awards points
    for at, known in RANKING_COUNTS:
        site = TEXT + at
        word = stock(site, known)
        name = f"mode_rows_{at:x}"
        r.add(name, test + f"""    beq mrw{at:x}_stock
""" + la(12, memmap.MODE_HUD_FLAGS) + f"""    lwz 12, 0(12)
    andi. 12, 12, {memmap.MODE_POINTS_PAGE}
    beq mrw{at:x}_stock
    .long {word + 1:#010x}
    @b {site + 4:#x}
mrw{at:x}_stock:
    .long {word:#010x}
    @b {site + 4:#x}
""")
        patch(site, word, 0, name)
        middle.append((site, ["r12", "cr0"], name))

    # the points page's order: the game's sort as a Grand Prix's (R+0x5 set for the call alone: every racer, by its
    # record's +0x4, which the call outside a Grand Prix overwrites with the VS points R+0x1480), each record's +0x4
    # first written from award's points (the block's, whatever the race or the results' set-up left there)
    sort_fn = FN(SORT)
    r.add("mode_sort", la(12, memmap.MODE_HUD_FLAGS) + f"""    lwz 12, 0(12)
    andi. 12, 12, {memmap.MODE_POINTS_PAGE}
    beq msrt_stock
""" + la(12, memmap.MODES) + f"""    lwz 12, 4(12)
    cmplwi 12, 0
    beq msrt_stock
    stwu 1, -0x20(1)
    mflr 0
    stw 0, 0x24(1)
    stw 31, 0x1C(1)
""" + la(31, REL_BSS + RACE) + f"""    lbz 10, 9(31)
    cmplwi 10, 30
    ble msrt_count
    li 10, 30
msrt_count:
    addi 12, 12, {memmap.MODE_POINTS - 4:#x}
    addi 11, 31, {RECORDS:#x}
    li 9, 0
msrt_seat:
    cmpw 9, 10
    bge msrt_sorted
    lwzu 0, 4(12)
    cmplwi 0, 0xFFFF
    ble msrt_fits
    li 0, -1
msrt_fits:
    sth 0, 4(11)
    addi 11, 11, {RECORD:#x}
    addi 9, 9, 1
    b msrt_seat
msrt_sorted:
    lbz 0, 5(31)
    stb 0, 0x18(1)
    li 0, 1
    stb 0, 5(31)
    @bl {sort_fn}
    lbz 0, 0x18(1)
    stb 0, 5(31)
    lwz 31, 0x1C(1)
    lwz 0, 0x24(1)
    mtlr 0
    addi 1, 1, 0x20
    blr
msrt_stock:
    @b {sort_fn}
""")
    for at, known in SORT_CALLS.items():
        site = TEXT + at
        word = stock(site, known)
        name = f"mode_sort_{at:x}"
        r.add(name, f"""    bl mode_sort
    @b {site + 4:#x}
""")
        patch(site, word, 0, name)
        # (in place of a call of the sort, which writes r0, r3 to r12 and cr0 itself)
        middle.append((site, ["lr"], name))

    # the wrong-way sign when WRONG_WAY was asked
    site = TEXT + WRONG_SITE
    word = stock(site, WRONG_KNOWN)
    r.add("mode_wrong", test + """    beq mwr_stock
""" + la(12, memmap.MODE_HUD + 0xC) + f"""    lwz 0, 0(12)
    cmpwi 0, 0
    beq mwr_stock
    li 0, 0
    stw 0, 0(12)
    @b {TEXT + WRONG_SIGN:#x}
mwr_stock:
    .long {word:#010x}
    @b {site + 4:#x}
""")
    patch(site, word, 0, "mode_wrong")
    middle.append((site, ["r12", "cr0"], "mode_wrong"))

    # the countdown's tick under clock_limit: time left = the limit - the race clock (r11, r12; r0 is written next).
    # The path through the game's own tick reaches this word after a call, so nothing volatile is live here
    site = TEXT + BEEP_SITE
    word = stock(site, BEEP_KNOWN)
    r.add("mode_beep", test + """    beq mbp_out
""" + la(12, memmap.MODES) + f"""    lwz 12, 4(12)
    cmplwi 12, 0
    beq mbp_out
    lwz 12, {memmap.MODE_CLOCK:#x}(12)
    cmpwi 12, 0
    beq mbp_out
""" + la(11, memmap._facts.RACE_CLOCK_AT) + f"""    lwz 11, 0(11)
    cmpwi 11, 0
    ble mbp_out
    subf. 12, 11, 12
    blt mbp_out
    cmpwi 12, {9 * 60}
    bgt mbp_out
    li 11, 60
    divwu 0, 12, 11
    mullw 0, 0, 11
    cmpw 0, 12
    bne mbp_out
    lis 3, {TICK[0]}
    addi 3, 3, {TICK[1]:#x}
    @bl {FN(PLAY_SOUND)}
mbp_out:
    .long {word:#010x}
    @b {site + 4:#x}
""")
    patch(site, word, 0, "mode_beep")
    middle.append((site, ["r12", "cr0", "lr"], "mode_beep"))

    # the story's pictures, once, on the frame the block is taken (MODES +0 then 1), when the core asks (called by
    # the frame's entry after mode_preload: every register but r0, r3 to r12 and cr0 kept). +8 of the window's words
    # says it is done (the window is never rolled back; one game process, one race)
    r.add("mode_story", la(12, memmap.MODE_HUD_FLAGS) + f"""    lwz 0, 0(12)
    andi. 0, 0, {memmap.MODE_STORY_PICTURES}
    beqlr
    lwz 0, 8(12)
    cmpwi 0, 0
    bnelr
""" + la(11, memmap.MODES) + f"""    lwz 0, 0(11)
    cmpwi 0, 1
    bnelr
    li 0, 1
    stw 0, 8(12)
    stwu 1, -0x10(1)
    mflr 0
    stw 0, 0x14(1)
    li 3, {STORY_GROUP:#x}
    @bl {FN(LOAD_GROUP)}
    lwz 0, 0x14(1)
    mtlr 0
    addi 1, 1, 0x10
    blr
""")

    # the banner asked at this run for the local seat, raised as the game raises it (called from the mode point after
    # the host's run: the volatile registers free)
    raise_ = "".join(f"""mbn_{n}:
""" + (f"""    lbz 0, 0(30)
    andi. 0, 0, {0xFF ^ ONCE_BITS[fn]}
    stb 0, 0(30)
""" if ONCE_BITS[fn] else "") + f"""    li 3, 0
    lwz 4, 0(31)
    @bl {FN(fn)}
    b mbn_out
""" for n, fn in ((1, FINAL_LAP), (2, BOOST_POWER), (3, TIME_BONUS))) + f"""mbn_4:
    li 3, 0
    lwz 4, {SEATS_AT + GAP_TIME:#x}(12)
    lwz 5, 0(31)
    @bl {FN(GAP_POPUP)}
    b mbn_out
mbn_5:
    lis 3, {WRONG_WAY_VOICE[0]}
    addi 3, 3, {WRONG_WAY_VOICE[1]:#x}
    @bl {FN(PLAY_SOUND)}
    b mbn_out
mbn_6:
    lis 3, {TICK[0]}
    addi 3, 3, {TICK[1]:#x}
    @bl {FN(PLAY_SOUND)}
    b mbn_out
"""
    r.add("mode_banners", la(12, memmap.MODES) + f"""    lwz 12, 4(12)
    cmplwi 12, 0
    beqlr
""" + la(11, SLOT_SEAT) + f"""    lwz 11, 0(11)
    cmplwi 11, 30
    bgelr
    mulli 11, 11, {SEAT_DISPLAY}
    add 12, 12, 11
    lbz 0, {SEATS_AT + BANNER:#x}(12)
    cmpwi 0, 0
    beqlr
    cmpwi 0, 6
    bgtlr
    stwu 1, -0x20(1)
    mflr 3
    stw 3, 0x24(1)
    stw 31, 0x1C(1)
    stw 30, 0x18(1)
""" + la(31, REL_BSS + BANNER_HEAP) + la(30, REL_BSS + HUD_ENTRIES) + """    cmpwi 0, 1
    beq mbn_1
    cmpwi 0, 2
    beq mbn_2
    cmpwi 0, 3
    beq mbn_3
    cmpwi 0, 4
    beq mbn_4
    cmpwi 0, 5
    beq mbn_5
    b mbn_6
""" + raise_ + """mbn_out:
    lwz 30, 0x18(1)
    lwz 31, 0x1C(1)
    lwz 0, 0x24(1)
    mtlr 0
    addi 1, 1, 0x20
    blr
""")
