"""The start grid (sites_grid.py), run here as the bytes the image holds: with the race block's grid the game's list of
fn_1_85934 is the grid, slot by slot, and the sort is not called; without one (a practice, a match file, every
recording made before the grid), in a plain start, with a count above 30 or a grid entry that is no machine of the
race, the game's own qsort runs on the list as it was. No game data."""
import os, struct, sys, unittest

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

    def test_start_gap_0_places_every_machine_on_slot_0(self):
        self.assertEqual(self.place(memmap.MODE_SAME_SPOT), 0)
        self.assertEqual(self.place(memmap.MODE_SAME_SPOT | memmap.MODE_ROLLING), 0)

    def test_without_it_each_machine_takes_its_own_slot(self):
        for flags, mode in ((0, 1), (memmap.MODE_ROLLING, 1), (memmap.MODE_SPREAD, 1), (memmap.MODE_SAME_SPOT, 0)):
            self.assertEqual(self.place(flags, mode), 7)


def bits(x):
    return struct.unpack(">I", struct.pack(">f", x))[0]


def single_of(word):
    return struct.unpack(">f", struct.pack(">I", word))[0]


def single(x):
    return struct.unpack(">f", struct.pack(">f", x))[0]


HEADER_AT, TABLE_AT, SEGS_AT, LAP_AT, WORK_AT, CAR = 0x80D00000, 0x80D10000, 0x80D20000, 0x80D30000, 0x80F00000, 0x80E00000
CHECKPOINTS, LAP, CP0, T0, LO, HI = 100, 3000.0, 99, 0.62, 2950.0, 3000.0
PROGRESS_AT, FLAGS, MOVED_FLAGS = 0x80E10000, 0x04000600, 0x84000502    # the machine's progress record; its flags


def spread_race(flags=memmap.MODE_SPREAD, gap=None, mode=1, laps=True, header=True, n=30):
    """Memory of the moment slot k's machine is placed: the window's flags and gap word, a course of CHECKPOINTS
    checkpoints whose last (CP0) runs LO..HI of a LAP-metre lap, and the race's n machines."""
    memory = {}
    put(memory, memmap.MODE, mode)
    put(memory, memmap.MODE_HUD_FLAGS, flags)
    put(memory, memmap.MODE_HUD_START_GAP, 0 if gap is None else bits(gap))
    put(memory, sites_grid.HEADER, HEADER_AT if header else 0)
    put(memory, HEADER_AT + 8, CHECKPOINTS)
    put(memory, HEADER_AT + 0xC, TABLE_AT)
    put(memory, HEADER_AT + 0x7D, 0 if laps else 1, 1)
    put(memory, HEADER_AT + 0x90, LAP_AT)
    put(memory, LAP_AT, bits(LAP))
    put(memory, TABLE_AT + CP0 * 12 + 4, SEGS_AT)
    put(memory, SEGS_AT + 0x40, bits(LO))
    put(memory, SEGS_AT + 0x44, bits(HI))
    put(memory, sites_grid.MACHINES, n, 1)
    put(memory, CAR + sites_grid.PROGRESS, PROGRESS_AT)
    return memory


