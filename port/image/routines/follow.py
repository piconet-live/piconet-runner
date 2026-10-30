"""The game's presentation follows the local racer, not port 0 (for the frame routine, netframe.py, which includes this
code and calls `follow_install` on entry every frame).

Every machine but seat 0 is a CPU with no pad port, so the parts of the race HUD and the results that pick their racer
by pad port (machine +0x475; port 0 = seat 0) showed seat 0's in every game. Each site below reads the port; the
installer replaces the read with a `bl` to a helper in this image that gives 0 for the local racer's machine (the
stand-in seat, slot 'FZSI' +4, which the camera and the HUD follow) and -1 for the others, so the game's port-0
presentation is the local racer's. A site is patched only while it still holds its original word (the mask leaves
out a relocated low half); main.rel's code is outside the rollback's undo log, so the patches stay. Control block
+264: the main.rel address the sites were patched for (once per load).

Sites (main.rel .text offset: the port read it replaces):
- the lap event fn_1_327F8 (0x32818-0x32A04) and the rule hook's live-lap entry (0x33004): the lap banner's HUD slot
  0 and the lap list get the local racer's laps, against the time of that lap's leader;
- RETIRED (fn_1_32600: 0x32660, 0x32684) and the time-bonus counter (fn_1_32A40: 0x32FD4);
- the results snapshot (per-port slot of progress record, max speed, skulls: .bss 0x26314, 0x26304, 0x26B04), filled
  at seat 0's finish (0x32D20: the guard only; the reads after it are reached only past it) and at race over
  (fn_1_3A330: 0x3A934-0x3AAA4; fn_1_33A20: 0x343C0-0x34440): slot 0 holds the local racer's results;
- the results board's name and machine (0x142748, `bl fn_1_86900(0)`, the player-0 machine): the local racer's car
  object;
- the results camera's racer (0x3B14C, `lha r4, 0x14FC(r29)`, the first port machine to finish; the results' start
  fn_1_3B11C then calls fn_1_5370(1, it): the TV cameras, automatic shots, on that racer, the stock results' backdrop):
  the local seat, but for a local machine out by the game's own verdict (outtest.py) whose body is gone. The game
  hides a machine it destroys: fn_1_25004 (a wreck that explodes, a fall below the course from fn_1_24A4C, a
  breakdown's wreck that another machine hits) and the scenery's crush (fn_1_2A8D0) set its car object's +0x390 bit
  0x01000000, which car_disp (fn_1_84860, 0x8488C) tests first and then draws nothing; only the car effect
  fn_1_68284 clears it, for a machine no longer broken down (0x80), that is one the Restore rule put back. So with the
  bit set the cameras would watch the empty place (or, after a fall, the void below the course) where the machine
  was: they follow the winner instead (the racer in first place, progress record +0x115, of those not out: the
  first to finish, as the stock game's own pick 0x14FC is; the leader if none has finished), as a finished player's
  results watch a racer on the track. A wreck still drawn (a breakdown at rest, a retirement) stays watched. Kept
  the local seat: a finished or racing machine, a spectator's game (memmap.RESULTS_KEEP, the host's), course 41
  (its scenery's collision follows the camera's machine: spectator.h RuleFor), and when no racer is left to follow.
  The pick is written to memmap.RESULTS_CAMERA for the host's log. The helper runs inside the results' first step,
  from the step's own memory, so a step run again picks the same. While the results run, `results_watch` (after
  every frame's steps) asks the same rule again while the cameras follow the local seat: a wreck still drawn at the
  results' start that explodes during them (a computer machine touched it) gives the winner then (RC_GONE);
- the rank icons (1st, 2nd, 3rd above the machines in those places; fn_1_12999C: 0x129AA8, 0x129AC0): a machine
  whose port is the viewport's own (fn_1_5910) gets none, so the local racer sees no icon above itself; in the native
  form the racer the HUD follows gets none (rankicons.py: a spectator sees none above the racer watched);
- the HUD's per-port "finished" bits (.bss 0x26B1E, set by seat 0's finish and part of the simulation's one-shot
  there, so not to be touched): its three HUD readers (0x15C3D4, 0x14211C, 0x15E03C) read control block +268 instead,
  bit 0 = the local racer finished, set by the frame routine's HUD pass (netframe.py `hudstate`);
- the results list by place (fn_1_144F40): its row test fn_1_145B58 lights a row whose racer (row +0x4C) is below
  the number of human players (0x145C3C, `lbz r4, 8(r3)`: .bss 0x3C38, 1 in a race of ours, so seat 0's row in every
  game); the helper gives the number that only the local seat's row is below (the test is signed: the largest
  number for that row, the smallest for the others); and where the list begins: the results pages put it on the
  place of the racer fn_1_8623C names, which is racer 0 always (`li r3, 0`), so every game's list began at seat 0's
  row and the local racer's own row was off the screen. The two calls a race of ours reaches (fn_1_14A328, leaving
  the first results page with rule flag 0x04000000 set: 0x14A3E4, 0x14A474) get the local seat instead; the ten
  like them for the other race types are left;
- the finish countdown's six picture readers (.bss 0x50EC[port], which the simulation counts for seat 0 only): the
  banner fn_1_15DFD4 (0x15E10C, 0x15E140, 0x15E190: the retired banners' fade, fn_1_15DD7C; 0x15E164: "FINISH!" and
  the place, fn_1_15C6C0), the HUD's switch to the results list (fn_1_15C36C: 0x15C4AC) and the results pages'
  wait (fn_1_14A028: 0x14A094) read memmap.FINISH_COUNT, the local racer's (frame_hud.py `finwatch`). What they
  reach draws, sets sprite and sound state, or moves the results pages, which each game moves by its own pad
  already. The readers the simulation has are left: the two routines that count it (fn_1_32200, fn_1_32A40), the
  race phase's wait before the results (fn_1_2DF98, 0x2E0F4 and 0x2E190, through the pointer the finish leaves at
  .bss 0x25B8C: the wait that holds the results online) and phase 0x2B's wait (0x3D358: the time-limit ending);
- the banner's racer (fn_1_15C6C0: 0x15C760, `bl fn_1_86254(port)`, the machine whose number is the port's): its
  number picks the racer's times of this race (rule block +0xD4, 0xA0 a racer) that the banner compares with the
  saved records before it adds the new-record picture; drawn at the local racer's finish it took racer 0's, who
  may still be racing. The helper gives the local seat's machine. (The second such call, 0x15CFC4, runs only in
  race type 2, Grand Prix, which a race of ours never is: left.)
- the place at the finish, as versus shows it (fn_1_15C6C0: 0x15C750, `li r28, 0`): the banner draws "FINISH!" while
  the countdown is at r28 or above and the place below it, zooming in from r28 down. r28 is 0 in Grand Prix
  (0x15C740, left) and with rule flag 0x04000000, which every race of ours has (direct.py), and 300 otherwise
  (0x15C758: versus): ours drew the place only at 0, the first frame of its zoom-in (20 times its size, half
  transparent). The helper gives 300, so FINISH! stands 480 to 300 and the place 300 to 0, settled for its last
  seconds; frame_hud.py `finwatch` holds a finished racer's countdown at 0 until the results, so the settled
  place stays up as long as the results wait (the race's end waits for other players, raceend.py), where versus
  goes to the results at 0 for its last human. Drawing only: r28 is the banner's own threshold.
- the results list's word for a racer without a time (fn_1_144F40 copies each racer's state into the row it draws:
  0x14557C and 0x145608, `lbzx` of the byte the rule block keeps for the racer; the row routine fn_1_148950 draws a
  time, or "RUNNING" for 0xFF, or "DISQUALIFIED" for 0xFE, the game's two strings, in its own font). The game
  writes 0xFE itself when a machine goes out without a finish (fn_1_2E928). A machine the host's rule ended the
  race for (raceend.py) still has 0xFF: the helper gives the row 0xFE in its place when the race has that rule
  and the race-over test has said "over" (memmap.END_OVER), so the list reads "DISQUALIFIED" for it in every game,
  as for a machine that went out. Only the row, a copy on the list's stack: the rule
  block's byte, which a rollback puts back and the game's own result routine reads, is not written. Without the
  rule the list is the stock game's.
These sites only steer presentation; none of the game-state writes near them uses the value (read in the game's code)."""
import memmap
import outtest

