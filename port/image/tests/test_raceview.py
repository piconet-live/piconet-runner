"""A racer's view change in a race of ours (raceview.py), run here as the bytes the image holds against made-up
input rows: the helpers the four patched words of fn_1_ABAC call, and fn_1_ABAC's last instructions as a stand-in
that does what the disassembly says of them (the view's change and its limits). Also what test_spectator.py shares:
the reader of the camera routines' instructions (`Cpu`) and the image with where the routines are (`Build`). No
game data."""
import os, struct, sys, tempfile, unittest

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, ".."))
import build, memmap
import frame_spectate, native, raceview
from test_race_end import Machine, BSS, MODULE, signed

IMAGE, CONTROL, INPUTS, TEXT = memmap.IMAGE, memmap.CONTROL, memmap.INPUTS, memmap.REL_TEXT
SESSION, RUNNING, STEP, OUT_FRAMES = CONTROL + 116, CONTROL + 120, CONTROL + 124, CONTROL + 248
PHASE = BSS + 0x3C2A
CHASE, STACK = 0x80B01000, 0x80C00000        # the chase camera's record, the routines' stack
SEAT = IMAGE + 0x14
LOCAL = 5                                    # this game's seat in the made-up races: the last of six


def put(memory, address, value, size=4):
    memory.update({address + i: b for i, b in enumerate((value & (1 << 8 * size) - 1).to_bytes(size, "big"))})


def get(memory, address, size=4):
    return int.from_bytes(bytes(memory.get(address + i, 0) for i in range(size)), "big")


