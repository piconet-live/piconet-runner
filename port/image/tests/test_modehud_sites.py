"""The game mode's display sites (sites_modehud.py), run here as the bytes the image holds against made-up memory: the
countdown's tick under clock_limit, the wrong-way sign's start. No game data."""
import os, sys, unittest

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, ".."))
from test_raceview import Build, get, put, TEXT
from test_mode_sites import ModeCpu
import memmap
import sites_modehud as hud

BLOCK = 0x80D00000
CLOCK_AT = memmap._facts.RACE_CLOCK_AT


class HudCpu(ModeCpu):
    """divwu and mullw (the tick's whole second) beside what ModeCpu reads."""

    def other(self, pc, w):
        if w >> 26 == 31 and (w >> 1) & 0x3FF in (459, 235):
            d, a, b = (w >> 21) & 31, (w >> 16) & 31, (w >> 11) & 31
            self.r[d] = self.r[a] // self.r[b] if (w >> 1) & 0x3FF == 459 else self.r[a] * self.r[b] & 0xFFFFFFFF
            return pc + 4
        return super().other(pc, w)


class TheTick(Build):
    def ticks(self, clock, limit=3000, mode=1, block=BLOCK):
        memory = {}
        put(memory, memmap.MODE, mode)
        put(memory, memmap.MODES + 4, block)
        put(memory, BLOCK + memmap.MODE_CLOCK, limit)
        put(memory, CLOCK_AT, clock)
        reached = []
        m = HudCpu(self.image, memory, lambda address, cpu: reached.append((address, cpu.r[3])))
        m.run(self.symbols["mode_beep"])
        return [r3 for address, r3 in reached if address == TEXT + hud.PLAY_SOUND]

    def test_the_games_tick_on_each_of_the_last_ten_seconds(self):
        tick = [0xA9092700]
        heard = [left for left in range(0, 700) if self.ticks(3000 - left) == tick]
        self.assertEqual(heard, [60 * s for s in range(10)])

    def test_none_before_the_race_past_the_limit_or_without_a_limit(self):
        self.assertEqual(self.ticks(0, limit=300), [])
        self.assertEqual(self.ticks(3060), [])
        self.assertEqual(self.ticks(2940, limit=0), [])
        self.assertEqual(self.ticks(2940, block=0), [])
        self.assertEqual(self.ticks(2940, mode=0), [])


class TheWrongWaySign(Build):
    def sign(self, asked):
        memory = {}
        put(memory, memmap.MODE, 1)
        put(memory, memmap.MODE_HUD + 0xC, asked)
        put(memory, 0x80D10000 + 0x10C, 0)
        reached = []
        m = HudCpu(self.image, memory, lambda address, cpu: reached.append(address))
        m.r[3] = 0x80D10000
        m.run(self.symbols["mode_wrong"])
        return reached, get(m.mem, memmap.MODE_HUD + 0xC)

    def test_an_ask_starts_the_sign_once(self):
        self.assertEqual(self.sign(1), ([TEXT + hud.WRONG_SIGN], 0))

    def test_no_ask_is_the_games_test(self):
        self.assertEqual(self.sign(0), ([TEXT + hud.WRONG_SITE + 4], 0))


class ThePointsOrder(Build):
    def fields(self, totals):
        memory = {}
        race = memmap.REL_BSS + hud.RACE if hasattr(memmap, "REL_BSS") else hud.REL_BSS + hud.RACE
        put(memory, memmap.MODE_HUD_FLAGS, memmap.MODE_POINTS_PAGE)
        put(memory, memmap.MODES + 4, BLOCK)
        put(memory, race + 8, len(totals) << 16)                     # R+0x9: the racers
        for seat, total in enumerate(totals):
            put(memory, BLOCK + memmap.MODE_POINTS + 4 * seat, total & 0xFFFFFFFF)
        reached = []
        m = HudCpu(self.image, memory, lambda address, cpu: reached.append(address))
        m.run(self.symbols["mode_sort"])
        self.assertIn(TEXT + hud.SORT, reached)
        return [get(m.mem, race + hud.RECORDS + hud.RECORD * seat + 4) >> 16 for seat in range(len(totals))]

    def test_a_negative_total_is_0_and_a_large_one_the_fields_end(self):
        self.assertEqual(self.fields([-5, 0, 7, 70000, -70000]), [0, 0, 7, 0xFFFF, 0])


if __name__ == "__main__":
    unittest.main()
