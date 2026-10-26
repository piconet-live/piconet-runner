"""A rival's effects follow its drawn body (sites_effects.py) and the drawn transform's readers (sites_drawn.py),
run here as the bytes the image holds against a made-up effect record, machine and view at the four calls of a draw
in fn_1_58994. No game data."""
import math, os, struct, sys, unittest

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, ".."))
import build, memmap
import sites_effects as fx
from test_race_end import MODULE, MACHINES, POINTER, signed
from test_raceview import Build, Cpu, get, put, CONTROL, IMAGE, TEXT
from test_smooth import floats, put_floats

SMOOTH, S = memmap.SMOOTH, memmap.EFFECTS
RECORD, DRAW = 0x80D00000, TEXT + 0x5D1B8           # an effect's record; a kind's draw (kind 13's)
CAMERA = 0xE0001C00                                  # the camera's struct, as [0x801A66CC] holds it
SEAT = 7                                             # the rival the effect belongs to
OFFSET = (3.0, -0.5, 4.0)                            # where smooth.py draws it, from where the race has it
# a view: a turn (orthonormal rows) and a place; the camera struct's is the frame before's
TURN = ((0.6, 0.0, -0.8), (0.0, 1.0, 0.0), (0.8, 0.0, 0.6))
VIEW_T, CAMERA_T = (96.0, -351.5, 1743.0), (96.5, -351.25, 1743.5)


class Effects(Cpu):
    """The test machine and the few words the effects' routine adds: `slw`, `stwx`."""

    def other(self, pc, w):
        r, op, d, a, b, xo = self.r, w >> 26, (w >> 21) & 31, (w >> 16) & 31, (w >> 11) & 31, (w >> 1) & 0x3FF
        if IMAGE <= pc < IMAGE + len(self.image) and op == 31 and xo == 24:                  # slw
            r[a] = (r[d] << (r[b] & 63)) & 0xFFFFFFFF if r[b] & 63 < 32 else 0
            return pc + 4
        if IMAGE <= pc < IMAGE + len(self.image) and op == 31 and xo == 151:                 # stwx
            self.write(((r[a] if a else 0) + r[b]) & 0xFFFFFFFF, 4, r[d])
            return pc + 4
        return super().other(pc, w)


def view_words(memory, at):
    return [get(memory, at + 16 * i + 12) for i in range(3)]


def world(turn, t):
    """R^T t: a view's place taken back to the world."""
    return tuple(sum(turn[i][j] * t[i] for i in range(3)) for j in range(3))