# (text offset, original word, mask, helper)
SITES = [
    (0x32818, 0x88630475, 0xFFFFFFFF, "f26_3"), (0x328AC, 0x887A0475, 0xFFFFFFFF, "f26_3"),
    (0x328D4, 0x887A0475, 0xFFFFFFFF, "f26_3"), (0x328E0, 0x887A0475, 0xFFFFFFFF, "f26_3"),
    (0x32928, 0x881A0475, 0xFFFFFFFF, "f26_0"), (0x329B4, 0x887A0475, 0xFFFFFFFF, "f26_3"),
    (0x32A04, 0x881A0475, 0xFFFFFFFF, "f26_0"), (0x33004, 0x887B0475, 0xFFFFFFFF, "f27_3"),
    (0x32660, 0x881E0475, 0xFFFFFFFF, "f30_0"), (0x32684, 0x881E0475, 0xFFFFFFFF, "f30_0"),
    (0x32FD4, 0x881B0475, 0xFFFFFFFF, "f27_0"), (0x32D20, 0x881B0475, 0xFFFFFFFF, "f27_0"),
    (0x3A934, 0x88030475, 0xFFFFFFFF, "f27_0"), (0x3A978, 0x881B0475, 0xFFFFFFFF, "f27_0"),
    (0x3A99C, 0x881B0475, 0xFFFFFFFF, "f27_0"), (0x3A9B4, 0x881B0475, 0xFFFFFFFF, "f27_0"),
    (0x3AA24, 0x88030475, 0xFFFFFFFF, "f27_0"), (0x3AA68, 0x881B0475, 0xFFFFFFFF, "f27_0"),
    (0x3AA8C, 0x881B0475, 0xFFFFFFFF, "f27_0"), (0x3AAA4, 0x881B0475, 0xFFFFFFFF, "f27_0"),
    (0x343C0, 0x88030475, 0xFFFFFFFF, "f27_0"), (0x34404, 0x881B0475, 0xFFFFFFFF, "f27_0"),
    (0x34428, 0x881B0475, 0xFFFFFFFF, "f27_0"), (0x34440, 0x881B0475, 0xFFFFFFFF, "f27_0"),
    (0x15C3D4, 0x88840000, 0xFFFFFFFF, "fin_4"), (0x14211C, 0x88030000, 0xFFFF0000, "fin_0"),
    (0x15E03C, 0x88030000, 0xFFFF0000, "fin_0"),
    (0x142748, 0x4BF441B9, 0xFFFFFFFF, "name"), (0x3B14C, 0xA89D14FC, 0xFFFFFFFF, "results_4"),
    (0x129AA8, 0x881E0475, 0xFFFFFFFF, "f30_0"), (0x129AC0, 0x881E0475, 0xFFFFFFFF, "f30_0"),
    (0x145C3C, 0x88830008, 0xFFFFFFFF, "row_4"),
    (0x15E10C, 0x7C632A2E, 0xFFFFFFFF, "count_3"), (0x15E140, 0x7C632A2E, 0xFFFFFFFF, "count_3"),
    (0x15E164, 0x7C63022E, 0xFFFFFFFF, "count_3"), (0x15E190, 0x7C632A2E, 0xFFFFFFFF, "count_3"),
    (0x15C4AC, 0x7C03022E, 0xFFFFFFFF, "count_0"), (0x14A094, 0xA0030000, 0xFFFFFFFF, "count_0"),
    (0x15C760, 0x4BF29AF5, 0xFFFFFFFF, "machine"), (0x15C750, 0x3B800000, 0xFFFFFFFF, "place"),
    (0x14A3E4, 0x4BF3BE59, 0xFFFFFFFF, "seat_3"), (0x14A474, 0x4BF3BDC9, 0xFFFFFFFF, "seat_3"),
    (0x14557C, 0x7CC720AE, 0xFFFFFFFF, "state_6"), (0x145608, 0x7DE320AE, 0xFFFFFFFF, "state_15"),
]
SLOT = 0x81800014           # slot 'FZSI' +4: the local seat
assert SLOT == memmap.IMAGE + 0x14
H = memmap.hi(SLOT)
C = memmap.hi(memmap.CONTROL)
COUNT = memmap.FINISH_COUNT - memmap.CONTROL


