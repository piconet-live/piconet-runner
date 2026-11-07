"""The spectator's controls and cameras (frame_spectate.py), run here as the bytes the image holds against a
made-up game. The game's memory is put back by a rollback as the core makes one (main memory: the pad block, the
cameras, the machines); the control block is not. A frame is, as frame_entry.py has it: `speccontrols` on entry,
then for every step the hook (a restore, the step's undo point), `specstep`, the step (the race's tasks, the task
manager's own hook where its pass reaches the cameras' list, the cameras' tasks); then `spectate` and `spechud`. The game's own routines are stand-ins that do what the disassembly says of them: fn_1_5370 (the
cameras' set-up) and the TV task fn_1_EE44 (a shot is set up when the wanted type or variant differs from the one
in use; the picker of the automatic style). No game data."""
import os, struct, sys, unittest

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, ".."))
import build, memmap  # noqa: F401  (build puts the generators' folder on the path)
from test_race_end import BSS, MODULE, MACHINES, POINTER
from test_raceview import (Build, Cpu, get, put, IMAGE, CONTROL, INPUTS, TEXT, SESSION, RUNNING, STEP, OUT_FRAMES,
                           CHASE, PHASE, LOCAL)

WATCHED, SPECTATOR, MASK_KEPT, PAD_BEFORE, HUD_OFF, INJECTED = (CONTROL + 252, CONTROL + 56, CONTROL + 60, CONTROL + 64,
                                                               CONTROL + 68, CONTROL + 72)
MODE, HEIGHT, CHANGE, PENDING, LOGGED = (memmap.SPEC_MODE, memmap.SPEC_HEIGHT, memmap.SPEC_CHANGE,
                                         memmap.SPEC_PENDING, memmap.SPEC_LOGGED)
GAME_MODE, COUNT = BSS + 0x960, BSS + 0x6D84A
PAD, PAD_CY = BSS + 0x998, BSS + 0x99D
MODE_POINTER, CHASE_POINTER, LIVE_POINTER = BSS + 0xF68, BSS + 0xF6C, BSS + 0xF70
HUD_MASK, HUD_BITS, HUD_BYTE = BSS - 0x10000 + 0xB04, (BSS + 0x26B18, BSS + 0x26B19, BSS + 0x26B1A), BSS + 0x8FDA8
RECORDS, CAMERAS, LIVE, STACK = 0x80A00000, 0x80B00000, 0x80B02000, 0x80C00000
SEAT, GENERATOR = IMAGE + 0x14, 0x801A63C0
FN_5370 = TEXT + 0x5370
LEFT, RIGHT, DOWN, UP, A, C_UP, C_DOWN = 0x1, 0x2, 0x4, 0x8, 0x100, 0x10000, 0x20000
RACERS = 6
VARIANTS = memmap.SPEC_HEIGHTS[memmap.SPEC_MANUAL:memmap.SPEC_MANUAL + 4]      # 6, 20, 9, 2
AUTOMATIC, COURSE, MANUAL, CHASING = memmap.SPEC_AUTOMATIC, memmap.SPEC_COURSE, memmap.SPEC_MANUAL, memmap.SPEC_CHASE


