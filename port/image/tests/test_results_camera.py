"""The racer the results' cameras follow (follow.py, the results camera's site 0x3B14C): the helper run here as the
bytes the image holds, against a made-up game of three machines and their car objects. The local seat is 1. A local
machine out by the game's own verdict whose body the game no longer draws (car object +0x390 bit 0x01000000, set by
fn_1_25004 for a wreck that explodes or a fall below the course) gives the winner; every other case the local seat,
as before. While the results run, `results_watch` (follow.py) moves the cameras from the local wreck to the winner when
the wreck goes (a computer machine hit it). No game data."""
import os, struct, sys, tempfile, unittest

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, ".."))
import build, follow, memmap, native
from test_race_end import Machine, signed, BSS, MODULE, MACHINES, POINTER, COUNT

IMAGE, CONTROL, TEXT = memmap.IMAGE, memmap.CONTROL, memmap.REL_TEXT
KEEP, CAMERA = memmap.RESULTS_KEEP, memmap.RESULTS_CAMERA
COURSE = memmap.RACE + memmap.RACE_COURSE
RULES, RACE_TYPE = BSS + 0x3C30, BSS + 0x3C35
CARS_POINTER, CARS = BSS + 0x6D83C, 0x80980000
RECORDS = 0x80A00000
SEAT = IMAGE + 0x14
SITE = 0x3B14C
RESTORE = 0x8000
FINISHED, CPU = 0x10000, 0x200
# what fn_1_25004 leaves (a wreck that explodes; a fall below the course, through fn_1_24A4C): the flags 0x800, 0x80,
# 0x00800000, 0x40000000, +0x58C 0x10, and the car object's 0x01000000
DESTROYED = 0x800 | 0x80 | 0x00800000 | 0x40000000
RETIRED, BROKEN = 0x08000000, 0x80
HIDDEN = follow.HIDDEN


def race(local=0, ranks=(1, 2, 0), flags=None, hidden=False, fallen=False, course=8, keep=0, rules=0, speed=0,
         cars=CARS, count=3):
    """Memory of three machines at the results' start: the local one (seat 1) with `local` flags; ranks by seat."""
    flags = list(flags or (FINISHED, local, FINISHED))
    flags[1] = local
    words = {0x800030C8: MODULE, POINTER: MACHINES, CARS_POINTER: cars, COURSE: course, KEEP: keep, RULES: rules}
    memory = {COUNT: count, RACE_TYPE: 1}
    for i, f in enumerate(flags):
        machine = MACHINES + i * 0x620
        words.update({machine: f, machine + 0x49C: RECORDS + i * 0x200, machine + 0x17C: speed,
                      machine + 0x58C: 0x10 if i == 1 and fallen else 0,
                      CARS + i * 0x440 + 0x390: HIDDEN if i == 1 and hidden else 0x00100000})
        memory[machine + 5] = i
        memory[RECORDS + i * 0x200 + 0x115] = ranks[i]
    for address, value in words.items():
        memory.update({address + k: b for k, b in enumerate(value.to_bytes(4, "big"))})
    return memory


