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


class TheLapShown(Build):
    def shown(self, at, lap, laps, mode=1):
        memory = {}
        put(memory, memmap.MODE, mode)
        m = HudCpu(self.image, memory, lambda address, cpu: None)
        m.r[4], m.r[6] = lap, laps
        m.run(self.symbols[f"hud_lap_{at:x}"])
        return m.r[5]

    def test_the_lap_shown_stops_at_the_races_laps(self):
        for at in hud.HUD_LAPS:
            self.assertEqual(self.shown(at, 0, 3), 1)
            self.assertEqual(self.shown(at, 2, 3), 3)
            self.assertEqual(self.shown(at, 3, 3), 3)
            self.assertEqual(self.shown(at, 7, 3), 3)

    def test_no_race_is_as_it_was(self):
        for at in hud.HUD_LAPS:
            self.assertEqual(self.shown(at, 3, 0), 4)

    def test_a_plain_start_is_as_it_was(self):
        for at in hud.HUD_LAPS:
            self.assertEqual(self.shown(at, 3, 3, mode=0), 4)


class TheDistanceLabel(Build):
    def reached(self, flags, mode=1):
        """The game addresses the label's site reaches, to the drawer's next word."""
        memory = {}
        put(memory, memmap.MODE, mode)
        put(memory, memmap.MODE_HUD_FLAGS, flags)
        reached = []
        m = HudCpu(self.image, memory, lambda address, cpu: reached.append(address - TEXT))
        m.run(self.symbols["mode_distance_label"])
        return reached

    def test_a_mode_that_says_off_has_no_label(self):
        for flags in (memmap.MODE_NO_DISTANCE_LABEL, memmap.MODE_NO_DISTANCE_LABEL | memmap.MODE_STORY_PICTURES):
            self.assertEqual(self.reached(flags), [hud.DISTANCE_LABEL + 4])

    def test_every_other_race_keeps_it(self):
        for args in ((0,), (memmap.MODE_STORY_PICTURES | memmap.MODE_SPREAD,), (memmap.MODE_NO_DISTANCE_LABEL, 0)):
            self.assertEqual(self.reached(*args), [hud.QUEUE_PICTURE, hud.DISTANCE_LABEL + 4])


class TheDistanceDigits(Build):
    def most(self, flags, mode=1):
        """The digits the loop draws at the most: the first count the site's compare ends the loop at."""
        for drawn in range(1, 8):
            memory = {}
            put(memory, memmap.MODE, mode)
            put(memory, memmap.MODE_HUD_FLAGS, flags)
            reached = []
            m = HudCpu(self.image, memory, lambda address, cpu: reached.append((address - TEXT, cpu.cr0)))
            m.r[26] = drawn
            m.run(self.symbols["mode_distance_digits"])
            self.assertEqual([at for at, _ in reached], [hud.DISTANCE_DIGITS + 4])
            if not reached[0][1][0]:   # not less: the drawer's `bge` leaves
                return drawn
        return None

    def test_a_mode_without_the_label_has_six(self):
        for flags in (memmap.MODE_NO_DISTANCE_LABEL, memmap.MODE_NO_DISTANCE_LABEL | memmap.MODE_STORY_PICTURES):
            self.assertEqual(self.most(flags), 6)

    def test_every_other_race_keeps_five(self):
        for args in ((0,), (memmap.MODE_STORY_PICTURES | memmap.MODE_SPREAD,), (memmap.MODE_NO_DISTANCE_LABEL, 0)):
            self.assertEqual(self.most(*args), 5)


