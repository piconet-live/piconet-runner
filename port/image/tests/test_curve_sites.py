"""The machine setting's curve as data (sites_curve.py) and the energy boost's length (sites_dash.py), run here as the
bytes the image holds: every site gives the seat's own number with the seat's record named, and the game's own word
without it; the two calls name the seat for the call's length only; the slots are protocol/stats.toml's (the
protocol's checkout: port/setup/parts.py), whose places the core's const_ranges.h holds to the facts. No game data,
except the last class: with the work folder's main.rel (port/setup/work-folder.env) it runs the game's fn_1_22B10
itself, patched as the patch list says, against the same function unpatched, for every setting and a sweep of
machines: the game's numbers give the game's bits, and F-Zero GX+'s DriftFastLoss of 1.0 gives that mod's own code's
bits."""
import os, random, struct, sys, tempfile, tomllib, unittest

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, ".."))
sys.path.insert(0, os.path.join(here, "..", "..", "setup"))
from test_raceview import Build, put, TEXT
import build, memmap, relink
import sites_curve, sites_dash
import mainrel, parts, work_folder

REPO = os.path.normpath(os.path.join(here, "..", "..", ".."))
SETTING = memmap.SEAT_BLOCK + memmap.SEAT_SETTING
RECORDS = memmap.SEAT_BLOCK + memmap.SEAT_CONSTANTS
POOL, BLOCK, STACK, MACHINE = 0x80E00000, 0x80D10000, 0x80C00000, 0x80D00000
single = lambda x: struct.unpack(">f", struct.pack(">f", x))[0]
f32 = lambda x: struct.unpack(">I", struct.pack(">f", x))[0]
signed = lambda v, bits=16: v - (1 << bits) if v & (1 << (bits - 1)) else v
RETURN = 0x80000100                       # a made-up return address: run() stops there


