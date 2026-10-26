"""A machine is out by the game's own verdict (outtest.py), in every routine of the
image that asks, and the race waits for the player seats alone (raceend.py, 6.2): run here as the bytes the image
holds, against made-up games. The verdict's cases are the game's fn_1_32600 read instruction by instruction.
No game data."""
import itertools, math, os, struct, sys, tempfile, unittest

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, ".."))
import build, memmap
import frame_hud, native, outtest, standin_routine
import test_race_end as race_end
from test_race_end import Machine, BSS, MACHINES, FINISHED
from test_raceview import Build, Cpu, get, put, OUT_FRAMES, LOCAL
from test_spectator import Spectating, SPECTATOR, WATCHED, RIGHT, HUD_MASK, HUD_BITS

IMAGE = memmap.IMAGE
RULES, RACE_TYPE = BSS + 0x3C30, BSS + 0x3C35
FLOW = memmap.RACE + memmap.RACE_FLOW
RESTORE, RETIRED, DESTROYED, BROKEN, CPU, TIME_OVER = 0x8000, 0x08000000, 0x800, 0x80, 0x200, 0x10000000
FALLEN, AT_REST_CPU = 0x10, 0x80                    # +0x58C: out of contact; a CPU at rest (with 0x08000000)
ASM = None


def word(value):
    """A float's bits, or a word as it is."""
    return value if isinstance(value, int) else struct.unpack(">I", struct.pack(">f", value))[0]


def assemble(text):
    global ASM
    if ASM is None:
        from keystone import Ks, KS_ARCH_PPC, KS_MODE_PPC32, KS_MODE_BIG_ENDIAN
        ASM = Ks(KS_ARCH_PPC, KS_MODE_PPC32 + KS_MODE_BIG_ENDIAN)
    return bytes(ASM.asm(text)[0])


def machine(memory, seat, flags, fallen=0, speed=0.0):
    put(memory, MACHINES + seat * 0x620, flags)
    put(memory, MACHINES + seat * 0x620 + 0x58C, fallen)
    put(memory, MACHINES + seat * 0x620 + 0x17C, word(speed))


class TheVerdict(unittest.TestCase):
    """The text alone (assembled as every routine inlines it) against the game's fn_1_32600."""

    @classmethod
    def setUpClass(cls):
        cls.code = assemble(outtest.clause(3, 30, 0, 7, "o", "r") + "o:\n    li 3, 1\n    blr\nr:\n    li 3, 0\n    blr\n")

    def says(self, flags, fallen=0, speed=0.0, rules=0, race_type=6):
        memory = {}
        machine(memory, 0, flags, fallen, speed)
        put(memory, RULES, rules)
        memory[RACE_TYPE] = race_type
        m = Machine(self.code, memory)
        m.r[3], m.r[30] = MACHINES, BSS
        said = m.run(IMAGE)
        self.assertEqual(m.written, set())
        return bool(said)

    def test_the_games_cases(self):
        """out-cases.tsv: the cases the core's out_test.h is held to as well (TestTheOutTestsCases)."""
        cases = 0
        with open(os.path.join(here, "out-cases.tsv"), encoding="utf-8") as f:
            next(f)                                          # the columns' names
            for line in f:
                flags, fallen, speed, rules, race_type, verdict, what = line.rstrip("\n").split("\t")
                case = (int(flags, 16), int(fallen, 16), int(speed, 16), int(rules, 16), int(race_type))
                self.assertIn(verdict, ("out", "racing"), what)
                self.assertEqual(self.says(*case), verdict == "out", what)
                self.assertEqual(outtest.out(*case), verdict == "out", what)
                cases += 1
        self.assertGreaterEqual(cases, 30)

    def test_the_text_is_the_model_over_every_combination(self):
        bits = (FINISHED, TIME_OVER, RETIRED, DESTROYED, BROKEN, CPU, 0x1, 0x40000000)
        speeds = (0.0, 0.5, 0.99999994, 1.0, 1.0000001, 300.0, -0.5, 0x80000000, math.inf, math.nan, 0xFFC00000)
        for n in range(1 << len(bits)):
            flags = sum(b for i, b in enumerate(bits) if n >> i & 1)
            for fallen, speed, rules, race_type in itertools.product((0, FALLEN, AT_REST_CPU), speeds,
                                                                     (0, RESTORE, 0x04000000), (6, 5)):
                if flags & (TIME_OVER | RETIRED | DESTROYED) and speed not in (0.0, 300.0):
                    continue                                 # (the speed is not read there: fewer runs)
                case = (flags, fallen, word(speed), rules, race_type)
                self.assertEqual(self.says(*case), outtest.out(*case), [hex(c) for c in case])