class Spectating:
    """A made-up race of six machines seen from seat LOCAL (5, in the last place), which is out. `pad(frame)` is this game's pad on a frame:
    buttons, or (buttons, C-stick Y)."""

    def __init__(self, t, game_mode=2, out=True, phase=0x29, places=None, logs=1):
        self.t, self.mem, self.calls, self.setups = t, {}, [], []
        self.section = False                 # the watched racer is on a section where the TV task picks the type
        self.picks = []                      # the types it draws there, when a test says them
        self.leaving = False                 # type 3 with its second variant asks for type 0 (seen in run cam-r3)
        self.undo, self.pictures, self.frames, self.touched = {}, [], 0, set()
        self.racing = lambda step: None      # the race's tasks of a step: a test puts a racer out in one
        m = self.mem
        put(m, 0x800030C8, MODULE); put(m, POINTER, MACHINES); m[COUNT] = RACERS
        put(m, GAME_MODE, game_mode, 2); put(m, PHASE, phase, 2)
        put(m, MODE_POINTER, CAMERAS); put(m, CHASE_POINTER, CHASE); put(m, LIVE_POINTER, LIVE)
        put(m, HUD_MASK, 0xFF9FFFFF); put(m, CHASE + 2, LOCAL, 2); put(m, CHASE + 0xA4, 1, 2)
        for i in range(RACERS):
            put(m, MACHINES + i * 0x620 + 0x49C, RECORDS + i * 0x200)
            m[RECORDS + i * 0x200 + 0x115] = (places or list(range(RACERS)))[i]
        if out:
            put(m, MACHINES + LOCAL * 0x620, 0x08000000)
        put(m, SESSION, 1); put(m, RUNNING, 1); put(m, STEP, 500); put(m, MASK_KEPT, 0xFF9FFFFF)
        put(m, memmap.IMAGE_LOGS, logs)      # the host's word: this game keeps the tests' logs (a players' game: 0)

    step = property(lambda self: get(self.mem, STEP))
    main = lambda self: {a: b for a, b in self.mem.items() if a < IMAGE}

    def game(self, target, cpu):
        """fn_1_5370(kind, racer): 1 the TV cameras (camera mode 9; the live camera as fn_1_EBE4 leaves it), 0 the
        chase camera (mode 0; the record as fn_1_8D08 makes it anew)."""
        self.t.assertEqual(target, FN_5370)
        kind, racer = cpu.r[3], cpu.r[4]
        self.calls.append((kind, racer, cpu.read(STEP, 4)))
        if kind == 1:
            cpu.write(CAMERAS + 0x48, 1, 9)
            for offset, value in ((0x12, 2), (0x14, 9), (0x8, racer), (0x6, racer)):
                cpu.write(LIVE + offset, 2, value)
            cpu.write(LIVE, 1, 5)
        else:
            cpu.write(CAMERAS + 0x48, 1, 0)
            for offset, value in ((0x2, racer), (0xA4, 1), (0xA8, 0)):
                cpu.write(CHASE + offset, 2, value)

    def call(self, name, **registers):
        cpu = Cpu(self.t.image, self.mem, self.game)
        cpu.r[31], cpu.r[30] = CONTROL, BSS
        cpu.run(self.t.routines[name], limit=20000)
        self.mem = cpu.mem
        self.touched |= {a for a in cpu.written if not STACK - 0x100 <= a < STACK + 0x100}
        return cpu

    def manager(self, task_list):
        """The task manager's pass at the top of a list (fn_1_407C's 0x40AC, where smooth.py's hook is called): its
        registers are the game's, and the hook gives back what the replaced instruction did."""
        cpu = Cpu(self.t.image, self.mem, self.game)
        cpu.r[27], cpu.r[30], cpu.r[31] = task_list, 0x30303030, 0x31313131
        cpu.run(self.t.symbols["smooth_hook"], limit=20000)
        self.t.assertEqual((cpu.r[0], cpu.r[1], cpu.r[27], cpu.r[30], cpu.r[31]), (1, STACK, task_list, 0x30303030, 0x31313131))
        self.mem = cpu.mem
        self.touched |= {a for a in cpu.written if not STACK - 0x100 <= a < STACK + 0x100}

    def simulate(self, step):
        """One step of the game after `specstep`: the race's tasks (the stand-in's camera record among them), the
        task manager's hook before the cameras' list, then the cameras' tasks."""
        self.racing(step)
        m = self.mem
        chasing = get(m, OUT_FRAMES) >= 180 and get(m, MODE) == CHASING
        cam_seat = bytes.fromhex("B1690002")                         # standin_routine.py: sth r11, 2(r9), left out
        self.t.assertEqual(self.t.image.count(cam_seat), 1)          # behind the spectator's chase camera
        if not chasing:
            put(m, CHASE + 2, LOCAL, 2)
        self.manager(20)
        m = self.mem
        if m.get(CAMERAS + 0x48) == 9:
            self.tv_task(step)

    def pick(self):
        """The type the TV task draws on a section where it picks the type itself: 1 or 3."""
        if self.picks:
            return self.picks.pop(0)
        word = (get(self.mem, GENERATOR) * 0x41C64E6D + 0x3039) & 0xFFFFFFFF
        put(self.mem, GENERATOR, word)
        return 1 if word >> 16 & 1 else 3

    def set_up(self, step, what):
        """A shot's set-up, as the type's four routines do it: with the type and the variant in use; the automatic
        styles draw their variant, style 4 reads the variant's table as it finds it."""
        m = self.mem
        kind, style = get(m, LIVE + 2, 2), m.get(LIVE, 0)
        if style in (1, 2, 5) and kind < 4:
            put(m, LIVE + 4, step % VARIANTS[kind], 2)
            put(m, LIVE + 0x14, step % VARIANTS[kind], 2)
        variant = get(m, LIVE + 4, 2)
        self.setups.append((step, what, kind, variant))
        if kind < 4:
            self.t.assertLess(variant, VARIANTS[kind], f"step {step}: type {kind} set up with variant {variant}")

    def tv_task(self, step):
        """fn_1_EE44 from 0xF080 on: a wanted type or variant that is not the one in use is set up, and on some
        sections the wanted type is drawn anew first (0xF0C0-0xF184, 0xF1F8-0xF2BC)."""
        m = self.mem
        style = m.get(LIVE, 0)
        if style in (1, 2, 5) and step % 7 == 0:                     # the picker, when its timer has run out
            put(m, LIVE + 0x12, (get(m, LIVE + 2, 2) + 1) % 4, 2)
        if get(m, LIVE + 2, 2) != get(m, LIVE + 0x12, 2):
            if self.section:
                put(m, LIVE + 0x12, self.pick(), 2)
            put(m, LIVE + 2, get(m, LIVE + 0x12, 2), 2)
            self.set_up(step, "type")
        if get(m, LIVE + 4, 2) != get(m, LIVE + 0x14, 2):
            if self.section:
                put(m, LIVE + 0x12, self.pick(), 2)
            put(m, LIVE + 4, get(m, LIVE + 0x14, 2), 2)
            self.set_up(step, "variant")
        put(m, LIVE + 6, get(m, LIVE + 8, 2), 2)
        if self.leaving and get(m, LIVE + 2, 2) == 3 and get(m, LIVE + 4, 2) == 1:
            put(m, LIVE + 0x12, 0, 2)                                # a shot's own update asks for another type

    def frame(self, pad=0, back_to=None, new=1):
        """A displayed frame: the pad is read, a rollback to `back_to` if given (the steps from there run again),
        `new` steps, the picture."""
        buttons, stick = pad if isinstance(pad, tuple) else (pad, 0)
        put(self.mem, PAD, buttons, 2); self.mem[PAD_CY] = stick & 0xFF
        self.call("speccontrols")
        now = self.step
        first = now if back_to is None else back_to
        if back_to is not None:                                      # the hook's restore; the session sets the step
            self.mem = {**{a: b for a, b in self.mem.items() if a >= IMAGE}, **self.undo[back_to]}
            put(self.mem, STEP, back_to)
        for step in range(first, now + new):
            self.undo[step] = self.main()                            # the hook: this step's undo point
            self.call("specstep")
            self.simulate(step)
            put(self.mem, STEP, step + 1)                            # net_step
        self.call("spectate")
        self.call("spechud")
        self.frames += 1
        self.pictures.append(self.picture())
        return self.pictures[-1]

    def picture(self):
        """What the frame shows: the cameras in use and the racer they are on, read from the game's own words."""
        m = self.mem
        if get(m, OUT_FRAMES) < 180:
            return ("racing", get(m, CHASE + 2, 2))
        hud = "hud" if get(m, HUD_MASK) else "no hud"
        if m.get(CAMERAS + 0x48) == 9:
            return ("tv", get(m, LIVE + 6, 2), m.get(LIVE, 0), get(m, LIVE + 2, 2), get(m, LIVE + 4, 2), hud)
        return ("chase", get(m, CHASE + 2, 2), get(m, CHASE + 0xA4, 2), hud)

    def watch(self, frames=180):
        """Out for three seconds: the TV cameras come on, on the leader."""
        for _ in range(frames):
            self.frame()
        return self

    def press(self, buttons, **how):
        """A press of one frame, then a frame with the pad let go."""
        self.frame(buttons, **how)
        return self.frame()