class Ppc:
    """The instructions fn_1_22B10, the sites' routines and the calls' routines are made of: integer loads and stores,
    compares into any CR field, `cror`, branches (a `bl` into the game runs `game(address, cpu)` and goes on), and
    the floating-point ones, every single-precision result rounded to a float as the chip rounds it once (a product or
    a quotient of two floats is exact or correctly rounded in a double first: no double rounding error)."""

    def __init__(self, memory, game=None):
        self.mem, self.game = memory, game
        self.r, self.f, self.cr, self.lr, self.ctr = [0] * 32, [0.0] * 32, [0] * 8, RETURN, 0
        self.r[1] = STACK

    def read(self, a, n):
        return int.from_bytes(bytes(self.mem.get(a + i, 0) for i in range(n)), "big")

    def write(self, a, n, v):
        for i, b in enumerate((v & ((1 << 8 * n) - 1)).to_bytes(n, "big")):
            self.mem[a + i] = b

    def setcr(self, field, a, b):
        self.cr[field] = 8 if a < b else 4 if a > b else 2 if a == b else 1

    def bit(self, i):
        return (self.cr[i >> 2] >> (3 - (i & 3))) & 1

    def taken(self, bo, bi):
        assert bo & 4, "no counting branches here"
        return bool(bo & 16) or self.bit(bi) == bool(bo & 8)

    def run(self, pc, limit=5000):
        r, f = self.r, self.f
        for _ in range(limit):
            if pc == RETURN:
                return
            w = self.read(pc, 4)
            op, d, a, b, imm = w >> 26, (w >> 21) & 31, (w >> 16) & 31, (w >> 11) & 31, w & 0xFFFF
            base = r[a] if a else 0
            ea = (base + signed(imm)) & 0xFFFFFFFF
            nxt = pc + 4
            if op == 15: r[d] = (base + (signed(imm) << 16)) & 0xFFFFFFFF
            elif op == 14: r[d] = ea
            elif op == 24: r[a] = r[d] | imm                                         # ori
            elif op == 32: r[d] = self.read(ea, 4)
            elif op == 42: r[d] = signed(self.read(ea, 2)) & 0xFFFFFFFF                # lha
            elif op == 36: self.write(ea, 4, r[d])
            elif op == 7: r[d] = (r[a] * signed(imm)) & 0xFFFFFFFF                   # mulli
            elif op == 11: self.setcr(d >> 2, signed(r[a], 32), signed(imm))          # cmpwi
            elif op == 10: self.setcr(d >> 2, r[a], imm)                              # cmplwi
            elif op == 31 and (w >> 1) & 0x3FF == 266: r[d] = (r[a] + r[b]) & 0xFFFFFFFF   # add
            elif op == 48: f[d] = struct.unpack(">f", self.read(ea, 4).to_bytes(4, "big"))[0]
            elif op == 50: f[d] = struct.unpack(">d", self.read(ea, 8).to_bytes(8, "big"))[0]
            elif op == 52: self.write(ea, 4, f32(f[d]))
            elif op == 18:
                target = (pc + signed(w & 0x03FFFFFC, 26)) & 0xFFFFFFFF
                if w & 1:
                    if self.game and self.game(target, self):
                        pass
                    else:
                        saved, self.lr = self.lr, nxt
                        self.run(target)
                        self.lr = saved
                    pc = nxt
                    continue
                pc = target
                continue
            elif op == 16:
                if self.taken(d, a):
                    pc = (pc + signed(w & 0xFFFC)) & 0xFFFFFFFF
                    continue
            elif op == 19 and (w >> 1) & 0x3FF == 16:                                 # bclr
                if self.taken(d, a):
                    return
            elif op == 19 and (w >> 1) & 0x3FF == 449:                                # cror
                v = self.bit(a) | self.bit(b)
                field, shift = d >> 2, 3 - (d & 3)
                self.cr[field] = (self.cr[field] & ~(1 << shift)) | (v << shift)
            elif op == 59:
                xo, c = (w >> 1) & 31, (w >> 6) & 31
                f[d] = single({20: lambda: f[a] - f[b], 21: lambda: f[a] + f[b], 18: lambda: f[a] / f[b],
                               25: lambda: f[a] * f[c]}[xo]())
            elif op == 63:
                xo, c = (w >> 1) & 0x3FF, (w >> 6) & 31
                if xo == 32: self.setcr(d >> 2, f[a], f[b])                             # fcmpo
                elif xo == 72: f[d] = f[b]                                              # fmr
                elif xo == 264: f[d] = abs(f[b])                                        # fabs
                elif xo == 40: f[d] = -f[b]                                             # fneg
                elif xo == 12: f[d] = single(f[b])                                      # frsp
                elif xo & 31 == 20: f[d] = f[a] - f[b]                                  # fsub
                elif xo & 31 == 21: f[d] = f[a] + f[b]                                  # fadd
                elif xo & 31 == 25: f[d] = f[a] * f[c]                                  # fmul
                else: raise AssertionError(f"{pc:08X}: {w:08X}")
            else:
                raise AssertionError(f"{pc:08X}: {w:08X} is not an instruction of these routines")
            pc = nxt
        raise AssertionError("no return")


def slots_of_the_game():
    """protocol/stats.toml's constants: name -> (offset in a seat's record, stock bits) (a double's high word)."""
    with open(parts.path_in("REPO_PROTOCOL", "protocol/stats.toml"), "rb") as f:
        constants = tomllib.load(f)["constant"]
    return {c["name"]: (c["seat"], c["stock"] >> 32 if c["bits"] == 64 else c["stock"]) for c in constants}


def image_memory(image):
    return {memmap.IMAGE + i: b for i, b in enumerate(image)}