class TheUniformPass(unittest.TestCase):
    """standin_routine.py: flag 0x08000000 for the machines that are out, run over every machine from each one's call."""

    @classmethod
    def setUpClass(cls):
        cls.dir = tempfile.TemporaryDirectory()
        image = build.build(cls.dir.name)["image"]
        text = standin_routine.asm(0, 0, 1, 0, 2)
        part = text[text.index("    lis 9, %d\n    lwz 0, %d(9)" % (memmap.hi(memmap.RACE),
                                                                    memmap.lo(memmap.RACE) + memmap.RACE_FLOW)):
                    text.index("u_retired:")]
        code = assemble(part + "u_retired:\n")
        assert image.count(code) == 1
        cls.code = code + bytes.fromhex("4E800020")          # the pass, then a return in place of what follows it

    @classmethod
    def tearDownClass(cls):
        cls.dir.cleanup()

    def flags(self, flow, machines, rules=0):
        memory = {}
        for seat, how in enumerate(machines):
            machine(memory, seat, *how)
        put(memory, RULES, rules); put(memory, FLOW, flow)
        memory[RACE_TYPE] = 6
        m = Machine(self.code, memory)
        m.r[10], m.r[29], m.ctr = MACHINES, BSS, len(machines)
        m.run(IMAGE)
        self.assertTrue(all(MACHINES <= a < MACHINES + len(machines) * 0x620 for a in m.written))
        return [m.read(MACHINES + seat * 0x620, 4) for seat in range(len(machines))]

    def test_with_the_flow_the_games_verdict(self):
        race = [(0, 0, 80.0), (DESTROYED, FALLEN, 0.0), (BROKEN, 0, 60.0), (BROKEN, 0, 0.2), (FINISHED, FALLEN, 0.0),
                (DESTROYED, 0, 0.0), (0, AT_REST_CPU, 0.0)]
        self.assertEqual(self.flags(1, race), [0, DESTROYED | RETIRED, BROKEN, BROKEN | RETIRED, FINISHED,
                                               DESTROYED | RETIRED, 0])
        # under the Restore rule nothing: every one of them is put back
        self.assertEqual(self.flags(1, race, rules=RESTORE), [h[0] for h in race])

    def test_without_it_the_masks_of_before(self):
        # (a recording made before the flow plays as it was raced: +0x58C 0x10 or 0x80, Restore or not)
        race = [(0, 0, 80.0), (DESTROYED, FALLEN, 0.0), (BROKEN, 0, 0.2), (FINISHED, FALLEN, 0.0), (0, AT_REST_CPU, 50.0)]
        for rules in (0, RESTORE):
            self.assertEqual(self.flags(0, race, rules), [0, DESTROYED | RETIRED, BROKEN, FINISHED, RETIRED])


