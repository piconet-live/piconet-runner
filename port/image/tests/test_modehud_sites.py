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


class TheScore(Build):
    """race_time_score (sites_hudmove.py): the race time's call, then SCORE and TEXT from the window's words."""
    TEMPLATE = [0, 0, 0, 0x3DCCCCCD, 0x3F800000, 0x3F800000, 0, 0, 0x3F800000, 0x3F800000, 0, 0x3F800000, 5, 0, 0xFFFFFFFF] + [0] * 7
    LABEL, MESSAGE = 0x80D20000, 0x80D20020

    def draw(self, number_plus_one, mode=1, block=BLOCK, hud_on=1, move=0, label=None, font=5, text=0, text_font=5, viewports=1):
        """What the routine queues: the game addresses reached with (image, x, y, z) for a picture and r3 for a call."""
        import struct
        import sites_hudmove as hm
        memory = {}
        put(memory, memmap.MODE, mode)
        put(memory, memmap.MODES + 4, block)
        put(memory, memmap.MODE_HUD_SCORE, number_plus_one)
        put(memory, memmap.MODE_HUD_SCORE_LABEL, label or self.LABEL)
        memory.update(getattr(self, "_label_bytes", None) or {self.LABEL + i: c for i, c in enumerate(b"POINTS\0")})
        put(memory, memmap.MODE_HUD_SCORE_FONT, font)
        put(memory, memmap.MODE_HUD_TEXT, text)
        put(memory, memmap.MODE_HUD_TEXT_FONT, text_font)
        put(memory, memmap.MODE_HUD_MOVE, move)
        put(memory, memmap.REL + memmap._facts.HUD_PARTS, hud_on)
        for i, w in enumerate(self.TEMPLATE):
            put(memory, memmap.REL + hm.SCORE_TEMPLATE + 4 * i, w)
        for d in range(10):
            put(memory, memmap.REL + hm.SCORE_DIGITS + 4 * d, 0x9440 + d)
        put(memory, memmap.REL + hm.SCORE_SHADOW_DZ, struct.unpack(">I", struct.pack(">f", 0.01))[0])
        reached = []

        def game(address, cpu):
            at = address - TEXT
            if at == hm.QUEUE_PICTURE:
                fl = lambda o: struct.unpack(">f", cpu.read(cpu.r[3] + o, 4).to_bytes(4, "big"))[0]
                reached.append((at, cpu.read(cpu.r[3], 4), fl(4), fl(8), round(fl(0xC), 3), cpu.read(cpu.r[3] + 0x30, 4),
                                (round(fl(0x18), 4), round(fl(0x20), 4), round(fl(0x10), 4))))
            elif at == hm.TEXT_AT:
                reached.append((at, cpu.f[1], cpu.f[2]))
            elif at == hm.TEXT_COLOUR:
                reached.append((at, cpu.read(cpu.r[3], 4)))
            elif at == hm.TEXT_SCALE:
                reached.append((at, round(cpu.f[1], 3)))
            elif at == hm.VIEWPORTS:
                cpu.r[3] = viewports
                reached.append((at,))
            else:
                reached.append((at, cpu.r[3]))
        m = HudCpu(self.image, memory, game)
        m.r[3], m.r[4], m.r[5], m.r[6], m.r[7] = 444, 67, 1, 2, 3
        m.run(self.symbols["race_time_score"])
        return reached

    def label(self, text, font=5, number=7):
        """What the routine queues for the label `text` (the window's text at LABEL): the letters' packets as (picture,
        x, u0, u1, scale_x) on the label's row, and the text drawer's calls."""
        import sites_hudmove as hm
        memory_text = {}
        for i, c in enumerate(text.encode() + b"\0"):
            memory_text[self.LABEL + i] = c
        self._label_bytes = memory_text
        got = self.draw(number + 1, font=font)
        del self._label_bytes
        letters = [(g[1], g[2], *g[6]) for g in got if g[0] == hm.QUEUE_PICTURE and g[3] == hm.SCORE_LABEL_Y]
        drawer = [g for g in got if g[0] in (hm.TEXT_BEGIN, hm.TEXT_FONT, hm.TEXT_UPPER, hm.TEXT_ALIGN, hm.TEXT_COLOUR, hm.TEXT_SCALE,
                                             hm.TEXT_AT, hm.TEXT_PUTS)]
        return letters, drawer

    def test_the_number_in_the_race_times_digits_under_the_label(self):
        import sites_hudmove as hm
        got = self.draw(1234 + 1)
        self.assertEqual(got[0], (hm.RACE_TIME_DRAW, 444))                       # the race time first, its arguments kept
        self.assertNotIn(hm.TEXT_PUTS, [g[0] for g in got])                      # the label POINTS in the HUD labels' letters, not the drawer's
        pictures = [g[1:6] for g in got if g[0] == hm.QUEUE_PICTURE and g[3] != hm.SCORE_LABEL_Y]
        # the units from the right: each digit over its shadow, 22 units apart, the template's flags and white kept
        self.assertEqual(pictures, [(0x9444, 596.0, 81.0, 0.1, 5), (0x9430, 596.0, 81.0, 0.11, 5),
                                    (0x9443, 574.0, 81.0, 0.1, 5), (0x9430, 574.0, 81.0, 0.11, 5),
                                    (0x9442, 552.0, 81.0, 0.1, 5), (0x9430, 552.0, 81.0, 0.11, 5),
                                    (0x9441, 530.0, 81.0, 0.1, 5), (0x9430, 530.0, 81.0, 0.11, 5)])
        self.assertNotIn(memmap.HOST_HUD - TEXT, [g[0] for g in got])  # no move word: no host call

    def test_zero_is_one_digit_and_the_most_six(self):
        import sites_hudmove as hm
        row = lambda got: [g for g in got if g[0] == hm.QUEUE_PICTURE and g[3] == hm.SCORE_Y and g[1] != hm.SCORE_SHADOW]
        digits = lambda got: [g[1] - 0x9440 for g in row(got)]
        self.assertEqual(digits(self.draw(0 + 1)), [0])
        self.assertEqual(digits(self.draw(999999 + 1)), [9] * 6)
        self.assertEqual(digits(self.draw(1000000 + 1)), [0, 0, 0, 0, 0, 0, 1])  # (the core holds it to 999999 before here)
        got = self.draw(100 + 1)
        self.assertEqual([(g[1], g[2]) for g in row(got)], [(0x9440, 596.0), (0x9440, 574.0), (0x9441, 552.0)])

    def test_nothing_but_the_race_time_without_a_score_a_mode_or_the_hud(self):
        import sites_hudmove as hm
        for kw in ({"number_plus_one": 0}, {"number_plus_one": 8, "mode": 0}, {"number_plus_one": 8, "block": 0}, {"number_plus_one": 8, "hud_on": 0}):
            self.assertEqual(self.draw(**kw), [(hm.RACE_TIME_DRAW, 444)])

    def test_the_part_is_pushed_and_popped_for_the_moves(self):
        import sites_hudmove as hm
        got = self.draw(5 + 1, move=1)
        host = [g for g in got if g[0] == memmap.HOST_HUD - TEXT]
        self.assertEqual([g[1] for g in host], [hm.PUSH, hm.POP])
        self.assertEqual(self.draw(5 + 1, move=0).count((memmap.HOST_HUD - TEXT, hm.PUSH)), 0)

    def test_the_label_is_the_hud_labels_letters_ending_at_times_right_edge(self):
        import sites_hudmove as hm
        letters, drawer = self.label("POINTS")
        self.assertEqual(drawer, [])
        # from the right: S (POS. 17-24), T (TIME 0-8), N (ENERGY 7-15), I (TIME 8-13), O (POS. 8-17), P (POS. 0-8); u0, u1 the
        # columns over the picture's width, scale_x the letter's share of it, each letter's left edge a letter's width before
        self.assertEqual(letters, [(0x9405, 611.0, round(17 / 28, 4), round(24 / 28, 4), round(7 / 28, 4)),
                                   (0x940A, 603.0, 0.0, 0.25, 0.25),
                                   (0x9400, 595.0, round(7 / 48, 4), round(15 / 48, 4), round(8 / 48, 4)),
                                   (0x940A, 590.0, 0.25, round(13 / 32, 4), round(5 / 32, 4)),
                                   (0x9405, 581.0, round(8 / 28, 4), round(17 / 28, 4), round(9 / 28, 4)),
                                   (0x9405, 573.0, 0.0, round(8 / 28, 4), round(8 / 28, 4))])
        self.assertEqual(hm.label_width("POINTS"), 618 - 573)
        # every letter the table has draws, in the picture its row names; a space is 4 units of nothing
        letters, drawer = self.label("A DEGHILMNOPRSTY.")
        self.assertEqual(drawer, [])
        self.assertEqual(len(letters), 16)
        self.assertEqual(letters[-1][1], 618.0 - hm.label_width("A DEGHILMNOPRSTY."))
        self.assertEqual(letters[-2][1], letters[-1][1] + (hm.LABEL_LETTERS["A"][2] - hm.LABEL_LETTERS["A"][1]) + hm.LABEL_SPACE)
        # the digits after it, with the packet's corners and scale put back
        got = self.draw(42 + 1)
        digits = [g for g in got if g[0] == hm.QUEUE_PICTURE and g[3] == hm.SCORE_Y]
        self.assertEqual([(g[1], g[2], g[6]) for g in digits][:2], [(0x9442, 596.0, (0.0, 1.0, 1.0)), (0x9430, 596.0, (0.0, 1.0, 1.0))])

    def test_a_label_with_another_character_goes_to_the_text_drawer_right_aligned_in_the_hud_labels_cyan(self):
        import sites_hudmove as hm
        for text in ("KOS", "WAVE 2", "SCORE", "A-B"):
            letters, drawer = self.label(text, font=8)
            self.assertEqual(letters, [], text)
            self.assertEqual([d[0] for d in drawer], [hm.TEXT_BEGIN, hm.TEXT_FONT, hm.TEXT_UPPER, hm.TEXT_ALIGN, hm.TEXT_COLOUR,
                                                      hm.TEXT_SCALE, hm.TEXT_AT, hm.TEXT_PUTS], text)
            self.assertIn((hm.TEXT_FONT, 8), drawer)
            self.assertIn((hm.TEXT_ALIGN, hm.SCORE_LABEL_RIGHT_ALIGNED), drawer)
            self.assertIn((hm.TEXT_COLOUR, hm.SCORE_LABEL_COLOUR), drawer)
            self.assertIn((hm.TEXT_SCALE, round(hm.SCORE_LABEL_SCALE, 3)), drawer)   # (set after the font, which resets it)
            self.assertIn((hm.TEXT_AT, hm.SCORE_LABEL_RIGHT, hm.SCORE_LABEL_Y), drawer)
            self.assertIn((hm.TEXT_PUTS, self.LABEL), drawer)

    def test_the_text_is_centred_at_its_home_in_its_font_inside_the_centre_group(self):
        import sites_hudmove as hm
        got = self.draw(0, text=self.MESSAGE, text_font=7)
        self.assertEqual(got[0], (hm.RACE_TIME_DRAW, 444))
        self.assertNotIn(hm.QUEUE_PICTURE, [g[0] for g in got])  # no score: no digits
        calls = [g for g in got if g[0] in (hm.VIEWPORTS, hm.ANCHOR_CLEAR, hm.ANCHOR_SET, hm.TEXT_BEGIN, hm.TEXT_FONT, hm.TEXT_UPPER,
                                            hm.TEXT_COLOUR, hm.TEXT_ALIGN, hm.TEXT_AT, hm.TEXT_PUTS)]
        self.assertEqual([c[0] for c in calls], [hm.VIEWPORTS, hm.ANCHOR_CLEAR, hm.ANCHOR_SET, hm.TEXT_BEGIN, hm.TEXT_FONT, hm.TEXT_UPPER,
                                                 hm.TEXT_COLOUR, hm.TEXT_ALIGN, hm.TEXT_AT, hm.TEXT_PUTS, hm.ANCHOR_CLEAR, hm.ANCHOR_SET])
        self.assertEqual([c[1] for c in calls if c[0] == hm.ANCHOR_SET], [hm.ANCHOR_CENTRE, hm.ANCHOR_RIGHT])
        self.assertIn((hm.TEXT_FONT, 7), calls)
        self.assertIn((hm.TEXT_ALIGN, 9), calls)
        self.assertIn((hm.TEXT_AT, 320.0, 116.0), calls)
        self.assertIn((hm.TEXT_PUTS, self.MESSAGE), calls)
        self.assertNotIn(hm.TEXT_SCALE, [c[0] for c in got])  # the font's own size
        self.assertNotIn(memmap.HOST_HUD - TEXT, [g[0] for g in got])

    def test_the_text_in_a_split_screen_sets_no_anchor(self):
        import sites_hudmove as hm
        got = self.draw(0, text=self.MESSAGE, viewports=2)
        self.assertNotIn(hm.ANCHOR_SET, [g[0] for g in got])
        self.assertNotIn(hm.ANCHOR_CLEAR, [g[0] for g in got])
        self.assertIn((hm.TEXT_PUTS, self.MESSAGE), got)

    def test_both_parts_are_drawn_each_between_its_own_push_and_pop(self):
        import sites_hudmove as hm
        got = self.draw(7 + 1, text=self.MESSAGE, move=1)
        host = [g[1] for g in got if g[0] == memmap.HOST_HUD - TEXT]
        self.assertEqual(host, [hm.PUSH, hm.POP, hm.PUSH, hm.POP])
        puts = [g for g in got if g[0] == hm.TEXT_PUTS]
        self.assertEqual(puts, [(hm.TEXT_PUTS, self.MESSAGE)])                   # (the label POINTS is letters, not the drawer's)
        pictures = [g for g in got if g[0] == hm.QUEUE_PICTURE]
        self.assertEqual([g[1] for g in pictures][:7], [0x9405, 0x940A, 0x9400, 0x940A, 0x9405, 0x9405, 0x9447])   # the label, then the digit
        before = [g for g in got if g[0] in (memmap.HOST_HUD - TEXT, hm.QUEUE_PICTURE, hm.TEXT_PUTS)]
        self.assertEqual([g[0] for g in before], [memmap.HOST_HUD - TEXT] + [hm.QUEUE_PICTURE] * 8 + [memmap.HOST_HUD - TEXT] * 2 + [hm.TEXT_PUTS, memmap.HOST_HUD - TEXT])
        self.assertEqual(self.draw(7 + 1, text=self.MESSAGE, move=0).count((memmap.HOST_HUD - TEXT, hm.PUSH)), 0)

    def test_no_text_without_a_mode_or_the_hud(self):
        import sites_hudmove as hm
        for kw in ({"mode": 0}, {"block": 0}, {"hud_on": 0}):
            self.assertEqual(self.draw(0, text=self.MESSAGE, **kw), [(hm.RACE_TIME_DRAW, 444)])


if __name__ == "__main__":
    unittest.main()
