"""Whether a machine is out of the race: the game's own verdict, one text that every routine of the image that asks
assembles (as raceend.py is one text for the race-over test and the frame routine's look at it), so no two answer
differently.

The game asks it once a step of every unfinished machine (fn_1_32A40, 0x32F20, calls fn_1_32600) and puts a machine
it says out of the race for good (fn_1_32200). fn_1_32600, read instruction by instruction:
- a finished machine (flag 0x10000) is never asked: it races on;
- with the Restore rule (.bss 0x3C30, the rule word, bit 0x8000) none of the tests below is made: every wreck, fall
  and breakdown is put back about 160 steps later, and the machine is never out for them;
- else out with flag 0x08000000 (retired), 0x800 (out of the course: destroyed, out of bounds or off the course), +0x58C bit 0x10 (out of contact: a fall), or flag 0x80
  (broken down: energy out) with flag 0x200, or race type 5 (.bss 0x3C35), or the speed +0x17C below 1.0 (.rodata
  0xBE4; fcmpo and bge: a speed that is not a number is not below). A breakdown alone coasts on with its controls
  zeroed and may still cross the line: it is out only at rest;
- in either regime the game also gives flag 0x10000000 and says out for a timed mode's or the time limit's end
  (rules 0x40000, 0x80000). A race of ours never has either rule (0x04000000 is set: no time limit), so no machine
  of ours gets the flag; a machine that has it is out here, in either regime, as the game keeps it out.
- Versus alone (race type 0) has its last-human timer (.bss 0x25B98); a race of ours is never type 0.
- With rule bit 0x200000 the game skips all of this, the finish's handling with it: fn_1_32A40 leaves at 0x32CD0
  (0x32DAC in the file's second column; `lwz r0, 0x30(r30)`, bit 10, to its end) before asking any machine. Our
  races never set it: the race start (fn_1_36330) makes the rule word from the race type's bits and the set-up
  word .bss 0x3C08, which only fn_1_3FCB0 (sets) and fn_1_3FCD4 (clears) write, and direct.py gives those
  0x04000000, 0x20000 cleared, 0x8000 by the race's Restore;
  fn_1_37E98 clears the set-up word with fn_1_3FCB0(0). No write of 0x200000 into the rule word was found in
  main.rel, sel, replay or winning (a search of their disassembly for it, not a proof). The text asks no such bit.

`clause(m, bss, t0, t1, out, racing)` is the text: `m` the register that holds the machine's block, `bss`
the one that holds main.rel's .bss (neither r0), `t0` and `t1` two registers it may change (r0 may be one), and it
leaves by a branch to `out` or to `racing`, never by falling through. It reads the machine's flags, +0x58C and
+0x17C, the rule word and the race type; it writes nothing. The speed is compared as the float's bits: below 1.0
is a word below 0x3F800000 as a signed number, but for a negative NaN (a sign bit and more than 0x7F800000 under
it), which is not below. `out(...)` is the same verdict in Python, for the tests; port/core/src/out_test.h is it
in C++, for what the core shows (the scrubber, the replay's racer watched, the live feed). Both and this text are
held to one list of cases, tests/out-cases.tsv."""
import struct
import memmap

# a machine's flags and second flags (+0x58C), the facts table's (memmap: its machine group)
FINISHED, TIME_OVER = memmap.MACHINE_FLAG_FINISHED, memmap.MACHINE_FLAG_TIME_OVER
RETIRED, OUT = memmap.MACHINE_FLAG_RETIRED, memmap.MACHINE_FLAG_OUT
BROKEN_DOWN, CPU = memmap.MACHINE_FLAG_BROKEN_DOWN, memmap.MACHINE_FLAG_CPU
FALLEN = memmap.MACHINE_FLAGS2_FALLEN  # +0x58C
RESTORE = 0x8000                      # the rule word's bit
ONE = 0x3F800000                      # 1.0, .rodata 0xBE4


def clause(m, bss, t0, t1, out, racing):
    assert 0 not in (m, bss) and len({m, bss, t0, t1}) == 4
    return f"""    lwz {t0}, 0({m})
    andis. {t1}, {t0}, {FINISHED >> 16}
    bne {racing}
    andis. {t1}, {t0}, {TIME_OVER >> 16}
    bne {out}
    lwz {t1}, 0x3C30({bss})
    andi. {t1}, {t1}, {RESTORE}
    bne {racing}
    andis. {t1}, {t0}, {RETIRED >> 16}
    bne {out}
    andi. {t1}, {t0}, {OUT}
    bne {out}
    lwz {t1}, 0x{memmap.MACHINE_FLAGS2:X}({m})
    andi. {t1}, {t1}, {FALLEN}
    bne {out}
    andi. {t1}, {t0}, {BROKEN_DOWN}
    beq {racing}
    andi. {t1}, {t0}, {CPU}
    bne {out}
    lbz {t1}, 0x3C35({bss})
    cmplwi {t1}, 5
    beq {out}
    lwz {t1}, 0x{memmap.MACHINE_SPEED:X}({m})
    lis {t0}, {ONE >> 16}
    cmpw {t1}, {t0}
    bge {racing}
    cmpwi {t1}, 0
    bge {out}
    rlwinm {t1}, {t1}, 0, 1, 31
    lis {t0}, 0x7F80
    cmplw {t1}, {t0}
    bgt {racing}
    b {out}
"""


def out(flags, fallen=0, speed=0.0, rules=0, race_type=6):
    """The game's verdict (fn_1_32600 as called by fn_1_32A40) on a machine; `speed` a float or a word of its bits."""
    if flags & FINISHED:
        return False
    if flags & TIME_OVER:
        return True
    if rules & RESTORE:
        return False
    if flags & (RETIRED | OUT) or fallen & FALLEN:
        return True
    if not flags & BROKEN_DOWN:
        return False
    if flags & CPU or race_type == 5:
        return True
    value = struct.unpack(">f", struct.pack(">I", speed))[0] if isinstance(speed, int) else speed
    return value < 1.0
