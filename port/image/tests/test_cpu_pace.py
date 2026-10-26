"""Computer drivers at their own pace (sites_cpu.py), run here as the bytes the image holds: in a race the pre-pass
calls no getter and gives the drivers the references best 0 and worst 0xFF, and the leader's state 0 is state 1;
with those references the driver's own choice (fn_1_C4ABC 0xC53CC, as the disassembly has it) gives state 1 for
every rank. In a plain start, and in a race whose rubber band word is 1 (a replay recorded before 0.4.0), both sites
are the game's own. No game data."""
import os, sys, unittest

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, ".."))
from test_raceview import Build, Cpu, put, get, TEXT
import build, memmap
import sites_cpu

GLOBALS = 0x803EF2E8          # .bss 0x7AC48, the drivers' globals: r29 in fn_1_C42F4, r28 in fn_1_C4ABC


def choice(best, worst, rank, leader_state=0):
    """fn_1_C4ABC 0xC53CC-0xC53F8: the state a driver of rank `rank` asks for (unsigned bytes), with the word at
    0xC53DC giving `leader_state`."""
    if best >= rank:
        return leader_state
    return 2 if worst <= rank else 1


class TheDriversPace(Build):
    def refs(self, mode, band=0):
        memory, called = {}, []
        put(memory, memmap.MODE, mode)
        put(memory, sites_cpu.BAND, band)
        put(memory, GLOBALS + sites_cpu.BEST, 0x0511, 2)          # whatever the step before left
        m = Cpu(self.image, memory, lambda address, cpu: called.append(address))
        m.r[29] = GLOBALS
        m.run(self.symbols["cpu_refs"])
        return called, get(m.mem, GLOBALS + sites_cpu.BEST, 1), get(m.mem, GLOBALS + sites_cpu.WORST, 1)

    def leader(self, mode, band=0):
        memory, left = {}, []
        put(memory, memmap.MODE, mode)
        put(memory, sites_cpu.BAND, band)
        m = Cpu(self.image, memory, lambda address, cpu: left.append(address))
        m.r[4] = 0x77
        m.run(self.symbols["cpu_leader"])
        return left, m.r[4]

    def test_the_sites(self):
        p = self.patches[TEXT + sites_cpu.REFS_SITE]
        self.assertEqual(p["what"], "cpu_refs")
        self.assertEqual(int(p["original"], 16), build.branch(TEXT + sites_cpu.REFS_SITE, TEXT + 0x8C3EC, True))
        self.assertEqual(int(p["new"], 16), build.branch(TEXT + sites_cpu.REFS_SITE, self.symbols["cpu_refs"]))
        p = self.patches[TEXT + sites_cpu.LEADER_SITE]
        self.assertEqual((p["original"], p["what"]), ("38800000", "cpu_leader"))     # li r4, 0
        self.assertEqual(int(p["new"], 16), build.branch(TEXT + sites_cpu.LEADER_SITE, self.symbols["cpu_leader"]))

    def test_a_race_reads_no_reference(self):
        # no getter is called, the references are the constants, and the pre-pass goes on after the fourth store
        called, best, worst = self.refs(1)
        self.assertEqual((called, best, worst), ([TEXT + sites_cpu.REFS_PAST], 0, 0xFF))

    def test_a_plain_start_calls_the_getter(self):
        called, best, worst = self.refs(0)
        self.assertEqual((called, best, worst), ([TEXT + 0x8C3EC, TEXT + sites_cpu.REFS_SITE + 4], 0x05, 0x11))
        self.assertEqual(self.refs(0, band=1), (called, best, worst))

    def test_a_race_with_the_band_calls_the_getter(self):
        # a replay recorded before 0.4.0: the race block's word is 1, and the pre-pass is the game's own
        called, best, worst = self.refs(1, band=1)
        self.assertEqual((called, best, worst), ([TEXT + 0x8C3EC, TEXT + sites_cpu.REFS_SITE + 4], 0x05, 0x11))

    def test_the_leader_is_in_state_1(self):
        self.assertEqual(self.leader(1), ([TEXT + sites_cpu.LEADER_BACK], 1))
        self.assertEqual(self.leader(0), ([TEXT + sites_cpu.LEADER_BACK], 0))       # the game's li r4, 0
        self.assertEqual(self.leader(1, band=1), ([TEXT + sites_cpu.LEADER_BACK], 0))   # a race with the band

    def test_every_rank_asks_for_state_1(self):
        best, worst = sites_cpu.BEST_RANK, sites_cpu.WORST_RANK
        self.assertEqual({choice(best, worst, rank, sites_cpu.STATE) for rank in range(30)}, {1})
        # the stock game's references give all three, so the test above is not empty
        self.assertEqual([choice(5, 20, rank) for rank in (0, 5, 6, 19, 20, 29)], [0, 0, 1, 1, 2, 2])


if __name__ == "__main__":
    unittest.main()
