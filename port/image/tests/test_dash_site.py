"""The dash plate's boost as data (sites_dash.py), run here as the bytes the image holds: the site's routine gives
int(A + B x D) from the machine's seat's A and B, the game's own 0 and 30 give the game's own frames for every D the
stat ranges allow, and GX Unleashed's 30 and 15 give its own formula. No game data."""
import os, struct, sys, unittest

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, ".."))
from test_raceview import Build, Cpu, put, TEXT
import memmap
import sites_dash

MACHINE = 0x80D00000
single = lambda x: struct.unpack(">f", struct.pack(">f", x))[0]
bits = lambda x: struct.unpack(">I", struct.pack(">f", x))[0]


class TheDashPlate(Build):
    def frames(self, duration, base, scale, mode=1, seat=7):
        """f0 after the site, with the game's registers as they stand there: f0 = single(60 x D), f2 = 0.5. The
        machine is seat `seat`'s; every other seat has other numbers."""
        memory = {}
        put(memory, memmap.MODE, mode)
        for k in range(30):
            at = memmap.SEAT_BLOCK + memmap.SEAT_CONSTANTS + k * memmap.SEAT_CONSTANT
            put(memory, at + memmap.SEAT_DASH_BASE, bits(base if k == seat else 7.0))
            put(memory, at + memmap.SEAT_DASH_SCALE, bits(scale if k == seat else 3.0))
        put(memory, MACHINE + 4, seat, 2)
        put(memory, MACHINE + sites_dash.DURATION, bits(duration))
        left = []
        m = Cpu(self.image, memory, lambda address, cpu: left.append(address))
        m.f = [0.0] * 32
        m.f[0], m.f[2], m.r[30] = single(60.0 * single(duration)), 0.5, MACHINE
        m.run(self.symbols["dash_plate"])
        self.assertEqual(left, [TEXT + sites_dash.SITE + 4])
        return m.f[0]

    def test_the_site_is_the_dash_plates_multiply(self):
        p = self.patches[TEXT + sites_dash.SITE]
        self.assertEqual((p["original"], p["what"]), ("EC020032", "dash_plate"))

    def test_the_games_own_numbers_give_the_games_own_frames(self):
        # every float D of the stat ranges' bounds and a sweep of the disc's kind of values: the same double, so
        # the same int(), as 0.5 x single(60 x D)
        cases = [0.0, 2 ** -126, 0.3, 0.5, 1.0, 1.1, 1.25, 1.5, 1.7, 2.0, 2.2, 3.33, 4.0, single(4.26) - 2 ** -21]
        cases += [single(k / 997.0) for k in range(1, 4250)]
        for d in cases:
            d = single(d)
            stock = single(0.5 * single(60.0 * d))
            self.assertEqual(struct.pack(">d", self.frames(d, 0.0, 30.0)), struct.pack(">d", stock), d)

    def test_gx_unleasheds_numbers_give_its_formula(self):
        for d in (0.5, 1.0, 1.5, 2.0, 1.2345):
            d = single(d)
            self.assertEqual(self.frames(d, 30.0, 15.0), 30.0 + single(15.0 * d))
        self.assertEqual([int(self.frames(single(d), 30.0, 15.0)) for d in (1.0, 1.5, 2.0)], [45, 52, 60])

    def test_each_seat_reads_its_own(self):
        self.assertEqual(self.frames(single(1.0), 30.0, 15.0, seat=0), 45.0)
        self.assertEqual(self.frames(single(1.0), 30.0, 15.0, seat=29), 45.0)
        # an index outside the seats: the game's own instruction
        self.assertEqual(self.frames(single(1.5), 30.0, 15.0, seat=30), single(0.5 * single(60.0 * single(1.5))))

    def test_a_plain_start_is_the_games_own_instruction(self):
        # mode 0: the replaced word with the registers as the game left them, whatever the race block holds
        self.assertEqual(self.frames(single(1.5), 30.0, 15.0, mode=0), single(0.5 * single(60.0 * single(1.5))))


if __name__ == "__main__":
    unittest.main()
