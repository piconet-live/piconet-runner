"""The seat routines' first part (standin.py assembles the three in order; its docstring says what they do): the
branch over the data, the slot 'FZSI' (`data`), and the per-machine routine from `routine` on: the uniform rules
and the retired flag (`u_*`: with the race block's flow word, memmap.RACE_FLOW, flag 0x08000000 for every machine
the game's own verdict says is out, outtest.py, so never one the Restore rule will put back; without the word, as
every recording made before it was raced, for every machine with +0x58C bit 0x10 or 0x80), the test knock-out (`ko_*`), the D-pad's seat switch (`switch` to `seat_check`) and
the test whether this machine's seat is a player's (`off_never`, `ps_mask`). The text starts with the empty line
the whole text started with."""
import struct
import memmap
import outtest

fw = lambda v: struct.unpack(">I", struct.pack(">f", v))[0]


def asm(seat, off, uniform, refmask, delay):
    return f"""
    mflr 0
    bl after_data
data:
    .long 0x465A5349
    .long {seat}
    .long 1
    .long 0
    .long {fw(0.8)}
    .long {fw(0.2)}
    .long {fw(0.4)}
    .long 0
    .long {off}
    .long {uniform}
    .long {refmask}
    .long {delay}
    .long {fw(100.0)}
    .long 0
    .long 0x43300000
    .long 0x80000000
    .long 0x43300000
    .long 0
routine:
    stwu 1, -32(1)
    mflr 0
    stw 0, 36(1)
    stw 31, 28(1)
    stw 30, 24(1)
    stw 29, 20(1)
    stw 28, 16(1)
    mr 31, 3
    bl r_here
r_here:
    mflr 30
    addi 30, 30, -108
    lis 12, 0x8000
    lwz 12, 0x30C8(12)
    addis 29, 12, 0x1C
    addi 29, 29, -0x5FC0
    lwz 0, 8(30)
    cmpwi 0, 0
    beq call_orig
    lwz 0, 36(30)
    cmpwi 0, 0
    beq not_uniform
    addis 10, 29, 7
    lbz 11, -0x27B6(10)
    lwz 10, -0x27C8(10)
    cmpwi 11, 0
    beq u_retired
    mtctr 11
    lis 9, {memmap.hi(memmap.RACE)}
    lwz 0, {memmap.lo(memmap.RACE) + memmap.RACE_FLOW}(9)
    cmpwi 0, 0
    bne u_game
u_out:
    lwz 0, 0x{memmap.MACHINE_FLAGS2:X}(10)
    andi. 0, 0, 0x90
    beq u_next
    lwz 9, 0(10)
    andis. 0, 9, 1
    bne u_next
    oris 9, 9, 0x0800
    stw 9, 0(10)
u_next:
    addi 10, 10, 0x{memmap.MACHINE_SIZE:X}
    bdnz u_out
    b u_retired
u_game:
{outtest.clause(10, 29, 0, 9, "u_game_out", "u_game_next")}u_game_out:
    lwz 9, 0(10)
    oris 9, 9, 0x0800
    stw 9, 0(10)
u_game_next:
    addi 10, 10, 0x{memmap.MACHINE_SIZE:X}
    bdnz u_game
u_retired:
    lwz 9, 0(31)
    rlwinm 9, 9, 0, 23, 21
    stw 9, 0(31)
    lha 9, 4(31)
    li 8, 50
    lwz 10, 40(30)
    cmpwi 10, 0
    bne u_mask
    lwz 0, 32(30)
    cmpwi 0, 0
    beq u_standin
    lwz 11, 0x50A4(29)
    cmpw 11, 0
    bge u_write
u_standin:
    lwz 11, 4(30)
    cmpw 9, 11
    bne u_write
    b u_player
u_mask:
    srw 0, 10, 9
    andi. 0, 0, 1
    beq u_write
u_player:
    li 8, 100
u_write:
    lis 11, {memmap.hi(memmap.RACE)}
    lwz 0, {memmap.lo(memmap.RACE)}(11)
    xoris 0, 0, 0x465A
    cmplwi 0, 0x5243
    bne u_store
    addi 11, 11, {memmap.lo(memmap.RACE) + memmap.RACE_SETTINGS}
    lbzx 8, 11, 9
u_store:
    addis 11, 29, 2
    addi 11, 11, 0x5C48
    slwi 0, 9, 1
    sthx 8, 11, 0
not_uniform:
    lis 8, {memmap.hi(memmap.CONTROL)}
    lwz 0, 240(8)
    cmpwi 0, 0
    beq no_ko
    lha 9, 4(31)
    cmpwi 0, 31
    bne ko_one
    cmpwi 9, 0
    beq no_ko
    lwz 0, 0(31)
    andi. 0, 0, 0x80
    bne no_ko
    b ko_frame
ko_one:
    addi 9, 9, 1
    cmpw 9, 0
    bne no_ko
ko_frame:
    lwz 0, 244(8)
    lwz 9, 0x50A4(29)
    cmpw 9, 0
    blt no_ko
    lwz 0, 240(8)
    cmpwi 0, 31
    li 0, 0
    bne ko_set
    lis 0, 0x3F80
ko_set:
    stw 0, 0x{memmap.MACHINE_ENERGY:X}(31)
no_ko:
    lha 9, 4(31)
    cmpwi 9, 0
    bne seat_check
    lwz 0, 0(31)
    andis. 0, 0, 0x0400
    bne switch
    lbz 0, 0x{memmap.MACHINE_PAD:X}(31)
    extsb 0, 0
    cmpwi 0, -1
    beq switch
    mr 3, 31
    addis 12, 12, 9
    addi 12, 12, -0x2690
    mtctr 12
    bctrl
    lwz 9, 28(30)
    addi 9, 9, 1
    stw 9, 28(30)
switch:
    lhz 9, 0x998(29)
    lwz 10, 12(30)
    stw 9, 12(30)
    andc 10, 9, 10
    lis 7, {memmap.hi(memmap.CONTROL)}
    lwz 7, 116(7)
    cmpwi 7, 0
    beq dpad_ok
    lis 7, {memmap.hi(memmap.CONTROL)}
    lwz 7, 0xA38(7)
    cmpwi 7, 0
    bne dpad_ok
    li 10, 0
dpad_ok:
    lwz 11, 4(30)
    addis 7, 29, 7
    lbz 8, -0x27B6(7)
    cmplw 11, 8
    blt seat_ok
    li 11, 0
seat_ok:
    andi. 0, 10, 2
    beq no_right
    addi 11, 11, 1
    cmpw 11, 8
    blt no_right
    li 11, 0
no_right:
    andi. 0, 10, 1
    beq no_left
    addi 11, 11, -1
    cmpwi 11, 0
    bge no_left
    addi 11, 8, -1
no_left:
    stw 11, 4(30)
    lwz 9, 0xF6C(29)
    cmpwi 9, 0
    beq seat_check
    lis 7, {memmap.hi(memmap.CONTROL)}
    lwz 0, 248(7)
    cmpwi 0, 180
    blt cam_seat
    lwz 0, {memmap.SPEC_MODE - memmap.CONTROL}(7)
    cmpwi 0, {memmap.SPEC_CHASE}
    beq seat_check
cam_seat:
    sth 11, 2(9)
seat_check:
    lwz 0, 32(30)
    cmpwi 0, 0
    beq off_never
    lwz 9, 0x50A4(29)
    cmpw 9, 0
    bge call_orig
off_never:
    lha 9, 4(31)
    lwz 11, 4(30)
    lwz 10, 40(30)
    cmpwi 10, 0
    bne ps_mask
    cmpw 9, 11
    bne call_orig
    b ps_yes
ps_mask:
    srw 0, 10, 9
    andi. 0, 0, 1
    beq call_orig
"""
