"""A wreck slides before its restore (sites_wreck.py), run here as the bytes the image holds against made-up
machines: the site at the game's question "is this machine's restore due?" (fn_1_24A4C 0x24CB8) and the one round
the machine's part of the step (fn_1_850C4 0x85260), with the game's routines as stand-ins. No game data."""
import itertools, math, os, sys, unittest

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, ".."))
from test_raceview import Build, Cpu, get, put, TEXT
import test_out_test
from test_out_test import word, FALLEN, AT_REST_CPU
import build
import memmap
import outtest
import sites_wreck

MODE, SLIDE = memmap.MODE, memmap.RACE + memmap.RACE_WRECK_SLIDE
BSS, MACHINE = memmap.REL_BSS, 0x80D00000
RULES, RACE_TYPE = BSS + 0x3C30, BSS + 0x3C35
RESTORE, BROKEN, DESTROYED, CPU, FINISHED, RETIRED, TIME_OVER = 0x1, 0x80, 0x800, 0x200, 0x10000, 0x08000000, 0x10000000
DUE, BACK = TEXT + sites_wreck.DUE, TEXT + sites_wreck.SLIDE + 4
PART, AFTER = TEXT + sites_wreck.MACHINE, TEXT + sites_wreck.STEP + 4


class TheVerdictWithoutRestore(test_out_test.TheVerdict):
    """outtest.clause(restore=False): the game's verdict with neither Restore asked."""

    @classmethod
    def setUpClass(cls):
        cls.code = test_out_test.assemble(outtest.clause(3, 30, 0, 7, "o", "r", restore=False) +
                            "o:\n    li 3, 1\n    blr\nr:\n    li 3, 0\n    blr\n")

    def test_the_games_cases(self):
        """out-cases.tsv with the rule and the machine's own Restore taken away: the verdict the list gives then."""
        with open(os.path.join(here, "out-cases.tsv"), encoding="utf-8") as f:
            next(f)
            for line in f:
                flags, fallen, speed, rules, race_type, verdict, what = line.rstrip("\n").split("\t")
                case = (int(flags, 16), int(fallen, 16), int(speed, 16), int(rules, 16), int(race_type))
                bare = (case[0] & ~RESTORE, case[1], case[2], case[3] & ~outtest.RESTORE, case[4])
                self.assertEqual(self.says(*case), outtest.out(*bare), what)
                self.assertEqual(outtest.out(*case, restore=False), outtest.out(*bare), what)

    def test_the_text_is_the_model_over_every_combination(self):
        bits = (FINISHED, TIME_OVER, RETIRED, DESTROYED, BROKEN, CPU, RESTORE, 0x40000000)
        speeds = (0.0, 0.99999994, 1.0, 300.0, -0.5, math.nan)
        for n in range(1 << len(bits)):
            flags = sum(b for i, b in enumerate(bits) if n >> i & 1)
            for fallen, speed, rules, race_type in itertools.product((0, FALLEN), speeds, (0, outtest.RESTORE), (6, 5)):
                case = (flags, fallen, word(speed), rules, race_type)
                self.assertEqual(self.says(*case), outtest.out(*case, restore=False), [hex(c) for c in case])