class TheResultsCamera(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.dir = tempfile.TemporaryDirectory()
        b = build.build(cls.dir.name)
        patches = {int(p["address"], 16): p for p in b["patches"]}
        image = bytearray(b["image"])
        struct.pack_into(">I", image, SEAT - IMAGE, 1)          # this game's seat: 1
        cls.image = bytes(image)
        p = patches[TEXT + SITE]
        entry = build.branch_target(int(p["new"], 16), TEXT + SITE)
        bne = struct.unpack_from(">I", cls.image, entry + 12 - IMAGE)[0]
        assert bne >> 16 == 0x4082, p                          # the mode test, then `bne` to the helper
        cls.helper = entry + 12 + signed(bne & 0xFFFC)

    @classmethod
    def tearDownClass(cls):
        cls.dir.cleanup()

    def pick(self, memory):
        """The racer and why, and that the helper wrote nothing but its word for the host."""
        m = Machine(self.image, memory)
        m.run(self.helper)
        self.assertEqual(m.written, {CAMERA})
        word = m.read(CAMERA, 4)
        self.assertEqual(word >> 16, 0x8000)
        self.assertEqual(word & 0xFF, m.r[4])
        return m.r[4], (word >> 8) & 0xFF

    def test_a_body_that_exploded_gives_the_winner(self):
        self.assertEqual(self.pick(race(local=DESTROYED, hidden=True, fallen=True)), (2, follow.RC_WINNER))

    def test_a_fall_below_the_course_gives_the_winner(self):
        # fn_1_24A4C destroys a machine below the course: the same flags, +0x58C 0x10, hidden
        self.assertEqual(self.pick(race(local=DESTROYED, hidden=True, fallen=True, ranks=(0, 2, 1))),
                         (0, follow.RC_WINNER))

    def test_a_wreck_still_drawn_is_watched(self):
        # a breakdown at rest (out: CPU or slow), a retirement, a destroyed flag without the body hidden
        for flags in (BROKEN | CPU, BROKEN, RETIRED, 0x800):
            self.assertEqual(self.pick(race(local=flags)), (1, follow.RC_WRECK), hex(flags))

    def test_the_winner_is_the_first_place_of_those_not_out(self):
        # seat 0 leads but is out too (destroyed): seat 2, second, is followed
        memory = race(local=DESTROYED, hidden=True, ranks=(0, 2, 1), flags=(DESTROYED, 0, 0))
        self.assertEqual(self.pick(memory), (2, follow.RC_WINNER))
        # a machine without a progress record is no racer
        memory = race(local=DESTROYED, hidden=True, ranks=(0, 2, 1))
        memory.update({MACHINES + 0x49C + k: 0 for k in range(4)})
        self.assertEqual(self.pick(memory), (2, follow.RC_WINNER))

    def test_nobody_left_keeps_the_local_seat(self):
        memory = race(local=DESTROYED, hidden=True, flags=(DESTROYED, 0, RETIRED))
        self.assertEqual(self.pick(memory), (1, follow.RC_ALONE))

    def test_course_41_keeps_the_local_seat(self):
        # its scenery's collision follows the camera's machine (spectator.h RuleFor)
        self.assertEqual(self.pick(race(local=DESTROYED, hidden=True, course=41)), (1, follow.RC_COURSE))

    def test_a_spectators_game_keeps_its_seat(self):
        self.assertEqual(self.pick(race(local=DESTROYED, hidden=True, keep=1)), (1, follow.RC_KEEP))

    def test_finished_racing_or_restored_keeps_the_local_seat(self):
        # finished (a wreck on the cool-down lap too), racing, and under the Restore rule nobody is out
        for local, rules in ((FINISHED, 0), (FINISHED | DESTROYED, 0), (0, 0), (DESTROYED, RESTORE)):
            self.assertEqual(self.pick(race(local=local, hidden=True, rules=rules)), (1, follow.RC_OWN),
                             (hex(local), rules))

    def test_no_machines_or_no_car_objects(self):
        self.assertEqual(self.pick(race(local=DESTROYED, hidden=True, count=0)), (1, follow.RC_OWN))
        self.assertEqual(self.pick(race(local=DESTROYED, hidden=True, cars=0)), (1, follow.RC_WRECK))


RESULTS = 0x1000                # rule flag: the results run
CAMERA_MODE, LIVE = 0x80990000, 0x80991000      # [.bss 0xF68] (+0x48 the mode: 9 the TV cameras), [.bss 0xF70]
SESSION, RACE_MODE = CONTROL + 116, BSS + 0x960


class TheResultsCamerasWatch(unittest.TestCase):
    """`results_watch`, after every frame's steps while the results run: the cameras on the local wreck move to the
    winner when the wreck goes, once; nothing else is touched."""

    @classmethod
    def setUpClass(cls):
        cls.dir = tempfile.TemporaryDirectory()
        b = build.build(cls.dir.name)
        image = bytearray(b["image"])
        struct.pack_into(">I", image, SEAT - IMAGE, 1)          # this game's seat: 1
        cls.image = bytes(image)
        from keystone import Ks, KS_ARCH_PPC, KS_MODE_PPC32, KS_MODE_BIG_ENDIAN
        code = bytes(Ks(KS_ARCH_PPC, KS_MODE_PPC32 + KS_MODE_BIG_ENDIAN).asm(follow.WATCH)[0])
        assert cls.image.count(code) == 1                       # (it names nothing outside itself)
        cls.watch = IMAGE + cls.image.index(code) + native.label_offsets(follow.WATCH)["results_watch"]

    @classmethod
    def tearDownClass(cls):
        cls.dir.cleanup()

    def results(self, followed=1, mode=9, session=1, race_mode=2, rules=RESULTS, **kw):
        """The results running, the TV cameras on `followed`; `race(...)` for the machines (the local wreck)."""
        memory = race(rules=rules, **kw)
        words = {BSS + 0xF68: CAMERA_MODE, BSS + 0xF70: LIVE, SESSION: session}
        for address, value in words.items():
            memory.update({address + k: b for k, b in enumerate(value.to_bytes(4, "big"))})
        memory.update({RACE_MODE: race_mode >> 8, RACE_MODE + 1: race_mode & 0xFF, CAMERA_MODE + 0x48: mode,
                       LIVE + 8: followed >> 8 & 0xFF, LIVE + 9: followed & 0xFF})
        return memory

    def run_watch(self, memory):
        """The frame's call: the racer the cameras want after it, the log word, and what it wrote."""
        m = Machine(self.image, memory)
        m.r[30], m.r[31] = BSS, CONTROL
        m.run(self.watch)
        return m.read(LIVE + 8, 2), m.read(CAMERA, 4), m.written, m

    def test_a_wreck_that_goes_during_the_results_gives_the_winner_once(self):
        followed, word, written, m = self.run_watch(self.results(local=DESTROYED, hidden=True))
        self.assertEqual((followed, word), (2, 0x80000000 | follow.RC_GONE << 8 | 2))
        self.assertEqual(written, {LIVE + 8, CAMERA})            # the cameras' racer wanted and the log word
        # the next frame: the cameras are on the winner; nothing more (one line in the log)
        m.mem.update({CAMERA + k: 0 for k in range(4)})
        followed, word, written, _ = self.run_watch(m.mem)
        self.assertEqual((followed, word, written), (2, 0, set()))
        # the winner is the first place of those not out, as at the results' start
        memory = self.results(local=DESTROYED, hidden=True, ranks=(0, 2, 1), flags=(DESTROYED, 0, 0))
        self.assertEqual(self.run_watch(memory)[:2], (2, 0x80000000 | follow.RC_GONE << 8 | 2))
        memory = self.results(local=DESTROYED, hidden=True, ranks=(0, 2, 1))
        self.assertEqual(self.run_watch(memory)[:2], (0, 0x80000000 | follow.RC_GONE << 8 | 0))

    def nothing(self, memory, why):
        followed, word, written, _ = self.run_watch(memory)
        self.assertEqual((word, written), (0, set()), why)
        return followed

    def test_a_wreck_still_drawn_stays_watched(self):
        for flags in (DESTROYED, BROKEN | CPU, RETIRED):
            self.assertEqual(self.nothing(self.results(local=flags), hex(flags)), 1)

    def test_the_other_picks_are_kept(self):
        # finished or racing (its own machine), the Restore rule, course 41, a spectator's game, nobody left
        self.nothing(self.results(local=FINISHED | DESTROYED, hidden=True), "finished")
        self.nothing(self.results(local=0, hidden=True), "racing")
        self.nothing(self.results(local=DESTROYED, hidden=True, rules=RESULTS | RESTORE), "restore")
        self.nothing(self.results(local=DESTROYED, hidden=True, course=41), "course 41")
        self.nothing(self.results(local=DESTROYED, hidden=True, keep=1), "a spectator's game")
        self.nothing(self.results(local=DESTROYED, hidden=True, flags=(DESTROYED, 0, RETIRED)), "nobody left")

    def test_only_the_results_cameras_on_the_local_seat(self):
        gone = dict(local=DESTROYED, hidden=True)
        self.assertEqual(self.nothing(self.results(followed=0, **gone), "on another racer"), 0)
        self.nothing(self.results(rules=0, **gone), "not the results")
        self.nothing(self.results(mode=0, **gone), "the chase camera")
        self.nothing(self.results(session=0, **gone), "no session")
        self.nothing(self.results(race_mode=0xC, **gone), "another mode than the race's")
        memory = self.results(**gone)
        memory.update({BSS + 0xF68 + k: 0 for k in range(4)})
        self.nothing(memory, "no camera mode")
        memory = self.results(**gone)
        memory.update({BSS + 0xF70 + k: 0 for k in range(4)})
        self.nothing(memory, "no live camera")


if __name__ == "__main__":
    unittest.main()
