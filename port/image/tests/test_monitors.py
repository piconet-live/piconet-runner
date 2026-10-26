"""The hand-back's monitors (monitors.py), run here as the bytes the image holds against a made-up race seen from seat
0, one drawn frame a step through a whole hand-back: when they are on, which picture the screen shows (and that each
is decoded once), which sounds play and when (once each, also when steps are skipped), where they are placed against
the followed racer's drawn matrix, how that frame is eased (monitors_ease.py), what the draw sends (the models in
StarterDisp's order, and not its depth-only box), and that the routine writes nothing but its stack, its own state and
the GX pipe. The game's routines are stand-ins that record what they are given; the constants of .rodata 0x78D0 the
routine reads are put in by hand."""
import math, os, struct, sys, unittest

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, ".."))
import build, memmap  # noqa: F401  (build puts the generators' folder on the path)
import monitors, monitors_ease
from test_race_end import BSS, MODULE, MACHINES, POINTER, signed
from test_raceview import Build, put, CONTROL, TEXT
from test_nametags import TagCpu, single

HB, LEFT, STEP, STACK = memmap.MONITORS, memmap.HANDBACK, CONTROL + 124, 0x80C00000
RODATA = MODULE + monitors.RODATA
LIST, Q, BLOCK, PIPE = 0x80A00000, 0x80A00100, 0x80B00000, 0xCC008000
TEXOBJ = {0x30: 0x80A10000, 0x38: 0x80A20000}           # the screen models' movie texture objects
FOLLOWED, END = 2, 5000
CONSTANTS = {0x0: 60.0, 0x4: 182.04445, 0x10: 1.0, 0x18: 0.0, 0x24: -1.0, 0xB4: -20.0, 0xDC: 1040.0, 0xE0: 9.5,
             0xE4: 40.0, 0xE8: 29.5, 0xEC: 140.0, 0xF0: 109.5, 0x108: 15.0}
MODELS = (0x08, 0x10, 0x18, 0x20, 0x28, 0x30) * 2 + (0x90, 0x38)


class MonitorCpu(TagCpu):
    """The instructions the monitors add: subfic, mulhwu, divw, and a double's subtraction and rounding; the GX
    pipe's words counted."""

    def other(self, pc, w):
        r, op, d, a, b = self.r, w >> 26, (w >> 21) & 31, (w >> 16) & 31, (w >> 11) & 31
        xo, f = (w >> 1) & 0x3FF, self.__dict__.setdefault("f", [0.0] * 32)
        if not memmap.IMAGE <= pc < memmap.IMAGE + len(self.image):
            return super().other(pc, w)
        if op == 8: r[d] = (signed(w & 0xFFFF) - r[a]) & 0xFFFFFFFF                             # subfic
        elif op == 31 and xo == 11: r[d] = (r[a] * r[b]) >> 32                                  # mulhwu
        elif op == 31 and xo == 491: r[d] = int(signed(r[a], 32) / signed(r[b], 32)) & 0xFFFFFFFF   # divw
        elif op == 63 and xo & 31 == 20: f[d] = f[a] - f[b]                                     # fsub
        elif op == 63 and xo == 12: f[d] = single(f[b])                                         # frsp
        elif op == 63 and xo & 31 == 26: f[d] = estimate(f[b])                                  # frsqrte
        else:
            return super().other(pc, w)
        return pc + 4

    def write(self, address, size, value):
        self.pipe = getattr(self, "pipe", 0) + (address == PIPE)
        super().write(address, size, value)


def estimate(x):
    """frsqrte as a 12-bit estimate of 1 / sqrt(x): the Newton steps after it must make it a float's."""
    m, e = math.frexp(1.0 / math.sqrt(x))
    return math.ldexp(round(m * 4096) / 4096, e)


def matrix(yaw):
    """A drawn matrix (3 x 4, rows) turned by yaw about y, at (100, 5, -30): forward is its third column negated."""
    c, s = math.cos(yaw), math.sin(yaw)
    return [[c, 0.0, s, 100.0], [0.0, 1.0, 0.0, 5.0], [-s, 0.0, c, -30.0]]