OVER = memmap.END_OVER - memmap.CONTROL
LIMIT = memmap.RACE + memmap.RACE_FINISH_LIMIT
PLACE_FROM = 300        # the banner's place threshold in versus (fn_1_15C6C0 0x15C758, rule flag 0x04000000 clear)
RUNNING, DISQUALIFIED = 0xFF, 0xFE      # a racer's state in the results list: the game's two words for "no time"


def state(dest, base, index):
    """The replaced `lbzx dest, base, index`, a racer's state for its row of the results list; then DISQUALIFIED for
    RUNNING when the host's rule ended the race (the rule's word is set and "over" was said). Uses r12 and CR0."""
    return f"""    lbzx {dest}, {base}, {index}
    cmplwi {dest}, {RUNNING}
    bnelr
    lis 12, {C}
    lwz 12, {OVER}(12)
    cmpwi 12, 0
    beqlr
    lis 12, {memmap.hi(LIMIT)}
    lwz 12, {memmap.lo(LIMIT)}(12)
    cmpwi 12, 0
    beqlr
    li {dest}, {DISQUALIFIED}
    blr
"""


# why the results' cameras follow the racer they do (memmap.RESULTS_CAMERA's second byte)
RC_OWN, RC_WRECK, RC_WINNER, RC_COURSE, RC_KEEP, RC_ALONE, RC_GONE = 1, 2, 3, 4, 5, 6, 7
COURSE_41 = 41                  # spectator.h RuleFor: no other racer followed there
HIDDEN = 0x01000000             # car object +0x390: car_disp draws nothing (fn_1_25004 sets it)


