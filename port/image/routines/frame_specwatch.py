"""A racer who is out is not watched (part of frame_spectate.py's text, which names the registers and words).

The rule: if the racer watched goes out, the camera swaps at once to the next racer still racing, and no button
lands on a racer who is out. Out is the game's own verdict (outtest.py: never a machine the Restore rule will put
back, a breakdown only at rest); a finished machine and a computer's machine are racers. The racer chosen (+252)
stays the choice; `ss_watched` gives the cameras the racer chosen while it races, else the racer the cameras are on
if that one races (they moved on in an earlier step), else the next racer in race order after the one chosen that
races; with nobody left racing, the racer chosen.

It is asked twice in a step. `specstep` asks before the game's task manager, as it writes the choices. A machine
goes out inside the step, in the race's tasks; so `specwatch` asks again where the task manager's pass reaches
the cameras' list (20: after the race's lists, before the cameras' tasks; smooth.py's hook calls it for a step
`specstep` has marked, memmap.SPEC_STEP): the cameras of the step a racer goes out in already follow the next,
and no picture shows the wreck. It writes the TV cameras' racer wanted (live camera +0x8), or with the chase
camera, when the racer changes, the camera set up anew behind the next (fn_1_5370(0, N): the state it took for
the wreck goes) with the racer and the view; nothing else of a choice (those are `specstep`'s). It is a routine
of its own from its first instruction to its return (the recompiler makes a function of each call's target: no
branch leaves one for the inside of another).

Where the cameras are is memory a rollback puts back, and the racer asked for (+252) is changed by a button only
(a frame routine that took the racer the cameras had moved on to for the choice, after a frame's steps, left the
view on the other racer when a rollback took the going-out back). So a rollback
across the step the racer went out in makes the same move in the same step, and one that takes the going-out back
finds the racer asked for racing and puts the cameras back on it."""

import outtest

TEXT = """ss_watched:
    addis 5, 30, 7
    lwz 8, -0x27C8(5)
    lbz 5, -0x27B6(5)
    lwz 4, 252(31)
    li 3, 0
    cmplw 4, 5
    bgelr
    mulli 10, 4, 0x620
    add 10, 10, 8
OUT_ASKED
sw_asked_racing:
    blr
sw_asked_out:
    lha 11, 0(6)
    cmplw 11, 5
    bge sw_pick
    mulli 10, 11, 0x620
    add 10, 10, 8
OUT_SHOWN
sw_shown_racing:
    mr 4, 11
    blr
sw_pick:
    mulli 10, 4, 0x620
    add 10, 10, 8
    lwz 10, 0x49C(10)
    cmpwi 10, 0
    beqlr
    lbz 11, 0x115(10)
    mtctr 5
sw_rank:
    addi 11, 11, 1
    cmpw 11, 5
    blt sw_seek
    li 11, 0
sw_seek:
    li 3, 0
    mr 10, 8
sw_find:
    lwz 6, 0x49C(10)
    cmpwi 6, 0
    beq sw_next
    lbz 0, 0x115(6)
    cmpw 0, 11
    bne sw_next
OUT_FOUND
sw_found:
    mr 4, 3
    li 3, 1
    blr
sw_next:
    addi 10, 10, 0x620
    addi 3, 3, 1
    cmpw 3, 5
    blt sw_find
sw_rank_next:
    bdnz sw_rank
    li 3, 0
    blr
specwatch:
    lwz 9, 248(31)
    cmpwi 9, 180
    bltlr
    lha 9, 0x3C2A(30)
    cmpwi 9, 0x29
    bnelr
    lha 0, 0x960(30)
    cmpwi 0, 2
    bnelr
    lwz 9, 0xF68(30)
    cmpwi 9, 0
    beqlr
    mflr 0
    stw 0, 20(1)
    lbz 11, 0x48(9)
    cmpwi 11, 9
    bne sx_chase
    lwz 7, 0xF70(30)
    cmpwi 7, 0
    beq sx_ret
    addi 6, 7, 8
    bl ss_watched
    sth 4, 8(7)
    b sx_ret
sx_chase:
    lwz 10, S_MODE(31)
    cmpwi 10, S_CHASE
    bne sx_ret
    lwz 9, 0xF6C(30)
    cmpwi 9, 0
    beq sx_ret
    addi 6, 9, 2
    bl ss_watched
    cmpwi 3, 0
    beq sx_ret
    li 3, 0
    bl ss_cameras
    lwz 9, 0xF6C(30)
    cmpwi 9, 0
    beq sx_ret
    addi 6, 9, 2
    bl ss_watched
    sth 4, 2(9)
    lwz 10, S_HEIGHT(31)
    addi 10, 10, 1
    sth 10, 0xA4(9)
sx_ret:
    lwz 0, 20(1)
    mtlr 0
    blr
"""
for _name, _m, _t1, _out, _racing in (("OUT_ASKED", 10, 12, "sw_asked_out", "sw_asked_racing"),
                                      ("OUT_SHOWN", 10, 12, "sw_pick", "sw_shown_racing"),
                                      ("OUT_FOUND", 10, 6, "sw_rank_next", "sw_found")):
    TEXT = TEXT.replace(_name + "\n", outtest.clause(_m, 30, 0, _t1, _out, _racing))
