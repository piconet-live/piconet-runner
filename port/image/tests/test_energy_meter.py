"""The energy meter's getter under a mode's max energy (sites_energy.py), run here as the bytes the image holds: at the game's 100
and in a plain start the game's own bits; otherwise energy x 100 / max, a machine's own; only the return registers change. No game data."""
import os, struct, sys, unittest

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, ".."))
from test_raceview import Build, Cpu, put, TEXT
import memmap
import sites_energy

MACHINE = 0x80D00000
STACK = 0x80E00000
single = lambda x: struct.unpack(">f", struct.pack(">f", x))[0]
bits = lambda x: struct.unpack(">I", struct.pack(">f", x))[0]


class TheMeter(Build):
    def meter(self, energy, most, mode=1):
        memory = {}
        put(memory, memmap.MODE, mode)
        put(memory, MACHINE + sites_energy.ENERGY, energy)
        put(memory, MACHINE + sites_energy.FULL, most, 2)
        left = []
        m = Cpu(self.image, memory, lambda address, cpu: left.append(address))
        m.f = [0.0] * 32
        m.f[1] = 123.0
        m.r[3], m.r[1] = MACHINE, STACK
        m.r[4] = 0x4444
        m.run(self.symbols["meter_energy"])
        self.assertEqual(left, [TEXT + sites_energy.GETTER + 4])
        self.assertEqual((m.r[1], m.r[3], m.r[4]), (STACK, MACHINE, 0x4444))
        return m.f[1]

    def test_the_site_is_the_getters_load(self):
        p = self.patches[TEXT + sites_energy.GETTER]
        self.assertEqual((p["original"], p["what"]), ("C0230184", "meter_energy"))

    def test_at_100_the_getter_gives_the_games_bits(self):
        for energy in (bits(100.0), bits(0.01), bits(33.3), bits(101.0), 0x3F800001, 0):
            self.assertEqual(bits(self.meter(energy, 100)), energy)

    def test_a_plain_start_is_the_games_own_whatever_the_field(self):
        self.assertEqual(bits(self.meter(bits(33.3), 300, mode=0)), bits(33.3))

    def test_other_values_give_the_share_of_a_hundred(self):
        self.assertEqual(self.meter(bits(150.0), 150), 100.0)
        self.assertEqual(self.meter(bits(150.0), 300), 50.0)
        self.assertEqual(self.meter(bits(25.0), 50), 50.0)
        self.assertEqual(self.meter(bits(1.0), 1), 100.0)
        self.assertEqual(self.meter(bits(0.0), 1000), 0.0)
        # one float product, one float quotient
        self.assertEqual(self.meter(bits(33.3), 150), single(single(single(33.3) * 100.0) / 150.0))


if __name__ == "__main__":
    unittest.main()
