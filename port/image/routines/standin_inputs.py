"""The seat routines' second part (standin.py assembles the three in order; its docstring, at "Input buffer", says
what it does): a player seat's controls from the input buffer, from `ps_yes` on: the buffer's index (the session's
step, or the race clock at `clock_index`), the local seat's capture, the record turned into the game's controls,
and the call of the game's own rule hook (`call_orig` to `co_done`). With a game mode (memmap.MODES +4, the mode's
block; sites_mode.py has a seat's record) the hook's call is wrapped as the mode's vocabulary says: before it, a
seat whose record says "pedals locked" (1) takes the record's accelerator (M+0x200) and brake (M+0x204) and no
accelerator press (flag 0x2000): after both input sources, so CPUs are held too, and before the game's own holds
(before GO, time over), which still win; after it, a seat whose record says "boost off" (2) has its boost request
(flag 0x40) cleared, after the lap-1 rule as the race map's refusal point (0x33244) would. Without a mode both are
one test each. The race side's words (mode.h's record flags): a seat handed to the CPU (32: hand_to_cpu, and every seat
through a rolling start's countdown) keeps the CPU's controls, as a record that says "the AI drives"; through the
rolling start's countdown (64) the CPU's steering and strafe are saved before the hook and put back after it, with
the game's own 0.6 accelerator and no brake, and its record says "boost off" as well: the hook's pre-GO hold does not
stop a CPU's boost or spin request, so both are cleared here until GO (flags 0x40, 0x8; a CPU never side-attacks); a seat whose
record says "boost" (8) has its boost request set before the hook (the mode's `boost seat`), so the game's whole path
judges it: the hook's start hold and lap-1 rule, then "boost off" after it. A seat whose stat record (memmap.MODE_STATS)
has stats the mode changed (seat.stat.*) takes their values into the machine's fields right after the hook, which has
just rebuilt them from the car data: the step then drives with them, and a stat the mode never touched stays the game's."""
import memmap

MODE_SEAT_SHIFT = 4       # a seat's record in the mode's block: 16 bytes
BOOST, HAND, ROLL = 8, 32, 64   # its flags (sites_mode.py)
ROLL_ACCEL = 0x3F19999A   # 0.6: the game's own accelerator before GO (.rodata 0xE24)
# the saved steering's place from the seat's record (block + 16 seat) less 8 seat: block + ROLL_SAVE + 8 seat
ROLL_AT = memmap.MODE_ROLL_SAVE
assert memmap.MODE_SEAT == 1 << MODE_SEAT_SHIFT and memmap.lo(memmap.MODES) + 4 < 0x8000
assert memmap.MODE_ROLL == 8 and ROLL_AT + 4 < 0x8000
# M's field of each stat, by its bit in the stat record (port/core/src/mode.h STAT_FIELDS; the image's test holds them equal)
STAT_FIELDS = (0x8, 0x220, 0x22C, 0xC, 0x14, 0x1C, 0x23C, 0x230, 0x234, 0x30, 0x24, 0x28, 0x10, 0x20, 0x18, 0x2C, 0x238, 0x477)
BYTE_FIELD = 0x477        # the grip window (byte), a byte: the record holds its whole number
FIRST_TWO = memmap.MODE_STATS_FIRST
assert memmap.MODE_STATS + 4 + 4 * FIRST_TWO < 0x8000
assert memmap.MODE_STATS_TWO + 4 * (len(STAT_FIELDS) - FIRST_TWO) < 0x8000 and FIRST_TWO <= 16


