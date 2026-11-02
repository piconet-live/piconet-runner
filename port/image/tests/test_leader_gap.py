"""The leader's gap popup starts in the step (sites_leadergap.py), run here as the bytes the image holds. No game data."""
import os, sys, unittest

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, ".."))
from test_raceview import Build, Cpu, put, TEXT
import memmap
import sites_leadergap as gap


class TheStart(Build):
    def calls(self, mode):
        memory = {}
        put(memory, memmap.MODE, mode)
        reached = []
        m = Cpu(self.image, memory, lambda address, cpu: reached.append(address))
        m.r[3] = 0x80A00000
        m.run(self.symbols["leader_gap"])
        return [address - TEXT for address in reached]

    def test_in_a_race_of_ours_the_flag_is_set_and_taken_in_the_step(self):
        self.assertEqual(self.calls(1), [gap.PENDING, gap.START, gap.SITE + 4])

    def test_in_a_plain_start_the_game_keeps_its_own_way(self):
        self.assertEqual(self.calls(0), [gap.PENDING, gap.SITE + 4])

    def test_the_site_is_the_lap_events_call_of_the_flag(self):
        self.assertIn(TEXT + gap.SITE, self.patches)


if __name__ == "__main__":
    unittest.main()