class TheSites(Build):
    def records(self, memory, seat_values):
        for seat in range(30):
            for name, value in seat_values(seat).items():
                at = RECORDS + seat * memmap.SEAT_CONSTANT + self.slots[name][0]
                put(memory, at, value, 8 if name == "AccelGain" else 4)

    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        cls.slots = slots_of_the_game()

    def test_the_slots_are_const_ranges_h(self):
        for offset, known, kind, fpr, name, record in sites_curve.SITES:
            self.assertEqual(self.slots[name][0], record, name)
        curve = [n for n, (at, _) in self.slots.items() if memmap.SEAT_CURVE <= at < memmap.SEAT_CONSTANT]
        self.assertEqual(sorted(curve), sorted(s[4] for s in sites_curve.SITES))
        self.assertEqual(self.slots["BoostLengthBase"], (memmap.SEAT_BOOST_BASE, f32(sites_dash.BOOST_BASE)))
        self.assertEqual(self.slots["BoostLengthScale"], (memmap.SEAT_BOOST_SCALE, f32(sites_dash.BOOST_SCALE)))

    def test_every_site_is_patched_at_its_word(self):
        for offset, known, kind, fpr, name, record in sites_curve.SITES:
            p = self.patches[TEXT + offset]
            self.assertEqual((p["original"], p["what"]), (f"{known:08X}", sites_curve.site_name(offset)))
        for offset, known in sites_curve.CALLS:
            self.assertEqual(self.patches[TEXT + offset]["original"], f"{known:08X}")
        self.assertEqual(self.patches[TEXT + sites_dash.BOOST_SITE]["original"], f"{sites_dash.BOOST_KNOWN:08X}")

    def one(self, offset, record, mode=1):
        """Runs a site's routine with made-up registers: r4 the pool (a word at every offset), f0..f7 apart; the
        seat's record at `record` (0: none). Answers the float registers and where it left."""
        memory = image_memory(self.image)
        for o in range(0, 0x100, 4):
            put(memory, POOL + o, f32(1000.0 + o))
        put(memory, POOL + 0xB8, struct.unpack(">Q", struct.pack(">d", 1234.5))[0], 8)
        rec = RECORDS + 7 * memmap.SEAT_CONSTANT
        for o in range(0, memmap.SEAT_CONSTANT, 4):
            put(memory, rec + o, f32(-1.0 - o))
        put(memory, rec + memmap.SEAT_CURVE_DOUBLE, struct.unpack(">Q", struct.pack(">d", -77.25))[0], 8)
        put(memory, SETTING, rec if record else 0)
        put(memory, memmap.MODE, mode)
        cpu = Ppc(memory)
        cpu.r[4] = POOL
        cpu.f[:8] = [single(0.5 + k) for k in range(8)]
        left = run_to_game(cpu, self.symbols[sites_curve.site_name(offset)], self.image)
        return cpu, [left], rec

    def test_each_number_is_the_records_with_the_seat_named_and_the_games_without(self):
        for offset, known, kind, fpr, name, record in sites_curve.SITES:
            with self.subTest(name=name):
                own, left, rec = self.one(offset, True)
                self.assertEqual(left, [TEXT + offset + 4])
                expected = -77.25 if kind == "double" else single(-1.0 - record)
                self.assertEqual(own.f[fpr], expected)
                plain, left, _ = self.one(offset, True, mode=0)     # a plain start: the game's word, named or not
                self.assertEqual(left, [TEXT + offset + 4])
                game, left, _ = self.one(offset, False)
                self.assertEqual(plain.f[:8], game.f[:8])
                self.assertEqual(left, [TEXT + offset + 4])
                stock = Ppc({})
                stock.r[4], stock.mem = POOL, dict(game.mem)
                stock.f[:8] = [single(0.5 + k) for k in range(8)]
                stock.mem.update({0x80F00000 + i: b for i, b in enumerate(known.to_bytes(4, "big"))})
                cpu_run_one(stock, 0x80F00000)
                self.assertEqual(game.f[:8], stock.f[:8])
                self.assertEqual(game.cr[0], stock.cr[0])
                if kind == "after":         # the replaced compare ran, then the load
                    self.assertEqual(own.cr[0], stock.cr[0])
                if kind == "before":        # the load, then the replaced word with it
                    mixed = Ppc({})
                    mixed.f[:8] = [single(0.5 + k) for k in range(8)]
                    mixed.f[fpr] = single(-1.0 - record)
                    mixed.mem = {0x80F00000 + i: b for i, b in enumerate(known.to_bytes(4, "big"))}
                    cpu_run_one(mixed, 0x80F00000)
                    self.assertEqual(own.f[:8], mixed.f[:8])

    def test_the_calls_name_the_seat_for_the_calls_length(self):
        for offset, known in sites_curve.CALLS:
            for mode, index, named in ((1, 7, True), (1, 29, True), (1, 30, False), (0, 7, False)):
                with self.subTest(offset=offset, mode=mode, index=index):
                    memory = image_memory(self.image)
                    put(memory, memmap.MODE, mode)
                    put(memory, MACHINE + 4, index, 2)
                    put(memory, SETTING, 0)
                    seen = []
                    def game(target, cpu):
                        self.assertEqual(target, TEXT + sites_curve.CURVE_FUNCTION)
                        seen.append(cpu.read(SETTING, 4))
                        return True
                    cpu = Ppc(memory, game)
                    cpu.r[27] = MACHINE
                    left = run_to_game(cpu, self.symbols[f"setting_call_{offset:05x}"], self.image)
                    self.assertEqual(left, TEXT + offset + 4)
                    self.assertEqual(seen, [RECORDS + index * memmap.SEAT_CONSTANT if named else 0])
                    self.assertEqual(cpu.read(SETTING, 4), 0)

    def test_the_boost_length_is_base_plus_scale_times_the_duration(self):
        for mode, index, base, scale, d in ((1, 3, 0.0, 60.0, 1.7), (1, 3, 10.0, 50.0, 1.25), (0, 3, 10.0, 50.0, 1.25),
                                           (1, 30, 10.0, 50.0, 1.25)):
            memory = image_memory(self.image)
            put(memory, memmap.MODE, mode)
            put(memory, MACHINE + 4, index, 2)
            for seat in range(30):
                rec = RECORDS + seat * memmap.SEAT_CONSTANT
                put(memory, rec + memmap.SEAT_BOOST_BASE, f32(base if seat == index else 99.0))
                put(memory, rec + memmap.SEAT_BOOST_SCALE, f32(scale if seat == index else 9.0))
            cpu = Ppc(memory)
            cpu.r[30] = MACHINE
            cpu.f[1], cpu.f[2] = single(d), 60.0
            self.assertEqual(run_to_game(cpu, self.symbols["boost_length"], self.image), TEXT + sites_dash.BOOST_SITE + 4)
            ours = mode == 1 and index < 30
            self.assertEqual(cpu.f[1], base + single(scale * single(d)) if ours else single(60.0 * single(d)))
        # the game's own numbers: the game's own product, for every duration the ranges allow
        for k in range(1, 4260, 7):
            d = single(k / 1000.0)
            memory = image_memory(self.image)
            put(memory, memmap.MODE, 1)
            put(memory, MACHINE + 4, 0, 2)
            put(memory, RECORDS + memmap.SEAT_BOOST_BASE, 0)
            put(memory, RECORDS + memmap.SEAT_BOOST_SCALE, f32(60.0))
            cpu = Ppc(memory)
            cpu.r[30], cpu.f[1], cpu.f[2] = MACHINE, d, 60.0
            run_to_game(cpu, self.symbols["boost_length"], self.image)
            self.assertEqual(struct.pack(">d", cpu.f[1]), struct.pack(">d", single(60.0 * d)))


