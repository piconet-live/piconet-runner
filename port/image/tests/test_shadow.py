"""A rival's shadow follows its drawn body (sites_shadow.py), run here as the bytes the image holds against a
made-up machine and shadow record at the place fn_1_56D4C has just written. No game data."""
import os, struct, sys, unittest

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, ".."))
import build, memmap
from test_race_end import MODULE, MACHINES, POINTER
from test_raceview import Build, Cpu, get, put, CONTROL, IMAGE, TEXT
from test_smooth import floats, put_floats

SMOOTH, LOG, GLOBALS = memmap.SMOOTH, memmap.SMOOTH_LOG, memmap.SMOOTH + 0x600
RECORD = 0x80D00000                               # a shadow's record; its matrix's place is +0x18, +0x28, +0x38
SEAT, FRAME = 3, 77                               # the rival's seat; the frames smooth.py has counted
RACE, CONTACT = (100.0, 20.0, 300.0), (100.5, 17.0, 299.0)   # the machine's place in the race; its point on the road
SITE = TEXT + build.SHADOW_PLACED[0]


class TheShadowsPlace(Build):
    def place(self, offset, up, mode=1, written=1, tests=0, seat=SEAT, largest=0.0, registers=None, logs=1):
        """The routine for the machine of `seat`, which smooth.py has drawn `offset` from where it is: the record's
        place after it, the log's field, the frame's largest square, the reader."""
        m, left = {}, []
        machine, entry = MACHINES + seat * 0x620, SMOOTH + seat * 48
        put(m, memmap.MODE, mode); put(m, 0x800030C8, MODULE); put(m, POINTER, MACHINES)
        put(m, CONTROL + 456, tests); put(m, GLOBALS + 8, FRAME); put_floats(m, GLOBALS + 56, largest)
        put(m, memmap.IMAGE_LOGS, logs)
        put_floats(m, machine + 0x1BC, *up); put_floats(m, machine + 0x1D4, *CONTACT)
        put_floats(m, entry, *RACE); put_floats(m, entry + 12, *offset)
        m[entry + 36] = written
        for i in range(3):
            put_floats(m, machine + 0x158 + 0x10 * i, RACE[i] + (offset[i] if written else 0.0))
            put_floats(m, RECORD + 0x18 + 0x10 * i, CONTACT[i])                      # as fn_1_56D4C leaves it
        cpu = Cpu(self.image, m, lambda address, c: left.append(address))
        cpu.f = [float(i) + 0.1 for i in range(32)]                                  # doubles the game may hold
        cpu.r[29], cpu.r[30], cpu.r[31] = RECORD, machine, 0x80E00000
        for number, value in (registers or {}).items():
            cpu.r[number] = value
        put_floats(cpu.mem, 0x80E00000 + 0x8C, 0.25)                                 # what the replaced word loads
        stack = cpu.r[1]
        cpu.run(self.symbols["shadow_follow"])
        self.assertEqual(left, [SITE + 4])
        self.assertEqual((cpu.r[1], cpu.r[29], cpu.r[30], cpu.r[31]), (stack, RECORD, machine, 0x80E00000))
        self.assertEqual(cpu.f[0], 0.25)                                             # `lfs f0, 0x8c(r31)`, done last
        # the float registers: f0 is the replaced instruction's, f1 the routine's to use (the game writes it at
        # 0x5717C before reading it); every other one holds its value, f2 to f5 kept on the stack and given back
        self.assertEqual(cpu.f[2:], [float(i) + 0.1 for i in range(2, 32)])
        # written: the record's place, the routine's own stack, the tests' log; no word of a machine's block
        game = {a for a in cpu.written if a < IMAGE and not stack - 64 <= a < stack}
        self.assertLessEqual(game, {RECORD + 0x18, RECORD + 0x28, RECORD + 0x38})
        placed = tuple(floats(cpu.mem, RECORD + 0x18 + 0x10 * i, 1)[0] for i in range(3))
        field = get(cpu.mem, LOG + 8 * FRAME + 6, 2)
        return placed, field, floats(cpu.mem, GLOBALS + 56, 1)[0], cpu

    def half(self, value):
        return struct.unpack(">H", struct.pack(">f", value)[:2])[0]

    def test_the_site_is_where_the_shadow_has_its_place(self):
        p = self.patches[SITE]
        self.assertEqual((int(p["original"], 16), int(p["new"], 16) & 1), (build.SHADOW_PLACED[1], 0))   # a plain branch
        self.assertEqual(build.branch_target(int(p["new"], 16), SITE), self.symbols["shadow_follow"])

    def test_on_the_road_the_shadow_moves_as_the_body(self):
        # smooth.py leaves a machine on the road an offset along the road only: flat, banked, a loop's top
        for up, offset in (((0.0, 1.0, 0.0), (3.0, 0.0, -2.0)), ((0.6, 0.8, 0.0), (4.0, -3.0, 1.5)), ((0.0, -1.0, 0.0), (-2.0, 0.0, 6.0))):
            placed, field, largest, _ = self.place(offset, up)
            for got, at, o in zip(placed, CONTACT, offset):
                self.assertAlmostEqual(got, at + o, places=4, msg=up)
            self.assertLess(largest, 1e-6)                           # body and shadow are as far apart as the game has them
            self.assertEqual(field, 0)

    def test_in_the_air_the_shadow_stays_on_the_road_under_the_drawn_body(self):
        up, offset = (0.0, 0.6, 0.8), (3.0, 5.0, -2.0)               # a banked road; the body is drawn off it too
        d = sum(o * u for o, u in zip(offset, up))
        placed, field, largest, _ = self.place(offset, up)
        for got, at, o, u in zip(placed, CONTACT, offset, up):
            self.assertAlmostEqual(got, at + o - d * u, places=4)
        self.assertAlmostEqual(sum((p - c) * u for p, c, u in zip(placed, CONTACT, up)), 0.0, places=4)   # still on the road
        self.assertLess(largest, 1e-6)

    def test_the_tests_bit_leaves_the_shadow_and_the_log_says_how_far(self):
        up, offset = (0.0, 1.0, 0.0), (3.0, 5.0, -4.0)
        placed, field, largest, _ = self.place(offset, up, tests=4)
        self.assertEqual(placed, CONTACT)
        self.assertAlmostEqual(largest, 25.0, places=3)              # 3 and 4 along the road: 5 apart, squared
        self.assertEqual(field, self.half(largest))
        # the frame's largest stays: a smaller one after it writes nothing
        placed, field, largest, cpu = self.place(offset, up, tests=4, largest=30.0)
        self.assertEqual((largest, field), (30.0, 0))
        self.assertNotIn(LOG + 8 * FRAME + 6, cpu.written)

    def test_nothing_for_a_machine_drawn_where_it_is(self):
        # this game's own machine and every machine outside a race of ours: smooth.py's byte is 0
        for case in (dict(written=0), dict(mode=0), dict(seat=29, written=0)):
            placed, field, largest, cpu = self.place((3.0, 0.0, -2.0), (0.0, 1.0, 0.0), **case)
            self.assertEqual((placed, cpu.written), (CONTACT, set()), case)
        # a record whose machine is none of the thirty (no race's machines): nothing either
        m = {}
        put(m, memmap.MODE, 1); put(m, 0x800030C8, MODULE); put(m, POINTER, MACHINES)
        left = []
        cpu = Cpu(self.image, m, lambda address, c: left.append(address))
        cpu.r[29], cpu.r[30] = RECORD, MACHINES + 30 * 0x620
        cpu.run(self.symbols["shadow_follow"])
        self.assertEqual((left, cpu.written), ([SITE + 4], set()))
        put(m, POINTER, 0)
        cpu = Cpu(self.image, m, lambda address, c: left.append(address))
        cpu.r[29], cpu.r[30] = RECORD, MACHINES
        cpu.run(self.symbols["shadow_follow"])
        self.assertEqual(cpu.written, set())

    def test_a_players_game_keeps_no_log(self):
        # the logs' switch is 0: the shadow moves as ever, and nothing of the log or its largest is written
        up, offset = (0.0, 1.0, 0.0), (3.0, 5.0, -4.0)
        placed, field, largest, cpu = self.place(offset, up, logs=0)
        self.assertAlmostEqual(placed[0], CONTACT[0] + 3.0, places=4)
        self.assertEqual({a for a in cpu.written if a >= IMAGE}, set())
        placed, field, largest, cpu = self.place(offset, up, logs=0, tests=4)
        self.assertEqual((placed, field, largest, {a for a in cpu.written if a >= IMAGE}), (CONTACT, 0, 0.0, set()))

    def test_the_last_seat_is_found(self):
        placed, field, largest, _ = self.place((1.0, 0.0, 2.0), (0.0, 1.0, 0.0), seat=29)
        self.assertAlmostEqual(placed[0], CONTACT[0] + 1.0, places=4)


if __name__ == "__main__":
    unittest.main()