class TheRaceEndsOnThePlayerSeats(unittest.TestCase):
    """raceend.py with the flow word, through `over` and `endtest` (test_race_end.py's made-up game)."""
    setUpClass = classmethod(race_end.TheRacesEnd.setUpClass.__func__)
    tearDownClass = classmethod(race_end.TheRacesEnd.tearDownClass.__func__)
    seated = race_end.TheRacesEnd.seated

    def said(self, step, players, flow=1, rules=0, speeds=(), fallen=(), **memory):
        """The race-over test's answer before `step` (every step below it official), and the frame routine's, which
        must agree."""
        answers = []
        for routine in ("over", "endtest"):
            m = race_end.game(step=step, official=step, rule=rules, **memory)
            put(m, FLOW, flow)
            for seat, speed in enumerate(speeds):
                put(m, MACHINES + seat * 0x620 + 0x17C, word(speed))
            for seat, bits in enumerate(fallen):
                put(m, MACHINES + seat * 0x620 + 0x58C, bits)
            cpu = Machine(self.seated(players), m)
            if routine == "endtest":
                cpu.r[31], cpu.r[30] = memmap.CONTROL, BSS
            answers.append(cpu.run(self.symbols["race_over"] if routine == "over" else self.endtest))
        self.assertEqual(answers[0], answers[1], (step, players, memory))
        return answers[0]

    def first_over(self, around, players, **how):
        said = [self.said(step, players, **how) for step in range(around - 3, around + 4)]
        self.assertEqual(sorted(said), said, said)
        self.assertIn(1, said)
        return around - 3 + said.index(1)

    def test_a_computer_racer_still_racing_holds_nothing_up(self):
        # seat 0 a player who finished in step f, seat 1 a computer racer stuck short of the line: without the host's
        # rule the race would wait for it for ever; with it, 480 steps after the player's finish
        f = 4486
        race = dict(flags=(FINISHED, 0), times=(race_end.finish_time(f), 0))
        self.assertEqual(self.first_over(f + 1 + 480, 0b01, **race), f + 1 + 480)
        self.assertEqual(self.said(f + 1 + 50000, 0b01, flow=0, **race), 0)      # (before the flow: never)
        # with the rule too: the player's 480, not the rule's end
        self.assertEqual(self.first_over(f + 1 + 480, 0b01, limit=5400, **race), f + 1 + 480)
        # a race of bots (no player seat named): every seat is waited for, as before
        self.assertEqual(self.said(f + 1 + 50000, 0, **race), 0)

    def test_a_player_seat_still_racing_holds_the_race(self):
        f = 4486
        race = dict(flags=(FINISHED, 0, 0), times=(race_end.finish_time(f), 0, 0))
        self.assertEqual(self.said(f + 1 + 50000, 0b011, **race), 0)
        # ... until the host's rule's end, as before
        self.assertEqual(self.first_over(f + 1 + 5400, 0b011, limit=5400, **race), f + 1 + 5400)

    def test_a_player_seat_is_done_when_the_game_says_it_is_out(self):
        f = 4486
        times = (race_end.finish_time(f), 0)
        after = f + 1 + 480
        # broken down and still moving: it may yet cross the line; at rest: out
        self.assertEqual(self.said(after + 9000, 0b11, flags=(FINISHED, BROKEN), times=times, speeds=(0, 40.0)), 0)
        self.assertEqual(self.first_over(after, 0b11, flags=(FINISHED, BROKEN), times=times, speeds=(0, 0.5)), after)
        # a fall: out, but under the Restore rule it is put back, and the race waits for it
        how = dict(flags=(FINISHED, DESTROYED), times=times, fallen=(0, FALLEN))
        self.assertEqual(self.first_over(after, 0b11, **how), after)
        self.assertEqual(self.said(after + 9000, 0b11, rules=RESTORE, **how), 0)
        # nobody finished and every player seat out: the results at once (the game's own wait: none started)
        self.assertEqual(self.said(400, 0b01, flags=(RETIRED | DESTROYED, 0), times=(0, 0)), 1)


class TheSpectatorAndTheVerdict(Build):
    """frame_spectate.py's count and frame_specwatch.py's choice of racer, by the verdict."""

    def race(self, flags, speed=0.0, fallen=0, rules=0):
        g = Spectating(self, out=False)
        machine(g.mem, LOCAL, flags, fallen, speed)
        put(g.mem, RULES, rules)
        g.mem[RACE_TYPE] = 6
        return g

    def test_spectating_starts_only_on_the_verdict(self):
        for how, spectates in (((BROKEN, 40.0), False), ((BROKEN, 0.3), True), ((DESTROYED, 0.0, FALLEN), True),
                               ((DESTROYED | RETIRED, 0.0, FALLEN, RESTORE), False), ((BROKEN, 0.0, 0, RESTORE), False),
                               ((RETIRED, 30.0), True)):
            g = self.race(*how).watch(185)
            self.assertEqual((get(g.mem, SPECTATOR), get(g.mem, OUT_FRAMES) >= 180), (spectates, spectates), how)
            self.assertEqual(get(g.mem, OUT_FRAMES) == 0, not spectates, how)

    def test_a_knock_out_a_rollback_takes_back_counts_from_0_again(self):
        # predicted out at a step, then the official inputs keep it racing: the count is the frame's, never latched
        g = self.race(0, speed=80.0)
        g.watch(10)
        at = g.step
        g.racing = lambda s: machine(g.mem, LOCAL, DESTROYED, FALLEN) if s == at else None
        for _ in range(20):
            g.frame()
        self.assertEqual(get(g.mem, OUT_FRAMES), 20)
        g.racing = lambda s: None
        g.frame(back_to=at)                                  # the rollback: the step runs again, no fall
        self.assertEqual(get(g.mem, OUT_FRAMES), 0)

    def test_a_coasting_breakdown_is_still_watched_and_a_racer_at_rest_is_not(self):
        g = Spectating(self).watch(182)
        g.press(RIGHT); g.press(RIGHT)                       # racer 2
        machine(g.mem, 2, BROKEN, 0, 55.0)
        self.assertEqual(g.frame()[1], 2)                    # it still races
        machine(g.mem, 2, BROKEN, 0, 0.0)
        self.assertEqual((g.frame()[1], get(g.mem, WATCHED)), (3, 2))   # at rest: the cameras leave it
        # a wreck the Restore rule will put back is watched on
        g = Spectating(self).watch(182)
        g.press(RIGHT)
        put(g.mem, RULES, RESTORE)
        machine(g.mem, 1, DESTROYED, FALLEN, 0.0)
        self.assertEqual(g.frame()[1], 1)