def pick(keep, p):
    """The results' cameras' racer by the rule above, from r4 = the local seat: r3 = why << 8 and r4 = the racer at
    the label `{p}say`, which follows. Labels start with `p`. Uses r0, r3 to r12 and CR0; reads only."""
    text = f"""    lis 4, {H}
    lwz 4, 0x14(4)
    lis 12, {C}
    li 3, {RC_KEEP << 8}
    lwz 0, {keep}(12)
    cmpwi 0, 0
    bne @say
    li 3, {RC_COURSE << 8}
    lis 11, {memmap.hi(memmap.RACE)}
    lwz 0, {memmap.lo(memmap.RACE + memmap.RACE_COURSE)}(11)
    cmpwi 0, {COURSE_41}
    beq @say
    lis 11, 0x8000
    lwz 11, 0x30C8(11)
    addis 11, 11, 0x1C
    addi 11, 11, -0x5FC0
    addis 10, 11, 7
    lwz 9, -0x27C8(10)
    lbz 8, -0x27B6(10)
    li 3, {RC_OWN << 8}
    cmpwi 9, 0
    beq @say
    cmplw 4, 8
    bge @say
    mulli 10, 4, 0x620
    add 10, 9, 10
OUT_LOCAL
@out:
    li 3, {RC_WRECK << 8}
    addis 6, 11, 7
    lwz 5, -0x27C4(6)
    cmpwi 5, 0
    beq @say
    lbz 7, 5(10)
    cmplw 7, 8
    bge @say
    mulli 7, 7, 0x440
    add 5, 5, 7
    lwz 7, 0x390(5)
    andis. 7, 7, {HIDDEN >> 16}
    beq @say
    li 3, {RC_ALONE << 8}
    li 6, 0x100
    li 5, 0
@find:
    cmplw 5, 8
    bge @found
    mulli 10, 5, 0x620
    add 10, 9, 10
    lwz 7, 0x49C(10)
    cmpwi 7, 0
    beq @next
OUT_OTHER
@in:
    lwz 7, 0x49C(10)
    lbz 7, 0x115(7)
    cmplw 7, 6
    bge @next
    mr 6, 7
    mr 12, 5
@next:
    addi 5, 5, 1
    b @find
@found:
    cmplwi 6, 0x100
    bge @say
    mr 4, 12
    li 3, {RC_WINNER << 8}
"""
    text = text.replace("OUT_LOCAL\n", outtest.clause(10, 11, 0, 7, "@out", "@say"))
    return text.replace("OUT_OTHER\n", outtest.clause(10, 11, 0, 7, "@next", "@in")).replace("@", p)


