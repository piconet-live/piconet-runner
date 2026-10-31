"""The name tags (nametags.py), run here as the bytes the image holds against a made-up race seen from seat 0: which
seats get a tag (player seats only, none for a CPU, a seat the AI drives, a finished one, one too far), how many
(the closest few), how they fade in and out (never a pop: a new tag, one the overlap rule let back, one back on screen,
the first frame of a race), how the set holds when machines trade places in a pack, and the
overlap rule. The game's routines are stand-ins: the projection puts a world point (x, y, z) at screen
(320 + 100x, 240 - 8y) with view depth z, and the text routines record what they are given. No game data."""
import os, struct, sys, unittest

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, ".."))
import build, memmap  # noqa: F401  (build puts the generators' folder on the path)
import native, nametags
from test_race_end import BSS, MODULE, MACHINES, POINTER, signed
from test_raceview import Build, Cpu, get, put, CONTROL, TEXT

SESSION, TESTS_ON, RUNNING, STEP = CONTROL + 116, CONTROL + 76, CONTROL + 120, CONTROL + 124
COUNT, STACK = BSS + 0x3C39, 0x80C00000
PLAYERS, NAMES, INPUTS, TAGS = nametags.PLAYERS, memmap.NAMES, memmap.INPUTS, memmap.TAGS
FOLLOWED = 0
FIRST = (1.0 / nametags.FADE_IN) * (2.0 - 1.0 / nametags.FADE_IN)   # the eased first frame


def single(x):
    return struct.unpack(">f", struct.pack(">f", x))[0]


class TagCpu(Cpu):
    """The instructions the tags add to the camera routines' reader: the saving of registers in one go, a division,
    a negation and the conversion to an integer."""

    def other(self, pc, w):
        r, op, d, a = self.r, w >> 26, (w >> 21) & 31, (w >> 16) & 31
        at = ((r[a] if a else 0) + signed(w & 0xFFFF)) & 0xFFFFFFFF
        f = self.__dict__.setdefault("f", [0.0] * 32)
        b, xo = (w >> 11) & 31, (w >> 1) & 0x3FF
        if op == 47:                                                                      # stmw
            for i in range(d, 32):
                self.write(at + 4 * (i - d), 4, r[i])
        elif op == 46:                                                                    # lmw
            for i in range(d, 32):
                r[i] = self.read(at + 4 * (i - d), 4)
        elif op == 59 and xo & 31 == 18: f[d] = single(f[a] / f[b])                       # fdivs
        elif op == 63 and xo == 40: f[d] = -f[b]                                          # fneg
        elif op == 63 and xo == 15:                                                       # fctiwz
            f[d] = struct.unpack(">d", struct.pack(">q", int(f[b])))[0]
        else:
            return super().other(pc, w)
        return pc + 4


