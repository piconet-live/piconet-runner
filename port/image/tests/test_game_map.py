"""The game's own course map is never drawn in a race (sites_present.py MAP_PASS): the overlay pass's site, run here as
the bytes the image holds. Its only drawing is the map (fn_1_79654 calls fn_1_7926C and nothing else that draws), and
ours replaces it, so in a race the site never calls it: in every phase, whether the frame draws or skips, whatever
the HUD part masks say, and in a frame whose memory a rollback put back (it reads the mode word alone, in the control
block, which no rollback puts back). In a plain start the game's own call. No game data."""
import os, sys, unittest

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, ".."))
from test_raceview import Build, Cpu, put, PHASE, TEXT
import build
import memmap
import sites_present
from test_race_end import BSS

SITE = TEXT + sites_present.MAP_PASS
MAP_DRAW, AFTER = TEXT + 0x79654, SITE + 4
MASKS = BSS - 0x10000 + 0xB04            # .data 0x3D544: the HUD part mask, a word a viewport
MAP_PART = 0x00800000
PHASES = (0x27, 0x28, 0x29, 0x2A)        # the course intro, the countdown, the race, the results


class Reads(Cpu):
    """The words the routine reads outside the image (its own instructions left out)."""

    def __init__(self, image, memory, game):
        super().__init__(image, memory, game)
        self.reads = set()

    def read(self, address, size):
        if not memmap.IMAGE <= address < memmap.IMAGE + len(self.image):
            self.reads.add(address)
        return super().read(address, size)


class GameMapNeverDrawn(Build):
    def frame(self, mode, phase=0x28, skip=0, masks=0xFF9FFFFF):
        """The site, from the main loop's tail: where it went into the game, in order, and what it read."""
        memory = {}
        put(memory, memmap.MODE, mode)
        put(memory, memmap.DRAW_SKIP, skip)
        put(memory, PHASE, phase, 2)
        for viewport in range(4):
            put(memory, MASKS + 4 * viewport, masks)
        went = []
        m = Reads(self.image, memory, lambda address, cpu: went.append(address))
        m.run(self.symbols["present_overlay"])
        return went, m.reads - set(went)          # (the game's instruction where it went is fetched, not read)

    def test_the_site_enters_the_routine(self):
        p = self.patches[SITE]
        self.assertEqual(int(p["new"], 16), build.branch(SITE, self.symbols["present_overlay"]))

    def test_never_in_a_race(self):
        for phase in PHASES:
            for skip in (0, 1):
                for masks in (0xFF9FFFFF, MAP_PART, 0):
                    went, read = self.frame(1, phase, skip, masks)
                    self.assertEqual(went, [AFTER], (phase, skip, hex(masks)))
                    # the mode word alone: nothing a restore puts back decides it
                    self.assertTrue(all(memmap.MODE <= a < memmap.MODE + 4 for a in read), sorted(map(hex, read)))

    def test_the_stock_game_in_a_plain_start(self):
        for phase in PHASES:
            went, _ = self.frame(0, phase)
            self.assertEqual(went, [MAP_DRAW, AFTER], phase)


if __name__ == "__main__":
    unittest.main()