class Cpu(Machine):
    """The instructions the camera's routines add to those test_race_end.py reads: halves and indexed bytes, sign
    extension, a shift, a store that moves the stack, a call inside the image (run to its return, then on) and the
    branch or jump into the game: `game(address, cpu)` stands in for the game's code there, and the routine has
    then returned or left."""

    def __init__(self, image, memory, game=None):
        super().__init__(image, memory)
        self.game = game
        self.r[1] = STACK

    def other(self, pc, w):
        r, op, d, a, b = self.r, w >> 26, (w >> 21) & 31, (w >> 16) & 31, (w >> 11) & 31
        xo, base = (w >> 1) & 0x3FF, (self.r[(w >> 16) & 31] if (w >> 16) & 31 else 0)
        if not IMAGE <= pc < IMAGE + len(self.image):                                         # a branch into the
            self.game(pc, self)                                                               # game: its own code
            return None
        if op == 44: self.write((base + signed(w & 0xFFFF)) & 0xFFFFFFFF, 2, r[d] & 0xFFFF)   # sth
        elif op == 33:                                                                        # lwzu
            r[a] = (base + signed(w & 0xFFFF)) & 0xFFFFFFFF
            r[d] = self.read(r[a], 4)
        elif op == 37:                                                                        # stwu
            self.write((base + signed(w & 0xFFFF)) & 0xFFFFFFFF, 4, r[d])
            r[a] = (base + signed(w & 0xFFFF)) & 0xFFFFFFFF
        elif op == 18:                                                                        # bl: to its return
            Cpu.run(self, pc + signed(w & 0x03FFFFFC, 26))
        elif op == 31 and xo == 87: r[d] = self.read((base + r[b]) & 0xFFFFFFFF, 1)           # lbzx
        elif op == 31 and xo == 23: r[d] = self.read((base + r[b]) & 0xFFFFFFFF, 4)           # lwzx
        elif op == 31 and xo == 60: r[a] = r[d] & ~r[b] & 0xFFFFFFFF                          # andc
        elif op == 31 and xo == 954: r[a] = signed(r[d] & 0xFF, 8) & 0xFFFFFFFF               # extsb
        elif op == 31 and xo == 922: r[a] = signed(r[d] & 0xFFFF) & 0xFFFFFFFF                # extsh
        elif op == 31 and xo == 536: r[a] = (r[d] >> (r[b] & 63)) & 0xFFFFFFFF if r[b] & 63 < 32 else 0   # srw
        elif op == 31 and xo == 235: r[d] = r[a] * r[b] & 0xFFFFFFFF                          # mullw
        elif op == 31 and xo == 316: r[a] = r[d] ^ r[b]                                       # xor
        elif op == 31 and xo == 339 and (b << 5 | a) == 8: r[d] = 0                           # mflr
        elif op == 31 and xo == 467 and (b << 5 | a) == 8: pass                               # mtlr
        elif op in (48, 50, 52, 54, 59, 63):
            self.floating(w, op, d, a, b, base)
        elif op == 19 and xo == 528 and not w & 1:                                            # bctr: the game's
            self.game(self.ctr, self)
            return None
        elif op == 19 and xo == 528:                                                          # bctrl: the game's
            self.game(self.ctr, self)                                                         # routine, and on
        else:
            raise AssertionError(f"{pc:08X}: {w:08X} is not an instruction these routines are made of")
        return pc + 4


    def floating(self, w, op, d, a, b, base):
        """The single-precision instructions of the rivals' smoothing (smooth.py) and the mines' fade (sites_mines.py):
        every result rounded to a float; and a register kept on the stack and taken back as the double it is
        (sites_shadow.py)."""
        f = self.__dict__.setdefault("f", [0.0] * 32)
        single = lambda x: struct.unpack(">f", struct.pack(">f", x))[0]
        c, xo, at = (w >> 6) & 31, (w >> 1) & 0x3FF, (base + signed(w & 0xFFFF)) & 0xFFFFFFFF
        if op == 48: f[d] = struct.unpack(">f", self.read(at, 4).to_bytes(4, "big"))[0]       # lfs
        elif op == 52: self.write(at, 4, int.from_bytes(struct.pack(">f", f[d]), "big"))      # stfs
        elif op == 50: f[d] = struct.unpack(">d", self.read(at, 8).to_bytes(8, "big"))[0]     # lfd
        elif op == 54: self.write(at, 8, int.from_bytes(struct.pack(">d", f[d]), "big"))      # stfd
        elif op == 59 and xo & 31 == 20: f[d] = single(f[a] - f[b])                           # fsubs
        elif op == 59 and xo & 31 == 21: f[d] = single(f[a] + f[b])                           # fadds
        elif op == 59 and xo & 31 == 18: f[d] = single(f[a] / f[b])                           # fdivs
        elif op == 59 and xo & 31 == 25: f[d] = single(f[a] * f[c])                           # fmuls
        elif op == 59 and xo & 31 == 29: f[d] = single(f[a] * f[c] + f[b])                    # fmadds
        elif op == 59 and xo & 31 == 30: f[d] = single(-(f[a] * f[c] - f[b]))                 # fnmsubs
        elif op == 63 and xo & 31 == 21: f[d] = f[a] + f[b]                                   # fadd (double)
        elif op == 63 and xo == 0: self.compare(f[a], f[b])                                   # fcmpu, field 0
        elif op == 63 and xo == 72: f[d] = f[b]                                               # fmr
        elif op == 63 and xo == 264: f[d] = abs(f[b])                                         # fabs
        else:
            raise AssertionError(f"{w:08X} is not a floating instruction these routines are made of")