def results(keep, camera):
    """r4 = the racer the results' cameras follow (fn_1_3B11C's fn_1_5370(1, r4)); the pick to memmap.RESULTS_CAMERA.
    The site stands between two calls: r0, r3 and r5 to r12, CR0 are free. Reads only."""
    return pick(keep, "rc_") + f"""rc_say:
    or 3, 3, 4
    oris 3, 3, 0x8000
    lis 12, {C}
    stw 3, {camera}(12)
    blr
"""


def watch(keep, camera):
    """`results_watch`, after a frame's steps (frame_entry.py `done`), while the results run in a session's race
    (rule flag 0x1000, race mode 2): the pick above is made once, at the results' start, and a wreck still drawn
    there (RC_WRECK) can go during the results: a computer machine that touches a wreck with 0x00200000 makes it
    explode (car_update 0x85194, fn_1_25004), and the cameras would watch the empty place. So while the TV cameras
    (camera mode [.bss 0xF68]+0x48 = 9) follow the local seat (the racer wanted, live camera [.bss 0xF70]+0x8), the
    rule is asked again from this frame's memory; when it now gives the winner, the cameras' racer wanted becomes
    the winner, as the spectator's controls change it, and memmap.RESULTS_CAMERA says it once (RC_GONE) for the
    host's log. Every other answer leaves the cameras as they are. Picture only: the live camera is presentation
    (the A1/A2 gates: 0 differing with every camera on every racer); nothing simulated is written. Called with r31
    the control block and r30 main.rel's .bss; uses r0, r3 to r12 and CR0."""
    return f"""results_watch:
    lwz 9, 116(31)
    cmpwi 9, 0
    beqlr
    lha 0, 0x960(30)
    cmpwi 0, 2
    bnelr
    lwz 0, 0x3C30(30)
    andi. 0, 0, 0x1000
    beqlr
    lwz 9, 0xF68(30)
    cmpwi 9, 0
    beqlr
    lbz 0, 0x48(9)
    cmpwi 0, 9
    bnelr
    lwz 7, 0xF70(30)
    cmpwi 7, 0
    beqlr
    lha 0, 8(7)
    lis 12, {H}
    lwz 12, 0x14(12)
    cmpw 0, 12
    bnelr
""" + pick(keep, "rw_") + f"""rw_say:
    cmpwi 3, {RC_WINNER << 8}
    bnelr
    lwz 7, 0xF70(30)
    sth 4, 8(7)
    ori 3, 4, {RC_GONE << 8}
    oris 3, 3, 0x8000
    lis 12, {C}
    stw 3, {camera}(12)
    blr
"""


def follow(machine, dest):   # dest = 0 (r0) or the machine's index == local seat ? 0 : -1; uses r12 and dest only
    return f"""    lis 12, {H}
    lwz 12, 0x14(12)
    lha {dest}, 4({machine})
    subf {dest}, 12, {dest}
    neg 12, {dest}
    or {dest}, {dest}, 12
    srawi {dest}, {dest}, 31
    blr
"""


