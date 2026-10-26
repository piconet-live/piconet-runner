"""The frame routine's state checksum `checksum`, stored after every step in the ring at memmap.CHECKSUMS (netframe.py
assembles the parts in order; its docstring's last paragraphs say what is summed). TABLES stands for the skip
masks' words, which netframe.py works out from the machine-state map and puts in.

`genlog`, after every step of a running session in a game told to keep the logs (memmap.IMAGE_LOGS; a players'
game writes nothing): the race's two generator words as that step left them, into the
tests' log (memmap.GENERATOR_LOG, 8 bytes at step & 2047: the executable's 0x801A63C0, then the effects' .data
0x1D628). Both are memory a rollback puts back and neither is in the checksum; a step run again writes its entry
again, so the log holds what the last run of each step left. Two games' logs, from dumps of the data window at one
step, say whether the words were equal step by step (the gates' reports/generator-log.py)."""
import memmap

assert memmap.EXECUTABLE_GENERATOR == 0x801A0000 + 0x63C0
assert 0x16D600 + memmap.EFFECTS_GENERATOR_DATA == 0x1BA040 - 0x30000 + 0xBE8    # .data from .bss
assert memmap.GENERATOR_LOG_STEPS & (memmap.GENERATOR_LOG_STEPS - 1) == 0


def asm():
    return _TEXT.replace("GENLOG_ON", str(memmap.IMAGE_LOGS - memmap.CONTROL)).replace("GENLOG_MASK", str(memmap.GENERATOR_LOG_STEPS - 1)).replace(
        "GENLOG_HI", str(memmap.hi(memmap.GENERATOR_LOG))).replace("GENLOG_LO", str(memmap.lo(memmap.GENERATOR_LOG)))


_TEXT = """checksum:
    lwz 9, 116(31)
    cmpwi 9, 0
    beq cs_do
    lwz 9, 216(31)
    cmpwi 9, 0
    beqlr
cs_do:
    mflr 0
    stw 0, 12(1)
    bl cs_tables
TABLES
cs_tables:
    mflr 11
    lis 3, 0x811C
    ori 3, 3, 0x9DC5
    lis 4, 0x0100
    ori 4, 4, 0x0193
    addis 5, 30, 7
    lbz 6, -0x27B6(5)
    lwz 7, -0x27C8(5)
    cmpwi 6, 0
    beq cs_store
cs_racer:
    li 8, 0
    mr 10, 7
    li 0, 0x620
    bl cs_words
    mr 12, 11
    lwz 10, 0x49C(7)
    addi 11, 11, 52
    li 8, 0
    li 0, 0x1FC
    bl cs_words
    mr 11, 12
    addi 7, 7, 0x620
    addi 6, 6, -1
    cmpwi 6, 0
    bne cs_racer
cs_store:
    lwz 0, 12(1)
    mtlr 0
    lwz 9, 120(31)
    cmpwi 9, 0
    lwz 9, 0x50A4(30)
    beq cs_clock
    lwz 9, 124(31)
cs_clock:
    stw 9, 20(31)
    andi. 9, 9, 1023
    slwi 9, 9, 2
    lis 10, CHECKSUMS_HI
    ori 10, 10, CHECKSUMS_LO
    stwx 3, 10, 9
    blr
cs_words:
    srwi 9, 8, 7
    slwi 9, 9, 2
    lwzx 9, 11, 9
    srwi 5, 8, 2
    andi. 5, 5, 31
    slw 9, 9, 5
    andis. 9, 9, 0x8000
    bne cs_wskip
    lwzx 9, 10, 8
    xor 3, 3, 9
    mullw 3, 3, 4
cs_wskip:
    addi 8, 8, 4
    cmplw 8, 0
    blt cs_words
    blr
genlog:
    lwz 9, 120(31)
    cmpwi 9, 0
    beqlr
    lwz 9, GENLOG_ON(31)
    cmpwi 9, 0
    beqlr
    lwz 9, 124(31)
    andi. 9, 9, GENLOG_MASK
    slwi 9, 9, 3
    lis 10, GENLOG_HI
    ori 10, 10, GENLOG_LO
    add 10, 10, 9
    lis 9, -0x7FE6
    lwz 0, 0x63C0(9)
    stw 0, 0(10)
    addis 9, 30, -3
    lwz 0, 0xBE8(9)
    stw 0, 4(10)
    blr
"""