class AWreckSlides(Build):
    def asked(self, flags, fallen=0, speed=0.0, rules=0, race_type=6, restoring=0, slide=1, mode=1):
        """The site for one machine: its flags as fn_1_248C0 finds them."""
        memory = {}
        put(memory, MODE, mode)
        put(memory, SLIDE, slide)
        put(memory, MACHINE, flags)
        put(memory, MACHINE + 0x58C, fallen)
        put(memory, MACHINE + 0x17C, word(speed))
        put(memory, MACHINE + 0x214, restoring, 2)
        put(memory, RULES, rules)
        memory[RACE_TYPE] = race_type
        left = []
        m = Cpu(self.image, memory, lambda address, cpu: left.append((address, get(cpu.mem, MACHINE), cpu.r[3], cpu.r[4])))
        m.r[3], m.r[4], m.r[29] = MACHINE, 1, MACHINE
        m.run(self.symbols["wreck_slide"])
        self.assertEqual([a for a, *_ in left], [DUE, BACK])                 # the game's question, then on
        self.assertEqual(left[0][2:], (MACHINE, 1))
        self.assertLessEqual(m.written, {MACHINE})
        return left[0][1]

    def test_the_sites_are_the_two_calls(self):
        for site, name, to in ((sites_wreck.SLIDE, "wreck_slide", DUE), (sites_wreck.STEP, "wreck_step", PART)):
            p = self.patches[TEXT + site]
            self.assertEqual(int(p["new"], 16), build.branch(TEXT + site, self.symbols[name]))
            self.assertEqual(int(p["original"], 16), build.branch(TEXT + site, to, True))

    def test_a_wreck_with_restore_keeps_sliding_until_the_game_without_restore_takes_it_out(self):
        wreck = BROKEN | RESTORE
        self.assertEqual(self.asked(wreck, speed=300.0), BROKEN)             # sliding: Restore is not seen
        self.assertEqual(self.asked(wreck, speed=1.0), BROKEN)
        self.assertEqual(self.asked(wreck, speed=0.99), wreck)               # at rest: the restore is due
        self.assertEqual(self.asked(wreck | DESTROYED, speed=300.0), wreck | DESTROYED)
        self.assertEqual(self.asked(wreck, fallen=FALLEN, speed=300.0), wreck)
        self.assertEqual(self.asked(wreck | CPU, speed=300.0), wreck | CPU)  # a CPU's breakdown is out at once
        self.assertEqual(self.asked(wreck, speed=300.0, race_type=5), wreck)
        self.assertEqual(self.asked(wreck, speed=300.0, rules=outtest.RESTORE), BROKEN)   # under the rule too
        # not a wreck, in its restore, finished, or a race without the word (a recording of before): the game's own
        self.assertEqual(self.asked(RESTORE, speed=300.0), RESTORE)
        self.assertEqual(self.asked(wreck, speed=300.0, restoring=180), wreck)
        self.assertEqual(self.asked(wreck | FINISHED, speed=300.0), wreck | FINISHED)
        self.assertEqual(self.asked(wreck, speed=300.0, slide=0), wreck)
        self.assertEqual(self.asked(wreck, speed=300.0, mode=0), wreck)
        self.assertEqual(self.asked(BROKEN, speed=300.0), BROKEN)            # without Restore nothing to do

    def test_the_site_is_the_verdict_without_restore_over_every_combination(self):
        bits = (FINISHED, TIME_OVER, RETIRED, DESTROYED, BROKEN, CPU, RESTORE, 0x40000000)
        for n in range(1 << len(bits)):
            flags = sum(b for i, b in enumerate(bits) if n >> i & 1)
            for fallen, speed, rules, restoring, slide in itertools.product(
                    (0, FALLEN, AT_REST_CPU), (0.0, 0.99999994, 1.0, 300.0, math.nan), (0, outtest.RESTORE), (0, 20, 180),
                    (1, 0)):
                slides = (slide and flags & RESTORE and flags & BROKEN and not flags & FINISHED and not restoring and
                          not outtest.out(flags, fallen, word(speed), rules, restore=False))
                self.assertEqual(self.asked(flags, fallen, speed, rules, 6, restoring, slide),
                                 flags & ~RESTORE if slides else flags, (hex(flags), fallen, speed, rules, restoring, slide))

    def stepped(self, flags, slide=1, mode=1, part=lambda f: f):
        """The site round the machine's part of the step, which leaves the flags `part` makes of them."""
        memory = {}
        put(memory, MODE, mode)
        put(memory, SLIDE, slide)
        put(memory, MACHINE, flags)
        left = []

        def game(address, cpu):
            left.append(address)
            if address == PART:
                self.assertEqual(cpu.r[3], MACHINE)
                put(cpu.mem, MACHINE, part(get(cpu.mem, MACHINE)))
        m = Cpu(self.image, memory, game)
        m.r[3] = m.r[27] = MACHINE
        m.run(self.symbols["wreck_step"])
        self.assertEqual(left, [PART, AFTER])
        return get(m.mem, MACHINE)

    def test_a_machine_with_restore_has_it_again_after_its_part_of_the_step(self):
        clear = lambda f: f & ~RESTORE
        self.assertEqual(self.stepped(BROKEN | RESTORE, part=clear), BROKEN | RESTORE)
        self.assertEqual(self.stepped(BROKEN | RESTORE), BROKEN | RESTORE)
        self.assertEqual(self.stepped(BROKEN | RESTORE, part=lambda f: clear(f) | DESTROYED), BROKEN | RESTORE | DESTROYED)
        self.assertEqual(self.stepped(BROKEN, part=clear), BROKEN)           # none before: none after
        self.assertEqual(self.stepped(BROKEN | RESTORE, slide=0, part=clear), BROKEN)   # a recording of before
        self.assertEqual(self.stepped(BROKEN | RESTORE, mode=0, part=clear), BROKEN)


if __name__ == "__main__":
    unittest.main()