def stat_writes():
    """After the hook, r11 the mode's block: the changed stats into the machine r31. Uses r0, r10, r12, cr0."""
    writes = "".join(f"""    andi. 0, 10, {1 << i}
    beq co_stat_{i}
    lwz 0, {memmap.MODE_STATS + 4 + 4 * i:#x}(12)
    stw 0, {field:#x}(31)
co_stat_{i}:
""" for i, field in enumerate(STAT_FIELDS[:FIRST_TWO]))
    # the stats page's: their bits are in the same word, their values in the page's record (a stat's bit can pass 16)
    last = len(STAT_FIELDS) - 1
    page = "".join(f"""    rlwinm. 0, 10, 0, {31 - i}, {31 - i}
    beq co_stat_{i}
    lwz 0, {memmap.MODE_STATS_TWO + 4 * (i - FIRST_TWO):#x}(12)
    {"stb" if field == BYTE_FIELD else "stw"} 0, {field:#x}(31)
co_stat_{i}:
""" for i, field in enumerate(STAT_FIELDS) if i >= FIRST_TWO)
    return f"""    lha 12, 4(31)
    mulli 12, 12, {memmap.MODE_STAT}
    add 12, 12, 11
    lwz 10, {memmap.MODE_STATS:#x}(12)
    cmpwi 10, 0
    beq co_stats_done
{writes}    rlwinm. 0, 10, 0, {31 - last}, {31 - FIRST_TWO}
    beq co_stats_done
    lha 12, 4(31)
    slwi 12, 12, {memmap.MODE_STAT_TWO_SHIFT}
    add 12, 12, 11
{page}co_stats_done:
"""


def asm():
    return f"""ps_yes:
    lis 8, {memmap.hi(memmap.MODES)}
    lwz 8, {memmap.lo(memmap.MODES) + 4}(8)
    cmplwi 8, 0
    beq ps_player
    slwi 0, 9, {MODE_SEAT_SHIFT}
    add 8, 8, 0
    lwz 0, 0(8)
    andi. 0, 0, {HAND}
    bne call_orig
ps_player:
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
    lis 11, {memmap.hi(memmap.MODES)}
    lwz 11, {memmap.lo(memmap.MODES) + 4}(11)
    cmplwi 11, 0
    beq co_plain
    lha 9, 4(31)
    slwi 9, 9, {MODE_SEAT_SHIFT}
    add 11, 11, 9
    lwz 9, 0(11)
    andi. 0, 9, {ROLL}
    beq co_pedals
    lha 12, 4(31)
    slwi 12, 12, 3
    subf 12, 12, 11
    lwz 0, 0x1FC(31)
    stw 0, {ROLL_AT:#x}(12)
    lwz 0, 0x1F8(31)
    stw 0, {ROLL_AT + 4:#x}(12)
co_pedals:
    andi. 0, 9, {BOOST}
    beq co_pedals_lock
    lwz 0, 0(31)
    ori 0, 0, 0x40
    stw 0, 0(31)
co_pedals_lock:
    andi. 0, 9, 1
    beq co_plain
    lwz 0, 4(11)
    stw 0, 0x200(31)
    lwz 0, 8(11)
    stw 0, 0x204(31)
    lwz 0, 0(31)
    rlwinm 0, 0, 0, 19, 17
    stw 0, 0(31)
co_plain:
    mr 3, 31
    lis 12, 0x8000
    lwz 12, 0x30C8(12)
    addis 12, 12, 3
    addi 12, 12, 0x32C8
    mtctr 12
    bctrl
    lis 11, {memmap.hi(memmap.MODES)}
    lwz 11, {memmap.lo(memmap.MODES) + 4}(11)
    cmplwi 11, 0
    beq co_boost
{stat_writes()}    lha 9, 4(31)
    slwi 9, 9, {MODE_SEAT_SHIFT}
    add 11, 11, 9
    lwz 9, 0(11)
    andi. 0, 9, {ROLL}
    beq co_off
    lha 12, 4(31)
    slwi 12, 12, 3
    subf 12, 12, 11
    lwz 0, {ROLL_AT:#x}(12)
    stw 0, 0x1FC(31)
    lwz 0, {ROLL_AT + 4:#x}(12)
    stw 0, 0x1F8(31)
    lis 0, {ROLL_ACCEL >> 16:#x}
    ori 0, 0, {ROLL_ACCEL & 0xFFFF:#x}
    stw 0, 0x200(31)
    li 0, 0
    stw 0, 0x204(31)
    lwz 0, 0(31)
    rlwinm 0, 0, 0, 29, 27
    stw 0, 0(31)
co_off:
    andi. 0, 9, 2
    beq co_boost
    lwz 0, 0(31)
    rlwinm 0, 0, 0, 26, 24
    stw 0, 0(31)
co_boost:
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
