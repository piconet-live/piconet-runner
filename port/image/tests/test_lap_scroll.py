"""The lap-time list's scroll counted in the step (sites_lapscroll.py, frame_replay.py `lapscroll`), run here as the
bytes the image holds. No game data."""
import os, sys, unittest

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, ".."))
import build, memmap  # noqa: F401  (build puts the generators' folder on the path)
import frame_replay, native, untracked
import sites_lapscroll as ls
from test_race_end import BSS
from test_raceview import Build, Cpu, get, put, IMAGE, TEXT
from test_sim_generator import assemble

ENTRIES = BSS + ls.ENTRIES


def counts(memory):
    return [get(memory, ENTRIES + port * ls.ENTRY + ls.COUNT, 1) for port in range(ls.PORTS)]


class TheScroll(Build):
    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        code = assemble(frame_replay._lapscroll())
        assert cls.image.count(code) == 1
        cls.lapscroll = IMAGE + cls.image.index(code)

    def entries(self, values):
        """The four entries: +8 the count `values[port]`, every other byte its own mark."""
        memory = {}
        for port in range(ls.PORTS):
            for i in range(ls.ENTRY):
                put(memory, ENTRIES + port * ls.ENTRY + i, 0x40 + port * 16 + i, 1)
            put(memory, ENTRIES + port * ls.ENTRY + ls.COUNT, values[port], 1)
        return memory

    def step(self, memory):
        """One step's count: `memory` as the routine leaves it."""
        cpu = Cpu(self.image, memory)
        cpu.r[30] = BSS
        cpu.run(self.lapscroll)
        memory.clear()
        memory.update(cpu.mem)

    def draw(self, mode, count, value):
        """The drawing's store at the site, with the count at `count` and the value it would store `value`."""
        memory = self.entries([count] * ls.PORTS)
        put(memory, memmap.MODE, mode)
        reached = []
        cpu = Cpu(self.image, memory, lambda address, c: reached.append(address))
        cpu.r[30], cpu.r[0] = ENTRIES, value
        cpu.run(self.symbols["lap_scroll"])
        return counts(cpu.mem)[0], [address - TEXT for address in reached]

    def test_each_step_counts_every_ports_count_down_to_0(self):
        memory = self.entries([15, 1, 0, 7])
        before = dict(memory)
        self.step(memory)
        self.assertEqual(counts(memory), [14, 0, 0, 6])
        changed = sorted(a for a in memory if memory[a] != before[a])
        self.assertEqual(changed, [ENTRIES + ls.COUNT, ENTRIES + ls.ENTRY + ls.COUNT, ENTRIES + 3 * ls.ENTRY + ls.COUNT])

    def test_a_lap_scrolls_for_15_steps_and_stops(self):
        memory = self.entries([ls.START, 0, 0, 0])
        seen = []
        for _ in range(ls.START + 3):
            seen.append(counts(memory)[0])
            self.step(memory)
        self.assertEqual(seen, list(range(ls.START, 0, -1)) + [0, 0, 0])

    def test_the_count_is_the_steps_own_however_the_frames_fall(self):
        # a rollback puts the entry back to a step's start and runs the steps again: the count after step s is the
        # same whichever frames ran it (the stock drawing's count went by displayed frames, which a rollback undid)
        straight = self.entries([ls.START, 0, 0, 0])
        kept = []
        for _ in range(10):
            self.step(straight)
            kept.append(dict(straight))
        again = dict(kept[3])                       # back to after step 4, then steps 5 to 10 run again
        for _ in range(6):
            self.step(again)
        self.assertEqual(counts(again), counts(straight))
        self.assertEqual(counts(straight)[0], ls.START - 10)

    def test_in_a_race_of_ours_the_drawing_leaves_the_count(self):
        self.assertEqual(self.draw(1, 9, 8), (9, [ls.SITE + 4]))

    def test_in_a_plain_start_the_drawing_counts_as_the_game_does(self):
        self.assertEqual(self.draw(0, 9, 8), (8, [ls.SITE + 4]))

    def test_the_site_is_the_drawings_store_of_the_count(self):
        self.assertIn(TEXT + ls.SITE, self.patches)
        self.assertEqual(int(self.patches[TEXT + ls.SITE]["original"], 16), ls.SITE_KNOWN)

    def test_the_step_wrapper_counts_before_the_task_manager(self):
        was = native.ON
        native.ON = True
        try:
            text = frame_replay.asm()
        finally:
            native.ON = was
        manager = text[text.index("manager:\n"):]
        self.assertLess(manager.index("    bl lapscroll\n"), manager.index("    bctrl\n"))

    def test_the_forks_image_has_neither(self):
        was = native.ON
        native.ON = False
        try:
            self.assertNotIn("lapscroll", frame_replay.asm())
            self.assertNotIn("addi 9, 9, %d\n    addi 10, 9, 3\n" % (untracked.LAP[0][0] - 0x80000), untracked.asm())
        finally:
            native.ON = was


class TheHighlight(unittest.TestCase):
    def test_the_highlight_bytes_are_kept_out_of_a_rollback_and_the_count_is_not(self):
        for port, (lo, hi) in enumerate(untracked.LAP):
            entry = ls.ENTRIES + port * ls.ENTRY
            self.assertEqual((lo, hi), (entry + 9, entry + 12))
            self.assertFalse(lo <= entry + ls.COUNT < hi)


if __name__ == "__main__":
    unittest.main()
