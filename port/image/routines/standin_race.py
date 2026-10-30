"""The seat routines' third part (standin.py assembles the three in order; its docstring says what they do): `refs`,
the rubber-band references among the player seats (read by nothing in the native form, where the band is off:
sites_cpu.py); `over`, the race-over test; and from `after_data` the
installer, the body of the fork's C0 code. OVER_OFF, REFS_OFF and REFS_HERE stand for offsets standin.py finds
by assembling once and puts in.

`over` in a running session (control block +120): the test runs once a displayed frame, outside the steps, on what
the frame before left, and that may rest on predicted inputs. What "over" starts cannot be simulated again: said on
a prediction, a rollback took the results phase back after it had begun and the race ended a second time.
So with every machine finished or out it says "over" only when every step run
so far ran with the official inputs (the step about to run, +124, is not past memmap.END_OFFICIAL), and tells the
host (memmap.END_OVER; the mute word +488 is cleared, so that what the end starts is heard after a frame that ran
no new step). Otherwise it asks the host to check those steps (memmap.END_ASKED) and says "not over" for this
frame. Outside a session, and with a host that never writes END_OFFICIAL, it answers as it always did.

What "every machine finished or out" is, and what the host's rule puts beside it (the race ends for everyone so many
steps after the first finish, and the results wait 480 steps after the last player seat's finish), is raceend.py's
text, which `endtest` (frame_replay.py) assembles too. With the rule, "over" is the step the results begin at: the
game's own wait before the results (fn_1_2DF98, on the countdown of the last machine with a pad port to finish or
go out: seat 0's, in every game) would hold them longer for seat 0 alone, so when "over" is said on official steps
the pointer to that countdown (.bss 0x25B8C) is cleared, and fn_1_2DF98 answers at once, as it does when no human
has finished. Nothing is simulated again from there (END_OVER), so the write is never taken back; it is made at the
same step in every game. Without the rule the pointer is left, and seat 0's countdown holds the results as it did."""
import memmap, raceend


def asm():
    return _TEXT.replace("CLAUSE", raceend.clause(11, "ov_", "over_yes", "not_all")).replace(
        "CONTROL_HI", str(memmap.hi(memmap.CONTROL))).replace(
        "END_OFFICIAL", str(memmap.END_OFFICIAL - memmap.CONTROL)).replace(
        "END_ASKED", str(memmap.END_ASKED - memmap.CONTROL)).replace(
        "END_OVER", str(memmap.END_OVER - memmap.CONTROL)).replace(
        "RACE_HI", str(memmap.hi(memmap.RACE))).replace(
        "FINISH_LIMIT", str(memmap.lo(memmap.RACE) + memmap.RACE_FINISH_LIMIT))