class Build(unittest.TestCase):
    """The image, with this game's seat LOCAL, and where the routines are in it."""

    @classmethod
    def setUpClass(cls):
        cls.dir = tempfile.TemporaryDirectory()
        b = build.build(cls.dir.name)
        cls.symbols, cls.patches = b["symbols"], {int(p["address"], 16): p for p in b["patches"]}
        image = bytearray(b["image"])
        struct.pack_into(">I", image, SEAT - IMAGE, LOCAL)
        cls.image = bytes(image)
        from keystone import Ks, KS_ARCH_PPC, KS_MODE_PPC32, KS_MODE_BIG_ENDIAN
        text = frame_spectate.asm()
        code = bytes(Ks(KS_ARCH_PPC, KS_MODE_PPC32 + KS_MODE_BIG_ENDIAN).asm(text)[0])
        assert cls.image.count(code) == 1
        at = IMAGE + cls.image.index(code)
        cls.routines = {name: at + offset for name, offset in native.label_offsets(text).items()}
        cls.spectator_text = (at, at + len(code))

    @classmethod
    def tearDownClass(cls):
        cls.dir.cleanup()

    def helper(self, site):
        """The helper a site's entry leaves for outside a plain start: the mode test, then `bne` to it."""
        p = self.patches[TEXT + site]
        entry = build.branch_target(int(p["new"], 16), TEXT + site)
        self.assertEqual(int(p["new"], 16) & 1, 1, p)                # a call: the helper returns to the site
        bne = struct.unpack_from(">I", self.image, entry + 12 - IMAGE)[0]
        self.assertEqual(bne >> 16, 0x4082, p)
        return entry + 12 + signed(bne & 0xFFFC)


