"""The spectator's controls, `speccontrols`: the first part of frame_spectate.py's text, which says what the routine
does and substitutes the names in capitals (S_MODE and the like). From the pad's sample and what is newly pressed
(`sc_not_up` to the tests of the spectator's HUD, the game's mode and the race phase), A (`sc_no_a`), the mode in
use taken from the TV cameras (to `sc_in_use`), D-pad down (`sc_mode`), the C-stick (`sc_height`), and the racer
before, after or first in race order (`sc_racer` to `sc_next_rank`).

The racer buttons (left, right) go on from the racer asked for (+252) while that one races, and from the racer
the cameras are on when it is out (the cameras have left it, frame_specwatch.py): the press then lands on the
racer before or after the one shown. +252 is written by a press only. The mode in use is taken with the variant
the game has in use, whatever it is: it is one of that type's.

D-pad down takes the mode after the one last asked for (memmap.SPEC_ASKED), not after the mode in use: where the
game leaves a manual shot's type for another at once (type 3 on course 24, run cam3-b), the mode in use fell back
to an earlier one, and a press that went on from there never reached the chase camera.

Out is the game's own verdict (outtest.py), as everywhere in the image: the racer asked for (r9), the one shown
(r9), each racer looked at in race order (r3)."""
import outtest
import memmap