class TheHud(Build):
    """frame_hud.py: `hudstate` (the retired bits, the HUD's parts) and `finwatch` (out: 300) by the verdict."""

    def hud(self, flags, speed=0.0, fallen=0, rules=0):
        g = Spectating(self, out=False)
        machine(g.mem, LOCAL, flags, fallen, speed)
        put(g.mem, RULES, rules)
        g.mem[RACE_TYPE] = 6
        put(g.mem, HUD_MASK, 0xFF9FFFFF)
        for bits in HUD_BITS:
            g.mem[bits] = 1                                  # what the game's own fn_1_32200 might have left
        text = frame_hud.asm()
        at = IMAGE + self.image.index(assemble(text))
        for name in ("hudstate", "finwatch"):
            cpu = Cpu(self.image, g.mem)
            cpu.r[31], cpu.r[30] = memmap.CONTROL, BSS
            cpu.run(at + native.label_offsets(text)[name], limit=20000)
            g.mem = cpu.mem
        return get(g.mem, HUD_MASK), tuple(g.mem.get(b, 0) & 1 for b in HUD_BITS), get(g.mem, memmap.FINISH_KIND)

    def test_the_hud_goes_only_on_the_verdict(self):
        on, off = 0xFF9FFFFF, 0
        self.assertEqual(self.hud(BROKEN, 45.0), (on, (0, 0, 0), 0))              # coasting: HUD, energy meter
        self.assertEqual(self.hud(BROKEN, 0.0), (off, (0, 1, 0), 2))              # at rest: retired
        self.assertEqual(self.hud(DESTROYED, 0.0, FALLEN), (off, (0, 0, 1), 2))
        self.assertEqual(self.hud(DESTROYED, 0.0, FALLEN, RESTORE), (on, (0, 0, 0), 0))   # put back: never retired
        self.assertEqual(self.hud(0, 90.0), (on, (0, 0, 0), 0))

    def test_one_cause_in_the_games_order(self):
        # fn_1_32200 0x32238-0x32288: 0x10000000, else 0x80, else 0x800; the banner (fn_1_15DFD4) then takes
        # 0x26B18, 0x26B1A, 0x26B19: a wreck (0x880, fn_1_25004) is the game's kind 1, not 2
        off = 0
        self.assertEqual(self.hud(DESTROYED | BROKEN, 0.0, FALLEN), (off, (0, 1, 0), 2))
        self.assertEqual(self.hud(TIME_OVER | DESTROYED | BROKEN, 0.0), (off, (1, 0, 0), 2))
        self.assertEqual(self.hud(TIME_OVER | DESTROYED, 0.0), (off, (1, 0, 0), 2))
        self.assertEqual(self.hud(RETIRED | DESTROYED, 0.0), (off, (0, 0, 1), 2))
        self.assertEqual(self.hud(RETIRED | BROKEN, 0.0), (off, (0, 1, 0), 2))
        self.assertEqual(self.hud(RETIRED, 30.0), (off, (0, 0, 0), 2))      # no cause flag: no cause bit

    def test_a_broken_finish_is_a_finish(self):
        # fn_1_32A40 0x32CE4: a finished machine is never asked; FINISH! (kind 1, 480), no cause bit, parts off
        off = 0
        for flags, speed, fallen in ((FINISHED | BROKEN, 45.0, 0), (FINISHED | BROKEN, 0.0, 0),
                                     (FINISHED | DESTROYED | BROKEN, 0.0, FALLEN),
                                     (FINISHED | DESTROYED | BROKEN | RETIRED, 0.0, FALLEN),
                                     (FINISHED | TIME_OVER | BROKEN, 0.0, 0)):
            self.assertEqual(self.hud(flags, speed, fallen), (off, (0, 0, 0), 1), hex(flags))


if __name__ == "__main__":
    unittest.main()