class TheSpreadStart(Build):
    """grid_spread: the game's placing, then the story's own mid-course placement of slot k at d0 - k x gap."""

    def spread(self, memory, k, speed=3.5):
        calls = []
        F = lambda offset: TEXT + offset

        def game(address, cpu):
            f = cpu.f
            if address == F(sites_grid.SLOT_POINT):
                calls.append(("slot_point", [get(cpu.mem, cpu.r[3] + 4 * i) for i in range(3)]))
                put(cpu.mem, cpu.r[4], bits(T0))
                cpu.r[3] = CP0
            elif address == F(sites_grid.AT_DISTANCE):
                calls.append(("at_distance", f[1], cpu.r[4]))
                put(cpu.mem, cpu.r[3], bits(0.25))
                cpu.r[3] = 41
            elif address == F(sites_grid.COURSE_FRAME):
                calls.append(("course_frame", cpu.r[3], cpu.r[4], f[1]))
            elif address == F(sites_grid.WORK):
                calls.append(("work",))
                cpu.r[3] = WORK_AT
            elif address == F(sites_grid.ON_ROAD):
                calls.append(("on_road", cpu.r[3], f[1]))
                for i, v in enumerate((10.0, 20.0, 30.0)):
                    put(cpu.mem, cpu.r[4] + 4 * i, bits(v))
            elif address == F(sites_grid.PUT_RACER):
                calls.append(("put_racer", cpu.r[3], cpu.r[4], f[1], f[2], f[3], f[4]))
                for i, v in enumerate((11.0, 21.0, 31.0)):                 # where the floor put it
                    put(cpu.mem, cpu.r[3] + 0x7C + 4 * i, bits(v))
                put(cpu.mem, cpu.r[3], MOVED_FLAGS)                         # what the moving left in its flags
                put(cpu.mem, PROGRESS_AT + sites_grid.SPOT, 7)             # the whole-course search's checkpoint
            elif address == F(sites_grid.LIGHT_RESET):
                spot = [get(cpu.mem, cpu.r[3] + sites_grid.SPOT + 4 * i) for i in range(5)]
                calls.append(("light_reset", cpu.r[3], cpu.r[4], spot[0], [single_of(w) for w in spot[1:]]))
            elif address == F(sites_grid.SEED):
                calls.append(("seed", cpu.r[3], cpu.r[4]))
            elif address == F(sites_grid.PLACE_CAR):
                calls.append(("place_car", cpu.r[3], f[1]))
                put(cpu.mem, cpu.r[3], FLAGS)
            else:
                calls.append(("left", address))
            for i in range(14):                                      # the callee's volatile registers
                cpu.f[i] = 1e30
            for r in (0, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12):
                if address not in (F(sites_grid.SLOT_POINT), F(sites_grid.AT_DISTANCE), F(sites_grid.WORK)) or r != 3:
                    cpu.r[r] = 0xDEAD0000 | r
        m = Cpu(self.image, memory, game)
        m.f = [0.0] * 32
        m.r[3], m.f[1], m.r[28], m.r[1] = CAR, speed, k, STACK
        m.run(self.symbols["grid_spread"])
        self.assertEqual(m.r[1], STACK)
        if any(c[0] == "put_racer" for c in calls):   # story action 13's: the flags before the move, its 0x400 now
            self.assertEqual(get(m.mem, CAR), FLAGS | MOVED_FLAGS & 0x400)
        return calls

    def test_the_spot_is_seeded_from_its_own_checkpoint_not_a_whole_course_search(self):
        # fn_1_85F90's fn_1_23500 clears the record and seeds it with fn_1_20994, the first checkpoint from the line
        # whose slab holds the spot: far from the line, a stretch beside it. The light reset keeps laps and spot.
        calls = self.spread(spread_race(), 12)
        self.assertEqual([c[0] for c in calls[-4:]], ["put_racer", "light_reset", "seed", "left"])

    def test_without_a_progress_record_only_the_flags_are_put_back(self):
        memory = spread_race()
        put(memory, CAR + sites_grid.PROGRESS, 0)
        calls = self.spread(memory, 3)
        self.assertEqual([c[0] for c in calls[-2:]], ["put_racer", "left"])

    def test_the_site_is_the_machines_placing(self):
        at = TEXT + sites_grid.SPREAD
        self.assertEqual(int(self.patches[at]["new"], 16), build.branch(at, self.symbols["grid_spread"]))
        self.assertEqual(int(self.patches[at]["original"], 16), build.branch(at, TEXT + sites_grid.PLACE_CAR, True))

    def test_without_the_flag_the_game_places_alone(self):
        after = ("left", TEXT + sites_grid.SPREAD + 4)
        for memory in (spread_race(flags=0), spread_race(flags=memmap.MODE_SAME_SPOT), spread_race(mode=0),
                       spread_race(header=False), spread_race(laps=False)):
            self.assertEqual(self.spread(memory, 7), [("place_car", CAR, 3.5), after])

    def expected(self, k, gap):
        d0 = single(LO + single(single(T0) * single(HI - LO)))
        d = single(d0 - single(float(k) * gap))
        lane = (-0.25, 0.0, 0.25)[k % 3]
        return [("place_car", CAR, 3.5), ("slot_point", [0, 0, sites_grid.SLOT_0_Z]), ("at_distance", d, CP0),
                ("course_frame", 41, 0, 0.25), ("work",), ("on_road", WORK_AT + 0x400, lane),
                ("put_racer", CAR, 41, 3.5, 10.0, 20.0, 30.0),
                ("light_reset", PROGRESS_AT, 0, 41, [0.25, 11.0, 21.0, 31.0]), ("seed", CAR, 1),
                ("left", TEXT + sites_grid.SPREAD + 4)]

    def test_a_gap_in_metres_moves_slot_k_k_gaps_back_in_its_lane(self):
        for k in (0, 1, 2, 7, 29):
            self.assertEqual(self.spread(spread_race(gap=12.9), k), self.expected(k, single(12.9)))
            self.assertEqual(self.spread(spread_race(gap=99.5), k), self.expected(k, 99.5))
            self.assertEqual(self.spread(spread_race(gap=250.0, n=12), k), self.expected(k, 250.0))   # 3000 / 12: even's

    def test_a_gap_above_evens_is_evens_so_the_spread_fits_one_lap(self):
        for k in (1, 29):
            self.assertEqual(self.spread(spread_race(gap=250.0), k), self.expected(k, single(LAP / 30)))
            self.assertEqual(self.spread(spread_race(gap=10000.0, n=12), k), self.expected(k, single(LAP / 12)))

    def test_even_shares_the_lap_out_among_the_machines(self):
        self.assertEqual(self.spread(spread_race(), 7), self.expected(7, single(LAP / 30)))
        self.assertEqual(self.spread(spread_race(n=12), 11), self.expected(11, single(LAP / 12)))


if __name__ == "__main__":
    unittest.main()