class TheEffectsDraw(Build):
    def scene(self, kind=13, owner=SEAT, offset=OFFSET, written=1, mode=1, tests=0, logs=1, camera=CAMERA,
              mark=0, draw_writes=None):
        """Memory for one call: the effect's record, its owner's drawn transform, the two views. `draw_writes`: a
        view word the draw writes itself (address, value)."""
        m = {}
        put(m, memmap.MODE, mode); put(m, 0x800030C8, MODULE); put(m, POINTER, MACHINES)
        put(m, CONTROL + 456, tests); put(m, memmap.IMAGE_LOGS, logs)
        put(m, RECORD + 0xC, kind, 2); put(m, RECORD + 0x18, owner & 0xFFFF, 2); put(m, RECORD + 0xB2, mark, 2)
        put_floats(m, RECORD + 0x3C, 10.0, 20.0, 30.0)
        if 0 <= owner < 30:
            put_floats(m, SMOOTH + owner * 48 + 12, *offset)
            m[SMOOTH + owner * 48 + 36] = written
        for at, t in ((fx.VIEW, VIEW_T), (CAMERA, CAMERA_T)):
            for i in range(3):
                put_floats(m, at + 16 * i, *TURN[i], t[i])
        put(m, fx.CAMERA_STRUCT, camera)
        return m

    def call(self, m, site=0x58A60, draw_writes=None):
        """The site's routine, entered as the game's branch enters it, with the dispatcher's registers. Gives the
        machine, what the draw saw (both views' places, its registers), and where the routine left to."""
        seen, left = [], []

        def game(address, cpu):
            if address == DRAW:
                seen.append(dict(view=floats(cpu.mem, fx.VIEW + 12, 1) + floats(cpu.mem, fx.VIEW + 28, 1) +
                                 floats(cpu.mem, fx.VIEW + 44, 1),
                                 camera=floats(cpu.mem, CAMERA + 12, 1) + floats(cpu.mem, CAMERA + 28, 1) +
                                 floats(cpu.mem, CAMERA + 44, 1),
                                 regs=list(cpu.r[:11]) + [cpu.r[1]]))
                if draw_writes:
                    cpu.write(draw_writes[0], 4, draw_writes[1])
                for reg in (0, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12):          # a C function's own registers
                    cpu.r[reg] = 0xDEAD0000 | reg
            else:
                left.append(address)

        cpu = Effects(self.image, m, game)
        for reg in range(4, 11):
            cpu.r[reg] = 0x40400000 + reg
        cpu.r[3], cpu.r[12], cpu.ctr = RECORD, DRAW, DRAW
        cpu.r[26], cpu.r[27], cpu.r[28] = 0x26262626, 0x27272727, RECORD
        stack = cpu.r[1]
        p = self.patches[TEXT + site]
        cpu.run(build.branch_target(int(p["new"], 16), TEXT + site))
        self.assertEqual(left, [TEXT + site + 4])
        self.assertEqual((cpu.r[1], cpu.r[26], cpu.r[27], cpu.r[28]), (stack, 0x26262626, 0x27272727, RECORD))
        self.assertEqual(len(seen), 1)                                         # the draw, once
        self.assertEqual(seen[0]["regs"][3:11], [RECORD] + [0x40400000 + reg for reg in range(4, 11)])
        # the routine's own stack frame, and the word every called function keeps its return in (r1 + 4)
        written = {a for a in cpu.written if not stack - 112 <= a < stack and a != stack + 4}
        return cpu, seen[0], written

    def moved(self, seen, offset=OFFSET):
        """How far each view's place was moved for the draw, in the world."""
        return (tuple(n - o for n, o in zip(world(TURN, seen["view"]), world(TURN, VIEW_T))),
                tuple(n - o for n, o in zip(world(TURN, seen["camera"]), world(TURN, CAMERA_T))))

    def test_the_sites_are_fn_1_58994s_four_draws(self):
        self.assertEqual(build.EFFECT_DRAWS, (0x58A60, 0x58AB4, 0x58B70, 0x58C28))
        for site in build.EFFECT_DRAWS:
            p = self.patches[TEXT + site]
            self.assertEqual((int(p["original"], 16), int(p["new"], 16) & 1), (0x4E800421, 0))   # bctrl; a plain branch
            self.assertEqual(build.branch_target(int(p["new"], 16), TEXT + site), self.symbols[f"effect_draw_{site:x}"])

    def test_a_rivals_effect_is_drawn_where_its_body_is(self):
        for site in build.EFFECT_DRAWS:
            m = self.scene()
            before = (view_words(m, fx.VIEW), view_words(m, CAMERA))
            cpu, seen, written = self.call(m, site)
            for moved in self.moved(seen):
                for got, o in zip(moved, OFFSET):
                    self.assertAlmostEqual(got, o, delta=1e-3)                 # p is drawn where p + o is
            # afterwards both views are what they were, bit for bit; the record and the owner are only read
            self.assertEqual((view_words(cpu.mem, fx.VIEW), view_words(cpu.mem, CAMERA)), before)
            game = {a for a in written if a < IMAGE}
            self.assertLessEqual(game, {fx.VIEW + 12, fx.VIEW + 28, fx.VIEW + 44, CAMERA + 12, CAMERA + 28, CAMERA + 44})

    def test_every_followed_kind_and_no_other(self):
        for kind in range(-1, 70):
            cpu, seen, written = self.call(self.scene(kind=kind))
            moved = any(abs(x) > 1e-3 for x in self.moved(seen)[0])
            self.assertEqual(moved, kind in fx.FOLLOWED, kind)
        for kind in (6, 9, 22, 19, 1, 4, 25):                                  # the ones the docstring leaves out
            self.assertNotIn(kind, fx.FOLLOWED)

    def test_the_courses_kind_13_stays(self):
        cpu, seen, written = self.call(self.scene(kind=13, mark=14))           # Sand Ocean's own (fn_1_D6C10)
        self.assertEqual(seen["view"], VIEW_T)
        cpu, seen, written = self.call(self.scene(kind=13, mark=0))            # a machine's low energy (fn_1_6C87C)
        self.assertNotEqual(seen["view"], VIEW_T)

    def test_nothing_for_an_effect_whose_machine_is_drawn_where_it_is(self):
        # this game's own machine, a frame that started over, a plain start, no machine: smooth.py's byte is 0
        for case in (dict(written=0), dict(mode=0), dict(owner=-1), dict(owner=30), dict(owner=0x7FFF)):
            m = self.scene(**case)
            cpu, seen, written = self.call(m)
            self.assertEqual((seen["view"], seen["camera"]), (VIEW_T, CAMERA_T), case)
            self.assertEqual(written, set(), case)

    def test_a_word_the_draw_writes_itself_stays_as_it_wrote_it(self):
        cpu, seen, written = self.call(self.scene(), draw_writes=(fx.VIEW + 28, 0x12345678))
        self.assertEqual(get(cpu.mem, fx.VIEW + 28), 0x12345678)
        self.assertEqual(view_words(cpu.mem, fx.VIEW)[0::2], [struct.unpack(">I", struct.pack(">f", v))[0]
                                                           for v in VIEW_T[0::2]])

    def test_without_the_camera_struct_the_view_alone_moves(self):
        cpu, seen, written = self.call(self.scene(camera=0))
        self.assertEqual(seen["camera"], CAMERA_T)
        for got, o in zip(self.moved(seen)[0], OFFSET):
            self.assertAlmostEqual(got, o, delta=1e-3)

    def test_the_counters_say_how_far_before_and_after(self):
        cpu, seen, written = self.call(self.scene())
        size = sum(o * o for o in OFFSET)                                      # 25.25: 5.02 units
        self.assertEqual(get(cpu.mem, S + fx.MOVED), 1)
        self.assertEqual(get(cpu.mem, S + fx.LEFT), 0)
        self.assertAlmostEqual(floats(cpu.mem, S + fx.LARGEST_BEFORE, 1)[0], size, places=3)
        self.assertLess(floats(cpu.mem, S + fx.LARGEST_AFTER, 1)[0], 1e-6)   # the place's rounding at 1,743 units
        self.assertEqual(get(cpu.mem, S + fx.BEFORE + 4 * 9), 1)               # 4 to 8 units: class 9
        self.assertEqual(get(cpu.mem, S + fx.AFTER + 4 * 0), 1)                # below 1/64
        self.assertEqual(get(cpu.mem, S + fx.BY_KIND + 4 * 13), 1)
        # the largest stays; a second draw counts again
        m = cpu.mem
        put_floats(m, S + fx.LARGEST_BEFORE, 100.0)
        cpu, seen, written = self.call(m)
        self.assertEqual((get(cpu.mem, S + fx.MOVED), floats(cpu.mem, S + fx.LARGEST_BEFORE, 1)[0]), (2, 100.0))

    def test_the_tests_bit_leaves_the_effects_and_the_counters_say_how_far(self):
        cpu, seen, written = self.call(self.scene(tests=fx.STAY))
        self.assertEqual((seen["view"], seen["camera"]), (VIEW_T, CAMERA_T))
        size = sum(o * o for o in OFFSET)
        self.assertEqual((get(cpu.mem, S + fx.MOVED), get(cpu.mem, S + fx.LEFT)), (0, 1))
        self.assertAlmostEqual(floats(cpu.mem, S + fx.LARGEST_AFTER, 1)[0], size, places=3)
        self.assertEqual(get(cpu.mem, S + fx.AFTER + 4 * 9), 1)

    def test_a_players_game_keeps_no_counters(self):
        cpu, seen, written = self.call(self.scene(logs=0))
        self.assertNotEqual(seen["view"], VIEW_T)                              # moved as ever
        self.assertEqual({a for a in written if IMAGE <= a < 0xE0000000}, set())

    def test_the_classes(self):
        # class k from 1 on: 2^(k-7) up to 2^(k-6) units; 0 below 1/64; 15 from 256 up
        for size, k in ((0.0, 0), (0.01, 0), (0.02, 1), (0.9, 6), (1.0, 7), (5.0, 9), (49.0, 12), (300.0, 15)):
            o = (size, 0.0, 0.0)
            cpu, seen, written = self.call(self.scene(offset=o, tests=fx.STAY))
            self.assertEqual(get(cpu.mem, S + fx.BEFORE + 4 * k), 1, size)
            self.assertEqual(sum(get(cpu.mem, S + fx.BEFORE + 4 * c) for c in range(fx.CLASSES)), 1, size)