_TEXT = f"""refs:
    stwu 1, -16(1)
    mflr 0
    stw 0, 20(1)
    stw 31, 12(1)
    mr 31, 3
    bl refs_here
refs_here:
    mflr 12
    addi 12, 12, -REFS_HERE
    lis 11, 0x8000
    lwz 11, 0x30C8(11)
    addis 11, 11, 0x1C
    addi 11, 11, -0x5FC0
    lwz 0, 8(12)
    cmpwi 0, 0
    beq refs_orig
    lwz 0, 36(12)
    cmpwi 0, 0
    beq refs_orig
    lwz 10, 40(12)
    cmpwi 10, 0
    bne refs_mask
    li 10, -1
    lwz 0, 32(12)
    cmpwi 0, 0
    beq refs_player
    lwz 9, 0x50A4(11)
    cmpw 9, 0
    bge refs_mask
refs_player:
    lwz 9, 4(12)
    li 10, 1
    slw 10, 10, 9
refs_mask:
    addis 9, 11, 7
    lbz 8, -0x27B6(9)
    lwz 7, -0x27C8(9)
    li 3, 0
    li 4, 255
    li 5, -1
    li 6, 0
    li 12, 0
refs_loop:
    cmpw 3, 8
    bge refs_done
    srw 0, 10, 3
    andi. 0, 0, 1
    beq refs_next
    lwz 9, 0x{memmap.MACHINE_PROGRESS:X}(7)
    lbz 0, 0x{memmap.PROGRESS_RANK:X}(9)
    cmplw 0, 4
    bge refs_not_best
    mr 4, 0
    mr 6, 3
refs_not_best:
    cmpw 0, 5
    ble refs_next
    mr 5, 0
    mr 12, 3
refs_next:
    addi 3, 3, 1
    addi 7, 7, 0x{memmap.MACHINE_SIZE:X}
    b refs_loop
refs_done:
    cmpwi 5, -1
    beq refs_orig
    addis 9, 11, 7
    stb 4, -0x2586(9)
    stb 5, -0x2585(9)
    stb 6, -0x2584(9)
    stb 12, -0x2583(9)
refs_orig:
    mr 3, 31
    lis 12, 0x8000
    lwz 12, 0x30C8(12)
    addis 12, 12, 3
    addi 12, 12, 0x2B1C
    mtctr 12
    bctrl
    lwz 31, 12(1)
    lwz 0, 20(1)
    mtlr 0
    addi 1, 1, 16
    blr
over:
    lis 12, 0x8000
    lwz 12, 0x30C8(12)
    addis 11, 12, 0x1C
    addi 11, 11, -0x5FC0
    li 3, 0
    addis 10, 11, 2
    lwz 0, 0x5BA0(10)
    cmpwi 0, 0
    bnelr
    lwz 7, 0x3C30(11)
    andi. 0, 7, 0x800
    bnelr
CLAUSEover_yes:
    li 3, 1
    lis 9, CONTROL_HI
    lwz 0, 120(9)
    cmpwi 0, 0
    beqlr
    lwz 0, END_OFFICIAL(9)
    cmpwi 0, 0
    beqlr
    lwz 8, 124(9)
    cmplw 8, 0
    bgt over_ask
    stw 3, END_OVER(9)
    li 0, 0
    stw 0, 488(9)
    lis 8, RACE_HI
    lwz 8, FINISH_LIMIT(8)
    cmpwi 8, 0
    beqlr
    addis 8, 11, 2
    stw 0, 0x5B8C(8)
    blr
over_ask:
    stw 3, END_ASKED(9)
    li 3, 0
    blr
not_all:
    lwz 7, 0x3C30(11)
    andis. 0, 7, 0x0400
    bnelr
    lwz 0, 0x50A4(11)
    lis 8, 5
    ori 8, 8, 0x7030
    cmpw 0, 8
    blelr
    oris 7, 7, 8
    stw 7, 0x3C30(11)
    li 3, -1
    blr
after_data:
    mflr 9
    mtlr 0
    lis 12, 0x8000
    lwz 12, 0x30C8(12)
    cmpwi 12, 0
    beq done
    addis 11, 12, 0x1C
    addi 11, 11, -0x5FC0
    addis 10, 11, 7
    lwz 10, -0x27B0(10)
    addis 8, 12, 3
    addi 8, 8, 0x32C8
    cmpw 10, 8
    bne over_install
    addi 10, 9, 72
    addis 8, 11, 7
    stw 10, -0x27B0(8)
over_install:
    lwz 10, 0x3CFC(11)
    addis 8, 12, 3
    addi 8, 8, 0x396C
    cmpw 10, 8
    bne refs_install
    addi 10, 9, OVER_OFF
    stw 10, 0x3CFC(11)
refs_install:
    addis 8, 11, 7
    lwz 10, -0x2690(8)
    addis 11, 12, 3
    addi 11, 11, 0x2B1C
    cmpw 10, 11
    bne done
    addi 10, 9, REFS_OFF
    stw 10, -0x2690(8)
done:
    blr
"""