class Race:
    """Seat 0 (the followed racer) at the origin, looking along +z; `machines` is {seat: (x, z)} on the road
    (y = 0), every one a player seat named after its seat unless `cpu` says otherwise."""

    def __init__(self, t, machines, cpu=(), ai=(), finished=(), session=1):
        self.t, self.mem, self.draws = t, {}, []
        self.colours, self.depths, self.sizes = {}, {}, {}
        m = self.mem
        racers = max(machines) + 1
        put(m, 0x800030C8, MODULE); put(m, POINTER, MACHINES); m[COUNT] = racers
        put(m, SESSION, session); put(m, RUNNING, 1); put(m, STEP, 501)
        mask = 0
        for seat in range(racers):
            if seat not in cpu:
                mask |= 1 << seat
                for i, c in enumerate(f"P{seat}".encode()):
                    m[NAMES + seat * memmap.NAME_SIZE + i] = c
            m[INPUTS + ((500 & 1023) * 30 + seat) * 8 + 1] = 0x80 if seat in ai else 0
        image = bytearray(t.image)                       # the player seats are the image's own (the core's)
        struct.pack_into(">I", image, PLAYERS - memmap.IMAGE, mask)
        self.image = bytes(image)
        self.ai = set(ai)
        self.place({FOLLOWED: (0.0, 0.0), **machines})
        for seat in finished:
            put(m, MACHINES + seat * 0x620, 0x10000)

    def place(self, machines):
        for seat, (x, z) in machines.items():
            base = MACHINES + seat * 0x620
            for off, v in ((0x158, x), (0x168, 0.0), (0x178, z), (0x1BC, 0.0), (0x1C0, 1.0), (0x1C4, 0.0)):
                self.mem.update({base + off + i: b for i, b in enumerate(struct.pack(">f", v))})

    def game(self, address, cpu):
        f = cpu.__dict__.setdefault("f", [0.0] * 32)
        if address == TEXT + 0x6B48:
            cpu.r[3] = FOLLOWED
        elif address in (TEXT + 0x5910,):
            cpu.r[3] = 0
        elif address == TEXT + 0xA71AC:
            f[1] = 1.0
        elif address == 0x8006F78C:
            x, y, z = struct.unpack(">3f", bytes(cpu.read(cpu.r[3] + i, 1) for i in range(12)))
            out = struct.pack(">3f", 320 + 100 * x, 240 - 8 * y, -z)
            for i, byte in enumerate(out):
                cpu.write(cpu.r[4] + i, 1, byte)
        elif address == TEXT + 0x49514:
            self.colour = cpu.read(cpu.r[3], 4)
        elif address == TEXT + 0x4954C:
            self.depth = f[1]
        elif address == TEXT + 0x4955C:
            self.size = f[1]
        elif address == TEXT + 0x4A0D8:
            name, at = "", cpu.r[3]
            while cpu.read(at, 1):
                name, at = name + chr(cpu.read(at, 1)), at + 1
            self.draws.append((name, (self.colour & 0xFF) / 255.0))
            self.colours[name], self.depths[name], self.sizes[name] = self.colour, self.depth, self.size

    def frame(self):
        """One drawn frame: the tags drawn, {name: alpha}."""
        self.draws = []
        cpu = TagCpu(self.image, self.mem, self.game)
        cpu.r[1] = STACK
        cpu.run(self.t.entry, limit=200000)
        outside = {a for a in cpu.written if not (STACK - 0x1000 <= a < STACK + 8 or TAGS <= a < TAGS + 0x200)}
        self.t.assertEqual(outside, set(), "the tags write nothing but their stack and their state")
        self.mem = cpu.mem
        return dict(self.draws)

    def draws_of(self, n):
        """The names drawn in the n-th frame from now, a list (two tags may read alike)."""
        for _ in range(n):
            self.frame()
        return [name for name, _ in self.draws]

    def frames(self, n):
        for _ in range(n - 1):
            self.frame()
        return self.frame()