TEXT = f"""speccontrols:
    lhz 9, 0x998(30)
    lbz 10, 0x99D(30)
    extsb 10, 10
    cmpwi 10, STICK_UP
    blt sc_not_up
    oris 9, 9, 1
sc_not_up:
    cmpwi 10, STICK_DOWN
    bgt sc_not_down
    oris 9, 9, 2
sc_not_down:
    lwz 0, 72(31)
    or 9, 9, 0
    li 0, 0
    stw 0, 72(31)
    lwz 8, 64(31)
    stw 9, 64(31)
    andc 8, 9, 8
    lwz 0, 56(31)
    cmpwi 0, 0
    beqlr
    lha 0, 0x960(30)
    cmpwi 0, 2
    bnelr
    lha 0, 0x3C2A(30)
    cmpwi 0, 0x29
    bnelr
    andi. 0, 8, 0x100
    beq sc_no_a
    lwz 0, 68(31)
    xori 0, 0, 1
    stw 0, 68(31)
sc_no_a:
    lwz 10, S_MODE(31)
    cmpwi 10, S_MANUAL
    blt sc_in_use
    cmpwi 10, S_CHASE
    bge sc_in_use
    lwz 0, S_PENDING(31)
    cmpwi 0, 0
    bne sc_in_use
    lwz 9, 0xF68(30)
    cmpwi 9, 0
    beq sc_in_use
    lbz 0, 0x48(9)
    cmpwi 0, 9
    bne sc_in_use
    lwz 7, 0xF70(30)
    cmpwi 7, 0
    beq sc_in_use
    lbz 0, 0(7)
    cmpwi 0, 4
    bne sc_in_use
    lha 9, 2(7)
    lha 0, 0x12(7)
    cmpw 9, 0
    bne sc_in_use
    cmplwi 9, 4
    bge sc_in_use
    addi 9, 9, S_MANUAL
    cmpw 9, 10
    beq sc_in_use
    mr 10, 9
    stw 10, S_MODE(31)
    lha 9, 4(7)
    stw 9, S_HEIGHT(31)
sc_in_use:
    andi. 0, 8, 0x4
    beq sc_no_mode
    lwz 10, S_ASKED(31)
    addi 10, 10, 1
    cmpwi 10, S_MODES
    blt sc_mode
    li 10, 0
sc_mode:
    stw 10, S_MODE(31)
    stw 10, S_ASKED(31)
    li 0, 0
    stw 0, S_HEIGHT(31)
    li 0, 1
    stw 0, S_PENDING(31)
sc_no_mode:
    andis. 0, 8, 3
    beq sc_racer
    cmpwi 10, S_MANUAL
    blt sc_racer
    lis 11, TOPS_HI
    ori 11, 11, TOPS_LO
    mulli 12, 10, 5
    srw 11, 11, 12
    andi. 11, 11, 31
    lwz 12, S_HEIGHT(31)
    andis. 0, 8, 1
    beq sc_lower
    addi 12, 12, 1
    cmpw 12, 11
    ble sc_height
    li 12, 0
    b sc_height
sc_lower:
    addi 12, 12, -1
    cmpwi 12, 0
    bge sc_height
    mr 12, 11
sc_height:
    stw 12, S_HEIGHT(31)
    cmpwi 10, S_CHASE
    beq sc_racer
    li 0, 1
    stw 0, S_PENDING(31)
sc_racer:
    addis 5, 30, 7
    lwz 6, -0x27C8(5)
    lbz 5, -0x27B6(5)
    cmpwi 5, 0
    beqlr
    andi. 0, 8, 0x8
    li 11, 0
    li 12, 1
    bne sc_seek
    andi. 0, 8, 0x3
    beqlr
    lwz 4, 252(31)
    cmplw 4, 5
    bgelr
    mulli 9, 4, 0x{memmap.MACHINE_SIZE:X}
    add 9, 9, 6
OUT_ASKED
sc_asked_out:
    lwz 9, 0xF68(30)
    cmpwi 9, 0
    beq sc_from
    lbz 0, 0x48(9)
    cmpwi 0, 9
    bne sc_from_chase
    lwz 9, 0xF70(30)
    cmpwi 9, 0
    beq sc_from
    lha 3, 8(9)
    b sc_from_shown
sc_from_chase:
    lwz 9, 0xF6C(30)
    cmpwi 9, 0
    beq sc_from
    lha 3, 2(9)
sc_from_shown:
    cmplw 3, 5
    bge sc_from
    mulli 9, 3, 0x{memmap.MACHINE_SIZE:X}
    add 9, 9, 6
OUT_SHOWN
sc_shown_racing:
    mr 4, 3
sc_from:
    mulli 4, 4, 0x{memmap.MACHINE_SIZE:X}
    add 4, 4, 6
    lwz 4, 0x{memmap.MACHINE_PROGRESS:X}(4)
    cmpwi 4, 0
    beqlr
    lbz 11, 0x{memmap.PROGRESS_RANK:X}(4)
    li 12, 1
    andi. 0, 8, 0x2
    bne sc_step
    li 12, -1
sc_step:
    add 11, 11, 12
sc_seek:
    mtctr 5
sc_rank:
    cmpwi 11, 0
    bge sc_low
    add 11, 11, 5
sc_low:
    cmpw 11, 5
    blt sc_high
    subf 11, 5, 11
sc_high:
    li 4, 0
    mr 3, 6
sc_find:
    lwz 9, 0x{memmap.MACHINE_PROGRESS:X}(3)
    cmpwi 9, 0
    beq sc_find_next
    lbz 9, 0x{memmap.PROGRESS_RANK:X}(9)
    cmpw 9, 11
    bne sc_find_next
OUT_FOUND
sc_found:
    stw 4, 252(31)
    li 0, 1
    stw 0, S_PENDING(31)
    blr
sc_find_next:
    addi 3, 3, 0x{memmap.MACHINE_SIZE:X}
    addi 4, 4, 1
    cmpw 4, 5
    blt sc_find
sc_next_rank:
    add 11, 11, 12
    bdnz sc_rank
    blr
"""
for _name, _m, _t0, _out, _racing in (("OUT_ASKED", 9, 0, "sc_asked_out", "sc_from"),
                                      ("OUT_SHOWN", 9, 0, "sc_from", "sc_shown_racing"),
                                      ("OUT_FOUND", 3, 9, "sc_next_rank", "sc_found")):
    TEXT = TEXT.replace(_name + "\n", outtest.clause(_m, 30, _t0, 10, _out, _racing))