class TheRacingView(Build):
    """raceview.py: fn_1_ABAC is fed from the input rows."""

    def rows(self, seat, bits, running=1, out_frames=0, phase=0x29, steer=0):
        """Memory with byte 0 of `seat`'s rows from `bits` (step: bits) and each row's steering byte `steer`."""
        memory = {}
        put(memory, 0x800030C8, MODULE); put(memory, RUNNING, running); put(memory, OUT_FRAMES, out_frames)
        put(memory, PHASE, phase, 2); put(memory, CHASE + 2, seat, 2)
        for step, value in bits.items():
            row = INPUTS + ((step & 1023) * 30 + seat) * 8
            memory[row], memory[row + 1] = value, steer
        return memory

    def bits(self, memory, step, r3=0x11111111, r4=0x22222222):
        put(memory, STEP, step)
        m = Cpu(self.image, memory)
        m.r[29], m.r[3], m.r[4], m.r[31] = CHASE, r3, r4, 2
        m.run(self.helper(raceview.BITS_SITES[0]))
        self.assertEqual((m.r[3], m.r[4], m.r[31], m.r[29]), (r3, r4, 2, CHASE))       # only r0 and r12 are its own
        self.assertTrue(all(memmap.VIEW_SCRATCH <= a < memmap.VIEW_SCRATCH + 8 for a in m.written), m.written)
        return m.r[0]

    def view(self, memory, steps, view=1):
        """fn_1_ABAC from 0xAD10 on, over `steps`: the two reads of the bits, the lowest view, the highest."""
        low = Cpu(self.image, {})
        low.run(self.helper(raceview.LOW_SITE))
        shown = []
        for step in steps:
            if self.bits(memory, step) & 2:
                view += 1
            if self.bits(memory, step) & 1:
                view -= 1
            if view <= 0:
                view = low.r[31]
            view = min(view, 3)
            shown.append(view)
        return shown

    def test_the_four_sites_are_fn_1_abacs(self):
        self.assertEqual([s[0] for s in raceview.SITES], [0xABF8, 0xAD10, 0xAD24, 0xAD3C])
        self.assertEqual(self.helper(raceview.BITS_SITES[0]), self.helper(raceview.BITS_SITES[1]))
        for offset, word, mask, _ in raceview.SITES:
            self.assertEqual(int(self.patches[TEXT + offset]["original"], 16), word)
            self.assertEqual(mask, 0xFFFFFFFF)                       # no relocated half: the word is the game's own

    def test_the_followed_machine_has_port_0_and_the_lowest_view_is_1(self):
        m = Cpu(self.image, {})
        m.r[3] = 0xFFFFFFFF                                          # what fn_1_86678 answers for a machine with no port
        m.run(self.helper(raceview.PORT_SITE))
        self.assertEqual((m.r[3], m.written), (0, set()))
        m = Cpu(self.image, {})
        m.run(self.helper(raceview.LOW_SITE))
        self.assertEqual((m.r[31], m.written), (1, set()))

    def test_the_helpers_are_the_races_whatever_the_mode_word(self):
        # a replay plays as the race: no mode word gives the game's own call or views back (the fork's image has none)
        for mode in (0, 1, 2):
            memory = {}
            put(memory, memmap.MODE, mode)
            m = Cpu(self.image, dict(memory))
            m.r[3] = 17
            m.run(self.helper(raceview.PORT_SITE))
            self.assertEqual((m.r[3], m.written), (0, set()))
            m = Cpu(self.image, memory)
            m.run(self.helper(raceview.LOW_SITE))
            self.assertEqual(m.r[31], 1)

    def test_a_press_is_new_in_one_step_only(self):
        memory = self.rows(3, {100: 0, 101: 2, 102: 2, 103: 0, 104: 1, 105: 0x43})
        self.assertEqual([self.bits(memory, s) for s in range(100, 106)], [0, 2, 0, 0, 1, 0x42])
        # the ring's first row is told against its last
        self.assertEqual(self.bits(self.rows(3, {1023: 2, 1024: 2}), 1024), 0)
        self.assertEqual(self.bits(self.rows(3, {2047: 0, 2048: 2}), 2048), 2)

    def test_only_the_followed_seats_row_counts(self):
        memory = self.rows(3, {100: 0, 101: 2})
        put(memory, CHASE + 2, 4, 2)
        self.assertEqual(self.bits(memory, 101), 0)
        put(memory, CHASE + 2, 0xFFFF, 2)                            # the record follows no machine
        self.assertEqual(self.bits(memory, 101), 0)
        put(memory, CHASE + 2, 30, 2)
        self.assertEqual(self.bits(memory, 101), 0)

    def test_no_change_outside_the_race_or_for_a_seat_the_ai_drives(self):
        bits = {100: 0, 101: 2}
        self.assertEqual(self.bits(self.rows(3, bits), 101), 2)
        self.assertEqual(self.bits(self.rows(3, bits, running=0), 101), 0)
        self.assertEqual(self.bits(self.rows(3, bits, phase=0x28), 101), 0)       # the countdown
        self.assertEqual(self.bits(self.rows(3, bits, steer=0x80), 101), 0)       # the row says the AI drives
        self.assertEqual(self.bits(self.rows(3, bits, out_frames=180), 101), 0)   # this game spectates
        self.assertEqual(self.bits(self.rows(3, bits, out_frames=179), 101), 2)

    def test_the_views_are_1_to_3(self):
        up, down = 2, 1                                              # the bits: the next view, the view before
        presses = [up, 0, up, 0, up, 0, down, 0, down, 0, down, 0, down, 0]
        memory = self.rows(3, {100 + i: b for i, b in enumerate(presses)})
        self.assertEqual(self.view(memory, range(100, 114)), [2, 2, 3, 3, 3, 3, 2, 2, 1, 1, 1, 1, 1, 1])
        # a view 0 the save file started the camera with becomes 1 the first time
        self.assertEqual(self.view(self.rows(3, {}), [200], view=0), [1])

    def test_a_rollback_makes_the_change_again_and_a_corrected_row_corrects_it(self):
        memory = self.rows(3, {100: 0, 101: 2, 102: 0, 103: 0})
        first = self.view(memory, range(100, 104))
        self.assertEqual(first, [1, 2, 2, 2])
        # the view is in memory a rollback puts back: back at step 101 with view 1, the same rows give the same
        self.assertEqual(self.view(memory, range(101, 104), view=first[0]), first[1:])
        # the press at 101 was a prediction; the official row has it at 102
        memory = self.rows(3, {100: 0, 101: 0, 102: 2, 103: 0})
        self.assertEqual(self.view(memory, range(101, 104), view=first[0]), [1, 2, 2])


if __name__ == "__main__":
    unittest.main()