SCRATCH = 0x80FFFF00


def cpu_run_one(cpu, pc):
    """Runs the one instruction at pc (no call) and sets cpu.pc to the next."""
    w = cpu.read(pc, 4)
    if w >> 26 in (16, 18):
        assert not w & 1
        if w >> 26 == 18 or cpu.taken((w >> 21) & 31, (w >> 16) & 31):
            cpu.pc = (pc + (signed(w & 0x03FFFFFC, 26) if w >> 26 == 18 else signed(w & 0xFFFC))) & 0xFFFFFFFF
        else:
            cpu.pc = pc + 4
        return
    cpu.write(SCRATCH, 4, w)
    cpu.write(SCRATCH + 4, 4, 0x4E800020)     # blr
    cpu.run(SCRATCH, 3)
    cpu.pc = pc + 4


def run_to_game(cpu, pc, image):
    """Runs an image routine to its branch back into the game: answers the address it goes to."""
    for _ in range(200):
        w = cpu.read(pc, 4)
        if w >> 26 == 18 and not w & 1:
            target = (pc + signed(w & 0x03FFFFFC, 26)) & 0xFFFFFFFF
            if not memmap.IMAGE <= target < memmap.IMAGE + len(image):
                return target
        if w >> 26 == 18 and w & 1:
            target = (pc + signed(w & 0x03FFFFFC, 26)) & 0xFFFFFFFF
            if cpu.game is None or not cpu.game(target, cpu):
                raise AssertionError(f"a call to {target:08X}")
            pc += 4
            continue
        cpu_run_one(cpu, pc)
        pc = cpu.pc
    raise AssertionError("the routine does not leave")


