"""When a race of ours is over: the one test that the race-over test (standin_race.py `over`) and the frame routine's
look at the machines (frame_replay.py `endtest`) both assemble, so the two cannot answer differently.

Without the host's rule (memmap.RACE_FINISH_LIMIT is 0: a match start without it, every recording made before it) the
race is over when every machine has finished (flag 0x10000) or is out (0x80), as it always was.

With the rule (the word is L steps, 5,400 for the 90 seconds Quick Play sets):
- the race ends for everyone L steps after the step in which the first machine finished, a computer racer's finish
  too: the machines still racing then are not waited for. A race nobody finishes has no such end;
- the results begin 480 steps after the last player seat's finish (memmap.RESULTS_WAIT, the game's own wait after a
  finish, during which that player's "FINISH!" and place are up; the stock game waits so for its last human, and
  ours had lost it: only seat 0 has a pad port), and at once when no player seat has finished. A finish after the
  first rule's end holds nothing up: the results begin at most L + 480 steps after the first finish.
"Over" is both: (every machine finished or out, or the first rule's end is reached) and the wait is done.

With the race block's flow word (memmap.RACE_FLOW, 1 in every race of a build from it on; 0 in every recording made
before it, which keeps the text above) the race waits for the player seats alone, as versus does:
"every machine finished or out" becomes every player seat finished or out by
the game's own verdict (outtest.py), and a computer racer still racing, at rest or stuck holds nothing up. Without
the host's rule the wait after the last player seat's finish holds as with it (the rule's end then never comes, and
every player seat's finish counts); with it, as above.

What it reads, at a step boundary, and nothing else:
- every machine's flags and, of a finished machine, its finish time (with the flow word, of an unfinished player
  seat what outtest.py reads: +0x58C, +0x17C, the rule word and the race type): the frames of its progress record's sum of
  laps (+0x1D8). Both are in the state checksum;
- the player seats (slot 'FZSI' +40, the match's; 0: every seat), the rule's word and the flow word, the same in
  every game;
- "now": the game's race clock, main.rel .bss 0x50A4. It is 0 through the course's intro and the countdown (the
  race's set-up and the intro clear it: fn_1_35BC4, fn_1_3908C, the rule block's reset handlers fn_1_30824,
  fn_1_30D84 and fn_1_3129C) and is counted up by one after every step from the race phase's first on (the frame
  function, 0x37BAC, in phase 0x29 and later; the frame routine's extra steps count it the same way: frame_entry.py
  `extra_on`); nothing else writes it: no machine going out, not the race-over test's "over", not the results phase
  (it stops only at 360,000, a hundred minutes, and the relay ends a race at thirty). It is in the memory a rollback
  puts back (port/facts/rollback-state.tsv, row simulation-memory), so a step run again finds the clock it had.
  It is not in the state checksum, and does not need to be: every racing machine's lap time counts a frame in a
  step only while the clock is not 0 (fn_1_8A1B4, 0x8A2C8), and those lap times are in the checksum, so a clock
  that started at another step in one game would make that game's checksums differ from the first race step on.
Nothing is latched: every answer is worked out from these words as they are, so a rollback has nothing to undo,
and two games whose machines agree answer alike at every boundary.

The two time bases, as measured (the playback of a 30-machine race of three laps with every step's machine state
traced, and a memory dump on either side of a finishing step):
- before step s of a session the clock is s - 364 (the intro and the countdown are steps 0 to 363);
- a machine that finishes in step f has +0x1D8.frames = f - 365, whether that line crossing was its first (a race of
  one lap) or a later one: the frames of the laps are whole steps, and the part of the finishing step before the
  line is in the fraction beside it (+0x1DC);
- so at the boundary after the finishing step the clock is that finish time + 2 (memmap.CLOCK_PAST_FINISH), and N
  steps later it is finish time + 2 + N. The first rule's end is the boundary where clock >= first finish + L + 2,
  the wait's end the one where clock >= last finish + 480 + 2: exactly L and 480 steps after the finishing steps.
  (The machines' own running time, +0x1E4, is not used: it is the laps' sum plus the lap under way plus a carry of
  their two fractions, which add to 1.0 or to 0.99999994 by rounding, so racing machines disagree on it by a frame.)

The clock word at the two places the frame routine asks: after the network hook of a frame's first step it is the
boundary's (the frame function counted the step before when that frame ended, and a rollback puts back the clock
of the boundary it goes to); before an extra step it is one behind, because the frame routine counts the step just
run only after it has asked (frame_entry.py `extra_on`). `endtest_extra` says so, and the text adds the one.

`clause(bss, prefix, yes, no, behind)` is the text: `bss` the register that holds main.rel's .bss, `prefix` what its
labels begin with, `yes` and `no` the labels it leaves by, `behind` (or None) the register that holds how many steps
the clock word is behind the boundary. It uses r0, r4 to r10, r12 and the count register."""
import memmap, outtest

