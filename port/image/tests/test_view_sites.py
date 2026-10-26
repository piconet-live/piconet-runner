"""True widescreen's two sites (sites_view.py), run here as the bytes the image holds: the race view's fovy left
whole, and the aspect getter's Wide Screen value taken from the host's word. The game's code a routine leaves for is
a stand-in. No game data."""
import os, struct, sys, unittest

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, ".."))
import build, memmap, sites_view
from test_raceview import Build, Cpu, put, TEXT

MODE, TRUE_ASPECT = memmap.MODE, memmap.TRUE_ASPECT
FOVY, CUT = 55.0, 0.8660253882408142     # the base fovy; rodata 0x1D8 as the float it is
WIDE = struct.unpack(">f", struct.pack(">f", 16 / 9))[0]           # rodata 0x49F8, the getter's 16:9
bits = lambda x: struct.unpack(">I", struct.pack(">f", x))[0]


class TrueWidescreen(Build):
    def leave(self, routine, mode, aspect, f):
        """Runs a routine entered by a plain branch until it leaves for the game: where, and the machine."""
        left, memory = [], {}
        put(memory, MODE, mode)
        put(memory, TRUE_ASPECT, aspect)
        m = Cpu(self.image, memory, lambda address, cpu: left.append(address))
        m.f = [0.0] * 32
        for register, value in f.items():
            m.f[register] = value
        m.run(self.symbols[routine])
        return left, m

    def test_both_sites_are_plain_branches_to_their_routines(self):
        for (offset, word), name in ((sites_view.VIEW_SITE, "true_view"), (sites_view.ASPECT_SITE, "true_aspect")):
            p = self.patches[TEXT + offset]
            self.assertEqual(int(p["original"], 16), word)
            new = int(p["new"], 16)
            self.assertEqual(new & 3, 0, name)                                   # no link: the routine goes back
            self.assertEqual(build.branch_target(new, TEXT + offset), self.symbols[name])

    def test_the_view_keeps_its_whole_fovy_only_with_the_word_in_a_race(self):
        for mode, aspect, fovy in ((0, 0, FOVY * CUT), (0, bits(WIDE), FOVY * CUT),    # a plain start: the game's
                                   (1, 0, FOVY * CUT),                                # the game's Wide Screen
                                   (1, bits(WIDE), FOVY), (1, bits(21 / 9), FOVY)):  # true widescreen
            left, m = self.leave("true_view", mode, aspect, {25: FOVY, 0: CUT})
            self.assertEqual(left, [TEXT + sites_view.VIEW_SITE[0] + 4], (mode, aspect))
            self.assertAlmostEqual(m.f[25], fovy, places=4, msg=(mode, aspect))
            self.assertEqual(m.written, set(), (mode, aspect))

    def test_the_getter_gives_the_words_aspect_and_at_16_9_the_games_own_bits(self):
        for mode, aspect, given in ((0, bits(2.37), WIDE), (1, 0, WIDE), (1, bits(WIDE), WIDE),
                                    (1, bits(2.37), struct.unpack(">f", struct.pack(">f", 2.37))[0])):
            left, m = self.leave("true_aspect", mode, aspect, {1: WIDE})
            self.assertEqual(left, [TEXT + sites_view.ASPECT_RETURN], (mode, aspect))
            self.assertEqual(bits(m.f[1]), bits(given), (mode, aspect))
            self.assertEqual(m.written, set(), (mode, aspect))


if __name__ == "__main__":
    unittest.main()