WORK = os.environ.get("PORT_WORK") or work_folder.WORK_DEFAULT
MAIN_REL = os.path.join(WORK, "derived", "main.rel")
MAIN_DOL = os.path.join(WORK, "extracted", "GFZE01", "sys", "main.dol.orig")
GX_PLUS = os.path.join(WORK, "notes", "packs", "fzero-gx-plus-line__.bin")


class Linked(Ppc):
    """A Ppc over a linked main.rel (its bytes from memmap.REL on), the image and made-up words."""

    def __init__(self, rel, image, memory, game=None):
        super().__init__(memory, game)
        self.rel, self.image = rel, image

    def read(self, a, n):
        if memmap.REL <= a < memmap.REL + len(self.rel):
            return int.from_bytes(self.rel[a - memmap.REL:a - memmap.REL + n], "big")
        if memmap.IMAGE <= a < memmap.IMAGE + len(self.image):
            return int.from_bytes(self.image[a - memmap.IMAGE:a - memmap.IMAGE + n], "big")
        return super().read(a, n)


@unittest.skipUnless(os.path.exists(MAIN_REL) and os.path.exists(MAIN_DOL), "the work folder's main.rel")
class TheGamesFunction(unittest.TestCase):
    """fn_1_22B10 as the disc has it, run with and without the patch list."""

    @classmethod
    def setUpClass(cls):
        cls.dir = tempfile.TemporaryDirectory()
        b = build.build(cls.dir.name, MAIN_REL, MAIN_DOL)
        cls.image = bytes(b["image"])
        with open(MAIN_REL, "rb") as f:
            clean, _ = relink.relocate(f.read(), memmap.REL, memmap.REL_BSS)
        cls.clean = bytes(clean)
        patched = bytearray(clean)
        for p in b["patches"]:
            at = int(p["address"], 16)
            if memmap.REL <= at < memmap.REL + len(clean):
                struct.pack_into(">I", patched, at - memmap.REL, int(p["new"], 16))
        cls.patched = bytes(patched)
        cls.slots = slots_of_the_game()
        rng = random.Random(22)
        cls.blocks = [cls.block(rng) for _ in range(100)]
        # edges: drift acceleration in each band and at the cap, light and heavy, every acceleration region
        for drift in (-3.0, 0.0, 0.5, 1.0, 1.2, 1.5, 2.0, 2.3, 2.7, 3.3, 5.0, 19.0):
            for accel, speed in ((0.3, 0.1), (0.31, 0.2), (0.45, 0.14), (0.5, 0.15), (0.65, 0.3), (0.8, 0.5)):
                cls.blocks.append(cls.block(rng, drift=drift, accel=accel, speed=speed))

    @classmethod
    def tearDownClass(cls):
        cls.dir.cleanup()

    @staticmethod
    def block(rng, drift=None, accel=None, speed=None):
        words = [rng.uniform(-1, 1) for _ in range(45)]
        words[1] = rng.choice([500.0, 699.0, 700.0, 1200.0, rng.uniform(400, 3000)])      # weight
        words[2] = rng.uniform(0.2, 0.9) if accel is None else accel                     # acceleration
        words[3] = rng.uniform(0.05, 0.6) if speed is None else speed                    # max speed
        words[4], words[5] = rng.uniform(0.1, 5), rng.uniform(0.05, 0.5)                  # grips
        words[7] = rng.uniform(-1, 6) if drift is None else drift                       # drift acceleration
        words[8] = rng.uniform(50, 300)                                                  # turn movement
        words[13] = rng.uniform(5, 25)                                                   # boost strength
        words[15] = rng.choice([0.005, 0.01, 0.03, 0.031, rng.uniform(0, 0.05)])         # turn deceleration
        return b"".join(struct.pack(">f", w) for w in words)

    def run_setting(self, rel, block, setting, record):
        memory = {BLOCK + i: v for i, v in enumerate(block)}
        if record is not None:
            rec = RECORDS + 11 * memmap.SEAT_CONSTANT
            for name, (at, bits) in self.slots.items():
                bits = record.get(name, bits)
                if name == "AccelGain":
                    low = self.slots_low
                    put(memory, rec + at, bits << 32 | low, 8)
                else:
                    put(memory, rec + at, bits)
            put(memory, SETTING, rec)
        put(memory, memmap.MODE, 1)
        cpu = Linked(rel, self.image, memory)
        cpu.r[3] = BLOCK
        cpu.f[1] = single(single(float(setting)) * single(0.01))
        cpu.run(TEXT + sites_curve.CURVE_FUNCTION)
        return bytes(cpu.read(BLOCK + i, 1) for i in range(0xB4)), cpu

    slots_low = 0x40000000

    def test_the_games_numbers_give_the_games_bits_and_no_seat_the_games_code(self):
        differ = 0
        for block in self.blocks:
            for setting in range(0, 101):
                game, _ = self.run_setting(self.clean, block, setting, None)
                ours, cpu = self.run_setting(self.patched, block, setting, {})
                plain, _ = self.run_setting(self.patched, block, setting, None)
                differ += game != ours or game != plain
                self.assertEqual(cpu.r[0] in (0, 1), True)
        self.assertEqual(differ, 0)

    def test_a_number_changes_its_role_alone(self):
        # DriftFastLoss: the speed side's drift; the acceleration side and every other stat as the game's
        block = self.block(random.Random(1), drift=3.0, accel=0.5, speed=0.3)
        game, _ = self.run_setting(self.clean, block, 80, None)
        ours, _ = self.run_setting(self.patched, block, 80, {"DriftFastLoss": f32(1.0)})
        self.assertNotEqual(game[0x1C:0x20], ours[0x1C:0x20])
        self.assertEqual(game[:0x1C] + game[0x20:], ours[:0x1C] + ours[0x20:])
        game, _ = self.run_setting(self.clean, block, 20, None)
        ours, _ = self.run_setting(self.patched, block, 20, {"DriftFastLoss": f32(1.0)})
        self.assertEqual(game, ours)

    @unittest.skipUnless(os.path.exists(GX_PLUS), "F-Zero GX+'s main module in the work folder")
    def test_f_zero_gx_plus_drift_is_its_own_codes(self):
        with open(GX_PLUS, "rb") as f:
            mod, _ = relink.relocate(mainrel.unpack(mainrel.decrypt(f.read())), memmap.REL, memmap.REL_BSS)
        mod = bytes(mod)
        for block in self.blocks[::4]:
            for setting in range(0, 101, 5):
                theirs, _ = self.run_setting(mod, block, setting, None)
                ours, _ = self.run_setting(self.patched, block, setting, {"DriftFastLoss": f32(1.0)})
                self.assertEqual(theirs, ours, setting)


if __name__ == "__main__":
    unittest.main()
