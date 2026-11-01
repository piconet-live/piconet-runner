"""The seats' boost trail colours (sites_trail.py), run here as the bytes the image holds against a made-up race block:
a seat with words takes them, a seat without and a plain start do what the game did. No game data."""
import os, struct, sys, unittest

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, ".."))
from test_raceview import Build, Cpu, get, put, TEXT
import memmap

TABLE = memmap.RACE + memmap.RACE_TRAILS
RECORD, BLOCK, EFFECT = 0x80C10000, 0x80C20000, 0x80C30000
SEAT = 7
NORMAL, BOOST = 0x01FF8000, 0x010040C0
OWN = 0x12345678    # what a record's float holds before


def floats(m, at):
    return [struct.unpack(">f", struct.pack(">I", get(m.mem, at + 4 * i)))[0] for i in range(3)]


class TheSeatsTrails(Build):
    def race(self, seat=SEAT, mode=1, normal=NORMAL, boost=BOOST):
        memory = {}
        put(memory, memmap.MODE, mode)
        e = TABLE + SEAT * memmap.TRAIL_ENTRY
        put(memory, e, normal)
        put(memory, e + 4, boost)
        put(memory, e + 8, boost)
        put(memory, BLOCK + 4, seat, 2)
        put(memory, EFFECT + 0x18, seat, 2)
        for at in range(0x28, 0x40, 4):
            put(memory, RECORD + at, OWN)
        return memory

    def run_site(self, routine, memory, floats_in=(), **registers):
        left = []
        m = Cpu(self.image, memory, lambda address, cpu: left.append(address))
        m.f = [0.0] * 32
        m.f[:len(floats_in)] = floats_in
        for name, value in registers.items():
            m.r[int(name[1:])] = value
        m.run(self.symbols[routine])
        return left, m

    def record(self, memory):
        return self.run_site("trail_record", memory, r3=RECORD, r30=RECORD, r31=BLOCK)

    def streak(self, memory):
        return self.run_site("trail_streak", memory, floats_in=(0.5, 0.25, 0, 0.5, 0.75), r31=EFFECT)

    def test_a_record_takes_the_seats_colours(self):
        left, m = self.record(self.race())
        self.assertEqual(left, [TEXT + 0x98884])
        self.assertEqual(floats(m, RECORD + 0x28), [255, 128, 0])
        self.assertEqual(floats(m, RECORD + 0x34), [0, 64, 192])
        self.assertEqual(m.r[3], get(m.mem, RECORD + 0x28))   # the word it replaced: the colour the game copies on
        self.assertEqual(m.r[1], 0x80C00000)                  # the stack as it was

    def test_each_colour_stands_alone(self):
        _, m = self.record(self.race(normal=0))
        self.assertEqual(get(m.mem, RECORD + 0x28), OWN)
        self.assertEqual(floats(m, RECORD + 0x34), [0, 64, 192])
        _, m = self.record(self.race(boost=0))
        self.assertEqual(floats(m, RECORD + 0x28), [255, 128, 0])
        self.assertEqual(get(m.mem, RECORD + 0x34), OWN)

    def test_the_game_own_where_none_is_set(self):
        for memory in (self.race(mode=0), self.race(seat=3), self.race(seat=30), self.race(seat=0xFFFF)):
            left, m = self.record(memory)
            self.assertEqual(left, [TEXT + 0x98884])
            self.assertEqual(m.r[3], OWN)
            self.assertEqual([get(m.mem, RECORD + a) for a in range(0x28, 0x40, 4)], [OWN] * 6)

    def test_the_streak_is_the_seats_word(self):
        left, m = self.streak(self.race())
        self.assertEqual(left, [TEXT + 0x6C3CC])
        self.assertEqual(m.r[1], 0x80C00000)
        self.assertAlmostEqual(m.f[1], 0, places=5)
        self.assertAlmostEqual(m.f[3], 64 / 255, places=5)
        self.assertAlmostEqual(m.f[4], 192 / 255, places=5)
        self.assertEqual(m.f[0], 0.5)                         # the game's scale, kept over the call

    def test_the_streak_keeps_the_machines_where_none_is_set(self):
        for memory in (self.race(mode=0), self.race(seat=3), self.race(boost=0)):
            left, m = self.streak(memory)
            self.assertEqual(left, [TEXT + 0x6C3CC])
            self.assertEqual((m.f[1], m.f[3], m.f[4], m.f[2]), (0.25, 0.5, 0.75, 0.125))   # f2 = f1 x f0, the game's word

if __name__ == "__main__":
    unittest.main()
