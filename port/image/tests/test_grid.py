"""The start grid (sites_grid.py), run here as the bytes the image holds: with the race block's grid the game's list of
fn_1_85934 is the grid, slot by slot, and the sort is not called; without one (a practice, a match file, every
recording made before the grid), in a plain start, with a count above 30 or a grid entry that is no machine of the
race, the game's own qsort runs on the list as it was. No game data."""
import os, sys, unittest

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, ".."))
from test_raceview import Build, Cpu, get, put, STACK, TEXT
import build, memmap
import sites_grid

LIST = STACK + 8                       # fn_1_85934's list: sp + 8
AFTER, QSORT = TEXT + sites_grid.SITE + 4, sites_grid.QSORT


def race(grid=None, mode=1, n=30):
    """Memory of the moment of the sort: the list 0..n-1 as the game filled it, the race block's grid (`grid`, slot
    by slot; None: the word 0)."""
    memory = {}
    put(memory, memmap.MODE, mode)
    for k in range(n):
        put(memory, LIST + 2 * k, k, 2)
    if grid is not None:
        put(memory, memmap.RACE + memmap.RACE_GRID_ON, 1)
        for k, seat in enumerate(grid):
            put(memory, memmap.RACE + memmap.RACE_GRID + k, seat, 1)
    return memory


class TheStartGrid(Build):
    def sort(self, memory, n):
        """The site's routine run with the list in r3 and the count in r4: where it left for the game, and the list."""
        left = []
        m = Cpu(self.image, memory, lambda address, cpu: left.append(address))
        m.r[3], m.r[4], m.r[1] = LIST, n, STACK
        m.run(self.symbols["grid_sort"])
        return left, [get(m.mem, LIST + 2 * k, 2) for k in range(n)]

    def test_the_site_branches_to_the_routine(self):
        p = self.patches[TEXT + sites_grid.SITE]
        self.assertEqual(int(p["new"], 16), build.branch(TEXT + sites_grid.SITE, self.symbols["grid_sort"]))

    def test_a_grid_is_the_list_and_nothing_sorts_it(self):
        grid = [5, 9, 0, 1] + [k for k in range(30) if k not in (5, 9, 0, 1)]
        left, out = self.sort(race(grid), 30)
        self.assertEqual((left, out), ([AFTER], grid))
        # a race of fewer machines: as many slots as machines
        left, out = self.sort(race([2, 0, 1]), 3)
        self.assertEqual((left, out), ([AFTER], [2, 0, 1]))

    def test_without_a_grid_the_game_sorts_its_own_list(self):
        for memory, n in ((race(None), 30), (race([1, 0], mode=0), 2), (race([3, 0, 1], n=3), 3),
                          (race(list(range(31)), n=31), 31)):
            left, out = self.sort(memory, n)
            self.assertEqual((left, out), ([QSORT, AFTER], list(range(n))))   # the list untouched: qsort, then on

    def place(self, flags, mode=1):
        """The placing's site run with k in r28: the slot it leaves in r3, where it leaves for, and cr0's condition."""
        memory = {}
        put(memory, memmap.MODE, mode)
        put(memory, memmap.MODE_HUD_FLAGS, flags)
        left = []
        m = Cpu(self.image, memory, lambda address, cpu: left.append(address))
        m.r[3], m.r[28], m.r[1] = 0x1234, 7, STACK
        m.run(self.symbols["grid_place"])
        self.assertEqual(left, [TEXT + sites_grid.PLACE + 4])
        return m.r[3]

    def test_the_placing_site_is_the_slots_argument(self):
        at = TEXT + sites_grid.PLACE
        self.assertEqual(int(self.patches[at]["new"], 16), build.branch(at, self.symbols["grid_place"]))
        self.assertEqual(int(self.patches[at]["original"], 16), sites_grid.PLACE_KNOWN)   # mr r3, r28

    def test_start_together_places_every_machine_on_slot_0(self):
        self.assertEqual(self.place(memmap.MODE_TOGETHER), 0)
        self.assertEqual(self.place(memmap.MODE_TOGETHER | memmap.MODE_ROLLING), 0)

    def test_without_it_each_machine_takes_its_own_slot(self):
        for flags, mode in ((0, 1), (memmap.MODE_ROLLING, 1), (memmap.MODE_TOGETHER, 0)):
            self.assertEqual(self.place(flags, mode), 7)


if __name__ == "__main__":
    unittest.main()
