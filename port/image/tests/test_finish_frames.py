"""The finish and the results, per player, over whole frames: a
wrongly predicted finish, with a rollback as the core makes one. `finwatch` and `hudstate` (frame_hud.py) run as the
bytes the image holds; the game is a made-up simulation whose memory a rollback puts back (main memory: the machines,
their records, the results slot, the HUD's bits), while the control block is never put back. A frame is, as
frame_entry.py has it: the hook (a restore, then the step's undo point), finwatch, the step; for every extra step
the hook, finwatch, the step; then finwatch, hudstate, and the picture. test_finish.py runs `finwatch` alone, one
boundary at a time. No game data."""
import os, struct, sys, tempfile, unittest

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, ".."))
import build, memmap
import follow, frame_hud, native
from test_race_end import Machine, BSS, MODULE, MACHINES, POINTER

IMAGE, CONTROL = memmap.IMAGE, memmap.CONTROL
KIND, END, COUNT = memmap.FINISH_KIND, memmap.FINISH_END, memmap.FINISH_COUNT
SESSION, STEP, SPECTATOR, FIN, KEPT, MASK = (CONTROL + 116, CONTROL + 124, CONTROL + 56, CONTROL + 268, CONTROL + 276,
                                             CONTROL + 60)
KEPT_RECORD, KEPT_SPEED, KEPT_PLACE = CONTROL + 0x400, CONTROL + 0x5FC, CONTROL + 0x600
PHASE, RULES, GAMES_COUNT = BSS + 0x3C2A, BSS + 0x3C30, BSS + 0x50EC
SLOT_SPEED, SLOT_RECORD, SLOT_PLACE = BSS + 0x26304, BSS + 0x26314, BSS + 0x26B04
BITS = (BSS + 0x26B18, BSS + 0x26B19, BSS + 0x26B1A)
HUD_MASK = BSS - 0x10000 + 0xB04
PRESS_A, PAGES_STATE = CONTROL + 0xA34, BSS + 0x8E51D       # the frame routine's A press; the results pages' state
RECORDS = 0x80A00000
SEAT = IMAGE + 0x14
LOCAL = MACHINES + 0x620
FINISHED, BROKEN, DESTROYED, RETIRED = 0x10000, 0x80, 0x10000000, 0x08000000


def put(memory, address, value, size=4):
    memory.update({address + i: b for i, b in enumerate(value.to_bytes(size, "big"))})


def get(memory, address, size=4):
    return int.from_bytes(bytes(memory.get(address + i, 0) for i in range(size)), "big")