class TheSpectator(Build):
    def race(self, **how):
        return Spectating(self, **how)

    def test_three_seconds_out_and_the_tv_cameras_are_on_the_leader(self):
        g = self.race(places=[2, 5, 0, 1, 3, 4])
        for _ in range(179):
            self.assertEqual(g.frame(), ("racing", LOCAL))
        self.assertEqual((get(g.mem, SPECTATOR), g.calls), (0, []))
        g.frame()                                                    # the 180th: the choice; the switch is a step's
        self.assertEqual((get(g.mem, SPECTATOR), get(g.mem, WATCHED)), (1, 2))
        shown = g.frame()                                            # the automatic style draws its own variant
        self.assertEqual(shown[:4] + shown[5:], ("tv", 2, 5, 2, "hud"))
        self.assertEqual(g.calls, [(1, 2, g.step - 1)])
        # a racer that finished is not a spectator
        g = self.race()
        put(g.mem, MACHINES + LOCAL * 0x620, 0x08010000)
        g.watch(200)
        self.assertEqual((get(g.mem, SPECTATOR), g.pictures[-1]), (0, ("racing", LOCAL)))

    def test_the_racer_buttons(self):
        g = self.race().watch(182)
        self.assertEqual(g.pictures[-1][:2], ("tv", 0))
        self.assertEqual(g.press(RIGHT)[1], 1)
        self.assertEqual(g.press(RIGHT)[1], 2)
        self.assertEqual(g.press(LEFT)[1], 1)
        self.assertEqual(g.press(LEFT)[1], 0)
        self.assertEqual(g.press(LEFT)[1], 4)                        # round, in race order, past our own wreck
        self.assertEqual(g.press(UP)[1], 0)                          # the leader
        put(g.mem, MACHINES + 2 * 0x620, 0x80)                       # the third is out: passed over
        g.press(RIGHT)
        self.assertEqual(g.press(RIGHT)[1], 3)
        self.assertEqual(len(g.calls), 1)                            # the TV cameras were set up once

    def test_a_held_button_is_one_press(self):
        g = self.race().watch(182)
        for _ in range(30):
            g.frame(RIGHT)
        self.assertEqual(g.pictures[-1][1], 1)
        g.frame()
        self.assertEqual(g.frame(RIGHT)[1], 2)

    def test_a_press_holds_through_a_rollback_of_any_depth(self):
        for depth in range(1, 18):
            g = self.race().watch(200)
            g.frame(RIGHT)                                           # the press; its first step sets it
            at = g.step - 1
            for back in range(depth, 0, -1):                         # every later frame goes back, deeper each time,
                g.frame(back_to=g.step - back)                       # over and to before the press's step
                self.assertEqual(g.pictures[-1][1], 1, (depth, back))
            g.frame(back_to=at - 3, new=2)
            self.assertEqual((g.pictures[-1][1], get(g.mem, WATCHED)), (1, 1), depth)

    def test_a_press_on_a_frame_with_a_rollback_is_taken_once(self):
        # the pad block is memory a rollback puts back: the restore of this frame leaves the pad of 9 frames ago
        # in it, with another button held. The press is read before the restore, and no press comes from the old pad
        g = self.race().watch(200)
        early = g.step
        g.frame(LEFT); g.frame(); self.assertEqual(g.pictures[-1][1], 4)
        for _ in range(8):
            g.frame()
        g.frame(RIGHT, back_to=early)                                # the undo point of `early` holds LEFT's pad
        self.assertEqual(get(g.mem, PAD, 2), LEFT)
        self.assertEqual(g.pictures[-1][1], 0)
        for _ in range(5):
            self.assertEqual(g.frame(back_to=g.step - 4)[1], 0)      # neither a second RIGHT nor the old LEFT
        self.assertEqual(get(g.mem, WATCHED), 0)

    def test_a_rollback_every_frame_loses_no_press_and_doubles_none(self):
        g = self.race().watch(200)
        wanted = 0
        for i in range(60):
            press = RIGHT if i % 3 == 0 else 0
            wanted = (wanted + bool(press)) % (RACERS - 1)           # our own wreck is passed over
            g.frame(press, back_to=g.step - (1 + i % 11))
            self.assertEqual((g.pictures[-1][1], get(g.mem, WATCHED)), (wanted, wanted), i)

    def test_the_modes_go_round(self):
        g = self.race().watch(182)
        seen = []
        for _ in range(8):
            g.press(DOWN)
            g.frame(); g.frame()
            seen.append((get(g.mem, MODE), g.pictures[-1][0], g.pictures[-1][-1]))
        self.assertEqual([s[0] for s in seen], [1, 2, 3, 4, 5, 6, 0, 1])
        self.assertEqual([s[1] for s in seen], ["tv"] * 5 + ["chase"] + ["tv"] * 2)
        self.assertEqual([s[2] for s in seen], ["hud"] * 5 + ["no hud"] + ["hud"] * 2)      # none behind the chase camera
        self.assertEqual(memmap.SPEC_MODES, 7)

    def test_the_styles_and_the_manual_shots(self):
        g = self.race().watch(182)
        self.assertEqual(g.pictures[-1][2], 5)                       # automatic: the game's own style and picker
        g.press(DOWN)
        self.assertEqual(g.pictures[-1][2], 1)                       # the course's cameras
        for kind in range(4):
            g.press(DOWN)
            self.assertEqual(g.pictures[-1][2:5], (4, kind, 0), kind)        # no picker; this type, variant 0
        variants = memmap.SPEC_HEIGHTS[MANUAL:MANUAL + 4]
        self.assertEqual(variants, (6, 20, 9, 2))
        g.press(DOWN); g.press(DOWN); g.press(DOWN); g.press(DOWN)           # round to the first manual shot
        self.assertEqual(g.pictures[-1][2:5], (4, 0, 0))
        for kind in range(4):
            for variant in list(range(1, variants[kind])) + [0]:     # the C-stick up: every variant, then round
                g.press((0, 100))
                self.assertEqual(g.pictures[-1][2:5], (4, kind, variant), (kind, variant))
            g.press((0, -100))                                       # down from the first: the last
            self.assertEqual(g.pictures[-1][4], variants[kind] - 1)
            g.press((0, -100))
            self.assertEqual(g.pictures[-1][4], variants[kind] - 2 if variants[kind] > 2 else 0)
            g.press(DOWN)

    def test_a_mode_holds_through_rollbacks(self):
        g = self.race().watch(200)
        g.press(DOWN); g.press(DOWN); g.press(DOWN); g.press(DOWN)   # manual shot 2
        g.frame((0, 100)); g.frame(); g.frame((0, 100))              # variant 2
        for i in range(40):
            g.frame(back_to=g.step - (1 + i % 17))
            self.assertEqual(g.pictures[-1][:5], ("tv", 0, 4, 2, 2), i)
        # automatic again: the picker's own shots, which no step of ours writes over
        for _ in range(3):
            g.press(DOWN)
        self.assertEqual(get(g.mem, MODE), AUTOMATIC)
        kinds = set()
        for i in range(40):
            kinds.add(g.frame(back_to=g.step - (1 + i % 5))[3])
            self.assertEqual(g.pictures[-1][2], 5)
        self.assertGreater(len(kinds), 1)

    def test_the_chase_camera_behind_the_watched_racer(self):
        g = self.race().watch(182)
        g.press(RIGHT); g.press(RIGHT)                               # racer 2
        for _ in range(6):
            g.press(DOWN)
        self.assertEqual(g.pictures[-1], ("chase", 2, 1, "no hud"))
        self.assertEqual(g.calls[-1][:2], (0, 2))
        calls = len(g.calls)
        g.press((0, 100))                                            # the C-stick: the view, with no set-up
        self.assertEqual(g.pictures[-1], ("chase", 2, 2, "no hud"))
        g.press((0, 100)); self.assertEqual(g.pictures[-1][2], 3)
        g.press((0, 100)); self.assertEqual(g.pictures[-1][2], 0)    # first person, a view like the others
        g.press((0, 100)); self.assertEqual(g.pictures[-1][2], 1)
        g.press((0, -100)); self.assertEqual(g.pictures[-1][2], 0)
        g.press((0, -100)); self.assertEqual(g.pictures[-1][2], 3)
        self.assertEqual(len(g.calls), calls)
        g.frame(RIGHT)                                               # another racer: the record is made anew
        at = g.step - 1
        self.assertEqual((g.pictures[-1], g.calls[calls:]), (("chase", 3, 3, "no hud"), [(0, 3, at)]))
        for i in range(30):                                          # and holds, with its view, through rollbacks
            g.frame(back_to=g.step - (1 + i % 17))
            self.assertEqual(g.pictures[-1], ("chase", 3, 3, "no hud"), i)
        # a rollback to before the TV cameras were left puts their mode back: the first step run again leaves them
        g = self.race().watch(182)
        for _ in range(5):
            g.press(DOWN)
        before = g.step
        g.frame(DOWN); g.frame()
        g.frame(back_to=before - 1)
        self.assertEqual(g.pictures[-1], ("chase", 0, 1, "no hud"))
        g.press(DOWN)                                                # and round to the TV cameras
        self.assertEqual(g.pictures[-1][:3], ("tv", 0, 5))

    def test_a_finished_racer_spectates_once_its_finish_camera_s_first_round_is_over(self):
        # the local racer finished at the boundary before step 500 (frame_hud.py `finwatch`: FINISH_END is that step
        # plus 480); memmap.FINISH_SPECTATE steps later its game spectates, as a racer out does, on the best-placed
        # racer still racing: racer 0 has finished too and racer 1 is out, so racer 2
        after = memmap.FINISH_SPECTATE
        g = self.race(out=False)
        put(g.mem, MACHINES + LOCAL * 0x620, 0x04010000)             # finished, a CPU's driver since
        put(g.mem, MACHINES, 0x04010000); put(g.mem, MACHINES + 0x620, 0x08000000)
        put(g.mem, memmap.FINISH_KIND, 1); put(g.mem, memmap.FINISH_END, 500 + 480)
        put(g.mem, CONTROL + 268, 1 << 24)                           # the finished bit for the HUD (a byte)
        for _ in range(after - 1):
            self.assertEqual(g.frame(), ("racing", LOCAL))
        self.assertEqual((get(g.mem, SPECTATOR), g.calls, g.step), (0, [], 500 + after - 1))
        g.frame()                                                    # FINISH_SPECTATE steps after the finish
        self.assertEqual((get(g.mem, SPECTATOR), get(g.mem, OUT_FRAMES), get(g.mem, WATCHED)), (1, 180, 2))
        self.assertEqual(g.mem.get(CONTROL + 268), 0)                # no FINISH! or place over the racer watched
        shown = g.frame()
        self.assertEqual(shown[:2] + shown[5:], ("tv", 2, "hud"))
        self.assertEqual(g.calls, [(1, 2, g.step - 1)])
        self.assertEqual(g.press(RIGHT)[1], 3)                       # the controls as for a racer out
        # nobody left racing: the leader, as for a racer out
        g = self.race(out=False)
        for seat in range(RACERS):
            put(g.mem, MACHINES + seat * 0x620, 0x04010000 if seat != 1 else 0x08000000)
        put(g.mem, memmap.FINISH_KIND, 1); put(g.mem, memmap.FINISH_END, 980)
        g.watch(after)
        self.assertEqual((get(g.mem, SPECTATOR), get(g.mem, WATCHED)), (1, 0))
        # the results began first: they stay (no spectating); and a racer only out by a rollback's guess keeps racing
        for kind, rules in ((1, 0x1000), (0, 0)):
            g = self.race(out=False)
            put(g.mem, MACHINES + LOCAL * 0x620, 0x04010000)
            put(g.mem, memmap.FINISH_KIND, kind); put(g.mem, memmap.FINISH_END, 980); put(g.mem, BSS + 0x3C30, rules)
            g.watch(after + 20)
            self.assertEqual((get(g.mem, SPECTATOR), g.pictures[-1]), (0, ("racing", LOCAL)), (kind, rules))

    def test_a_hides_the_hud(self):
        g = self.race().watch(182)
        self.assertEqual(g.press(A)[-1], "no hud")
        self.assertEqual(g.frame(back_to=g.step - 5)[-1], "no hud")
        self.assertEqual(g.press(A)[-1], "hud")
        for a, bit in zip(HUD_BITS, (0, 0, 0)):                      # port 0 is not marked retired for the HUD
            self.assertEqual(g.mem.get(a, 0) & 1, bit)

    def test_the_results_take_the_hud_off(self):
        # rule flag 0x1000: frame_hud.py `hudstate` gives the out racer the results pages; the HUD's parts would
        # stand over them, so they stay off whatever the HUD switch says
        g = self.race().watch(182)
        self.assertEqual(g.frame()[-1], "hud")
        put(g.mem, BSS + 0x3C30, 0x1000)
        self.assertEqual(g.frame()[-1], "no hud")
        self.assertEqual(g.press(A)[-1], "no hud")
        self.assertEqual(g.press(A)[-1], "no hud")

    def test_injected_buttons_are_presses_too(self):
        g = self.race().watch(182)
        put(g.mem, INJECTED, RIGHT); g.frame()
        self.assertEqual((g.frame()[1], get(g.mem, INJECTED)), (1, 0))
        g.press(DOWN); g.press(DOWN)
        put(g.mem, INJECTED, C_UP); g.frame()
        self.assertEqual(g.frame()[2:5], (4, 0, 1))
        put(g.mem, INJECTED, C_DOWN); g.frame()
        self.assertEqual(g.frame()[2:5], (4, 0, 0))

    def test_nothing_is_read_before_spectating(self):
        g = self.race(out=False)
        for buttons in (RIGHT, 0, DOWN, 0, A, 0, (0, 100), 0):
            g.frame(buttons)
        self.assertEqual([get(g.mem, a) for a in (WATCHED, MODE, HEIGHT, HUD_OFF, PENDING)], [0] * 5)
        self.assertEqual(g.pictures[-1], ("racing", LOCAL))

    def test_what_the_routines_write(self):
        g = self.race().watch(182)
        for buttons in (RIGHT, 0, DOWN, 0, DOWN, 0, DOWN, 0, (0, 100), 0, A, 0, DOWN, DOWN, 0, DOWN, 0, DOWN, 0, DOWN, 0, LEFT):
            g.frame(buttons, back_to=g.step - 3)
        log = (memmap.SPEC_LOG, memmap.SPEC_LOG + 8 * memmap.SPEC_LOG_ENTRIES)
        window = {a for a in g.touched if a >= IMAGE and not log[0] <= a < log[1]}
        self.assertEqual(window, {SPECTATOR, MASK_KEPT, PAD_BEFORE, HUD_OFF, INJECTED, OUT_FRAMES, WATCHED, MODE, HEIGHT,
                                  CHANGE, PENDING, LOGGED, memmap.SPEC_STEP, memmap.SPEC_ASKED})
        main = {a for a in g.touched if a < IMAGE}
        # the cameras' words and the HUD's: no machine, no progress record, no input row, not the generator
        self.assertEqual(main, {LIVE, LIVE + 4, LIVE + 8, LIVE + 0x12, LIVE + 0x14, CHASE + 2, CHASE + 0xA4,
                                CHASE + 0xA8, LIVE + 6, CAMERAS + 0x48, HUD_MASK, HUD_BYTE, *HUD_BITS})
        for a in g.touched:
            self.assertFalse(MACHINES <= a < MACHINES + RACERS * 0x620 or RECORDS <= a < RECORDS + RACERS * 0x200, hex(a))
            self.assertFalse(INPUTS <= a < memmap.INPUTS_END or a == SEAT or a == GENERATOR, hex(a))

    def test_the_log_has_an_entry_a_frame(self):
        g = self.race().watch(182)
        logged = get(g.mem, LOGGED)
        self.assertEqual(logged, 3)                                  # from the frame the spectator's HUD came on
        g.press(RIGHT); g.press(DOWN); g.press(DOWN); g.frame((0, 100)); g.frame()
        entry = lambda n: bytes(g.mem.get(memmap.SPEC_LOG + 8 * n + i, 0) for i in range(8))
        last = entry(get(g.mem, LOGGED) - 1)
        self.assertEqual(struct.unpack(">H", last[:2])[0], g.step & 0xFFFF)
        self.assertEqual(tuple(last[2:]), (1, MANUAL << 4 | 9, 1, 1, 0, 1))      # racer 1, manual shot 0, variant 1
        for _ in range(4):
            g.press(DOWN)
        last = entry(get(g.mem, LOGGED) - 1)
        self.assertEqual(tuple(last[2:]), (1, CHASING << 4 | 0, 0, 1, 1, 0))     # the chase camera: racer, view, state
        put(g.mem, LOGGED, memmap.SPEC_LOG_ENTRIES - 1)
        g.frame(); g.frame()
        self.assertEqual(struct.unpack(">H", entry(0)[:2])[0], g.step & 0xFFFF)  # round, inside its own memory

    def test_only_the_spectators_routines_name_its_words(self):
        # an instruction whose displacement is one of the five words' offsets, off any register: inside
        # frame_spectate.py's text, but for the stand-in's one read of the mode (the camera record, chase or not);
        # and ghost_disp's reads of a machine's part record, +0x3A0 of the car: another register, the same number
        words = {a - CONTROL for a in (MODE, HEIGHT, CHANGE, PENDING, LOGGED)}
        outside = []
        for o in range(0x10, len(self.image), 4):
            w = struct.unpack_from(">I", self.image, o)[0]
            if w >> 26 in (32, 36) and w & 0xFFFF in words and not self.spectator_text[0] <= IMAGE + o < self.spectator_text[1]:
                outside.append((IMAGE + o, w))
        outside = [(at, w) for at, w in outside if w != 0x807D0000 | 0x3A0]
        self.assertEqual(MODE - CONTROL, 0x3A0)
        self.assertEqual([w for _, w in outside], [0x80070000 | (MODE - CONTROL)])     # lwz r0, SPEC_MODE(r7)
        self.assertLess(outside[0][0], self.symbols["frame_routine"])                 # in the seat routines


if __name__ == "__main__":
    unittest.main()