HELPERS = {
    "f26_3": follow(26, 3), "f26_0": follow(26, 0), "f27_3": follow(27, 3), "f27_0": follow(27, 0),
    "f30_0": follow(30, 0),
    "fin_4": f"    lis 12, {C}\n    lbz 4, 268(12)\n    blr\n",
    "fin_0": f"    lis 12, {C}\n    lbz 0, 268(12)\n    blr\n",
    "results_4": results(memmap.RESULTS_KEEP - memmap.CONTROL, memmap.RESULTS_CAMERA - memmap.CONTROL),
    "seat_3": f"    lis 3, {H}\n    lwz 3, 0x14(3)\n    blr\n",
    "count_3": f"    lis 12, {C}\n    lwz 3, {COUNT}(12)\n    blr\n",
    "count_0": f"    lis 12, {C}\n    lwz 0, {COUNT}(12)\n    blr\n",
    "state_6": state(6, 7, 4), "state_15": state(15, 3, 4),
    "place": f"    li 28, {PLACE_FROM}\n    blr\n",
    # r30 = the row: 0x7FFFFFFF when its racer is the local seat, 0x80000000 when not
    "row_4": f"""    lis 12, {H}
    lwz 12, 0x14(12)
    lwz 4, 0x4C(30)
    subf 4, 12, 4
    neg 12, 4
    or 4, 4, 12
    srawi 4, 4, 31
    xoris 4, 4, 0x7FFF
    xori 4, 4, 0xFFFF
    blr
""",
    # the local seat's machine (0x620 each from [.bss 0x6D838]), or 0 when there are none, as fn_1_86254 answers
    "machine": f"""    lis 12, 0x8000
    lwz 12, 0x30C8(12)
    addis 12, 12, 0x22
    lwz 3, 0x7878(12)
    cmplwi 3, 0
    beqlr
    lis 12, {H}
    lwz 12, 0x14(12)
    mulli 12, 12, 0x620
    add 3, 3, 12
    blr
""",
    # the car object (0x440 each from [.bss 0x6D83C], count .bss 0x6D84A) whose machine ([+0x32C] +0x4) is the local
    # seat; the first one if none is
    "name": f"""    lis 12, 0x8000
    lwz 12, 0x30C8(12)
    addis 12, 12, 0x22
    lwz 3, 0x787C(12)
    lbz 11, 0x788A(12)
    mr 10, 3
    lis 12, {H}
    lwz 12, 0x14(12)
name_loop:
    cmpwi 11, 0
    beq name_none
    lwz 9, 0x32C(3)
    lha 9, 4(9)
    cmpw 9, 12
    beqlr
    addi 3, 3, 0x440
    addi 11, 11, -1
    b name_loop
name_none:
    mr 3, 10
    blr
""",
}
assert 0x220000 + 0x787C == 0x1BA040 + 0x6D83C and 0x220000 + 0x788A == 0x1BA040 + 0x6D84A
assert 0x220000 + 0x7878 == 0x1BA040 + 0x6D838 and 0x15C760 + 0x03F29AF4 - 0x04000000 == 0x86254
assert 0x14A3E4 + 0x03F3BE58 - 0x04000000 == 0x8623C == 0x14A474 + 0x03F3BDC8 - 0x04000000


def s16(v):              # lis takes a signed immediate
    return v - 0x10000 if v >= 0x8000 else v


def asm():
    """The helpers and `follow_install` (r31 = the control block; called with bl; uses r0, r3-r12 and 32(r1)). In the
    native form the rank icons' two sites take `icon` (the racer the HUD follows); the fork's image is as before."""
    import native, rankicons
    if not native.ON:
        return installer("follow_install", "f", SITES, HELPERS, 264) + WATCH
    names = {rankicons.SITES[0]: "icon_c", rankicons.SITES[1]: "icon_0"}
    sites = [(off, orig, mask, names.get(off, helper)) for off, orig, mask, helper in SITES]
    assert sum(off in names and helper == "f30_0" for off, _, _, helper in SITES) == 2
    helpers = dict(HELPERS, icon_c=rankicons.icon("ic_", True), icon_0=rankicons.icon("iz_", False))
    return installer("follow_install", "f", sites, helpers, 264) + WATCH


WATCH = watch(memmap.RESULTS_KEEP - memmap.CONTROL, memmap.RESULTS_CAMERA - memmap.CONTROL)