class Race:
    """A made-up race of two machines; this game's seat is 1. `inputs(step)` says what the step does to the local
    machine with the inputs known now: "" nothing, "F7" finish in place 7, "X" destroyed, "B" broken down."""

    def __init__(self, image, finwatch, hudstate, control=None, spectator=0, session=1, phase=0x29):
        self.image, self.finwatch, self.hudstate = image, finwatch, hudstate
        self.main, self.window = {}, {}
        put(self.main, 0x800030C8, MODULE); put(self.main, POINTER, MACHINES)
        put(self.main, PHASE, phase, 2); put(self.main, GAMES_COUNT, 0xFFFF, 2)
        put(self.main, HUD_MASK, 0xFF9FFFFF)
        for i in (0, 1):
            put(self.main, MACHINES + i * 0x620 + 0x49C, RECORDS + i * 0x200)
        put(self.window, SESSION, session); put(self.window, SPECTATOR, spectator); put(self.window, MASK, 0xFF9FFFFF)
        for address, value in (control or {}).items():
            put(self.window, address, value)
        self.undo, self.written, self.pictures = {}, set(), []

    step = property(lambda self: get(self.window, STEP))

    def call(self, routine):
        m = Machine(self.image, {**self.main, **self.window})
        m.r[31], m.r[30] = CONTROL, BSS
        m.run(routine)
        for address in m.written:
            size = 1 if address in (FIN, KEPT_PLACE, SLOT_PLACE) + BITS else 4
            target = self.window if address >= IMAGE else self.main
            for i in range(size):
                target[address + i] = m.mem.get(address + i, 0)
        self.written |= m.written
        return m

    def simulate(self, step, what):
        """One step of the made-up simulation: main memory only."""
        flags = get(self.main, LOCAL)
        if what.startswith("F") and not flags & FINISHED:
            flags |= FINISHED
            self.main[LOCAL + 0x482] = int(what[1:])
            put(self.main, LOCAL + 0xD8, 0x44000000 + step)
        elif what == "X":
            flags |= DESTROYED | RETIRED
        elif what == "B":
            flags |= BROKEN
        put(self.main, LOCAL, flags)
        # the progress record goes on changing every step, finished or not (the lap clock, the distance)
        self.main.update({RECORDS + 0x200 + k: (step + k) & 0xFF for k in range(0x1FC)})

    def frame(self, inputs, back_to=None, new=1):
        """A displayed frame: a rollback to `back_to` if given (the steps from there run again), then `new` steps."""
        now = self.step
        first = now if back_to is None else back_to
        if back_to is not None:
            self.main = dict(self.undo[back_to])                 # the hook's restore; the session sets the step word
            put(self.window, STEP, back_to)
        for step in range(first, now + new):
            self.undo[step] = dict(self.main)                    # the hook: this step's undo point
            self.call(self.finwatch)
            self.simulate(step, inputs(step))
            put(self.window, STEP, step + 1)                     # net_step
        self.call(self.finwatch)
        self.call(self.hudstate)
        self.pictures.append(self.picture())
        return self.pictures[-1]

    def picture(self):
        """What fn_1_15C36C and fn_1_15DFD4 draw from the words they read (the six sites and +268)."""
        retired = [self.main.get(a, 0) & 1 for a in BITS]
        fin = self.window.get(FIN, 0) & 1
        count = get(self.window, COUNT)
        results = bool(get(self.main, RULES) & 0x1000)
        if not (fin or any(retired)):
            return "race"
        if results:
            return "list" if count == 0 else "nothing"
        if any(retired):
            return f"retired {count}"
        if count > 480:
            return "bare"                                          # no letters, no HUD parts
        if count < follow.PLACE_FROM:                              # the banner's threshold, as the site gives it
            return f"place {count}"
        return f"FINISH {count}"

    def slot(self):
        return (get(self.main, SLOT_SPEED), bytes(self.main.get(SLOT_RECORD + k, 0) for k in range(0x1FC)),
                self.main.get(SLOT_PLACE, 0))

    def words(self):
        return get(self.window, KIND), get(self.window, END), get(self.window, COUNT), get(self.window, KEPT)