class TheNameTags(Build):
    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        from keystone import Ks, KS_ARCH_PPC, KS_MODE_PPC32, KS_MODE_BIG_ENDIAN
        code = bytes(Ks(KS_ARCH_PPC, KS_MODE_PPC32 + KS_MODE_BIG_ENDIAN).asm(nametags.asm())[0])
        assert cls.image.count(code) == 1
        cls.entry = cls.image.index(code) + memmap.IMAGE

    def test_player_seats_only(self):
        race = Race(self, {1: (-3.0, 8.0), 2: (2.0, 8.0), 3: (0.0, 9.0), 4: (-2.0, 10.0)}, cpu=(1,), ai=(2,),
                    finished=(4,))
        self.assertEqual(set(race.frames(10)), {"P3"})

    def test_off_outside_a_session(self):
        race = Race(self, {1: (0.0, 8.0)}, session=0)
        self.assertEqual(race.frames(3), {})
        put(race.mem, TESTS_ON, 1)
        self.assertEqual(set(race.frames(3)), {"P1"})

    def test_close_only_with_a_quick_fade(self):
        race = Race(self, {1: (0.0, 10.0), 2: (2.0, 30.0), 3: (-2.0, 45.0)})
        first = race.frame()
        self.assertEqual(set(first), {"P1", "P2"})                         # P3 is past FAR
        self.assertAlmostEqual(first["P1"], FIRST, delta=1 / 255)           # fades in, never pops
        full = race.frames(nametags.FADE_IN)
        self.assertAlmostEqual(full["P1"], 1.0, delta=1 / 255)              # within NEAR: full
        d2, near2, far2 = 4 + 900, nametags.NEAR ** 2, nametags.FAR ** 2
        self.assertAlmostEqual(full["P2"], (far2 - d2) / (far2 - near2), delta=2 / 255)

    def pack(self, spread=1.0):
        """Eight player machines round seat 0, 12 to 19 units away, each in a lane of its own on screen."""
        return {seat: (race_x(seat), 12.0 + spread * (seat - 1)) for seat in range(1, 9)}

    def test_the_closest_few_in_a_pack(self):
        race = Race(self, self.pack())
        shown = race.frames(nametags.FADE_IN + 1)
        self.assertEqual(set(shown), {"P1", "P2", "P3"})
        self.assertEqual(len(shown), nametags.SHOWN)

    def test_a_pack_trading_places_keeps_its_tags(self):
        race = Race(self, self.pack())
        race.frames(nametags.HOLD + 1)
        # P4 comes level with P3 and passes it by a little, then P3 drops back by less than the margin
        for z in (14.5, 14.2, 13.9, 13.6):
            race.place({4: (race_x(4), z)})
            self.assertEqual(set(race.frame()), {"P1", "P2", "P3"})

    def test_a_challenger_clearly_closer_takes_a_place(self):
        race = Race(self, self.pack())
        race.frames(nametags.HOLD + 1)
        race.place({8: (race_x(8), 8.0)})                                  # from 19 to 8: far more than the margin
        shown = race.frame()
        self.assertIn("P8", shown)
        self.assertLess(shown["P8"], 0.5)                                   # fading in
        self.assertIn("P3", shown)                                          # fading out where it is
        settled = race.frames(nametags.FADE_OUT)
        self.assertEqual(set(settled), {"P1", "P2", "P8"})

    def test_a_new_tag_holds_its_place(self):
        race = Race(self, self.pack())
        race.frames(nametags.HOLD + 1)
        race.place({8: (race_x(8), 8.0)})
        race.frames(2)                                                      # P8 in, P3 out
        race.place({8: (race_x(8), 19.0)})                                  # P8 drops back, the farthest again
        held = race.frames(nametags.HOLD - 4)
        self.assertEqual(set(held), {"P1", "P2", "P8"})                     # P8 stays for the hold
        later = race.frames(nametags.FADE_OUT + 4)
        self.assertEqual(set(later), {"P1", "P2", "P3"})                    # then P3, clearly closer, is back

    def test_overlapping_tags_do_not_flicker(self):
        # P1 and P2 at one screen place (same x, both on the road), trading the nearer place every frame
        race = Race(self, {1: (0.0, 10.0), 2: (0.0, 10.5)})
        race.frames(nametags.FADE_IN + 1)
        seen = []
        for i in range(20):
            race.place({1: (0.0, 10.0 + 0.6 * (i % 2)), 2: (0.0, 10.6 - 0.6 * (i % 2))})
            seen.append(race.frame())
        self.assertTrue(all(set(s) == {"P1"} for s in seen), seen)

    def test_a_dense_pack_settles_and_stays(self):
        # the worst case: 29 players round seat 0, 6 to 20 units away, on top of each other on screen, every one
        # moving a little every frame (and their order changing with it)
        import random
        rng = random.Random(18)
        home = {seat: (rng.uniform(-3.5, 3.5), rng.uniform(6.0, 20.0)) for seat in range(1, 30)}
        race = Race(self, home)
        race.frames(nametags.HOLD + nametags.FADE_IN)
        settled, seen = None, []
        for _ in range(120):
            race.place({seat: (x * rng.uniform(0.97, 1.03), z * rng.uniform(0.97, 1.03))
                        for seat, (x, z) in home.items()})
            drawn = race.frame()
            self.assertLessEqual(len(drawn), nametags.SHOWN)
            settled = settled or set(drawn)
            seen.append(set(drawn))
        self.assertTrue(settled)
        self.assertTrue(all(s == settled for s in seen), seen)

    def rises(self, race, name, frames=None):
        """`name`'s alpha over the next frames: from at most 0.2, up by at most 0.2 a frame, to full."""
        seen = [race.frame().get(name, 0.0) for _ in range(frames or nametags.FADE_IN + 2)]
        self.assertLessEqual(seen[0], 0.2, seen)
        self.assertTrue(all(0 <= b - a <= 0.2 for a, b in zip(seen, seen[1:])), seen)
        self.assertAlmostEqual(seen[-1], 1.0, delta=1 / 255)
        self.assertGreater(seen[nametags.FADE_IN // 2], 0.6, seen)   # eased: past the middle by half way
        return seen

    def falls(self, race, name, frames):
        """`name`'s alpha over the next frames: still drawn at first, never rising, gone at the end."""
        seen = [race.frame().get(name, 0.0) for _ in range(frames)]
        self.assertGreater(seen[0], 0.5, seen)
        self.assertTrue(all(b <= a for a, b in zip(seen, seen[1:])), seen)
        self.assertEqual(seen[-1], 0.0, seen)

    def test_a_chosen_tag_fades_in(self):
        self.rises(Race(self, {1: (0.0, 10.0)}), "P1")

    def test_a_tag_the_overlap_rule_lets_back_fades_in(self):
        race = Race(self, {1: (0.0, 10.0), 2: (0.0, 10.5)})
        race.frames(nametags.FADE_IN + nametags.FADE_OUT)
        self.assertEqual(set(race.frame()), {"P1"})                         # P2 left out under P1
        race.place({2: (2.0, 10.5)})                                       # P2 moves clear
        self.rises(race, "P2")

    def test_a_tag_off_screen_fades_out_and_back_in(self):
        race = Race(self, {1: (0.0, 10.0)})
        race.frames(nametags.FADE_IN + 1)
        race.place({1: (5.0, 10.0)})                                       # 820 pixels: off screen
        self.falls(race, "P1", nametags.FADE_OUT + 1)
        race.place({1: (0.0, 10.0)})
        self.rises(race, "P1")

    def test_a_seat_that_finishes_fades_out_quickly(self):
        race = Race(self, {1: (0.0, 10.0)})
        race.frames(nametags.FADE_IN + 1)
        put(race.mem, MACHINES + 1 * 0x620, 0x10000)
        self.falls(race, "P1", nametags.FADE_LEAVE + 1)

    def test_a_seat_the_ai_takes_fades_out_quickly(self):
        race = Race(self, {1: (0.0, 10.0)})
        race.frames(nametags.FADE_IN + 1)
        race.mem[INPUTS + (500 * 30 + 1) * 8 + 1] = 0x80
        self.falls(race, "P1", nametags.FADE_LEAVE + 1)

    def test_a_new_race_fades_in_again(self):
        race = Race(self, {1: (0.0, 10.0)})
        race.frames(nametags.FADE_IN + 1)
        for step in (502, 503, 504):                                        # steps on: the tag stays
            put(race.mem, STEP, step)
            self.assertAlmostEqual(race.frame()["P1"], 1.0, delta=1 / 255)
        put(race.mem, STEP, 0)                                             # the next race, or a seek back
        self.rises(race, "P1")
        put(race.mem, STEP, 5000)                                          # a seek forward
        self.rises(race, "P1")


    # a game mode's `tag`: the text from the window, on any seat, far, first, through walls
    def tagged(self, race, seats, texts=("BOMB",)):
        for i, text in enumerate(texts):
            for j, c in enumerate(text.encode()):
                race.mem[memmap.MODE_HUD_TAG_TEXTS + i * memmap.MODE_HUD_TAG_TEXT + j] = c
        for seat in range(30):
            race.mem[memmap.MODE_HUD_TAGS + seat] = seats.get(seat, 0)

    def test_a_tag_shows_its_text_in_place_of_the_name(self):
        race = Race(self, {1: (0.0, 10.0), 2: (2.0, 10.0)})
        self.tagged(race, {1: 2}, ("BOMB", "RIVAL"))
        self.assertEqual(set(race.frames(nametags.FADE_IN + 1)), {"RIVAL", "P2"})

    def test_a_tag_on_any_seat(self):
        # a CPU (no name, not a player seat), a seat the AI drives: all tagged; the plain CPU keeps none
        race = Race(self, {1: (-3.0, 8.0), 2: (2.0, 8.0), 3: (0.0, 9.0)}, cpu=(1, 3), ai=(2,))
        self.tagged(race, {1: 1, 2: 1})
        self.assertEqual(sorted(race.draws_of(5)), ["BOMB", "BOMB"])

    def test_a_tag_not_on_the_followed_racer(self):
        race = Race(self, {1: (0.0, 10.0)})
        self.tagged(race, {FOLLOWED: 1})
        self.assertEqual(set(race.frames(3)), {"P1"})

    def test_a_tag_reads_from_further_than_a_name(self):
        z = nametags.TAG_NEAR - 10.0
        race = Race(self, {1: (0.0, z), 2: (1.0, z)})
        self.tagged(race, {1: 1})
        shown = race.frames(nametags.FADE_IN + 1)
        self.assertEqual(set(shown), {"BOMB"})                              # the name is past FAR
        self.assertAlmostEqual(shown["BOMB"], 1.0, delta=1 / 255)           # full to TAG_NEAR
        race.place({1: (0.0, nametags.TAG_FAR - 10.0)})
        d2, near2, far2 = (nametags.TAG_FAR - 10.0) ** 2, nametags.TAG_NEAR ** 2, nametags.TAG_FAR ** 2
        self.assertAlmostEqual(race.frame()["BOMB"], (far2 - d2) / (far2 - near2), delta=2 / 255)
        race.place({1: (0.0, nametags.TAG_FAR + 1.0)})
        self.assertEqual(race.frame(), {})                                  # past TAG_FAR

    def test_tags_come_first_in_the_limit(self):
        # the three names are closer and held; the tag, further, still takes a place at once
        race = Race(self, {**self.pack(), 8: (race_x(8), 100.0)})
        race.frames(5)
        self.tagged(race, {8: 1})
        shown = race.frame()
        self.assertIn("BOMB", shown)
        full = race.frames(nametags.FADE_OUT + 2)
        self.assertEqual(set(full), {"BOMB", "P1", "P2"})

    def test_a_tag_wins_the_overlap_over_a_closer_name(self):
        race = Race(self, {1: (0.0, 10.0), 2: (0.1, 30.0)})
        self.tagged(race, {2: 1})
        shown = race.frames(nametags.FADE_OUT + nametags.FADE_IN + 2)
        self.assertEqual(set(shown), {"BOMB"})

    def test_a_tag_has_its_colour_and_a_name_stays_white(self):
        race = Race(self, {1: (0.0, 10.0), 2: (2.0, 10.0)})
        self.tagged(race, {1: 1})
        race.frames(nametags.FADE_IN + 1)
        self.assertEqual(race.colours["BOMB"] >> 8, nametags.TAG_COLOUR)
        self.assertEqual(race.colours["P2"] >> 8, 0xFFFFFF)

    def test_a_tag_draws_before_walls_and_a_name_behind_them(self):
        race = Race(self, {1: (0.0, 50.0), 2: (2.0, 10.0)})
        self.tagged(race, {1: 1})
        race.frames(nametags.FADE_IN + 1)
        self.assertAlmostEqual(race.depths["BOMB"], 90000.1 - 9000.0, delta=0.01)   # as a point 1 before the camera
        self.assertAlmostEqual(race.depths["P2"], 90000.1 - 9000.0 / 10.0, delta=0.01)

    def test_a_tag_is_larger_than_a_name(self):
        race = Race(self, {1: (0.0, 10.0), 2: (2.0, 10.0)})
        self.tagged(race, {1: 1})
        race.frames(nametags.FADE_IN + 1)
        self.assertAlmostEqual(race.sizes["BOMB"], 1.2, delta=0.001)
        self.assertAlmostEqual(race.sizes["P2"], 1.0, delta=0.001)

    def test_a_tag_going_off_fades_out_then_the_name_comes_back(self):
        race = Race(self, {1: (0.0, 10.0)})
        self.tagged(race, {1: 1})
        race.frames(nametags.FADE_IN + 1)
        self.tagged(race, {})
        last = 1.0
        for _ in range(nametags.FADE_LEAVE - 1):                            # the text it had, falling: never a pop
            shown = race.frame()
            self.assertEqual(set(shown), {"BOMB"})
            self.assertLess(shown["BOMB"], last)
            last = shown["BOMB"]
        self.assertEqual(set(race.frames(nametags.FADE_IN + 2)), {"P1"})

    def test_a_tag_on_a_finished_seat_fades_out_quickly(self):
        race = Race(self, {1: (0.0, 10.0)}, cpu=(1,))
        self.tagged(race, {1: 1})
        race.frames(nametags.FADE_IN + 1)
        put(race.mem, MACHINES + 1 * 0x620, 0x10000)
        self.falls(race, "BOMB", nametags.FADE_LEAVE + 1)

    def test_a_leaving_tag_leaves_the_seats_after_it_alone(self):
        race = Race(self, {1: (-2.0, 10.0), 2: (2.0, 10.0)})
        self.tagged(race, {1: 1})
        race.frames(nametags.FADE_IN + 1)
        put(race.mem, MACHINES + 1 * 0x620, 0x10000)
        self.assertEqual(set(race.frame()), {"BOMB", "P2"})

    def test_no_tags_draw_as_they_always_did(self):
        # the same race with a window of zeros and with the texts there but no seat naming one: the same draws
        machines = {1: (-3.0, 8.0), 2: (2.0, 30.0), 3: (0.0, 20.0)}
        plain, texts = Race(self, machines), Race(self, machines)
        self.tagged(texts, {})
        for _ in range(nametags.FADE_IN + 2):
            self.assertEqual(plain.frame(), texts.frame())
        self.assertEqual(plain.colours, texts.colours)
        self.assertEqual(plain.depths, texts.depths)
        self.assertEqual(plain.sizes, texts.sizes)
        self.assertEqual(plain.colours["P1"] >> 8, 0xFFFFFF)


def race_x(seat):
    return seat - 4.5                       # 100 pixels apart on screen


if __name__ == "__main__":
    unittest.main()
