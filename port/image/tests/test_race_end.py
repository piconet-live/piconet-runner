"""The race's end, held until it is official: the image's two routines that decide it,
run here as the bytes the image holds. `over` (standin_race.py) is the game's race-over test in a race of ours;
`endtest` (frame_replay.py) is what the frame routine asks before and after every step. Both are leaves of a few
dozen instructions, so a reader of just those instructions runs them against a made-up game: two machines, the
race's flags, the control block. No game data. The frame routine's cut itself is checked as words in place.

The host's rule that ends a race so long after the first finish (raceend.py) is the same text in both routines.
Its two time bases, the race clock and a machine's finish time, are tied here to what was measured in the game
(MEASURED, below: step numbers and counters read from runs, no game data), so that a change of either constant, or
of the text, fails against the game's own numbers and not against the text's own arithmetic."""
import os, struct, sys, tempfile, unittest

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, ".."))
import build, memmap

IMAGE, CONTROL = memmap.IMAGE, memmap.CONTROL
MODULE = memmap.REL
BSS = MODULE + 0x1C0000 - 0x5FC0                 # as the routines work it out from the module's address
assert BSS == memmap.REL_BSS
MACHINES = 0x80900000
OFFICIAL, ASKED, OVER = memmap.END_OFFICIAL, memmap.END_ASKED, memmap.END_OVER
RUNNING, STEP, MUTE, PHASE = CONTROL + 120, CONTROL + 124, CONTROL + 488, BSS + 0x3C2A
FLAGS, GUARD, COUNT, POINTER = BSS + 0x3C30, BSS + 0x25BA0, BSS + 0x6D84A, BSS + 0x6D838
FINISHED, OUT = 0x10000, 0x80
CLOCK, WAITED_FOR = BSS + 0x50A4, BSS + 0x25B8C     # the race clock; the countdown the game's own results wait reads
LIMIT, PLAYERS = memmap.RACE + memmap.RACE_FINISH_LIMIT, IMAGE + 0x10 + 40
RECORDS = 0x80A00000                                # a progress record for each machine: +0x1D8 its finish time
RULE = 5400                                         # the rule as Quick Play sets it: 90 seconds, in steps
WAIT = 480

# What the rule's constants were measured from (the runs' names are the work
# folder's). A playback, as the race, of a 30-machine race of three laps and of one of a single lap, with the
# machines' state traced after every step; and a memory dump on either side of one finishing step.
MEASURED = {
    # (the step a machine finished in, its progress record's +0x1D8 frames after that step)
    "finishes of three laps (run raceend-r1-1)": [(4486, 4121), (4510, 4145), (4553, 4188), (4943, 4578), (6933, 6568)],
    "finishes of one lap, the first line crossing (run raceend-r1l-1)": [(4391, 4026), (4408, 4043), (4816, 4451)],
    # (the step about to run, the race clock at that boundary): before and after step 4486 of run raceend-r1d-1
    "the clock at two boundaries (run raceend-r1d-1)": [(4486, 4122), (4487, 4123)],
}
FINISH_OFFSET = {f - time for name, rows in MEASURED.items() if name.startswith("finishes") for f, time in rows}
CLOCK_OFFSET = {step - clock for step, clock in MEASURED["the clock at two boundaries (run raceend-r1d-1)"]}
assert FINISH_OFFSET == {365} and CLOCK_OFFSET == {364}


def finish_time(step):
    """A machine's +0x1D8 frames once it has finished in `step`, as measured: on its first line crossing or a later."""
    return step - 365


def clock_before(step):
    """The race clock at the boundary before `step`, as measured."""
    return step - 364


def signed(value, bits=16):
    return value - (1 << bits) if value & (1 << (bits - 1)) else value