class WholeFrames(unittest.TestCase):
    """Nine races of a few frames each; every one starts at step 1000 with the local machine racing."""
    @classmethod
    def setUpClass(cls):
        cls.dir = tempfile.TemporaryDirectory()
        b = build.build(cls.dir.name)
        image = bytearray(b["image"])
        struct.pack_into(">I", image, SEAT - IMAGE, 1)
        cls.image = bytes(image)
        from keystone import Ks, KS_ARCH_PPC, KS_MODE_PPC32, KS_MODE_BIG_ENDIAN
        text = frame_hud.asm()
        code = bytes(Ks(KS_ARCH_PPC, KS_MODE_PPC32 + KS_MODE_BIG_ENDIAN).asm(text)[0])
        at = IMAGE + cls.image.index(code)
        offsets = native.label_offsets(text)
        cls.finwatch, cls.hudstate = at + offsets["finwatch"], at + offsets["hudstate"]

    @classmethod
    def tearDownClass(cls):
        cls.dir.cleanup()

    def race(self, **more):
        r = Race(self.image, self.finwatch, self.hudstate, **more)
        put(r.window, STEP, 1000)
        return r

    def record(self, step):
        return bytes((step + k) & 0xFF for k in range(0x1FC))

    def test_a_finish_predicted_then_taken_back_is_made_again_three_steps_later(self):
        r = self.race()
        predicted = lambda s: "F3" if s == 1004 else ""
        official = lambda s: "F5" if s == 1007 else ""
        seen = [r.frame(predicted) for _ in range(8)]              # steps 1000..1007 on predictions
        self.assertEqual(seen, ["race"] * 4 + ["FINISH 480", "FINISH 479", "FINISH 478", "FINISH 477"])
        self.assertEqual(r.words(), (1, 1485, 477, 1))
        self.assertEqual(r.slot(), (0x44000000 + 1004, self.record(1004), 3))      # the prediction's results
        # the official inputs of step 1003 on arrive: back to 1003, six steps again (1003..1008)
        after = r.frame(official, back_to=1003)
        self.assertEqual(after, "FINISH 479")                      # finished again at 1007: 480 at 1008, 479 at 1009
        self.assertEqual(r.words(), (1, 1488, 479, 1))
        self.assertEqual(r.slot(), (0x44000000 + 1007, self.record(1007), 5))      # the official results
        # and a rollback that goes back before the official finish and finds it again changes nothing
        again = r.frame(official, back_to=1005)
        self.assertEqual((again, r.words()), ("FINISH 478", (1, 1488, 478, 1)))
        self.assertEqual(r.slot(), (0x44000000 + 1007, self.record(1007), 5))

    def test_a_finish_taken_back_for_good_gives_the_race_picture_back(self):
        r = self.race()
        predicted = lambda s: "F3" if s == 1004 else ""
        official = lambda s: "F5" if s == 1030 else ""
        for _ in range(8):
            r.frame(predicted)
        self.assertEqual(r.frame(official, back_to=1003), "race")  # the banner goes, the HUD is back
        self.assertEqual(r.words(), (0, 1485, 0xFFFF, 0))
        self.assertEqual(r.slot(), (0, bytes(0x1FC), 0))           # the slot is empty again: the restore's
        self.assertEqual(get(r.main, HUD_MASK), 0xFF9FFFFF)
        seen = [r.frame(official) for _ in range(25)]
        self.assertEqual(seen[:20], ["race"] * 20)
        self.assertEqual(seen[21:24], ["FINISH 480", "FINISH 479", "FINISH 478"])
        self.assertEqual(r.slot(), (0x44000000 + 1030, self.record(1030), 5))

    def test_a_finish_taken_back_and_the_machine_destroyed_instead(self):
        r = self.race()
        predicted = lambda s: "F3" if s == 1004 else ""
        official = lambda s: "X" if s == 1006 else ""
        for _ in range(8):
            r.frame(predicted)
        self.assertEqual(r.frame(official, back_to=1004), "retired 298")    # out at 1006: 300 at 1007, now 1009
        self.assertEqual(r.words(), (2, 1307, 298, 0))
        self.assertEqual(r.slot(), (0, bytes(0x1FC), 0))           # no results of its own
        seen = [r.frame(official) for _ in range(300)]
        self.assertEqual((seen[296], seen[297], seen[299]), ("retired 1", "retired 0", "retired 0"))

    def test_a_wreck_taken_back_and_the_machine_finished_instead(self):
        r = self.race()
        predicted = lambda s: "X" if s == 1002 else ""
        official = lambda s: "F9" if s == 1005 else ""
        seen = [r.frame(predicted) for _ in range(5)]
        self.assertEqual(seen, ["race", "race", "retired 300", "retired 299", "retired 298"])
        self.assertEqual(r.frame(official, back_to=1001), "FINISH 480")     # 1001..1005 again: finished at 1005
        self.assertEqual(r.words(), (1, 1486, 480, 1))
        self.assertEqual(r.slot(), (0x44000000 + 1005, self.record(1005), 9))
        self.assertEqual([m & 1 for m in (r.main.get(a, 0) for a in BITS)], [0, 0, 0])

    def test_the_same_finish_step_with_another_place_keeps_the_countdown_and_takes_the_place(self):
        # another racer's corrected input changed the order: the finish step is run again and finishes again
        r = self.race()
        for _ in range(8):
            r.frame(lambda s: "F3" if s == 1004 else "")
        r.frame(lambda s: "F4" if s == 1004 else "", back_to=1004)
        self.assertEqual(r.words(), (1, 1485, 476, 1))             # the countdown goes on unbroken
        self.assertEqual(r.slot()[2], 4)

    def test_a_finish_inside_a_frame_of_three_steps_is_dated_to_its_own_step(self):
        r = self.race()
        self.assertEqual(r.frame(lambda s: "F2" if s == 1001 else "", new=3), "FINISH 479")
        self.assertEqual(r.words(), (1, 1482, 479, 1))
        self.assertEqual(r.slot(), (0x44000000 + 1001, self.record(1001), 2))

    def test_a_machine_still_racing_when_the_hosts_rule_ends_the_race_gets_the_results_as_a_finisher_does(self):
        r = self.race()
        nothing = lambda s: ""
        self.assertEqual([r.frame(nothing) for _ in range(3)], ["race"] * 3)
        # the race-over test says "over" before step 1003 (the session predicts nothing from here): the row is
        # kept at that boundary, before the step runs; the picture is the race's until the results are on
        put(r.window, memmap.END_OVER, 1)
        self.assertEqual(r.frame(nothing), "race")
        self.assertEqual(r.words(), (0, 0, 0xFFFF, 1))
        self.assertEqual(r.slot(), (0, self.record(1002), 0))      # as step 1002 left it: the standing at the end
        self.assertEqual(get(r.main, HUD_MASK), 0xFF9FFFFF)
        # the results phase's first frame sets rule flag 0x1000: the list, the HUD's parts off, the bit the first
        # results page asks for; the machine itself races on, and its row stays the one kept
        put(r.main, RULES, 0x1000)
        self.assertEqual([r.frame(nothing) for _ in range(3)], ["list"] * 3)
        self.assertEqual(r.words(), (0, 0, 0, 1))
        self.assertEqual((r.window.get(FIN, 0), get(r.main, HUD_MASK)), (1, 0))
        self.assertEqual(r.slot(), (0, self.record(1002), 0))
        self.assertEqual([m & 1 for m in (r.main.get(a, 0) for a in BITS)], [0, 0, 0])   # not shown as retired
        self.assertEqual(get(r.main, LOCAL) & FINISHED, 0)         # and not marked finished: only shown so
        # the first results page is a finisher's (its laps, their total): while it stands, the game's own step to
        # the list is asked for, every frame; not once the pages have moved on, where a press would leave the results
        self.assertEqual(get(r.window, PRESS_A), 1)
        put(r.window, PRESS_A, 0)                                  # (the next frame's entry takes it)
        r.main[PAGES_STATE] = 7
        r.frame(nothing)
        self.assertEqual(get(r.window, PRESS_A), 0)
        # without the rule's end nothing of this happens to a machine that races: flag 0x1000 alone keeps no row
        r = self.race()
        put(r.main, RULES, 0x1000)
        r.frame(nothing)
        self.assertEqual((r.words()[3], r.slot()), (0, (0, bytes(0x1FC), 0)))
        # and no finisher is ever given the press: their first page is theirs to read
        r = self.race()
        put(r.window, memmap.END_OVER, 1)
        r.frame(lambda s: "F3" if s == 1000 else "")
        put(r.main, RULES, 0x1000)
        r.frame(nothing)
        self.assertEqual((r.pictures[-1], get(r.window, PRESS_A)), ("list", 0))

    def test_the_watch_writes_the_control_block_only_and_the_hud_pass_what_it_always_wrote(self):
        r = self.race()
        for _ in range(8):
            r.frame(lambda s: "F3" if s == 1004 else "")
        r.frame(lambda s: "X" if s == 1006 else "", back_to=1003)
        window = {a for a in r.written if a >= IMAGE}
        kept = {KEPT_RECORD + 4 * k for k in range(127)} | {KEPT_SPEED, KEPT_PLACE}
        self.assertEqual(window, {KIND, END, COUNT, KEPT, FIN} | kept)
        slot = {SLOT_RECORD + 4 * k for k in range(127)} | {SLOT_SPEED, SLOT_PLACE}
        self.assertEqual({a for a in r.written if a < IMAGE}, slot | set(BITS) | {HUD_MASK})

    def test_a_finish_shows_finish_then_the_place_held_until_the_results(self):
        # versus: FINISH! from 480 to 300, then the place, settled at 0; ours holds it there while the results wait
        r = self.race()
        seen = [r.frame(lambda s: "F4" if s == 1000 else "") for _ in range(700)]
        self.assertEqual((seen[0], seen[180], seen[181], seen[479], seen[480], seen[699]),
                         ("FINISH 480", "FINISH 300", "place 299", "place 1", "place 0", "place 0"))
        put(r.main, RULES, 0x1000)                                 # the results: the list, at once
        self.assertEqual(r.frame(lambda s: ""), "list")

    def test_a_racer_out_and_spectating_never_shows_finish(self):
        # a player report: after a KO, while spectating, FINISH! came later. FINISH! needs the
        # finished bit (+268), which only `hudstate` writes, and `hudstate` writes nothing while spectating (+56)
        # until the results: whatever comes before (seat 0's finish and the game's own countdown), it stays clear
        r = self.race()
        seen = [r.frame(lambda s: "X" if s == 1001 else "") for _ in range(200)]
        self.assertEqual(seen[1], "retired 300")
        r.window[SPECTATOR + 3] = 1                                # the spectator came on (frame_spectate.py, at 180)
        put(r.main, GAMES_COUNT, 480, 2)                           # seat 0, another player's, finishes
        r.frame(lambda s: "")
        put(r.main, LOCAL, get(r.main, LOCAL) | FINISHED)          # even a finished flag on the wreck
        r.frame(lambda s: "")
        self.assertEqual(r.window.get(FIN, 0) & 1, 0)              # never before the results
        self.assertEqual(r.words()[0], 2)                          # the kind stands: out

    def ko_then_results(self, spectating):
        """A KO at step 1001; the race goes on 200 frames (the spectator comes on at 180, if `spectating`); the
        race ends ("over" said); then the results. The machine's place and max speed as the game left them."""
        r = self.race()
        seen = [r.frame(lambda s: "X" if s == 1001 else "") for _ in range(200)]
        self.assertEqual(seen[1], "retired 300")
        if spectating:
            r.window[SPECTATOR + 3] = 1
        r.main[LOCAL + 0x482] = 12
        put(r.main, LOCAL + 0xD8, 0x43000000)
        put(r.window, memmap.END_OVER, 1)
        before = r.frame(lambda s: "")
        return r, before

    def test_a_racer_knocked_out_gets_the_results_pages_with_its_own_row(self):
        # a KO'd player sees the results like everyone else; stock versus shows none
        for spectating in (False, True):
            with self.subTest(spectating=spectating):
                r, before = self.ko_then_results(spectating)
                # "over" before step 1200: the row kept at that boundary, as step 1199 left it
                self.assertEqual(r.words(), (2, 1302, 101, 1))
                kept = (get(r.window, KEPT_SPEED), bytes(r.window.get(KEPT_RECORD + k, 0) for k in range(0x1FC)),
                        r.window.get(KEPT_PLACE, 0))
                self.assertEqual(kept, (0x43000000, self.record(1199), 12))
                self.assertEqual(get(r.window, PRESS_A), 0)        # no results yet: nothing pressed
                self.assertEqual(r.window.get(FIN, 0) & 1, 0)
                # the results: the pages for this viewport, the race's HUD and the RETIRED banner's bits off
                put(r.main, RULES, 0x1000)
                self.assertEqual([r.frame(lambda s: "") for _ in range(3)], ["list"] * 3)
                self.assertEqual((r.window.get(FIN, 0), get(r.main, HUD_MASK)), (1, 0))
                self.assertEqual([m & 1 for m in (r.main.get(a, 0) for a in BITS)], [0, 0, 0])
                self.assertEqual(r.slot(), (0x43000000, self.record(1199), 12))   # the row stays the kept one
                self.assertEqual(get(r.main, LOCAL) & FINISHED, 0)                # shown so, never marked
                # no lap-times page: the game's own step past it is asked for while it stands, and only then
                self.assertEqual(get(r.window, PRESS_A), 1)
                put(r.window, PRESS_A, 0)
                r.main[PAGES_STATE] = 7
                r.frame(lambda s: "")
                self.assertEqual(get(r.window, PRESS_A), 0)

    def test_a_finished_racer_spectating_gets_its_results_pages(self):
        # frame_spectate.py `spectate` puts a finished racer to spectating (+56) and takes its finished bit (+268)
        # off: no FINISH! or place over the racer watched. Until the results nothing writes it back; with them the
        # pages are its own, as a finisher's, the race's HUD off
        r = self.race()
        seen = [r.frame(lambda s: "F4" if s == 1000 else "") for _ in range(500)]
        self.assertEqual((seen[1], seen[499]), ("FINISH 480", "place 0"))
        r.window[SPECTATOR + 3] = 1
        r.window[FIN] = 0
        self.assertEqual([r.frame(lambda s: "") for _ in range(3)], ["race"] * 3)
        self.assertEqual(r.window.get(FIN, 0), 0)
        put(r.main, RULES, 0x1000)
        self.assertEqual([r.frame(lambda s: "") for _ in range(3)], ["list"] * 3)
        self.assertEqual((r.window.get(FIN, 0), get(r.main, HUD_MASK), get(r.window, PRESS_A)), (1, 0, 0))

    def test_a_spectator_whose_machine_races_is_left_alone_at_the_results(self):
        r = self.race(spectator=1)
        put(r.main, RULES, 0x1000)
        r.frame(lambda s: "")
        self.assertEqual((r.window.get(FIN, 0), get(r.window, PRESS_A), get(r.main, HUD_MASK)), (0, 0, 0xFF9FFFFF))

    def test_a_race_before_in_the_same_process_leaves_nothing_in_the_next(self):
        left = {KIND: 1, END: 5000, COUNT: 77, KEPT: 1}
        r = self.race(control=left, phase=0x27)                    # the intro: before the race phase
        r.window[FIN] = 1
        r.frame(lambda s: "", new=0)
        self.assertEqual(r.words(), (0, 5000, 0xFFFF, 0))
        r = self.race(control=left)                                # the race phase, the machine racing
        r.window[FIN] = 1
        self.assertEqual(r.frame(lambda s: ""), "race")
        self.assertEqual(r.words(), (0, 5000, 0xFFFF, 0))

    def test_a_spectator_word_left_set_keeps_the_race_before_s_countdown(self):
        # an open item: no process runs two races today, and the word is the core's to clear
        left = {KIND: 2, END: 5000, COUNT: 0, KEPT: 0}
        r = self.race(control=left, spectator=1)                   # +56 is never cleared by the image
        r.frame(lambda s: "")
        self.assertEqual(r.words()[:3], (2, 5000, 5000 - 1001))    # a countdown of 3,999: the old race's end step


if __name__ == "__main__":
    unittest.main()
