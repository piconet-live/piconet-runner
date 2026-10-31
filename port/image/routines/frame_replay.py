"""The frame routine's steps (netframe.py assembles the parts in order; its docstring, at +24 and below, says why):
`replay` and `replay_filter` set the task filter for every step of a frame but its last, `filters` puts back the
values the frame began with, `net_step` counts the session's step, and `manager` is the step wrapper around the
game's simulation manager: it sets the words mute.py reads (+488, +492) and, in a session, calls `rt_cam`
(frame_tasks.py) and `specstep` (frame_spectate.py) first, then `simgen`, which seeds the simulation's own
generator for the step (sites_generator.py). `endtest` says whether the race-over test (standin_race.py,
`over`) would find the race over on the machines as they are: r3 = 1 in a running session's race phase when the
test's own guards are clear and the race is over as raceend.py's text says, the text the race-over test itself
assembles: every machine has finished or is out, or, with the host's rule, its end is reached and the results' wait
is done. The frame routine asks before and after every step and runs no further step in a frame once a step has
made it so (frame_entry.py, `extra_cut`): with the rule too, every game stands before the same step when the
race-over test speaks. `endtest_extra` is the same question before an extra step, where the race clock has not yet
counted the step just run (raceend.py)."""
import memmap, raceend


def asm():
    return _TEXT.replace("CLAUSE", raceend.clause(30, "et_", "et_yes", "et_no", behind=11)).replace(
        "SIMGEN_TEXT", _simgen())


# The simulation's own generator (sites_generator.py): before every step of a running race whose race block says so
# (memmap.RACE_SIM_GENERATOR 1), SIM_GENERATOR = fmix32(CPU seed ^ step * 0x9E3779B9) (MurmurHash3's finaliser, so
# that neighbouring steps' words look unrelated to the LCG) and SIM_GENERATOR_SHIFT = SIM_GENERATOR - 0x801A63C0;
# otherwise the shift is 0 and the race's draws take the executable's word as the stock game does. The step (+124)
# and the CPU seed (race block +28) are the same in every game of the race, in a step run again and in a playback,
# so a step's draws start from the same word everywhere. Writes r0, r9, r10, r11, r12 and cr0.
def _simgen():
    race, word, shift = memmap.RACE, memmap.SIM_GENERATOR, memmap.SIM_GENERATOR_SHIFT
    assert memmap.lo(race + memmap.RACE_SIM_GENERATOR) < 0x8000 and memmap.lo(race + memmap.RACE_CPUSEED) < 0x8000
    assert memmap.hi(word) == memmap.hi(memmap.CONTROL) and word - memmap.CONTROL < 0x8000
    delta = (word - memmap.EXECUTABLE_GENERATOR) & 0xFFFFFFFF
    return f"""simgen:
    li 9, 0
    lwz 0, 120(31)
    cmpwi 0, 0
    beq sg_shift
    lis 12, {memmap.hi(race)}
    lwz 0, {memmap.lo(race + memmap.RACE_SIM_GENERATOR)}(12)
    cmpwi 0, 0
    beq sg_shift
    lwz 10, {memmap.lo(race + memmap.RACE_CPUSEED)}(12)
    lwz 11, 124(31)
    lis 9, 0x9E37
    ori 9, 9, 0x79B9
    mullw 11, 11, 9
    xor 10, 10, 11
    srwi 11, 10, 16
    xor 10, 10, 11
    lis 9, 0x85EB
    ori 9, 9, 0xCA6B
    mullw 10, 10, 9
    srwi 11, 10, 13
    xor 10, 10, 11
    lis 9, 0xC2B2
    ori 9, 9, 0xAE35
    mullw 10, 10, 9
    srwi 11, 10, 16
    xor 10, 10, 11
    stw 10, {word - memmap.CONTROL}(31)
    lis 9, {delta >> 16}
    ori 9, 9, {delta & 0xFFFF}
sg_shift:
    stw 9, {shift - memmap.CONTROL}(31)
    blr
"""


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
    bl simgen
    lis 12, 0x8000
    lwz 12, 0x30C8(12)
    addi 12, 12, 0x4158
    mtctr 12
    bctrl
    lwz 0, 16(1)
    mtlr 0
    blr
SIMGEN_TEXTendtest_extra:
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
