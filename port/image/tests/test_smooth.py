"""A rival on the road is drawn at its true height (smooth.py), run here as the bytes the image holds against a
made-up race of two machines: the offset a rollback's correction is faded out with keeps only its part along the
road for a machine on the road, and all of it for a machine in the air. No game data."""
import os, struct, sys, unittest

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, ".."))
import build, memmap  # noqa: F401  (build puts the generators' folder on the path)
import native, smooth
from test_race_end import BSS, MODULE, MACHINES, POINTER
from test_raceview import Build, Cpu, get, put, CONTROL, IMAGE, LOCAL

SMOOTH, LOG = memmap.SMOOTH, memmap.SMOOTH_LOG
GLOBALS = SMOOTH + 0x600
COUNT, PHASE = BSS + 0x6D84A, BSS + 0x3C2A
RIVAL = 0                                         # the other machine; this game's own is LOCAL
AIR = 0x2                                         # flags +0x0: all four hover corners are off the road


def floats(memory, address, n=3):
    return struct.unpack(f">{n}f", bytes(memory.get(address + i, 0) for i in range(4 * n)))


def put_floats(memory, address, *values):
    memory.update({address + i: b for i, b in enumerate(struct.pack(f">{len(values)}f", *values))})


class TheRivalsHeight(Build):
    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        from keystone import Ks, KS_ARCH_PPC, KS_MODE_PPC32, KS_MODE_BIG_ENDIAN
        text = smooth._draw()                                        # the part that names nothing outside itself
        code = bytes(Ks(KS_ARCH_PPC, KS_MODE_PPC32 + KS_MODE_BIG_ENDIAN).asm(text)[0])
        assert cls.image.count(code) == 1
        cls.draw = IMAGE + cls.image.index(code) + native.label_offsets(text)["smooth_draw"]

    def frame(self, offset, up, flags=0, rollbacks=0, step=1000, expected=None, tests=0, hook=False, since=1, logs=1):
        """One displayed frame of `smooth_draw` for the rival: it stands at (100, 20, 300), was drawn with `offset`
        last frame and has moved (1, 0, 2) in this frame's one step. Gives its offset, where it is drawn, the log.
        `since`: the steps since the frame before (0: a frame the time sync held, its step run again)."""
        m = {}
        put(m, 0x800030C8, MODULE); put(m, POINTER, MACHINES); m[COUNT] = LOCAL + 1
        put(m, PHASE, 0x29, 2); put(m, CONTROL + 116, 1); put(m, CONTROL + 120, 1); put(m, CONTROL + 124, step)
        put(m, CONTROL + 136, rollbacks); put(m, CONTROL + 456, tests); put(m, memmap.IMAGE_LOGS, logs)
        put(m, GLOBALS, step - since); put(m, GLOBALS + 8, 7)        # the step of the frame before; frames so far
        machine, seat = MACHINES + RIVAL * 0x620, SMOOTH + RIVAL * 48
        put(m, machine, flags)
        put_floats(m, machine + 0x7C, 100.0, 20.0, 300.0); put_floats(m, machine + 0x88, 99.0, 20.0, 298.0)
        for k, value in enumerate((100.0, 20.0, 300.0)):
            put_floats(m, machine + 0x158 + 0x10 * k, value)
        put_floats(m, machine + 0x1BC, *up)
        put_floats(m, seat + 12, *offset)
        last = expected or tuple(p - d + o for p, d, o in zip((100.0, 20.0, 300.0), (1.0, 0.0, 2.0), offset))
        put_floats(m, seat + 24, *last)                              # where it was drawn last frame
        m[seat + 37] = 1
        cpu = Cpu(self.image, m)
        if hook:                                                     # as the task manager's pass reaches list 10
            put(cpu.mem, CONTROL + 460, 1)                           # the frame's last step is about to run
            cpu.r[27], cpu.r[30], cpu.r[31], stack = 10, 0x30303030, 0x31313131, cpu.r[1]
            cpu.run(self.symbols["smooth_hook"], limit=20000)
            self.assertEqual((cpu.r[0], cpu.r[1], cpu.r[30], cpu.r[31]), (1, stack, 0x30303030, 0x31313131))
            self.assertEqual(get(cpu.mem, CONTROL + 460), 0)
            cpu.written = {a for a in cpu.written if not stack - 32 <= a < stack + 8}
        else:
            cpu.r[31], cpu.r[30] = CONTROL, BSS
            cpu.run(self.draw, limit=20000)
        drawn = tuple(floats(cpu.mem, machine + 0x158 + 0x10 * k, 1)[0] for k in range(3))
        entry = bytes(cpu.mem.get(LOG + 8 * 8 + i, 0) for i in range(8))             # frame 8 of the log
        # written: the data window, and of the game's memory only the machines' drawn translations
        self.assertEqual({(a - MACHINES) % 0x620 for a in cpu.written if a < IMAGE}, {0x158, 0x168, 0x178})
        return floats(cpu.mem, seat + 12), drawn, entry

    def along(self, offset, up):
        return sum(o * u for o, u in zip(offset, up))

    def test_a_machine_on_the_road_keeps_its_height(self):
        for up in ((0.0, 1.0, 0.0), (0.6, 0.8, 0.0), (0.0, -1.0, 0.0), (0.0, 0.6, -0.8)):   # flat, banked, a loop's top
            offset, drawn, entry = self.frame((3.0, 4.0, -2.0), up)
            faded = tuple(0.85 * v for v in (3.0, 4.0, -2.0))        # a frame without a rollback: it fades
            cut = self.along(faded, up)
            self.assertAlmostEqual(self.along(offset, up), 0.0, places=5, msg=up)        # nothing along the up vector
            for got, was, u in zip(offset, faded, up):
                self.assertAlmostEqual(got, was - cut * u, places=5, msg=up)             # the rest is as it was
            for got, at, o in zip(drawn, (100.0, 20.0, 300.0), offset):
                self.assertAlmostEqual(got, at + o, places=4)
            self.assertEqual((struct.unpack(">H", entry[:2])[0], entry[2], entry[3]), (1000, RIVAL, 1))
            self.assertEqual(entry[4:6], struct.pack(">f", abs(cut))[:2])                # the upper half of the float
            self.assertEqual(entry[6:], bytes(2))                                        # the shadows' task's field

    def test_the_task_managers_hook_draws_once_at_list_10(self):
        direct = self.frame((3.0, 4.0, -2.0), (0.6, 0.8, 0.0))
        self.assertEqual(self.frame((3.0, 4.0, -2.0), (0.6, 0.8, 0.0), hook=True), direct)
        for task_list, marked in ((10, 0), (9, 1), (11, 1), (20, 1)):        # not marked, or another list: nothing
            m = {}
            put(m, CONTROL + 460, marked)
            cpu = Cpu(self.image, m)
            cpu.r[27] = task_list
            cpu.run(self.symbols["smooth_hook"])
            self.assertEqual((cpu.r[0], cpu.written, get(cpu.mem, CONTROL + 460)), (1, set(), marked), task_list)

    def test_a_held_frame_keeps_the_offset(self):
        # the time sync held the frame: the same step was run again, so the machine is where it was, and it was
        # drawn at that place plus its offset. It is a frame like any other: the offset fades on, nothing snaps
        at, offset, air = (100.0, 20.0, 300.0), (3.0, 4.0, -2.0), 0x2
        drawn_before = tuple(a + o for a, o in zip(at, offset))
        got, drawn, entry = self.frame(offset, (0.0, 1.0, 0.0), flags=air, since=0, expected=drawn_before)
        for g, o, d, a in zip(got, offset, drawn, at):
            self.assertAlmostEqual(g, 0.85 * o, places=5)
            self.assertAlmostEqual(d, a + 0.85 * o, places=4)
        self.assertEqual(entry[3] & 1, 1)                            # the log: not a frame that started over
        # with a rollback in it (the memory went back a step): drawn exactly where it was drawn before
        got, drawn, entry = self.frame(offset, (0.0, 1.0, 0.0), flags=air, since=0, expected=drawn_before, rollbacks=4)
        self.assertEqual((tuple(round(v, 4) for v in got), tuple(round(v, 4) for v in drawn), entry[3] & 3), (offset, drawn_before, 3))
        # on the road the same, less the part along the up vector
        got, drawn, entry = self.frame(offset, (0.0, 1.0, 0.0), since=0, expected=drawn_before, rollbacks=4)
        self.assertEqual(tuple(round(v, 4) for v in drawn), (103.0, 20.0, 298.0))
        # a frame more than 30 steps on still starts over, and so does a rival without a frame before
        got, drawn, entry = self.frame(offset, (0.0, 1.0, 0.0), flags=air, since=31)
        self.assertEqual((got, drawn, entry[3] & 1), ((0.0, 0.0, 0.0), at, 0))
        got, drawn, entry = self.frame(offset, (0.0, 1.0, 0.0), flags=air, since=30)
        self.assertEqual(entry[3] & 1, 1)

    def test_a_players_game_keeps_no_log(self):
        # the logs' switch is 0: the drawing is the same, and the log's entry is not written
        with_log = self.frame((3.0, 4.0, -2.0), (0.6, 0.8, 0.0))
        offset, drawn, entry = self.frame((3.0, 4.0, -2.0), (0.6, 0.8, 0.0), logs=0)
        self.assertEqual((offset, drawn, entry), (with_log[0], with_log[1], bytes(8)))

    def test_what_is_not_a_number_snaps_or_cuts_nothing(self):
        nan = float("nan")
        # an offset that is not a number (it would stay so for good): the rival is drawn where it is, with none
        offset, drawn, entry = self.frame((nan, 4.0, -2.0), (0.0, 1.0, 0.0), flags=0x2, expected=(100.0, 20.0, 300.0))
        self.assertEqual((offset, drawn), ((0.0, 0.0, 0.0), (100.0, 20.0, 300.0)))
        offset, drawn, entry = self.frame((0.0, 0.0, 0.0), (0.0, 1.0, 0.0), rollbacks=2, expected=(nan, 26.0, 298.0))
        self.assertEqual((offset, drawn), ((0.0, 0.0, 0.0), (100.0, 20.0, 300.0)))
        # an up vector that is not a number: nothing is cut, the offset stays a number
        offset, drawn, entry = self.frame((3.0, 4.0, -2.0), (nan, 1.0, 0.0))
        for got, was in zip(offset, (3.0, 4.0, -2.0)):
            self.assertAlmostEqual(got, 0.85 * was, places=5)

    def test_a_machine_in_the_air_keeps_the_whole_offset(self):
        offset, drawn, entry = self.frame((3.0, 4.0, -2.0), (0.0, 1.0, 0.0), flags=AIR)
        for got, was in zip(offset, (3.0, 4.0, -2.0)):
            self.assertAlmostEqual(got, 0.85 * was, places=5)
        self.assertAlmostEqual(drawn[1], 20.0 + 0.85 * 4.0, places=4)
        self.assertEqual(entry[4:], bytes(4))                        # nothing was cut

    def test_a_rollbacks_correction_is_cut_too(self):
        # the machine was expected 6 above and 5 beside where the rollback put it: it is drawn 5 beside, on the road
        offset, drawn, entry = self.frame((0.0, 0.0, 0.0), (0.0, 1.0, 0.0), rollbacks=3, expected=(104.0, 26.0, 298.0))
        self.assertEqual(tuple(round(v, 4) for v in offset), (5.0, 0.0, 0.0))
        self.assertEqual(tuple(round(v, 4) for v in drawn), (105.0, 20.0, 300.0))
        self.assertEqual((entry[3], entry[4:]), (3, struct.pack(">f", 6.0)[:2] + bytes(2)))   # a rollback frame; 6 cut

    def test_the_tests_bits_give_the_drawing_as_it_was(self):
        # +456 bit 2: the part along the up vector stays (the log then says what is drawn); the frame's flag 8
        offset, drawn, entry = self.frame((3.0, 4.0, -2.0), (0.0, 1.0, 0.0), tests=2)
        for got, was in zip(offset, (3.0, 4.0, -2.0)):
            self.assertAlmostEqual(got, 0.85 * was, places=5)
        self.assertEqual((entry[3], entry[4:6]), (1 | 8, struct.pack(">f", 0.85 * 4.0)[:2]))
        # bit 4 is the shadows' (flag 16) and changes nothing here; bit 1 is no smoothing at all (flag 4)
        offset, drawn, entry = self.frame((3.0, 4.0, -2.0), (0.0, 1.0, 0.0), tests=4)
        self.assertEqual((entry[3], round(offset[1], 5)), (1 | 16, 0.0))
        offset, drawn, entry = self.frame((3.0, 4.0, -2.0), (0.0, 1.0, 0.0), tests=1)
        self.assertEqual((entry[3], offset, drawn), (1 | 4, (0.0, 0.0, 0.0), (100.0, 20.0, 300.0)))


if __name__ == "__main__":
    unittest.main()
