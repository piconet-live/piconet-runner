"""The seat routines' second part (standin.py assembles the three in order; its docstring, at "Input buffer", says
what it does): a player seat's controls from the input buffer, from `ps_yes` on: the buffer's index (the session's
step, or the race clock at `clock_index`), the local seat's capture, the record turned into the game's controls,
and the call of the game's own rule hook (`call_orig` to `co_done`)."""
import memmap


def asm():
    return f"""ps_yes:
    lwz 0, 0(31)
    andis. 0, 0, 1
    bne call_orig
    lis 8, {memmap.hi(memmap.CONTROL)}
    lwz 0, 116(8)
    cmpwi 0, 0
    beq clock_index
    lwz 0, 120(8)
    cmpwi 0, 0
    lwz 8, 124(8)
    bne have_index
    li 8, 1023
    b have_index
clock_index:
    lwz 8, 0x50A4(29)
have_index:
    lis 5, {memmap.hi(memmap.INPUTS)}
    cmpw 9, 11
    bne no_capture
    lis 4, {memmap.hi(memmap.CONTROL)}
    lwz 0, 116(4)
    cmpwi 0, 0
    bne no_capture
    lwz 4, 12(4)
    cmpwi 4, 0
    bne no_capture
    lwz 7, 44(30)
    add 7, 7, 8
    andi. 7, 7, 1023
    mulli 7, 7, 30
    add 7, 7, 9
    slwi 7, 7, 3
    add 7, 7, 5
    lwz 0, 0xA88(29)
    stw 0, 0(7)
    lhz 0, 0xA8C(29)
    sth 0, 4(7)
no_capture:
    andi. 7, 8, 1023
    mulli 7, 7, 30
    add 7, 7, 9
    slwi 7, 7, 3
    add 7, 7, 5
    lbz 0, 1(7)
    cmpwi 0, 0x80
    beq call_orig
    addi 6, 8, -1
    andi. 6, 6, 1023
    mulli 6, 6, 30
    add 6, 6, 9
    slwi 6, 6, 3
    add 6, 6, 5
    lis 0, 0x4330
    stw 0, 8(1)
    lfd 4, 56(30)
    lfd 5, 64(30)
    lfs 6, 48(30)
    lbz 0, 1(7)
    extsb 0, 0
    xoris 0, 0, 0x8000
    stw 0, 12(1)
    lfd 1, 8(1)
    fsubs 1, 1, 4
    fdivs 1, 1, 6
    lbz 0, 2(7)
    extsb 0, 0
    xoris 0, 0, 0x8000
    stw 0, 12(1)
    lfd 0, 8(1)
    fsubs 0, 0, 4
    fdivs 0, 0, 6
    stfs 0, 0x1F4(31)
    lbz 0, 3(7)
    extsb 0, 0
    xoris 0, 0, 0x8000
    stw 0, 12(1)
    lfd 0, 8(1)
    fsubs 0, 0, 4
    fdivs 0, 0, 6
    stfs 0, 0x1F8(31)
    lbz 0, 4(7)
    stw 0, 12(1)
    lfd 0, 8(1)
    fsubs 0, 0, 5
    fdivs 0, 0, 6
    stfs 0, 0x200(31)
    lbz 0, 5(7)
    stw 0, 12(1)
    lfd 0, 8(1)
    fsubs 0, 0, 5
    fdivs 0, 0, 6
    stfs 0, 0x204(31)
    fabs 2, 1
    fmuls 0, 1, 2
    stfs 0, 0x1FC(31)
    lwz 9, 0(31)
    li 0, 0x1048
    andc 9, 9, 0
    lwz 0, 0x190(31)
    cmpwi 0, 0
    beq spin_idle
    ori 9, 9, 8
spin_idle:
    lbz 10, 0(7)
    andi. 0, 10, 0x80
    beq no_side
    lfs 3, 16(30)
    .long 0xFC021840
    ble no_side
    oris 9, 9, 2
    lfs 3, 24(30)
    fmuls 3, 3, 1
    stfs 3, 0x610(31)
    lbz 0, 1(7)
    extsb 0, 0
    li 4, 1
    cmpwi 0, 0
    bgt side_dir
    li 4, -1
side_dir:
    stb 4, 0x614(31)
no_side:
    andi. 0, 9, 0x8000
    bne no_spin
    lbz 0, 0x481(31)
    cmplwi 0, 60
    ble no_spin
    andi. 0, 10, 0x20
    beq no_spin
    lfs 3, 20(30)
    .long 0xFC021840
    ble no_spin
    ori 9, 9, 8
no_spin:
    lbz 0, 0(6)
    andc 0, 10, 0
    andi. 4, 0, 4
    beq no_edge
    ori 9, 9, 0x2000
no_edge:
    andi. 4, 0, 0x40
    beq no_boost
    ori 9, 9, 0x40
no_boost:
    andi. 0, 10, 0x10
    beq no_1000
    ori 9, 9, 0x1000
no_1000:
    stw 9, 0(31)
    lwz 0, 0x{memmap.MACHINE_FLAGS2:X}(31)
    rlwinm 0, 0, 0, 30, 28
    stw 0, 0x{memmap.MACHINE_FLAGS2:X}(31)
call_orig:
    li 28, 0
    lwz 0, 36(30)
    cmpwi 0, 0
    beq co_call
    lha 9, 4(31)
    slwi 9, 9, 2
    addi 9, 9, 0x53F8
    lwzx 28, 29, 9
    li 0, 0
    stwx 0, 29, 9
    lwz 9, 0(31)
    andis. 0, 9, 0x0400
    stw 0, 8(1)
    rlwinm 9, 9, 0, 6, 4
    stw 9, 0(31)
co_call:
    mr 3, 31
    lis 12, 0x8000
    lwz 12, 0x30C8(12)
    addis 12, 12, 3
    addi 12, 12, 0x32C8
    mtctr 12
    bctrl
    lwz 0, 36(30)
    cmpwi 0, 0
    beq co_done
    lwz 9, 0(31)
    lwz 0, 8(1)
    or 9, 9, 0
    stw 9, 0(31)
    cmpwi 28, 0
    beq co_done
    lha 9, 4(31)
    slwi 9, 9, 2
    addi 9, 9, 0x53F8
    stwx 28, 29, 9
co_done:
    lwz 28, 16(1)
    lwz 29, 20(1)
    lwz 30, 24(1)
    lwz 31, 28(1)
    lwz 0, 36(1)
    mtlr 0
    addi 1, 1, 32
    blr
"""