class Machine:
    """Runs a leaf routine of the image: the instructions `over`, `endtest` and the finish's routines
    (test_finish.py, test_finish_frames.py) are made of, and no other."""

    def __init__(self, image, memory):
        self.image, self.mem = image, dict(memory)          # memory: address -> byte
        self.r = [0] * 32
        self.cr0 = (False, False, False)                    # lt, gt, eq
        self.ctr = 0
        self.written = set()

    def read(self, address, size):
        if IMAGE <= address < IMAGE + len(self.image):
            return int.from_bytes(self.image[address - IMAGE:address - IMAGE + size], "big")
        return int.from_bytes(bytes(self.mem.get(address + i, 0) for i in range(size)), "big")

    def write(self, address, size, value):
        assert not IMAGE <= address < IMAGE + len(self.image)
        for i, b in enumerate(value.to_bytes(size, "big")):
            self.mem[address + i] = b
        self.written.add(address)

    def compare(self, a, b):
        self.cr0 = (a < b, a > b, a == b)

    def taken(self, bo, bi):
        if not bo & 4:                                      # counts: bdnz
            self.ctr = (self.ctr - 1) & 0xFFFFFFFF
            if (self.ctr != 0) == bool(bo & 2):
                return False
        if bo & 16:
            return True
        assert bi < 4, "a condition outside CR field 0"
        bit = self.cr0[bi] if bi < 3 else False
        return bit == bool(bo & 8)

    MORE = {266: lambda x, y: x + y, 40: lambda x, y: y - x, 104: lambda x, y: -x,    # add, subf, neg
            444: lambda x, y: x | y, 824: lambda x, y: signed(x, 32) >> y,            # or, srawi
            536: lambda x, y: 0 if y & 32 else x >> (y & 31)}                         # srw

    def more(self, w, d, a, b):
        """The register-to-register instructions the finish's routines add: `or` and `srawi` write rA from rS."""
        r, xo = self.r, (w >> 1) & 0x3FF
        if xo == 444: r[a] = r[d] | r[b]
        elif xo == 824: r[a] = self.MORE[xo](r[d], b) & 0xFFFFFFFF
        elif xo == 536: r[a] = self.MORE[xo](r[d], r[b]) & 0xFFFFFFFF
        else: r[d] = self.MORE[xo](r[a], r[b]) & 0xFFFFFFFF
        if w & 1: self.compare(signed(r[a] if xo in (444, 824, 536) else r[d], 32), 0)

    def rotate_and_mask(self, w, s, a):
        """rlwinm rA, rS, SH, MB, ME (the HUD pass takes single bits of the flags with it); a mask may wrap."""
        sh, mb, me = (w >> 11) & 31, (w >> 6) & 31, (w >> 1) & 31
        rotated = ((self.r[s] << sh) | (self.r[s] >> (32 - sh))) & 0xFFFFFFFF
        inside = (lambda i: mb <= i <= me) if mb <= me else (lambda i: i >= mb or i <= me)
        self.r[a] = rotated & sum(1 << (31 - i) for i in range(32) if inside(i))
        if w & 1: self.compare(signed(self.r[a], 32), 0)

    def other(self, pc, w):
        """An instruction `run` does not read: none here. A reader of more (test_raceview.py) answers the address to
        go on at, or None when the routine has returned."""
        raise AssertionError(f"{pc:08X}: {w:08X} is not an instruction these routines are made of")

    def run(self, entry, limit=2000):
        pc, r = entry, self.r
        for _ in range(limit):
            w = self.read(pc, 4)
            op, d, a, imm = w >> 26, (w >> 21) & 31, (w >> 16) & 31, w & 0xFFFF
            b, nxt = (w >> 11) & 31, pc + 4
            base = r[a] if a else 0
            if op == 15:   r[d] = (base + (signed(imm) << 16)) & 0xFFFFFFFF          # addis, lis
            elif op == 14: r[d] = (base + signed(imm)) & 0xFFFFFFFF                  # addi, li
            elif op == 32: r[d] = self.read((base + signed(imm)) & 0xFFFFFFFF, 4)    # lwz
            elif op == 34: r[d] = self.read((base + signed(imm)) & 0xFFFFFFFF, 1)    # lbz
            elif op == 42: r[d] = signed(self.read((base + signed(imm)) & 0xFFFFFFFF, 2)) & 0xFFFFFFFF   # lha
            elif op == 40: r[d] = self.read((base + signed(imm)) & 0xFFFFFFFF, 2)    # lhz
            elif op == 36: self.write((base + signed(imm)) & 0xFFFFFFFF, 4, r[d])    # stw
            elif op == 38: self.write((base + signed(imm)) & 0xFFFFFFFF, 1, r[d] & 0xFF)   # stb
            elif op == 44: self.write((base + signed(imm)) & 0xFFFFFFFF, 2, r[d] & 0xFFFF)   # sth
            elif op == 7:  r[d] = (r[a] * signed(imm)) & 0xFFFFFFFF                  # mulli
            elif op == 10: self.compare(r[a], imm)                                   # cmplwi
            elif op == 26: r[a] = r[d] ^ imm                                         # xori
            elif op == 27: r[a] = r[d] ^ imm << 16                                   # xoris
            elif op == 18 and not w & 1: nxt = pc + signed(w & 0x03FFFFFC, 26)       # b
            elif op == 31 and (w >> 1) & 0x3FF in self.MORE: self.more(w, d, a, b)
            elif op == 21: self.rotate_and_mask(w, d, a)                             # rlwinm
            elif op == 24: r[a] = r[d] | imm                                         # ori
            elif op == 25: r[a] = r[d] | imm << 16                                   # oris
            elif op == 28: r[a] = r[d] & imm; self.compare(signed(r[a], 32), 0)      # andi.
            elif op == 29: r[a] = r[d] & imm << 16; self.compare(signed(r[a], 32), 0)   # andis.
            elif op == 11: self.compare(signed(r[a], 32), signed(imm))               # cmpwi
            elif op == 31 and (w >> 1) & 0x3FF == 0: self.compare(signed(r[a], 32), signed(r[b], 32))   # cmpw
            elif op == 31 and (w >> 1) & 0x3FF == 32: self.compare(r[a], r[b])       # cmplw
            elif op == 31 and (w >> 1) & 0x3FF == 28:                                # and, and.
                r[a] = r[d] & r[b]
                if w & 1: self.compare(signed(r[a], 32), 0)
            elif op == 31 and (w >> 1) & 0x3FF == 87: r[d] = self.read((base + r[b]) & 0xFFFFFFFF, 1)    # lbzx
            elif op == 31 and (w >> 1) & 0x3FF == 467 and (b << 5 | a) == 9: self.ctr = r[d]   # mtctr
            elif op == 16:                                                           # bc
                if self.taken(d, a): nxt = pc + signed(w & 0xFFFC)
            elif op == 19 and (w >> 1) & 0x3FF == 16:                                # bclr: the routine returns
                if self.taken(d, a): return r[3]
            else:
                nxt = self.other(pc, w)
                if nxt is None: return r[3]
            pc = nxt
        raise AssertionError("the routine did not return")


