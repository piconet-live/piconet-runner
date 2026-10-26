"""A racer who is out is not watched (frame_specwatch.py's rule), against
test_spectator.py's made-up game: the cameras leave a watched racer in the step it goes out in, for the next one
in race order that still races; no button lands on a racer who is out; with nobody left racing the cameras stay.
No game data."""
import os, sys, unittest

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, ".."))
import build, memmap  # noqa: F401  (build puts the generators' folder on the path)
from test_race_end import MACHINES
from test_raceview import Build, CHASE, get, put
from test_spectator import Spectating, WATCHED, DOWN, LEFT, RIGHT, UP

LOG = memmap.SPEC_LOG


class TheRacerWhoIsOut(Build):
    def race(self, **how):
        return Spectating(self, **how)

    def out(self, g, *seats, flags=0x08000000):
        for seat in seats:
            put(g.mem, MACHINES + seat * 0x620, flags)

    def test_a_watched_racer_who_goes_out_is_left_in_that_step(self):
        for flags in (0x08000000, 0x800, 0x80):                      # retired, 0x800, broken down
            g = self.race().watch(182)
            g.press(RIGHT); g.press(RIGHT)                           # racer 2
            self.assertEqual(g.pictures[-1][1], 2)
            self.out(g, 2, flags=flags)                              # it is out when this frame's step begins
            at = g.step
            # the next in race order, at once; the racer asked for is still the player's choice
            self.assertEqual((g.frame()[1], get(g.mem, WATCHED)), (3, 2), hex(flags))
            self.assertEqual(g.step, at + 1)
            self.assertEqual(g.press(LEFT)[1], 1)                    # the buttons go on from the racer shown,
            self.assertEqual(get(g.mem, WATCHED), 1)                 # past the wreck, and the press is the choice
            self.assertEqual(g.press(RIGHT)[1], 3)
        # a finished machine and a computer's machine are racers
        g = self.race().watch(182)
        self.out(g, 1, flags=0x10000); self.out(g, 2, flags=0x04000200)
        self.assertEqual((g.press(RIGHT)[1], g.press(RIGHT)[1]), (1, 2))

    def last_entry(self, g):
        place = (get(g.mem, memmap.SPEC_LOGGED) - 1) % memmap.SPEC_LOG_ENTRIES
        return bytes(g.mem.get(LOG + 8 * place + i, 0) for i in range(8))

    def test_the_cameras_leave_in_the_step_the_racer_goes_out_in(self):
        # the race's tasks of step `at` put racer 1 out; the task manager's hook, before the cameras' list of that
        # same step, moves the cameras: no picture shows the wreck
        for chase in (False, True):
            g = self.race().watch(200)
            g.press(RIGHT)                                           # racer 1
            if chase:
                for _ in range(6):
                    g.press(DOWN)
            at = g.step
            g.racing = lambda s: self.out(g, 1) if s == at else None
            calls = len(g.calls)
            shown = g.frame()                                        # the frame whose one step is `at`
            self.assertEqual((g.step, shown[:2], get(g.mem, WATCHED)), (at + 1, ("chase" if chase else "tv", 2), 1), chase)
            self.assertEqual(self.last_entry(g)[4] & 0x80, 0)        # the log: the racer followed is racing
            self.assertEqual(g.calls[calls:], [(0, 2, at)] if chase else [])     # the chase camera set up anew, once
            self.assertEqual(get(g.mem, memmap.SPEC_STEP), 0)        # the hook took the step's mark
            for depth in (1, 2, 3, 5, 9):                            # across the step it went out in: the same seat
                g.frame(back_to=g.step - depth)
                self.assertEqual((g.pictures[-1][1], get(g.mem, WATCHED)), (2, 1), depth)
            # behind racer 2 the chase camera is set up only in the step racer 1 goes out in, each time that step
            # runs; a rollback to before the step of the choice sets it up behind racer 1 again, as it was then
            self.assertEqual({c for c in g.calls[calls:] if c[1] == 2}, {(0, 2, at)} if chase else set())
            self.assertTrue(all(c[:2] == (0, 1) and c[2] < at for c in g.calls[calls:] if c[1] != 2), g.calls[calls:])

    def test_a_rollback_that_takes_the_going_out_back_puts_the_cameras_back(self):
        # a guessed input put racer 1 out; the official inputs keep it racing. Nothing here touches the image's
        # words: the frame routine alone must bring the view back (it would not if the choice were overwritten
        # with the racer shown)
        for chase in (False, True):
            g = self.race().watch(200)
            g.press(RIGHT)                                           # racer 1
            if chase:
                for _ in range(6):
                    g.press(DOWN)
            at = g.step
            wrong = [True]
            g.racing = lambda s: self.out(g, 1) if s == at and wrong[0] else None
            g.frame(); g.frame(); g.frame()                          # the predicted steps: the view is on racer 2
            self.assertEqual((g.pictures[-1][1], get(g.mem, WATCHED)), (2, 1), chase)
            wrong[0] = False                                         # the truth arrives: the step runs again
            g.frame(back_to=at)
            self.assertEqual((g.pictures[-1][:2], get(g.mem, WATCHED)), (("chase" if chase else "tv", 1), 1), chase)
            self.assertEqual(g.press(RIGHT)[1], 2)                   # and the buttons go on from racer 1

    def test_the_hook_acts_only_at_the_cameras_list_of_a_marked_step(self):
        g = self.race().watch(200)
        g.press(RIGHT)
        self.out(g, 1)                                               # out, and no step has seen it yet
        before = dict(g.mem)
        for task_list in (0, 9, 10, 19, 21, 31):                     # other lists: the mark stays, nothing is written
            put(g.mem, memmap.SPEC_STEP, 1)
            g.manager(task_list)
            self.assertEqual(get(g.mem, memmap.SPEC_STEP), 1, task_list)
            put(g.mem, memmap.SPEC_STEP, 0)
            self.assertEqual(g.mem, before, task_list)
        put(g.mem, memmap.SPEC_STEP, 0)                              # no mark (a draw pass, a game that races)
        g.manager(20)
        self.assertEqual(g.mem, before)
        put(g.mem, memmap.SPEC_STEP, 1)
        g.manager(20)
        self.assertEqual((get(g.mem, memmap.SPEC_STEP), get(g.mem, 0x80B02000 + 8, 2)), (0, 2))   # the racer wanted
        # a game that races never marks a step
        g = Spectating(self, out=False)
        for _ in range(5):
            g.frame()
        self.assertEqual(get(g.mem, memmap.SPEC_STEP), 0)

    def test_the_log_says_when_the_racer_followed_is_out(self):
        g = self.race().watch(182)
        g.press(RIGHT)
        self.assertEqual(self.last_entry(g)[4] & 0x80, 0)
        self.out(g, 0, 1, 2, 3, 4)                                   # nobody is left racing: the cameras stay
        g.frame()
        entry = self.last_entry(g)
        self.assertEqual((entry[2], entry[4] & 0xC0, entry[5]), (1, 0xC0, 1))         # the racer asked for and shown: out
        # the racer asked for out, another shown: the second bit alone
        g = self.race().watch(182)
        g.press(RIGHT)
        self.out(g, 1)
        g.frame()
        entry = self.last_entry(g)
        self.assertEqual((entry[2], entry[4] & 0xC0, entry[5]), (1, 0x40, 2))

    def test_a_players_game_keeps_no_log(self):
        # the logs' switch is 0 (no launch file asked for a dump): the spectator's log and its place are untouched
        g = Spectating(self, logs=0).watch(182)
        for buttons in (RIGHT, 0, DOWN, 0, RIGHT, 0):
            g.frame(buttons)
        self.assertEqual(g.pictures[-1][:2], ("tv", 2))
        log = range(LOG, LOG + 8 * memmap.SPEC_LOG_ENTRIES)
        self.assertEqual([a for a in g.touched if a in log or a == memmap.SPEC_LOGGED], [])

    def test_the_cameras_tasks_made_anew_run_in_the_step_that_made_them(self):
        # fn_1_5370 makes the cameras' tasks with the flags of [.bss 0xDB0], which is 0: without bit 1 a step that
        # is not a frame's last would leave them out. After the set-up the two cameras' tasks of list 20 have it
        from test_race_end import BSS, MODULE
        manager, sentinel, tasks = 0x80D10000, 0x80D10100, (0x80D10200, 0x80D10300, 0x80D10400)
        routines = (MODULE + 0x90E0 + 0xDC, MODULE + 0x12340, MODULE + 0xEE44 + 0xDC)     # chase, another's, TV
        g = self.race().watch(182)
        g.press(RIGHT)
        for _ in range(6):
            g.press(DOWN)                                            # the chase camera, behind racer 1
        put(g.mem, BSS + 0xDAC, manager); put(g.mem, manager + 0x68, sentinel); put(g.mem, sentinel + 8, tasks[0])
        for task, routine, following in zip(tasks, routines, tasks[1:] + (0,)):
            put(g.mem, task, routine); put(g.mem, task + 0x14, 0); put(g.mem, task + 8, following)
        at = g.step
        g.racing = lambda s: self.out(g, 1) if s == at else None     # the set-up behind racer 2, inside the step
        calls = len(g.calls)
        g.frame()
        self.assertEqual(g.calls[calls:], [(0, 2, at)])
        self.assertEqual([get(g.mem, task + 0x14) for task in tasks], [1, 0, 1])

    def test_no_button_lands_on_a_racer_who_is_out(self):
        g = self.race().watch(182)
        self.out(g, 1); self.out(g, 3, flags=0x80); self.out(g, 4, flags=0x800)
        self.assertEqual(g.pictures[-1][1], 0)
        self.assertEqual(g.press(RIGHT)[1], 2)
        self.assertEqual(g.press(RIGHT)[1], 0)                       # 3, 4 and our own wreck are passed over
        self.assertEqual(g.press(LEFT)[1], 2)
        self.out(g, 0)                                               # the leader goes out while 2 is watched
        self.assertEqual(g.press(UP)[1], 2)                          # the leader is the first still racing
        self.assertEqual(g.press(RIGHT)[1], 2)                       # nobody else is left

    def test_with_nobody_left_racing_the_cameras_stay(self):
        g = self.race().watch(182)
        g.press(RIGHT)
        self.out(g, 0, 1, 2, 3, 4)
        calls = len(g.calls)
        for buttons in (0, RIGHT, 0, LEFT, 0, UP, 0, 0):
            self.assertEqual((g.frame(buttons)[1], get(g.mem, WATCHED)), (1, 1))
        self.assertEqual(len(g.calls), calls)
        for i in range(10):
            self.assertEqual(g.frame(back_to=g.step - 1 - i % 4)[1], 1)

    def test_behind_the_chase_camera_the_move_sets_it_up_anew(self):
        g = self.race().watch(182)
        g.press(RIGHT)
        for _ in range(6):
            g.press(DOWN)
        self.assertEqual(g.pictures[-1], ("chase", 1, 1, "no hud"))
        put(g.mem, CHASE + 0xA8, 5, 2)                               # the state the game's camera takes for a wreck
        at = g.step
        g.racing = lambda s: self.out(g, 1) if s == at else None     # it goes out in this frame's step
        calls = len(g.calls)
        self.assertEqual(g.frame(), ("chase", 2, 1, "no hud"))
        self.assertEqual((g.calls[calls:], get(g.mem, CHASE + 0xA8, 2), get(g.mem, WATCHED)), ([(0, 2, at)], 0, 1))
        for i in range(6):
            self.assertEqual(g.frame(back_to=g.step - 1 - i)[:2], ("chase", 2))
        # set up anew in the step it went out in, each time that step ran, and in no other
        self.assertEqual(set(g.calls[calls:]), {(0, 2, at)})


if __name__ == "__main__":
    unittest.main()