class TheResultsWithoutTimes(Build):
    A, B = 0x0100, 0x0200   # the pad word's bits the two tests read

    def reached(self, name, flags, mode=1, pad=0):
        """Where the site goes on (the game's addresses), and r0 and cr0's equal there."""
        memory = {}
        put(memory, memmap.MODE, mode)
        put(memory, memmap.MODE_HUD_FLAGS, flags)
        reached = []
        m = HudCpu(self.image, memory, lambda address, cpu: reached.append((address - TEXT, cpu.r[0], cpu.cr0[2])))
        m.r[0] = pad
        m.run(self.symbols[name])
        return reached

    OFF = (memmap.MODE_NO_RESULTS_TIMES, memmap.MODE_NO_RESULTS_TIMES | memmap.MODE_POINTS_PAGE | memmap.MODE_POINTS_ONLY)
    OTHER = ((0,), (memmap.MODE_POINTS_PAGE | memmap.MODE_POINTS_ONLY,), (memmap.MODE_NO_RESULTS_TIMES, 0))

    def test_the_times_page_turns_by_itself_without_its_sound(self):
        for flags in self.OFF:
            self.assertEqual([at for at, *_ in self.reached("mode_results_skip", flags)], [hud.RESULTS_TURN])
            self.assertEqual([at for at, *_ in self.reached("mode_results_quiet", flags)], [hud.RESULTS_QUIET + 4])

    def test_the_points_page_does_not_go_back_to_it(self):
        for flags in self.OFF:
            self.assertEqual(self.reached("mode_results_back", flags, pad=self.B), [(hud.RESULTS_BACK + 4, 0, True)])

    def test_every_other_race_keeps_the_page(self):
        for args in self.OTHER:
            for pad in (0, self.A, self.B):
                self.assertEqual(self.reached("mode_results_skip", *args, pad=pad),
                                 [(hud.RESULTS_SKIP + 4, int(pad == self.A), pad != self.A)])
                self.assertEqual(self.reached("mode_results_back", *args, pad=pad),
                                 [(hud.RESULTS_BACK + 4, int(pad == self.B), pad != self.B)])
            self.assertEqual([at for at, *_ in self.reached("mode_results_quiet", *args)],
                             [hud.PLAY_SOUND, hud.RESULTS_QUIET + 4])


class TheObjectsCounts(Build):
    def counts(self, of_plus_one, gone, mode=1, block=BLOCK):
        """Both sites' answers (r3 at the drawer's next word) and the game addresses each reached."""
        out = []
        for name, at in (("mode_objects_of", hud.OBJECTS_OF), ("mode_objects_gone", hud.OBJECTS_GONE)):
            memory = {}
            put(memory, memmap.MODE, mode)
            put(memory, memmap.MODES + 4, block)
            put(memory, memmap.MODE_HUD_OBJECTS, of_plus_one)
            put(memory, memmap.MODE_HUD_OBJECTS + 4, gone)
            reached = []
            m = HudCpu(self.image, memory, lambda address, cpu: reached.append((address, cpu.r[3])))
            m.r[3] = 1
            m.run(self.symbols[name])
            out.append([(address - TEXT, r3) for address, r3 in reached])
        return out

    def test_the_modes_count_reads_n_of(self):
        # show_objects 3, 5: the drawer is given 5 and 2, and draws 5 - 2 = 3 / 5
        self.assertEqual(self.counts(6, 2), [[(hud.OBJECTS_OF + 4, 5)], [(hud.OBJECTS_GONE + 4, 2)]])
        self.assertEqual(self.counts(1, 0), [[(hud.OBJECTS_OF + 4, 0)], [(hud.OBJECTS_GONE + 4, 0)]])

    def test_without_the_modes_count_the_games_calls(self):
        for args in ((0, 0), (6, 2, 0), (6, 2, 1, 0)):
            of, gone = self.counts(*args)
            self.assertEqual([a for a, _ in of], [hud.OBJECTS_OF_GET, hud.OBJECTS_OF + 4])
            self.assertEqual([a for a, _ in gone], [hud.OBJECTS_GONE_GET, hud.OBJECTS_GONE + 4])
            self.assertEqual(of[0][1], 1)  # the game's argument, as it was


if __name__ == "__main__":
    unittest.main()