def posed(yaw=0.0, roll=0.0, p=(100.0, 5.0, -30.0)):
    """A drawn matrix rolled about its back axis, then turned by yaw about y, at p."""
    c, s, cr, sr = math.cos(yaw), math.sin(yaw), math.cos(roll), math.sin(roll)
    turn = [[c, 0.0, s], [0.0, 1.0, 0.0], [-s, 0.0, c]]
    rolled = [[cr, -sr, 0.0], [sr, cr, 0.0], [0.0, 0.0, 1.0]]
    rows = [[sum(turn[i][k] * rolled[k][j] for k in range(3)) for j in range(3)] for i in range(3)]
    return [rows[i] + [p[i]] for i in range(3)]


def column(m, j):
    return [m[i][j] for i in range(3)]


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def angle(a, b):
    c = [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]
    return math.atan2(math.sqrt(dot(c, c)), dot(a, b))


def apart(a, b):
    return math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)))


class Race:
    def __init__(self, t, yaw=0.0, state=2, movie=2):
        self.t, self.mem = t, {}
        m = self.mem
        put(m, 0x800030C8, MODULE); put(m, POINTER, MACHINES); m[BSS + 0x3C39] = 4
        put(m, BSS + 0x85280 + 0x248, movie)
        put(m, HB + monitors.STATE, state); put(m, HB + monitors.BLOCK, BLOCK)
        for off, v in CONSTANTS.items():
            put(m, RODATA + off, struct.unpack(">I", struct.pack(">f", v))[0])
        put(m, RODATA + 8, 0x43300000); put(m, RODATA + 12, 0x80000000)
        put(m, BSS + 0x38460 + 8, LIST); put(m, LIST + 8, Q)
        for off in set(MODELS):
            model = 0x80A40000 + off * 0x100
            put(m, Q + off, model)
            if off in TEXOBJ:                         # two texture entries: the second is the movie's
                put(m, model + 0x18, 2, 2)
                m[model + 0x40 + 0x20 + 0xD] = 1
                put(m, model + 0x40 + 0x20 + 8, TEXOBJ[off])
        self.followed = FOLLOWED
        self.place(matrix(yaw))

    def place(self, m, seat=FOLLOWED):
        """The drawn matrix of `seat` (3 x 4, rows)."""
        self.m = m
        base = MACHINES + seat * 0x620 + 0x14C
        for i, row in enumerate(m):
            for j, v in enumerate(row):
                put(self.mem, base + 16 * i + 4 * j, struct.unpack(">I", struct.pack(">f", v))[0])

    def floats(self, address, n):
        return [struct.unpack(">f", bytes(self.mem.get(address + 4 * i + k, 0) for k in range(4)))[0]
                for i in range(n)]

    def eased(self):
        """The eased frame: position, back, up (memmap.MONITORS +EASED)."""
        v = self.floats(HB + monitors_ease.EASED, 9)
        return v[0:3], v[3:6], v[6:9]

    def turn(self):
        """The record's turn: rows of right, up, back."""
        v = self.floats(HB + monitors.RECORD + 0x48, 12)
        return [v[4 * i:4 * i + 3] for i in range(3)]

    def game(self, address, cpu):
        f, r = cpu.__dict__.setdefault("f", [0.0] * 32), cpu.r
        if address == TEXT + 0x5910:
            r[3] = 0
        elif address == TEXT + 0x6B48:
            r[3] = self.followed
        elif address == 0x8006E0A4:
            self.point = struct.unpack(">3f", bytes(cpu.read(r[3] + i, 1) for i in range(12)))
        elif address == 0x8006E324:
            self.angles.append(signed(r[3] & 0xFFFF))
        elif address == 0x8006E398:
            self.yaw = signed(r[3] & 0xFFFF)
        elif address == TEXT + 0xA2D84:
            self.sounds.append(r[3])
        elif address == TEXT + 0x151AF0:
            self.decoded.append((r[3] - BLOCK - monitors.JPEGS) // monitors.JPEG_SLOT)
            self.t.assertEqual((r[4], r[5], r[6], r[7], r[8]), (0, BLOCK, 0, 0, BLOCK + monitors.WORK))
            put(cpu.mem, BLOCK + 0x9000, 0x1000 + self.decoded[-1])     # the picture, as the decoder leaves it
            r[3] = 1
        elif address == 0x80035C50:
            self.bound.add((r[3], r[4], r[5], r[6], r[7], r[8], r[9], r[10]))
        elif address == 0x8003462C:
            self.begins.append((r[3], r[4], r[5]))
        elif address == TEXT + 0x556B8:
            self.models.append(r[3])

    def frame(self, step, end=END):
        """One drawn frame with `step` the step about to run, the host's counter of a hand-back at `end` (0: none)."""
        put(self.mem, LEFT, max(0, end - step))
        self.point, self.yaw, self.angles, self.sounds, self.decoded = None, None, [], [], []
        self.bound, self.begins, self.models = set(), [], []
        put(self.mem, STEP, step)
        cpu = MonitorCpu(self.t.image, self.mem, self.game)
        cpu.r[1] = STACK
        cpu.run(self.t.entry, limit=400000)
        outside = {a for a in cpu.written if not (STACK - 0x1000 <= a < STACK + 8 or HB <= a < HB + 0x100
                                                   or PIPE <= a < PIPE + 4)}
        self.t.assertEqual(outside, set(), "the monitors write nothing but their stack, their state and the pipe")
        self.pipe = getattr(cpu, "pipe", 0)
        self.mem = cpu.mem
        return self.point is not None


def count(step):
    return END + 59 - step


class MonitorsBuild(Build):
    """The image, and where the monitors' routine is in it."""

    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        from keystone import Ks, KS_ARCH_PPC, KS_MODE_PPC32, KS_MODE_BIG_ENDIAN
        code = bytes(Ks(KS_ARCH_PPC, KS_MODE_PPC32 + KS_MODE_BIG_ENDIAN).asm(monitors.asm())[0])
        assert cls.image.count(code) == 1
        cls.entry = cls.image.index(code) + memmap.IMAGE


class TheMonitors(MonitorsBuild):

    def test_on_only_in_a_hand_back(self):
        race = Race(self)
        self.assertFalse(race.frame(END + 59 - 421))                     # before the start's 420 steps
        self.assertTrue(race.frame(END + 59 - 420))
        self.assertTrue(race.frame(END + 58))                            # GO, the last step
        self.assertFalse(race.frame(END + 59))
        fresh = Race(self)
        self.assertFalse(fresh.frame(END, end=0))                        # no hand-back
        self.assertFalse(fresh.frame(END + 10))                          # its counter at 0: none seen yet
        for blocked in (Race(self, state=3), Race(self, movie=1)):       # no pictures; the start's movie still on
            self.assertFalse(blocked.frame(END))
            self.assertEqual((blocked.sounds, blocked.models), ([], []))

    def test_the_pictures_follow_the_count(self):
        race, shown, decodes = Race(self), {}, []
        for step in range(END - 361, END + 59):
            self.assertTrue(race.frame(step))
            decodes += race.decoded
            shown[count(step)] = decodes[-1]
            self.assertEqual(race.bound, {(TEXOBJ[0x30], BLOCK, 128, 128, 6, 0, 0, 0),
                                          (TEXOBJ[0x38], BLOCK, 128, 128, 6, 0, 0, 0)})
        self.assertEqual(decodes, [0, 1, 2, 3, 4])                       # blank, 3, 2, 1, GO: each decoded once
        self.assertEqual({c for c, s in shown.items() if s == 1}, set(range(180, 240)))
        self.assertEqual({c for c, s in shown.items() if s == 4}, set(range(1, 60)))
        self.assertTrue(all(shown[c] == 0 for c in range(240, 421)))

    def test_a_rollback_that_puts_the_picture_back(self):
        race = Race(self)
        race.frame(END - 30)
        self.assertEqual(race.decoded, [3])                              # "1"
        race.frame(END - 29)
        self.assertEqual(race.decoded, [])
        put(race.mem, BLOCK + 0x9000, 0x1002)                            # the texture as it was before that decode
        race.frame(END - 28)
        self.assertEqual(race.decoded, [3])                              # decoded again

    def test_a_counter_off_by_a_step_is_the_same_hand_back(self):
        race, played, decodes = Race(self), [], []
        for step in range(END - 240, END + 59):
            race.frame(step, end=END + (step % 2))                       # the counter wobbles by one step
            played += race.sounds
            decodes += race.decoded
        self.assertEqual(played, [s for c, ids in monitors.SOUNDS if c < 300 for s in ids])
        self.assertEqual(decodes, [0, 1, 2, 3, 4])

    def test_the_sounds_play_once_each(self):
        expected = [s for _, ids in monitors.SOUNDS for s in ids]
        for stride in (1, 3, 7):
            race, played = Race(self), []
            for step in range(END - 361, END + 59, stride):
                race.frame(step)
                played += race.sounds
            self.assertEqual(played, expected, stride)
        self.assertEqual(expected[2], 0xA9060000)                        # the start's own "three"

    def test_placed_before_the_followed_racer(self):
        for yaw in (0.0, 1.1):
            race = Race(self, yaw)
            m = race.m
            fwd = [-m[i][2] for i in range(3)]
            up = [m[i][1] for i in range(3)]
            p = [m[i][3] for i in range(3)]
            key = lambda a, b: [p[i] + a * fwd[i] + b * up[i] for i in range(3)]
            for c, want, angle, yaw_now in ((420, key(1040, 9.5), -20, 36000), (390, key(540, 9.5), -20, 18000),
                                            (360, key(40, 9.5), -20, 0), (330, key(40, 9.5), -2.5, 0),
                                            (100, key(40, 9.5), 15, 0), (20, key(90, 59.5), 15, 12000)):
                race.frame(END + 59 - c)
                for got, w in zip(race.point, want):
                    self.assertAlmostEqual(got, w, delta=1e-3 * max(1.0, abs(w)))
                self.assertEqual(race.yaw, signed(yaw_now & 0xFFFF))
                tilt = int(single(single(182.04445) * single(angle)))
                self.assertEqual(race.angles[-1], tilt, c)
            turn = race.floats(HB + monitors.RECORD + 0x48, 12)          # a still machine: its own turn
            for got, w in zip(turn, [m[i][j] if j < 3 else 0.0 for i in range(3) for j in range(4)]):
                self.assertAlmostEqual(got, w, delta=1e-5)

    def test_the_draw_is_starter_disp(self):
        race = Race(self)
        race.frame(END - 1)
        self.assertEqual(race.begins, [])                                # no box: drawn before the scene, its
        self.assertEqual(race.pipe, 0)                                   # depth would hide the scene behind it
        self.assertEqual(race.models, [0x80A40000 + off * 0x100 for off in MODELS])


class TheEasedFrame(MonitorsBuild):
    """The monitors' frame follows the drawn matrix softly (monitors_ease.py): toward it without passing it, whole
    (unit, square, right-handed) every frame, at once where it must snap."""
    START = END - 300                                                    # a step with the monitors on

    def orthonormal(self, race):
        rows = race.turn()
        cols = [column(rows, j) for j in range(3)]
        for i in range(3):
            for j in range(3):
                self.assertAlmostEqual(dot(cols[i], cols[j]), float(i == j), delta=1e-4)
        r, u, b = cols
        self.assertAlmostEqual(dot([u[1] * b[2] - u[2] * b[1], u[2] * b[0] - u[0] * b[2],
                                    u[0] * b[1] - u[1] * b[0]], r), 1.0, delta=1e-4)   # right-handed: no flip

    def at_target(self, race, m, delta=1e-4):
        p, b, u = race.eased()
        for got, want in ((p, column(m, 3)), (b, column(m, 2)), (u, column(m, 1))):
            self.assertLess(apart(got, want), delta * max(1.0, max(abs(x) for x in want)))
        for got, want in zip(race.turn(), m):
            self.assertLess(apart(got, want[:3]), delta)

    def test_converges_and_never_overshoots(self):
        race = Race(self)
        race.frame(self.START)
        self.at_target(race, race.m)                                     # a hand-back's first frame: as drawn
        keep = 1.0 - monitors_ease.ALPHA
        move = (20.0, 3.0, -50.0)                                        # sideways, up, and ahead
        m = posed(0.6, 0.3, [100.0 + move[0], 5.0 + move[1], -30.0 + move[2]])
        race.place(m)
        tp, tb = column(m, 3), column(m, 2)
        error = [-x for x in move]
        along = dot(error, tb)
        side = [e - along * b for e, b in zip(error, tb)]                # ahead follows at once, the rest eases
        last_d, last_b, last_turn = apart(side, (0, 0, 0)), math.pi, math.pi
        for k in range(1, 91):
            race.frame(self.START + k)
            self.orthonormal(race)
            p, b, u = race.eased()
            d = apart(p, tp)
            gone_b = angle(b, tb)
            rows = race.turn()                                           # the whole turn still to go
            off = math.sqrt(sum(apart(rows[i], m[i][:3]) ** 2 for i in range(3)))   # |E - T| = 2.83 sin(a / 2)
            gone = 2 * math.asin(min(1.0, off / 8 ** 0.5))
            if k == 1:
                self.assertAlmostEqual(d, keep * last_d, delta=1e-3)
                self.assertGreater(gone_b, 0.4)                          # eased, not snapped
            self.assertLessEqual(d, last_d + 1e-4)
            self.assertLessEqual(gone_b, last_b + 1e-5)
            self.assertLessEqual(gone, last_turn + 1e-5)
            self.assertGreaterEqual(b[2] * tb[0] - b[0] * tb[2], -1e-5)  # turning toward it, never past it
            last_d, last_b, last_turn = d, gone_b, gone
        self.assertLess(last_d, 0.05)                                    # 1.5 s: 7.5 time constants
        self.assertLess(last_b, 0.01)
        self.assertLess(last_turn, 0.01)

    def eased_after(self, change, step=1, end=END, followed=FOLLOWED):
        """True when the frame drawn `step` steps after a still one is eased (not at the new matrix at once)."""
        race = Race(self)
        race.frame(self.START, end=END)
        change(race)
        race.followed = followed
        self.assertTrue(race.frame(self.START + step, end=end))
        self.orthonormal(race)
        p, b, u = race.eased()
        m = race.m
        return apart(p, column(m, 3)) > 1e-3 or apart(b, column(m, 2)) > 1e-4

    def test_snaps(self):
        aside = lambda d: (lambda race: race.place(posed(0.4, 0.0, (100.0 + d, 5.0, -30.0))))
        self.assertTrue(self.eased_after(aside(2.0)))                    # a rollback's correction
        self.assertTrue(self.eased_after(aside(250.0)))
        self.assertFalse(self.eased_after(aside(400.0)))                 # a respawn
        self.assertTrue(self.eased_after(aside(2.0), step=monitors_ease.GAP))
        self.assertFalse(self.eased_after(aside(2.0), step=monitors_ease.GAP + 1))   # a scrub forward
        self.assertFalse(self.eased_after(aside(2.0), step=-1))          # a restore
        self.assertFalse(self.eased_after(lambda race: race.place(posed(0.4), seat=1), followed=1))   # a new racer
        self.assertFalse(self.eased_after(aside(2.0), end=END + 50))    # a new hand-back
        self.assertFalse(self.eased_after(lambda race: race.place(posed(math.pi))))   # a half turn
        self.assertFalse(self.eased_after(lambda race: race.place(posed(0.0, math.pi))))
        self.assertTrue(self.eased_after(lambda race: race.place(posed(2.0, 1.5))))  # 115 degrees: eased

    def test_a_spin_stays_whole(self):
        race = Race(self)
        for k in range(120):                                             # a full turn in 2 s, rolling
            race.place(posed(k * math.pi / 60, 0.5 * math.sin(k / 5)))
            race.frame(self.START + k)
            self.orthonormal(race)


if __name__ == "__main__":
    unittest.main()