def installer(name, prefix, sites, helpers, guard):
    """Helpers and an installer routine `name` that replaces each site's word with a `bl` to its helper, once per
    main.rel load (control block +guard: the main.rel address patched for). Labels start with prefix. The installer
    finds a helper from its own address (`bl` to the next instruction, `mflr`); the distances are worked out here from
    the assembled sizes (keystone takes no label differences as operands)."""
    from keystone import Ks, KS_ARCH_PPC, KS_MODE_PPC32, KS_MODE_BIG_ENDIAN
    import native
    ks = Ks(KS_ARCH_PPC, KS_MODE_PPC32 + KS_MODE_BIG_ENDIAN)
    size = lambda text: len(bytes(ks.asm(text)[0]))
    out, where, at = "", {}, 0
    for helper, body in helpers.items():
        where[helper] = at
        out += f"{prefix}o_{helper}:\n{body}"
        at += size(body)
    if native.ON:
        # The native form: a site's `bl` goes to an entry that tests the mode word and, in a plain start, does what
        # the replaced instruction did; one entry for every distinct replaced word of a helper (a call's target
        # depends on where it stands, so a replaced call gets an entry of its own).
        entries = {}
        for off, orig, mask, helper in sites:
            word, complete = native.original(off, orig, mask)
            field = native.test_field(word)
            key = (helper, word, off if word >> 26 == 18 else None)
            if key not in entries:
                entries[key] = f"{prefix}n_{len(entries)}"
                entry = native.mode_test(field) + f"    bne {field}, {prefix}o_{helper}\n" + native.stock(word, off, complete)
                out += f"{entries[key]}:\n{entry}"
                at += 4 * len(native.instructions(entry))
            native.SITES.append({"text": off, "original": word, "complete": complete, "label": entries[key],
                                 "helper": f"{prefix}o_{helper}", "field": field})
    head = f"""{name}:
    lis 12, 0x8000
    lwz 12, 0x30C8(12)
    lwz 11, {guard}(31)
    cmpw 11, 12
    beqlr
    mflr 0
    stw 0, 32(1)
"""
    out += head
    at += size(head.replace(f"{name}:", ""))
    # The native form's installer writes nothing: the sites are the patch list's, compiled into the module as
    # patched. Its site code is left out, not left to find the sites patched: a site whose table entry is masked
    # ("any bl": the other racers' boost and slide sounds) takes the list's own `bl` for the stock call and writes
    # the fork's over it, and the piece of the game around it then fails its check and leaves native code (met in
    # the native form's first match race: two pieces). The guard word is still kept.
    for k, (off, orig, mask, helper) in enumerate([] if native.ON else sites):
        a = 0xDC + off
        hi, lo = (a + 0x8000) >> 16, a & 0xFFFF
        lo = lo - 0x10000 if lo >= 0x8000 else lo
        pc = at + 11 * 4                     # {prefix}s_pc_k: after the 11 instructions before it (the bl's return address)
        site = f"""    addis 9, 12, {hi}
    addi 9, 9, {lo}
    lwz 10, 0(9)
    lis 8, {s16(mask >> 16)}
    ori 8, 8, {mask & 0xFFFF}
    and 10, 10, 8
    lis 8, {s16(orig >> 16)}
    ori 8, 8, {orig & 0xFFFF}
    cmpw 10, 8
    bne {prefix}s_{k}
    bl {prefix}s_pc_{k}
{prefix}s_pc_{k}:
    mflr 8
    addi 8, 8, {where[helper] - pc}
    subf 10, 9, 8
    rlwinm 10, 10, 0, 6, 29
    oris 10, 10, 0x4800
    ori 10, 10, 1
    stw 10, 0(9)
    dcbst 0, 9
    sync
    icbi 0, 9
    isync
{prefix}s_{k}:
"""
        out += site
        at += 22 * 4
    out += f"""    stw 12, {guard}(31)
    lwz 0, 32(1)
    mtlr 0
    blr
"""
    return out
