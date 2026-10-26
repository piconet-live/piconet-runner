"""The frame routine's steps (netframe.py assembles the parts in order; its docstring, at +24 and below, says why):
`replay` and `replay_filter` set the task filter for every step of a frame but its last, `filters` puts back the
values the frame began with, `net_step` counts the session's step, and `manager` is the step wrapper around the
game's simulation manager: it sets the words mute.py reads (+488, +492) and, in a session, calls `rt_cam`
(frame_tasks.py) and `specstep` (frame_spectate.py) first. `endtest` says whether the race-over test (standin_race.py,
`over`) would find the race over on the machines as they are: r3 = 1 in a running session's race phase when the
test's own guards are clear and the race is over as raceend.py's text says, the text the race-over test itself
assembles: every machine has finished or is out, or, with the host's rule, its end is reached and the results' wait
is done. The frame routine asks before and after every step and runs no further step in a frame once a step has
made it so (frame_entry.py, `extra_cut`): with the rule too, every game stands before the same step when the
race-over test speaks. `endtest_extra` is the same question before an extra step, where the race clock has not yet
counted the step just run (raceend.py)."""
import memmap, raceend


def asm():
    return _TEXT.replace("CLAUSE", raceend.clause(30, "et_", "et_yes", "et_no", behind=11))


_TEXT = """replay:
    lwz 9, 8(31)
    li 0, 1
    cmpwi 9, 0
    bne replay_k
    stw 0, 460(31)
    blr
replay_k:
    lwz 9, 24(31)
    cmpwi 9, 0
    beq replay_filter
    lwz 9, 0xDAC(30)
    lwz 0, 24(1)
    lwz 10, 312(31)
    or 0, 0, 10
    stw 0, 0x10(9)
    blr
replay_filter:
    lwz 9, 0xDAC(30)
    li 0, 1
    stw 0, 0x14(9)
    addis 9, 30, -1
    li 0, 0
    stw 0, -0x70A4(9)
    blr
filters:
    lwz 9, 0xDAC(30)
    lwz 0, 8(1)
    stw 0, 0x14(9)
    lwz 0, 24(1)
    stw 0, 0x10(9)
    addis 9, 30, -1
    lwz 0, 28(1)
    stw 0, -0x70A4(9)
    blr
net_step:
    lwz 9, 120(31)
    cmpwi 9, 0
    beqlr
    lwz 9, 124(31)
    addi 9, 9, 1
    stw 9, 124(31)
    blr
manager:
    mflr 0
    stw 0, 16(1)
    li 10, 0
    lwz 9, 120(31)
    cmpwi 9, 0
    bne m_running
    stw 10, 492(31)
    b m_flag
m_running:
    lwz 9, 124(31)
    lwz 11, 492(31)
    cmplw 9, 11
    li 10, 1
    blt m_flag
    li 10, 0
    addi 11, 9, 1
    stw 11, 492(31)
m_flag:
    stw 10, 488(31)
    lwz 9, 116(31)
    cmpwi 9, 0
    beq m_call
    bl rt_cam
    bl specstep
m_call:
    lis 12, 0x8000
    lwz 12, 0x30C8(12)
    addi 12, 12, 0x4158
    mtctr 12
    bctrl
    lwz 0, 16(1)
    mtlr 0
    blr
endtest_extra:
    li 11, 1
    b et_enter
endtest:
    li 11, 0
et_enter:
    li 3, 0
    lwz 9, 120(31)
    cmpwi 9, 0
    beqlr
    lha 9, 0x3C2A(30)
    cmpwi 9, 0x29
    bnelr
    addis 10, 30, 2
    lwz 0, 0x5BA0(10)
    cmpwi 0, 0
    bnelr
    lwz 0, 0x3C30(30)
    andi. 0, 0, 0x800
    bnelr
CLAUSEet_yes:
    li 3, 1
et_no:
    blr
"""