def game(running=1, step=101, official=101, flags=(FINISHED, OUT), phase=0x29, rule=0, guard=0, mute=1,
         limit=0, clock=None, times=None, waited_for=BSS + 0x50EC):
    """Memory of a race: the control block's words, the race's flags, and one machine block for each of `flags`,
    each with a progress record whose finish time is `times`' (0 without). `limit` is the host's rule in the race
    block (0: none), `clock` the race clock (as measured for `step`, unless given). The player seats are in the
    image's own slot: `seated`."""
    words = {0x800030C8: MODULE, RUNNING: running, STEP: step, OFFICIAL: official, MUTE: mute,
             FLAGS: rule, GUARD: guard, POINTER: MACHINES, LIMIT: limit, WAITED_FOR: waited_for,
             CLOCK: (clock_before(step) if clock is None else clock) & 0xFFFFFFFF}
    words.update({MACHINES + i * 0x620: f for i, f in enumerate(flags)})
    words.update({MACHINES + i * 0x620 + 0x49C: RECORDS + i * 0x200 for i in range(len(flags))})
    words.update({RECORDS + i * 0x200 + 0x1D8: time for i, time in enumerate(times or ())})
    memory = {}
    for address, value in words.items():
        for i, b in enumerate(value.to_bytes(4, "big")):
            memory[address + i] = b
    memory[COUNT] = len(flags)
    memory[PHASE], memory[PHASE + 1] = phase >> 8, phase & 0xFF
    return memory


