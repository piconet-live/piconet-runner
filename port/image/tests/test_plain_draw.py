"""A plain start (offline play) with the host's PLAIN_DRAW set: the mode machine's routine makes the frame tick and
the draw half stores DRAW_ASK = 1 and 2, as in a race, and nothing else (sites.py mode_machine, sites_present.py);
with the word 0, every one of these sites does what the game does, as before. The routines run here as the bytes the
image holds; the game's code they leave for is a stand-in. No game data."""
import os, sys, unittest

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, ".."))
import memmap, sites_present
from test_raceview import Build, Cpu, put, TEXT

ECC = TEXT + 0xECC
DRAW = TEXT + sites_present.DRAW[1]                 # the draw half's first call, fn_1_3CC4 (or the hfr routine)
DEPTH, DEPTH_BACK = TEXT + sites_present.DEPTH[1], TEXT + sites_present.DEPTH[0] + 4


class Saves(Cpu):
    """Cpu, with the stack's stmw and lmw of the mode machine's routine."""

    def other(self, pc, w):
        op, d, a = w >> 26, (w >> 21) & 31, (w >> 16) & 31
        if op in (46, 47) and memmap.IMAGE <= pc < memmap.IMAGE + len(self.image):
            at = (self.r[a] + (w & 0xFFFF if w & 0x8000 == 0 else (w & 0xFFFF) - 0x10000)) & 0xFFFFFFFF
            for n in range(d, 32):
                if op == 47:
                    self.write(at + 4 * (n - d), 4, self.r[n])
                else:
                    self.r[n] = self.read(at + 4 * (n - d), 4)
            return pc + 4
        return super().other(pc, w)


class PlainDraw(Build):
    def go(self, routine, mode, plain, skip=0):
        """Runs a routine until it returns or leaves for the game: where it went, in order, and what it wrote."""
        went, memory = [], {}
        put(memory, memmap.MODE, mode)
        put(memory, memmap.PLAIN_DRAW, plain)
        put(memory, memmap.DRAW_SKIP, skip)
        m = Saves(self.image, memory, lambda address, cpu: went.append(address))
        m.run(self.symbols[routine])
        asked = [a for a in sorted(m.written) if a == memmap.DRAW_ASK]
        return went, asked, m

    def test_the_mode_machine_ticks_in_a_plain_start_only_with_the_word(self):
        went, _, _ = self.go("mode_machine", 0, 0)
        self.assertEqual(went, [ECC + 4])                                    # the stock instruction, and on
        went, _, m = self.go("mode_machine", 0, 1)
        self.assertEqual(went, [memmap.HOST_TICK, ECC + 4])                 # the tick, then the stock instruction
        self.assertNotIn(self.symbols["boot_race"], went)

    def test_the_draw_half_asks_in_a_plain_start_only_with_the_word(self):
        went, asked, _ = self.go("present_draw", 0, 0)
        self.assertEqual(asked, [])
        self.assertNotIn(memmap.HOST_TICK, went)
        went, asked, m = self.go("present_draw", 0, 1, skip=1)              # the host's answer is never read here
        self.assertEqual(asked, [memmap.DRAW_ASK])
        self.assertEqual(m.read(memmap.DRAW_ASK, 4), 1)
        self.assertNotIn(TEXT + sites_present.DRAW[3], went)                # never the epilogue: nothing skipped

    def test_the_depth_read_back_tells_the_end_in_a_plain_start_only_with_the_word(self):
        went, asked, _ = self.go("present_depth", 0, 0)
        self.assertEqual((went, asked), ([DEPTH, DEPTH_BACK], []))
        went, asked, m = self.go("present_depth", 0, 1, skip=1)
        self.assertEqual(went, [DEPTH, DEPTH_BACK])                         # the read-back made, never skipped
        self.assertEqual((asked, m.read(memmap.DRAW_ASK, 4)), ([memmap.DRAW_ASK], 2))

    def test_a_race_does_not_read_the_word(self):
        # in a race (mode 1) the word changes nothing: the routines read the mode word first (the mode machine's race
        # way, the boot routine and the seats' installer, is the one the race tests run)
        for routine, skip in (("present_draw", 1), ("present_depth", 0), ("present_depth", 1)):
            a, b = self.go(routine, 1, 0, skip)[:2], self.go(routine, 1, 1, skip)[:2]
            self.assertEqual(a, b, routine)


if __name__ == "__main__":
    unittest.main()