REPORT = os.path.join(here, "..", "..", "gates", "reports", "smoothing-log.py")


# the gates are optional in the runner: their report is checked where they are
@unittest.skipUnless(os.path.isdir(os.path.join(here, "..", "..", "gates")), "no port/gates in this checkout")
class TheReport(unittest.TestCase):
    def test_the_report_reads_the_counters_where_they_are(self):
        import importlib.util
        path = REPORT
        spec = importlib.util.spec_from_file_location("smoothing_log", path)
        report = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(report)
        self.assertEqual((report.MOVED, report.LARGEST_BEFORE, report.LARGEST_AFTER, report.LEFT, report.BEFORE,
                          report.AFTER, report.BY_KIND, report.CLASSES, report.KINDS),
                         (fx.MOVED, fx.LARGEST_BEFORE, fx.LARGEST_AFTER, fx.LEFT, fx.BEFORE, fx.AFTER, fx.BY_KIND,
                          fx.CLASSES, fx.KINDS))
        self.assertEqual(report.facts.EFFECTS_AT, memmap.EFFECTS)
        self.assertEqual(report.EFFECTS_LEFT, fx.STAY << 2)                   # +456's bits, shifted into the flags


class TheDrawnTransform(Build):
    """sites_drawn.py: the one reader of a machine's drawn transform."""

    def entry(self, routine, r9, written=1, seat=SEAT, pointer=MACHINES):
        m = {}
        put(m, 0x800030C8, MODULE); put(m, POINTER, pointer)
        m[SMOOTH + seat * 48 + 36] = written
        cpu = Cpu(self.image, m)
        cpu.r[9] = r9
        cpu.run(self.symbols[routine])
        self.assertEqual(cpu.written, set())
        return cpu.r[10]

    def test_by_index_and_by_block(self):
        self.assertEqual(self.entry("drawn_entry", SEAT), SMOOTH + SEAT * 48)
        self.assertEqual(self.entry("drawn_machine", MACHINES + SEAT * 0x620), SMOOTH + SEAT * 48)
        self.assertEqual(self.entry("drawn_machine", MACHINES + 29 * 0x620, seat=29), SMOOTH + 29 * 48)

    def test_none(self):
        self.assertEqual(self.entry("drawn_entry", SEAT, written=0), 0)
        for index in (30, 0xFFFFFFFF, 0x8000):
            self.assertEqual(self.entry("drawn_entry", index), 0)
        for block in (MACHINES + 30 * 0x620, MACHINES - 0x620, MACHINES + SEAT * 0x620 + 4):
            self.assertEqual(self.entry("drawn_machine", block), 0)
        self.assertEqual(self.entry("drawn_machine", MACHINES + SEAT * 0x620, pointer=0), 0)


if __name__ == "__main__":
    unittest.main()