SLOT = memmap.IMAGE + 0x10          # the seat routines' slot 'FZSI' (standin.py): +40 the player seats
PLAYERS = SLOT + 40
FINISHED_HI, OUT = 0x0001, 0x80     # a machine's flags: 0x10000 finished, 0x80 out
assert memmap.hi(memmap.RACE) == memmap.hi(memmap.RACE + memmap.RACE_FLOW) and memmap.lo(memmap.RACE) < 0x7000
assert memmap.hi(PLAYERS) == memmap.hi(memmap.IMAGE) and memmap.lo(PLAYERS) < 0x8000


def clause(bss, prefix, yes, no, behind=None):
    p = prefix
    counted = f"    add 8, 8, {behind}\n" if behind is not None else ""
    return f"""    lis 9, {memmap.hi(memmap.RACE)}
    lwz 6, {memmap.lo(memmap.RACE) + memmap.RACE_FINISH_LIMIT}(9)
    lwz 12, {memmap.lo(memmap.RACE) + memmap.RACE_FLOW}(9)
    addis 10, {bss}, 7
    lbz 9, -0x27B6(10)
    lwz 10, -0x27C8(10)
    cmpwi 9, 0
    beq {no}
    mtctr 9
    li 4, 1
    li 5, -1
    cmpwi 12, 0
    bne {p}flow
{p}each:
    lwz 0, 0(10)
    andis. 7, 0, {FINISHED_HI}
    beq {p}unfinished
    lwz 7, 0x49C(10)
    cmplwi 7, 0
    beq {p}next
    lwz 7, 0x1D8(7)
    cmplw 7, 5
    bge {p}next
    mr 5, 7
    b {p}next
{p}unfinished:
    andi. 0, 0, {OUT}
    bne {p}next
    li 4, 0
{p}next:
    addi 10, 10, 0x620
    bdnz {p}each
    cmpwi 6, 0
    bne {p}rule
    cmpwi 4, 0
    beq {no}
    b {yes}
{p}flow:
    lis 7, {memmap.hi(PLAYERS)}
    lwz 8, {memmap.lo(PLAYERS)}(7)
    cmpwi 8, 0
    bne {p}f_seats
    li 8, -1
{p}f_seats:
    li 12, 0
{p}f_each:
    lwz 0, 0(10)
    andis. 7, 0, {FINISHED_HI}
    beq {p}f_unfinished
    lwz 7, 0x49C(10)
    cmplwi 7, 0
    beq {p}f_next
    lwz 7, 0x1D8(7)
    cmplw 7, 5
    bge {p}f_next
    mr 5, 7
    b {p}f_next
{p}f_unfinished:
    srw 7, 8, 12
    andi. 7, 7, 1
    beq {p}f_next
{outtest.clause(10, bss, 0, 7, p + "f_next", p + "f_racing")}{p}f_racing:
    li 4, 0
{p}f_next:
    addi 12, 12, 1
    addi 10, 10, 0x620
    bdnz {p}f_each
    cmpwi 6, 0
    bne {p}rule
    lis 6, 0x7FFF
    ori 6, 6, 0xFFFF
{p}rule:
    lwz 8, 0x50A4({bss})
{counted}    cmpwi 5, -1
    bne {p}timer
    cmpwi 4, 0
    beq {no}
    b {yes}
{p}timer:
    add 6, 5, 6
    cmpwi 4, 0
    bne {p}wait
    addi 7, 6, {memmap.CLOCK_PAST_FINISH}
    cmplw 8, 7
    blt {no}
{p}wait:
    lis 7, {memmap.hi(PLAYERS)}
    lwz 7, {memmap.lo(PLAYERS)}(7)
    cmpwi 7, 0
    bne {p}seats
    li 7, -1
{p}seats:
    addis 10, {bss}, 7
    lbz 9, -0x27B6(10)
    lwz 10, -0x27C8(10)
    mtctr 9
    li 9, 0
    li 12, -1
{p}last:
    lwz 0, 0(10)
    andis. 0, 0, {FINISHED_HI}
    beq {p}last_next
    srw 0, 7, 9
    andi. 0, 0, 1
    beq {p}last_next
    lwz 4, 0x49C(10)
    cmplwi 4, 0
    beq {p}last_next
    lwz 4, 0x1D8(4)
    cmplw 4, 6
    bgt {p}last_next
    cmpwi 12, -1
    beq {p}last_take
    cmplw 4, 12
    ble {p}last_next
{p}last_take:
    mr 12, 4
{p}last_next:
    addi 9, 9, 1
    addi 10, 10, 0x620
    bdnz {p}last
    cmpwi 12, -1
    beq {yes}
    addi 12, 12, {memmap.RESULTS_WAIT + memmap.CLOCK_PAST_FINISH}
    cmplw 8, 12
    blt {no}
    b {yes}
"""
