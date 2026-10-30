"""The frame routine's small services, called with bl (netframe.py assembles the parts in order; its docstring lists
the control block's words): `netsync` calls the network hook; `racetasks` (on entry, in a session) sets the
no-pause rule flag and keeps viewport 0's camera record on the local seat, then, from `rt_cam`, which `manager`
also calls inside every step, sets the tests' lap-1 boost flag and flag bit 1 of the presentation tasks that are
race state (from the race phase, 0x29) and of the two cameras' tasks (fn_1_90E0, fn_1_EE44: from the countdown,
0x28, so that a camera button pressed in the countdown is taken in the step that has it and taken again by a
rollback: raceview.py); `stepclock` sets the executable's frame counters from the step; `leave` marks the player done at the
results screens' end."""


def asm():
    return """netsync:
    mflr 0
    stw 0, 20(1)
    lis 12, NETHOOK_HI
    ori 12, 12, NETHOOK_LO
    mtctr 12
    bctrl
    lwz 0, 20(1)
    mtlr 0
    blr
racetasks:
    lwz 9, 116(31)
    cmpwi 9, 0
    beqlr
    lwz 0, 260(31)
    cmpwi 0, 0
    bne rt_pause
    lwz 0, 0x3C30(30)
    oris 0, 0, 0x0800
    stw 0, 0x3C30(30)
rt_pause:
    lwz 0, 248(31)
    cmpwi 0, 180
    bge rt_cam
    lwz 9, 0xF6C(30)
    cmpwi 9, 0
    beq rt_cam
    lis 8, 0x8180
    lwz 8, 0x14(8)
    sth 8, 2(9)
rt_cam:
    lis 7, 0x8000
    lwz 7, 0x30C8(7)
    lha 9, 0x3C2A(30)
    cmpwi 9, 0x28
    bltlr
    addis 11, 7, 1
    addi 11, 11, -0x6E44
    addis 12, 7, 1
    addi 12, 12, -0x10E0
    cmpwi 9, 0x29
    bge rt_race
    mr 6, 11
    mr 4, 11
    mr 3, 11
    mr 7, 11
    b rt_scan
rt_race:
    lwz 9, 520(31)
    cmpwi 9, 0
    beq rt_noboost
    lwz 9, 0x3C30(30)
    ori 9, 9, 0x10
    stw 9, 0x3C30(30)
rt_noboost:
    addis 6, 7, 0x16
    addi 6, 6, -0x1D3C
    addis 4, 7, 0xA
    addi 4, 4, -0x5958
    addis 3, 7, 6
    addi 3, 3, -0x7890
    addis 7, 7, 0xA
    addi 7, 7, -0x7CF4
rt_scan:
    lwz 9, 0xDAC(30)
    li 5, 0x40
rt_list:
    lwzx 10, 9, 5
    cmpwi 10, 0
    beq rt_done
    lwz 10, 8(10)
rt_next:
    cmpwi 10, 0
    beq rt_done
    lwz 8, 0(10)
    cmpw 8, 11
    beq rt_flag
    cmpw 8, 12
    beq rt_flag
    cmpw 8, 6
    beq rt_flag
    cmpw 8, 4
    beq rt_flag
    cmpw 8, 3
    beq rt_flag
    cmpw 8, 7
    bne rt_skip
rt_flag:
    lwz 8, 0x14(10)
    ori 8, 8, 1
    stw 8, 0x14(10)
rt_skip:
    lwz 10, 8(10)
    b rt_next
rt_done:
    cmpwi 5, 0x74
    beqlr
    cmpwi 5, 0x68
    beq rt_to74
    cmpwi 5, 0x4C
    beq rt_to68
    cmpwi 5, 0x44
    beq rt_to4C
    li 5, 0x44
    b rt_list
rt_to4C:
    li 5, 0x4C
    b rt_list
rt_to68:
    li 5, 0x68
    b rt_list
rt_to74:
    li 5, 0x74
    b rt_list
stepclock:
    lwz 9, 120(31)
    cmpwi 9, 0
    beqlr
    lwz 9, 124(31)
    addis 9, 9, 0x10
    lis 10, -0x7FE6
    stw 9, 0x66A0(10)
    stw 9, 0x66A4(10)
    stw 9, 0x66A8(10)
    stw 9, 0x66C0(10)
    stw 9, 0x66C8(10)
    blr
leave:
    lwz 9, 116(31)
    cmpwi 9, 0
    beqlr
    addis 9, 30, 9
    lbz 0, -0x1AE3(9)
    cmpwi 0, 8
    bnelr
    li 0, 1
    stw 0, 0xA30(31)
    blr
"""