class TheRacesEnd(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.dir = tempfile.TemporaryDirectory()
        b = build.build(cls.dir.name)
        cls.image, cls.symbols = b["image"], b["symbols"]
        # endtest: li r3, 0; lwz r9, 120(r31); cmpwi r9, 0; beqlr; lha r9, 0x3C2A(r30)
        mark = bytes.fromhex("38600000" "813F0078" "2C090000" "4D820020" "A93E3C2A")
        assert cls.image.count(mark) == 1
        # before them its two entries: endtest_extra (li r11, 1; b) and endtest (li r11, 0)
        at = cls.image.index(mark)
        assert cls.image[at - 12:at] == bytes.fromhex("39600001" "48000008" "39600000")
        cls.endtest_extra, cls.endtest = IMAGE + at - 12, IMAGE + at - 4

    @classmethod
    def tearDownClass(cls):
        cls.dir.cleanup()

    def seated(self, players):
        """The image with the match's player seats in its slot (bit i: seat i)."""
        image = bytearray(self.image)
        struct.pack_into(">I", image, PLAYERS - IMAGE, players)
        return bytes(image)

    def over(self, players=0x3FFFFFFF, **memory):
        m = Machine(self.seated(players), game(**memory))
        return m.run(self.symbols["race_over"]), m

    def looks_over(self, extra=False, players=0x3FFFFFFF, **memory):
        """`endtest`; with `extra` the question before an extra step, where the clock is a step behind."""
        m = Machine(self.seated(players), game(**memory))
        m.r[31], m.r[30] = CONTROL, BSS
        result = m.run(self.endtest_extra if extra else self.endtest)
        self.assertEqual(m.written, set())                  # it only looks
        return result

    def test_outside_a_session_the_race_ends_as_it_always_did(self):
        result, m = self.over(running=0, official=0)
        self.assertEqual(result, 1)
        self.assertEqual(m.written, set())
        result, m = self.over(running=0, official=0, flags=(FINISHED, 0))
        self.assertEqual((result, m.written), (0, set()))

    def test_a_host_that_never_says_how_far_the_official_steps_go(self):
        # the session of before this image: its word stays 0, and the race must still end
        result, m = self.over(official=0)
        self.assertEqual((result, m.written), (1, set()))

    def test_over_is_said_only_on_official_steps(self):
        # every machine finished or out, and the step before the one about to run was a prediction: not over, asked
        result, m = self.over(step=101, official=100)
        self.assertEqual(result, 0)
        self.assertEqual((m.read(ASKED, 4), m.read(OVER, 4)), (1, 0))
        self.assertEqual(m.written, {ASKED})
        # every step below the one about to run is official: over, the host is told, and what the end starts is
        # heard although the frame before ran no new step
        for official in (101, 250):
            result, m = self.over(step=101, official=official)
            self.assertEqual(result, 1)
            self.assertEqual((m.read(ASKED, 4), m.read(OVER, 4), m.read(MUTE, 4)), (0, 1, 0))
            self.assertEqual(m.written, {OVER, MUTE})

    def test_a_race_still_on_asks_nothing(self):
        for flags in ((FINISHED, 0), (0, OUT), (0x08000000, FINISHED)):
            result, m = self.over(step=101, official=90, flags=flags)
            self.assertEqual((result, m.written), (0, set()), flags)
        # nor one the game's own guards hold open, whatever the machines say
        for held in ({"rule": 0x800}, {"guard": 1}):
            result, m = self.over(step=101, official=90, **held)
            self.assertEqual((result, m.written), (0, set()), held)

    def test_the_frame_routine_sees_the_end_as_the_race_over_test_does(self):
        cases = [{}, {"flags": (FINISHED, 0)}, {"flags": (OUT, OUT)}, {"flags": (FINISHED,) * 30},
                 {"flags": (FINISHED,) * 29 + (0,)}, {"rule": 0x800}, {"guard": 1}, {"flags": ()}]
        for case in cases:
            # (with every step official the race-over test's answer is the plain one)
            self.assertEqual(self.looks_over(**case), self.over(step=101, official=101, **case)[0], case)
        self.assertEqual(self.looks_over(), 1)
        # only a running session's race: the countdown and the results never cut a frame
        self.assertEqual(self.looks_over(running=0), 0)
        self.assertEqual(self.looks_over(phase=0x28), 0)
        self.assertEqual(self.looks_over(phase=0x2A), 0)

    # --- the host's rule: the race ends for everyone so long after the first finish (raceend.py)

    def both(self, step, **memory):
        """The race-over test and the frame routine's two questions before `step`, every step below it official.
        They must agree; the question before an extra step is asked with the clock a step behind, as it is there."""
        said, m = self.over(step=step, official=step, **memory)
        self.assertEqual(self.looks_over(step=step, **memory), said, (step, memory))
        behind = dict(memory, clock=memory.get("clock", clock_before(step)) - 1)
        self.assertEqual(self.looks_over(extra=True, step=step, **behind), said, (step, memory))
        return said, m

    def first_over(self, around, **memory):
        """The one step, among those near `around`, before which the race is first over, and stays over."""
        said = [self.both(step, **memory)[0] for step in range(around - 3, around + 4)]
        self.assertEqual(sorted(said), said, said)                       # not over, then over
        self.assertIn(1, said)
        return around - 3 + said.index(1)

    def test_the_constants_are_the_measured_ones(self):
        # the clock at the boundary after a finishing step, less that machine's finish time
        after = {clock_before(f + 1) - finish_time(f) for f in (400, 4486, 100000)}
        self.assertEqual(after, {memmap.CLOCK_PAST_FINISH})
        self.assertEqual(memmap.RESULTS_WAIT, WAIT)
        # and the measurements themselves say the same, with no arithmetic of this file's between
        clocks = dict(MEASURED["the clock at two boundaries (run raceend-r1d-1)"])
        self.assertEqual(clocks[4487] - dict(MEASURED["finishes of three laps (run raceend-r1-1)"])[4486],
                         memmap.CLOCK_PAST_FINISH)

    def test_without_the_rule_the_clock_and_the_finish_times_decide_nothing(self):
        for clock in (0, 5000, 300000):
            for times in ((0, 0), (10, 4000), (4000, 10)):
                self.assertEqual(self.both(9000, flags=(FINISHED, 0), clock=clock, times=times)[0], 0)
                said, m = self.both(9000, flags=(FINISHED, FINISHED), clock=clock, times=times)
                self.assertEqual((said, m.written), (1, {OVER, MUTE}))   # the game's own wait is left as it was
                self.assertEqual(m.read(WAITED_FOR, 4), BSS + 0x50EC)

    def test_the_race_ends_so_many_steps_after_the_first_finishing_step(self):
        # a machine finishes in step f and another races on: over before step f + 1 + L, the boundary L steps after
        # the one that first showed the finish, and not a step sooner. As measured on a finish of three laps and on
        # one of a single lap (the first line crossing): the same.
        for name, rows in MEASURED.items():
            for f, time in rows if name.startswith("finishes") else ():
                for limit in (RULE, 600, 1):
                    # (a rule shorter than the results' wait: the wait after that one finish is what holds)
                    end = self.first_over(f + 1 + max(limit, WAIT), flags=(FINISHED, 0), times=(time, 0), limit=limit)
                    self.assertEqual(end, f + 1 + max(limit, WAIT), (name, f, limit))
        # the first finish is the earliest, wherever it sits, and a computer racer's counts
        for times, flags, players in (((4121, 4578, 0), (FINISHED, FINISHED, 0), 0b111),
                                      ((4578, 4121, 0), (FINISHED, FINISHED, 0), 0b111),
                                      ((4578, 0, 4121), (FINISHED, 0, FINISHED), 0b011)):
            end = self.first_over(4486 + 1 + RULE, flags=flags, times=times, limit=RULE, players=players)
            self.assertEqual(end, 4486 + 1 + RULE)

    def test_the_results_wait_480_steps_after_the_last_players_finish(self):
        # every machine has finished, the last in step g: over before step g + 1 + 480, where that player's own
        # countdown (frame_hud.py: 480 at the boundary after the finish step) reaches 0
        first, last = 4486, 6933
        times = (finish_time(first), finish_time(last))
        end = self.first_over(last + 1 + WAIT, flags=(FINISHED, FINISHED), times=times, limit=RULE)
        self.assertEqual(end, last + 1 + WAIT)
        # out machines are not waited for, and a machine out last ends the race where it goes out
        self.assertEqual(self.both(last + 1 + WAIT - 1, flags=(FINISHED, FINISHED, OUT), times=times + (0,), limit=RULE)[0], 0)
        self.assertEqual(self.both(8000, flags=(FINISHED, FINISHED, 0), times=times + (0,), limit=RULE)[0], 0)
        self.assertEqual(self.both(8000, flags=(FINISHED, FINISHED, OUT), times=times + (0,), limit=RULE)[0], 1)
        # a computer racer that finishes last holds nothing: the wait is the last player seat's
        end = self.first_over(first + 1 + WAIT, flags=(FINISHED, FINISHED), times=times, limit=RULE, players=0b01)
        self.assertEqual(self.both(last + 1, flags=(FINISHED, FINISHED), times=times, limit=RULE, players=0b01)[0], 1)
        self.assertEqual(self.both(last, flags=(FINISHED, 0), times=times, limit=RULE, players=0b01)[0], 0)
        self.assertEqual(end, first + 1 + WAIT)
        # no player seats named (a race of the tests' own): every seat is one
        end = self.first_over(last + 1 + WAIT, flags=(FINISHED, FINISHED), times=times, limit=RULE, players=0)
        self.assertEqual(end, last + 1 + WAIT)

    def test_a_finish_late_in_the_rules_time_is_waited_for_and_one_after_it_is_not(self):
        first = 4486
        end = first + 1 + RULE                                           # the step the rule's time is over before
        for late, results in ((end - 1, end + WAIT), (end - 100, end - 100 + WAIT + 0), (end - WAIT - 1, end)):
            # the second machine finishes in step `late`, the last step of the rule's time or before; a third races
            times = (finish_time(first), finish_time(late), 0)
            got = self.first_over(max(results, end), flags=(FINISHED, FINISHED, 0), times=times, limit=RULE)
            self.assertEqual(got, max(late + 1 + WAIT, end), late)
        # the longest it can be: first finish + L + 480 steps
        self.assertEqual(self.both(end + WAIT, flags=(FINISHED, FINISHED, 0), limit=RULE,
                                   times=(finish_time(first), finish_time(end - 1), 0))[0], 1)
        # a finish after the rule's time, while the results still wait, holds nothing up
        times = (finish_time(first), finish_time(end - 1), finish_time(end + 200))
        got = self.first_over(end + WAIT, flags=(FINISHED, FINISHED, FINISHED), times=times, limit=RULE)
        self.assertEqual(got, end + WAIT)
        self.assertEqual(self.both(end + 300, flags=(FINISHED, 0, FINISHED), limit=RULE,
                                   times=(finish_time(first), 0, finish_time(end + 200)))[0], 1)

    def test_a_race_nobody_finishes_has_no_end_by_the_rule(self):
        for step in (4000, 9000, 150000):
            self.assertEqual(self.both(step, flags=(0, OUT), times=(0, 0), limit=RULE)[0], 0)
            self.assertEqual(self.both(step, flags=(0, 0), times=(4000, 20), limit=RULE)[0], 0)   # lap sums, no finish
            self.assertEqual(self.both(step, flags=(OUT, OUT), times=(0, 0), limit=RULE)[0], 1)

    def test_the_rule_is_held_until_it_is_official_as_the_plain_end_is(self):
        f = 4486
        race = dict(flags=(FINISHED, 0), times=(finish_time(f), 0), limit=RULE)
        end = f + 1 + RULE
        # the step before the end's was a prediction: asked, not over, and the game's wait is not touched
        said, m = self.over(step=end, official=end - 1, **race)
        self.assertEqual((said, m.written), (0, {ASKED}))
        # official: over, the host is told, and the game's own wait before the results is taken away: its pointer to
        # seat 0's countdown is cleared, so the results begin here in every game and not when seat 0's has run out
        said, m = self.over(step=end, official=end, **race)
        self.assertEqual((said, m.written), (1, {OVER, MUTE, WAITED_FOR}))
        self.assertEqual((m.read(OVER, 4), m.read(WAITED_FOR, 4)), (1, 0))
        # the game's own guards hold it as they hold the plain end
        for held in ({"rule": 0x800}, {"guard": 1}):
            self.assertEqual(self.over(step=end, official=end, **race, **held)[1].written, set())
        # outside a running session the rule's test still answers, and writes nothing
        said, m = self.over(step=end, official=0, running=0, **race)
        self.assertEqual((said, m.written), (1, set()))

    def test_the_frame_routine_asks_about_the_boundary_it_stands_at(self):
        # before an extra step the clock has not counted the step just run: asked as after a first step's hook, the
        # frame routine would find the rule's end a step late there, and the frame would not be cut where the
        # race-over test, a frame later, finds the race over
        f = 4486
        end = f + 1 + RULE
        race = dict(flags=(FINISHED, 0), times=(finish_time(f), 0), limit=RULE)
        behind = clock_before(end) - 1
        self.assertEqual(self.looks_over(step=end, clock=behind, **race), 0)
        self.assertEqual(self.looks_over(extra=True, step=end, clock=behind, **race), 1)
        self.assertEqual(self.looks_over(extra=True, step=end - 1, clock=behind - 1, **race), 0)

    def test_only_the_race_over_test_names_the_three_words(self):
        # a load, a store or an addi whose displacement is one of the three words' offsets in the control block: the
        # race-over test's read of the first and its two writes, and nothing else in the image (no step's routine)
        names = {OFFICIAL - CONTROL: "official", ASKED - CONTROL: "asked", OVER - CONTROL: "over"}
        named = [(o, w >> 26, names[w & 0xFFFF]) for o in range(0, len(self.image), 4)
                 for w in [struct.unpack_from(">I", self.image, o)[0]]
                 if (w >> 26 == 14 or 32 <= w >> 26 <= 47) and w & 0xFFFF in names]
        test = self.symbols["race_over"] - IMAGE
        inside = [n for n in named if test <= n[0] < test + 0x400]
        self.assertEqual(sorted(n[1:] for n in inside), [(32, "official"), (36, "asked"), (36, "over")])
        # and four reads of "over" outside it, all the picture's: the local racer's finish watch keeps the row of
        # a machine still racing or out when the race ends (frame_hud.py `finwatch`, two), and the results list's
        # two state helpers give such a machine the game's word for it (follow.py; test_finish.py says what each does)
        self.assertEqual([n[1:] for n in named if n not in inside], [(32, "over")] * 4)

    def test_the_frame_is_cut_at_the_step_that_makes_the_race_over(self):
        image = self.image
        words = [struct.unpack_from(">I", image, o)[0] for o in range(0, len(image), 4)]
        calls = {to: [o * 4 for o, w in enumerate(words)
                      if w >> 26 == 18 and w & 3 == 1 and IMAGE + o * 4 + signed(w & 0x03FFFFFC, 26) == to]
                 for to in (self.endtest, self.endtest_extra)}
        # after the first step's hook; and before every extra step, by the entry that knows the clock is a step behind
        self.assertEqual([len(calls[self.endtest]), len(calls[self.endtest_extra])], [1, 1])
        first, extra = calls[self.endtest][0], calls[self.endtest_extra][0]
        self.assertLess(first, extra)
        # between that question and the step, the frame routine counts the step just run: lwz, addi 1, stw of the clock
        counted = [o for o in range(extra // 4, extra // 4 + 16) if words[o:o + 3] == [0x813E50A4, 0x39290001, 0x913E50A4]]
        self.assertEqual(len(counted), 1)
        self.assertEqual(words[first // 4 + 1], 0x90610020)                 # stw r3, 32(r1): the answer is kept
        # lwz r9, 32(r1); stw r3, 32(r1); cmpwi r3, 0; beq on; cmpwi r9, 0; beq cut
        self.assertEqual(words[extra // 4 + 1:extra // 4 + 4], [0x81210020, 0x90610020, 0x2C030000])
        self.assertEqual(words[extra // 4 + 4] & 0xFFFF0003, 0x41820000)
        self.assertEqual(words[extra // 4 + 5], 0x2C090000)
        beq = words[extra // 4 + 6]
        self.assertEqual(beq & 0xFFFF0003, 0x41820000)
        cut = (extra + 24 + signed(beq & 0xFFFC)) // 4
        # the cut: no step left (+8), the steps left out were never simulated (+492 = +124), then the loop's end
        self.assertEqual(words[cut:cut + 4], [0x39200000, 0x913F0008, 0x813F007C, 0x913F01EC])
        self.assertEqual(words[cut + 4] >> 26, 18)
        end = (cut + 4) * 4 + signed(words[cut + 4] & 0x03FFFFFC, 26)
        self.assertEqual(words[end // 4:end // 4 + 2], [0x39200000, 0x913F000C])    # li r9, 0; stw r9, 12(r31)


if __name__ == "__main__":
    unittest.main()
