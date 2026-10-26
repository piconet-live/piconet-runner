"""The spectator's manual shots (frame_spectate.py), against test_spectator.py's made-up game: a shot is set up in
the step of its choice, and no shot is ever set up with a variant its type does not have, whatever the game's own
TV task does with the type (its pick of type 1 or 3 on some sections, a shot's own change of type). The TV task's
stand-in fails the test at any set-up of a type with a variant past its count. No game data."""
import os, sys, unittest

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, ".."))
import build  # noqa: F401  (puts the generators' folder on the path)
from test_raceview import Build, get, put
from test_spectator import Spectating, MODE, HEIGHT, MANUAL, CHASING, DOWN, LEFT, RIGHT


class TheManualShots(Build):
    def race(self, **how):
        return Spectating(self, **how)

    def test_a_manual_shot_is_set_up_in_one_step_and_again_only_by_a_rollback_over_it(self):
        g = self.race().watch(182)
        g.press(DOWN); g.press(DOWN); g.press(DOWN)                  # manual shot 1
        for _ in range(5):
            g.frame()
        g.frame((0, 100))                                            # variant 1
        at = g.step - 1
        self.assertEqual(g.setups[-1], (at, "variant", 1, 1))
        count = len(g.setups)
        for _ in range(20):
            g.frame()
        self.assertEqual(len(g.setups), count)                       # nothing sets the shot up again
        g.frame(back_to=at + 2)                                      # a rollback to after that step: memory holds it
        self.assertEqual((len(g.setups), g.pictures[-1][2:5]), (count, (4, 1, 1)))
        g.frame(back_to=at - 2)                                      # one to before it: the first step run again
        self.assertEqual((g.setups[count:], g.pictures[-1][2:5]), ([(at - 2, "variant", 1, 1)], (4, 1, 1)))   # makes it
        # two choices inside one rollback: the picture is the same
        g.frame(DOWN); g.frame((0, 100))
        self.assertEqual(g.pictures[-1][2:5], (4, 2, 1))
        g.frame(back_to=g.step - 4)
        self.assertEqual(g.pictures[-1][2:5], (4, 2, 1))

    def test_another_type_is_set_up_with_a_variant_every_type_has(self):
        # from shot 1 at its last variant to shot 3 (two variants): the type's set-up reads the variant in use
        g = self.race().watch(182)
        g.press(DOWN); g.press(DOWN); g.press(DOWN)                  # manual shot 1 (type 1)
        g.press((0, -100))                                           # down from the first: variant 19
        self.assertEqual(g.pictures[-1][2:5], (4, 1, 19))
        g.frame(DOWN)                                                # shot 2: the step of the choice
        self.assertEqual((g.setups[-1][1:], g.pictures[-1][2:5]), (("type", 2, 0), (4, 2, 0)))
        g.press((0, 100)); g.press((0, 100)); g.press((0, 100))      # its variant 3
        g.frame(DOWN)                                                # shot 3
        self.assertEqual(g.setups[-1][1:], ("type", 3, 0))
        g.press((0, 100))
        self.assertEqual(g.pictures[-1][2:5], (4, 3, 1))
        # a height chosen for a type before it is in use: the type first, with 1 at most, the height a step later
        g = self.race().watch(182)
        g.press(DOWN); g.press(DOWN); g.press(DOWN)
        g.frame(DOWN, back_to=g.step - 1); g.frame((0, 100)); g.frame(); g.frame((0, 100)); g.frame(); g.frame((0, 100))
        self.assertEqual(g.pictures[-1][2:5], (4, 2, 3))
        g.frame(back_to=g.step - 9)                                  # back to before the type: its step, with
        self.assertEqual((g.setups[-2][1:], g.setups[-1][1:], g.pictures[-1][2:5]),             # variant 1, then 3
                         (("type", 2, 1), ("variant", 2, 3), (4, 2, 3)))

    def test_where_the_game_picks_the_type_no_variant_is_past_its_types_end(self):
        # the reviewer's case: the watched racer on a section where the TV task draws the type, 1 or 3. Type 3 has
        # two variants; a height of 2 or more kept through the pick read its table past the end.
        g = self.race().watch(182)
        g.press(DOWN); g.press(DOWN); g.press(DOWN)                  # manual shot 1 (type 1, 20 variants)
        for _ in range(15):
            g.press((0, 100))
        self.assertEqual(g.pictures[-1][2:5], (4, 1, 15))
        g.section, g.picks = True, [3, 3]
        g.frame((0, 100))                                            # variant 16: its set-up leaves type 3 wanted
        self.assertEqual(g.setups[-1][1:], ("variant", 1, 16))
        g.frame()                                                    # the type's set-up: with a variant it has
        self.assertEqual(g.setups[-1][1:], ("type", 3, 1))
        g.frame()                                                    # and the mode is the one in use
        self.assertEqual((get(g.mem, MODE), get(g.mem, HEIGHT), g.pictures[-1][2:5]), (MANUAL + 3, 1, (4, 3, 1)))
        count = len(g.setups)
        for i in range(20):
            g.frame(back_to=g.step - (1 + i % 9))
        self.assertEqual((len([x for x in g.setups[count:] if x[0] >= g.step - 1]), g.pictures[-1][2:5]), (0, (4, 3, 1)))
        # the C-stick goes on from the mode in use: shot 3's first variant. D-pad down goes on from the mode asked
        # for (shot 1): shot 2, and from there every mode in turn
        g.section = False
        g.press((0, 100))
        self.assertEqual(g.pictures[-1][2:5], (4, 3, 0))
        g.press(DOWN)
        self.assertEqual((get(g.mem, MODE), g.pictures[-1][2:4]), (MANUAL + 2, (4, 2)))
        g.press(DOWN); g.press(DOWN)
        self.assertEqual(g.pictures[-1][0], "chase")
        # a type chosen on such a section: the game's pick is set up, never ours with a variant it lacks
        g = self.race().watch(182)
        g.press(DOWN); g.press(DOWN)                                 # manual shot 0
        g.press((0, 100)); g.press((0, 100)); g.press((0, 100)); g.press((0, 100)); g.press((0, 100))
        g.section, g.picks = True, [3]
        g.press(DOWN)                                                # shot 1 asked; the task draws type 3
        g.frame()
        self.assertEqual((get(g.mem, MODE), g.pictures[-1][2:5]), (MANUAL + 3, (4, 3, 0)))

    def test_a_variant_the_game_picked_is_adopted_as_it_is(self):
        # the game went to type 0 with its variant 4 by itself: the mode and the height are the game's, and no
        # later step puts another variant in its place (the trap: a height taken as 1 at most, and the step after
        # replacing the game's variant with it)
        from test_spectator import LIVE
        g = self.race().watch(182)
        g.press(DOWN); g.press(DOWN); g.press(DOWN)                  # manual shot 1 (type 1)
        for offset, value in ((2, 0), (0x12, 0), (4, 4), (0x14, 4)):
            put(g.mem, LIVE + offset, value, 2)
        g.frame()
        self.assertEqual((get(g.mem, MODE), get(g.mem, HEIGHT)), (MANUAL, 4))
        for _ in range(4):
            g.frame()
            self.assertEqual((get(g.mem, HEIGHT), g.pictures[-1][2:5]), (4, (4, 0, 4)))
        g.press((0, 100))                                            # and the C-stick goes on from it
        self.assertEqual(g.pictures[-1][2:5], (4, 0, 5))

    def test_a_shot_that_leaves_for_another_type_takes_a_variant_it_has(self):
        # type 3 at its second variant asks for type 0 by itself (run cam-r3's log): the mode follows
        g = self.race().watch(182)
        g.leaving = True
        for _ in range(5):
            g.press(DOWN)                                            # manual shot 3
        self.assertEqual(g.pictures[-1][2:5], (4, 3, 0))
        g.press((0, 100)); g.frame(); g.frame()
        self.assertEqual((get(g.mem, MODE), get(g.mem, HEIGHT), g.pictures[-1][2:5]), (MANUAL, 1, (4, 0, 1)))
        # the chase camera is still the next press (run cam3-b: on course 24 the game leaves type 3 at once, the
        # mode in use fell back to shot 0, and a press that went on from there never reached it)
        self.assertEqual(g.press(DOWN)[0], "chase")
        self.assertEqual(g.press(DOWN)[:3], ("tv", g.pictures[-1][1], 5))    # and round to the automatic shots

    def test_any_presses_on_any_section_through_rollbacks_stay_inside_the_variants(self):
        # the TV task's stand-in fails the test at any set-up of a type with a variant it does not have
        g = self.race().watch(182)
        buttons = (DOWN, (0, 100), (0, 100), (0, -100), RIGHT, 0, 0, (0, 100), DOWN, (0, 100), 0, LEFT, (0, 100))
        for i in range(900):
            g.section = (i // 40) % 3 != 0
            g.leaving = (i // 90) % 2 == 1
            g.frame(buttons[(i * 7 + i // 13) % len(buttons)] if i % 3 == 0 else 0, back_to=g.step - (i % 12) if i % 4 else None)
        g.section = g.leaving = False
        for _ in range(6):
            g.frame()
        mode, picture = get(g.mem, MODE), g.pictures[-1]
        if MANUAL <= mode < CHASING:                                 # settled: the mode and the height are the camera's
            self.assertEqual(picture[2:5], (4, mode - MANUAL, get(g.mem, HEIGHT)))


if __name__ == "__main__":
    unittest.main()
