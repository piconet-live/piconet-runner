"""The game mode's race-side sites (sites_mode.py), run here as the bytes the image holds against made-up machines:
the pair gate (a pair the mode keeps apart is not answered and keeps the game's immunity bits; an answered pair goes
into the touching rows), the timer (the game's countdown skipped under a clock limit) and the explosion that keeps
the motion. The game's own code at the calls is a stand-in that does what the disassembly says of it. No game data."""
import os, re, struct, sys, unittest

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, ".."))
from test_raceview import Build, Cpu, get, put, IMAGE, TEXT, native
from test_race_end import signed
import build, memmap
import sites_mode
import sites_combat
import standin_inputs

MODE, MODES = memmap.MODE, memmap.MODES
BLOCK = 0x80D00000
MI, MJ = 0x80D10000, 0x80D20000           # the pair's machines
I, J = 3, 7                               # their seats (r28, r27)


class ModeCpu(Cpu):
    """rlwnm. (the pair gate's bit test) and the mode point's saving of registers beside what Cpu reads."""

    def taken(self, bo, bi):
        """cr7 too (combat_wall's): Machine keeps one field, so its test says apart that no compare writes cr0."""
        return super().taken(bo, bi - 28 if bi >= 28 else bi)

    def other(self, pc, w):
        op, d, a, xo = w >> 26, (w >> 21) & 31, (w >> 16) & 31, (w >> 1) & 0x3FF
        spr = ((w >> 11) & 31) << 5 | a
        if IMAGE <= pc < IMAGE + len(self.image):
            at = (self.r[a] if a else 0) + signed(w & 0xFFFF)
            if op == 31 and xo == 954 and w & 1:                                          # extsb.
                self.r[a] = signed(self.r[d] & 0xFF, 8) & 0xFFFFFFFF
                value = signed(self.r[a], 32)
                self.compare(value, 0)
                return pc + 4
            if op == 13:                                                                  # addic.
                self.r[d] = (self.r[a] + signed(w & 0xFFFF)) & 0xFFFFFFFF
                self.compare(signed(self.r[d], 32), 0)
                return pc + 4
            if op == 47:                                                                  # stmw
                for n in range(d, 32):
                    self.write((at + 4 * (n - d)) & 0xFFFFFFFF, 4, self.r[n])
                return pc + 4
            if op == 46:                                                                  # lmw
                for n in range(d, 32):
                    self.r[n] = self.read((at + 4 * (n - d)) & 0xFFFFFFFF, 4)
                return pc + 4
            if op == 31 and xo == 339 and spr == 8 and hasattr(self, "lr"):               # mflr, a caller's
                self.r[d] = self.lr
                return pc + 4
            if op == 31 and xo == 19:                                                     # mfcr: nothing reads it
                self.r[d] = 0
                return pc + 4
            if op == 31 and xo == 144:                                                    # mtcrf
                return pc + 4
            if op == 31 and xo == 339 and spr == 9:                                       # mfctr
                self.r[d] = 0
                return pc + 4
            if op == 31 and xo == 467 and spr == 9:                                       # mtctr
                self.ctr = self.r[d]
                return pc + 4
        if w >> 26 == 23:
            s, a, b, mb, me = (w >> 21) & 31, (w >> 16) & 31, (w >> 11) & 31, (w >> 6) & 31, (w >> 1) & 31
            sh = self.r[b] & 31
            rotated = ((self.r[s] << sh) | (self.r[s] >> (32 - sh))) & 0xFFFFFFFF if sh else self.r[s]
            inside = (lambda i: mb <= i <= me) if mb <= me else (lambda i: i >= mb or i <= me)
            self.r[a] = rotated & sum(1 << (31 - i) for i in range(32) if inside(i))
            if w & 1:
                self.compare(signed(self.r[a], 32), 0)
            return pc + 4
        return super().other(pc, w)


class ThePairGate(Build):
    def race(self, mode=1, block=BLOCK, any_row=0, rows=None):
        memory = {}
        put(memory, MODE, mode)
        put(memory, MODES + 4, block)
        put(memory, BLOCK + memmap.MODE_NC_ANY, any_row)
        for seat, row in (rows or {}).items():
            put(memory, BLOCK + memmap.MODE_NC + 4 * seat, row)
        put(memory, MI + 0x514, 0)
        put(memory, MJ + 0x514, 0)
        return memory

    def pair(self, memory, answer=1):
        """One pair of the contact loop through the site: gives the game addresses reached and the memory."""
        reached = []

        def game(address, cpu):
            reached.append(address)
            if address == TEXT + sites_mode.PAIR_CALL:
                cpu.r[3] = answer
        m = ModeCpu(self.image, memory, game)
        m.r[3], m.r[4], m.r[28], m.r[27] = MI, MJ, I, J
        m.run(self.symbols["mode_pair"])
        return reached, m

    def test_the_site_is_the_pair_call(self):
        p = self.patches[TEXT + sites_mode.PAIR]
        self.assertEqual(int(p["original"], 16), sites_mode.PAIR_KNOWN)        # bl fn_1_239C4

    def test_without_a_mode_the_pair_is_the_games(self):
        for memory in (self.race(mode=0, any_row=~0), self.race(block=0, any_row=~0)):
            reached, m = self.pair(memory)
            self.assertEqual(reached, [TEXT + sites_mode.PAIR_CALL, TEXT + sites_mode.PAIR + 4])
            self.assertEqual(m.r[3], 1)
            self.assertEqual(get(m.mem, BLOCK + memmap.MODE_TOUCH + 4 * I), 0)

    def test_an_answered_pair_is_touching_both_ways(self):
        reached, m = self.pair(self.race())
        self.assertEqual(reached, [TEXT + sites_mode.PAIR_CALL, TEXT + sites_mode.PAIR + 4])
        self.assertEqual(get(m.mem, BLOCK + memmap.MODE_TOUCH + 4 * I), 0x80000000 >> J)
        self.assertEqual(get(m.mem, BLOCK + memmap.MODE_TOUCH + 4 * J), 0x80000000 >> I)
        reached, m = self.pair(self.race(), answer=0)
        self.assertEqual((m.r[3], get(m.mem, BLOCK + memmap.MODE_TOUCH + 4 * I)), (0, 0))

    def test_a_pair_kept_apart_is_not_answered_and_stays_immune(self):
        for rows in ({I: 0x80000000 >> J}, {J: 0x80000000 >> I}, {I: 0xFFFFFFFF}):
            reached, m = self.pair(self.race(any_row=1, rows=rows))
            self.assertEqual(reached, [TEXT + sites_mode.PAIR + 4])
            self.assertEqual(m.r[3], 0)
            self.assertEqual(get(m.mem, MI + 0x514), 0x80000000 >> J)
            self.assertEqual(get(m.mem, MJ + 0x514), 0x80000000 >> I)
            self.assertEqual(get(m.mem, BLOCK + memmap.MODE_TOUCH + 4 * I), 0)

    def test_a_row_for_another_seat_leaves_the_pair_the_games(self):
        reached, m = self.pair(self.race(any_row=1, rows={I: 0x80000000 >> (J + 1), 5: 0xFFFFFFFF}))
        self.assertEqual(reached, [TEXT + sites_mode.PAIR_CALL, TEXT + sites_mode.PAIR + 4])
        self.assertEqual(get(m.mem, MI + 0x514), 0)


class ThePreload(Build):
    """The block is taken on the black frame before the start gate; under start_together it comes with every
    no-contact row on, before the race's first contact pass."""
    BLOCK_AT = 0x80A01000

    def preload(self, flags, wanted=sites_mode.WANTED):
        memory = {}
        sizes = []
        put(memory, memmap.MODES, wanted)
        put(memory, memmap.MODE_HUD_FLAGS, flags)
        put(memory, memmap.REL_BSS + 0x960, 2, 2)
        put(memory, memmap.REL_BSS + 0x3C2A, 0x27, 2)
        put(memory, memmap.REL_BSS + 0x26C28, 1)
        put(memory, build.MAIN_HEAP, 1)
        put(memory, build.HEAP_TABLE, 0x80E00000)
        put(memory, 0x80E00000 + 12 + 4, 0x80E10000)
        put(memory, 0x80E10000 + 8, 0x200000)
        for at in range(self.BLOCK_AT, self.BLOCK_AT + memmap.MODE_BLOCK + memmap.MODE_STATS_PAGE + 4):
            memory[at] = 0xFF
        calls = []

        def game(address, cpu):
            calls.append(address)
            sizes.append(cpu.r[4])
            cpu.r[3] = {1: 0x80F00000, 2: self.BLOCK_AT}.get(len(calls), 0)
        m = Cpu(self.image, memory, game)
        m.r[24], m.r[30], m.r[31] = 0x2424, 0x3030, 0x3131
        m.run(self.symbols["mode_preload"], limit=40000)     # (the block's zeroing is 7,168 turns of a loop)
        self.assertEqual((m.r[24], m.r[30], m.r[31], get(m.mem, memmap.MODES + 4)), (0x2424, 0x3030, 0x3131, self.BLOCK_AT))
        self.assertEqual(sizes[1], memmap.MODE_ALLOC + (memmap.MODE_STATS_PAGE if wanted == sites_mode.WANTED_STATS else 0))
        return m.mem

    def test_the_stats_page_is_taken_and_zeroed_only_when_asked(self):
        plain = self.preload(0)
        self.assertEqual(plain.get(self.BLOCK_AT + memmap.MODE_BLOCK - 4), 0)
        self.assertEqual(plain.get(self.BLOCK_AT + memmap.MODE_BLOCK), 0xFF)
        page = self.preload(0, sites_mode.WANTED_STATS)
        self.assertEqual(get(page, self.BLOCK_AT + memmap.MODE_BLOCK + memmap.MODE_STATS_PAGE - 4), 0)
        self.assertEqual(page.get(self.BLOCK_AT + memmap.MODE_BLOCK + memmap.MODE_STATS_PAGE), 0xFF)

    def row(self, memory, seat):
        return get(memory, self.BLOCK_AT + memmap.MODE_NC + 4 * seat)

    def test_the_block_comes_zeroed_whatever_the_mode(self):
        for flags in (memmap.MODE_ROLLING, memmap.MODE_TOGETHER):
            memory = self.preload(flags)
            self.assertEqual({memory[a] for a in range(self.BLOCK_AT, self.BLOCK_AT + memmap.MODE_BLOCK)}, {0})


class TheFirstPass(Build):
    """mode_begin: the host's game_start at the countdown task's entry, while the block's run count is 0."""

    def enter(self, mode=1, block=BLOCK, runs=0):
        memory = {}
        put(memory, MODE, mode)
        put(memory, MODES + 4, block)
        put(memory, BLOCK + memmap.MODE_RUNS, runs)
        reached = []
        m = ModeCpu(self.image, memory, lambda address, cpu: reached.append(address))
        m.r[3] = 0x80123456
        m.r[1] = 0x80F00000
        m.lr = 0x80000111
        m.run(self.symbols["mode_begin"])
        return m, reached

    def test_the_first_entry_calls_the_host_and_keeps_the_tasks_argument(self):
        m, reached = self.enter()
        self.assertEqual(reached, [memmap.HOST_MODE, TEXT + sites_mode.BEGIN + 4])
        self.assertEqual((m.r[3], m.r[1], m.lr), (0x80123456, 0x80F00000 - 0x20, 0x80000111))   # (r1: the game's own first word has run)

    def test_later_entries_and_other_starts_call_nothing(self):
        for mode, block, runs in ((1, BLOCK, 1), (1, 0, 0), (0, BLOCK, 0)):
            self.assertEqual(self.enter(mode, block, runs)[1], [TEXT + sites_mode.BEGIN + 4])

    def test_the_count_is_the_interpreters(self):
        self.assertEqual(memmap.MODE_RUNS, 0x1E0 + 12)     # mode.h RUNS


class TheTimer(Build):
    def timer(self, rule, mode=1, block=BLOCK, clock=3600):
        memory = {}
        put(memory, MODE, mode)
        put(memory, MODES + 4, block)
        put(memory, BLOCK + memmap.MODE_CLOCK, clock)
        reached = []
        m = ModeCpu(self.image, memory, lambda address, cpu: reached.append(address))
        m.r[0] = 0x40000 if rule else 0       # the site's rlwinm. of the rule's timed bit
        m.cr0 = (False, rule, not rule)
        m.run(self.symbols["mode_timer"])
        return reached

    def test_the_site_is_the_timed_rules_branch(self):
        p = self.patches[TEXT + sites_mode.TIMER]
        self.assertEqual(int(p["original"], 16), sites_mode.TIMER_KNOWN)

    def test_the_games_countdown_unless_a_clock_limit(self):
        count, skip = [TEXT + sites_mode.TIMER + 4], [TEXT + sites_mode.TIMER_SKIP]
        self.assertEqual(self.timer(False), skip)                    # no timed rule: as the game
        self.assertEqual(self.timer(True, mode=0), count)            # a plain start: the game's countdown
        self.assertEqual(self.timer(True, block=0), count)           # no mode
        self.assertEqual(self.timer(True, clock=0), count)           # a mode without a clock limit
        self.assertEqual(self.timer(True), skip)                     # the mode keeps the timer


class TheExplosion(Build):
    def test_the_game_explodes_the_machine_and_its_motion_stays(self):
        machine = 0x80D30000
        memory = {}
        motion = {0x94: 0x44000000, 0x98: 0x3F800000, 0x9C: 0xC2000000, 0xA0: 1, 0xA4: 2, 0xA8: 3,
                  0x224: 0x40A00000, 0x17C: 0x447A0000}
        for at, value in motion.items():
            put(memory, machine + at, value)
        reached = []

        def game(address, cpu):
            reached.append((address, cpu.r[3]))
            if address == TEXT + sites_mode.DESTROY:                 # fn_1_25004: the motion 0, the flags
                for at in motion:
                    put(cpu.mem, machine + at, 0)
                put(cpu.mem, machine, 0x40800880)
        m = ModeCpu(self.image, memory, game)
        m.r[3] = machine
        m.run(self.symbols["mode_explode"])
        self.assertEqual(reached, [(TEXT + sites_mode.DESTROY, machine)])
        self.assertEqual({at: get(m.mem, machine + at) for at in motion}, motion)
        self.assertEqual(get(m.mem, machine), 0x40800880)


def bits(value):
    return struct.unpack(">I", struct.pack(">f", value))[0]


class TheCombatSites(Build):
    """Each routine of sites_combat.py as the image holds it: with no mode or a word of 0 the game's own instruction, with a
    word what the vocabulary says."""
    M = 0x80D80000
    R = 0x80D90000                                    # the registers the game's instructions read from

    def run_site(self, name, word=None, value=0, mode=True, lr=None, regs=None, floats=None, stored=None):
        memory = {}
        put(memory, MODE, 1)
        put(memory, MODES + 4, BLOCK if mode else 0)
        if word is not None:
            put(memory, BLOCK + memmap.MODE_COMBAT + word, value)
        put(memory, BLOCK + memmap.MODE_COMBAT + memmap.MODE_COMBAT_ONE, bits(1.0))
        for at, v in (stored or {}).items():
            put(memory, at, v)
        out = []
        m = ModeCpu(self.image, memory, lambda address, cpu: out.append(address))
        for n, v in (regs or {}).items():
            m.r[n] = v
        m.__dict__.setdefault("f", [0.0] * 32)
        for n, v in (floats or {}).items():
            m.f[n] = v
        if lr is not None:
            m.lr = lr
        m.run(self.symbols[name])
        self.assertEqual(len(out), 1)
        return m, out[0]

    def test_every_site_is_patched(self):
        offsets = [a for a, _, _ in sites_combat.FACTORS] + [sites_combat.CAP, sites_combat.KNOCK, sites_combat.WALL,
                                                             sites_combat.BOUNCE, sites_combat.SPIN, sites_combat.SIDE]
        for offset in offsets + [a for a, _, _, _ in sites_combat.IMMUNE]:
            self.assertIn(TEXT + offset, self.patches)

    def test_the_factor_is_the_games_without_a_word_and_the_live_one_with(self):
        stored = {self.R + 0x128: bits(0.002)}
        for name in ("combat_factor_a", "combat_factor_b"):
            for mode in (False, True):
                m, back = self.run_site(name, mode=mode, regs={31: self.R}, stored=stored)
                self.assertAlmostEqual(m.f[0], 0.002)
            m, _ = self.run_site(name, memmap.MODE_COMBAT_FACTOR, bits(0.004), regs={31: self.R}, stored=stored)
            self.assertAlmostEqual(m.f[0], 0.004)

    def test_the_cap_moves_only_for_a_contacts_call(self):
        stored = {self.R + 0x178: bits(20.0)}
        for call in sites_combat.CONTACT_CALLS:
            m, _ = self.run_site("combat_cap", memmap.MODE_COMBAT_CAP, bits(40.0), lr=TEXT + call + 4, regs={6: self.R}, stored=stored)
            self.assertEqual(m.f[0], 40.0)
        m, _ = self.run_site("combat_cap", memmap.MODE_COMBAT_CAP, bits(40.0), lr=TEXT + 0x24AD0, regs={6: self.R}, stored=stored)
        self.assertEqual(m.f[0], 20.0)

    def test_knockback_scales_both_machines_scales(self):
        stored = {self.R + 0x94: bits(7.0)}
        m, _ = self.run_site("combat_knock", memmap.MODE_COMBAT_KNOCK, bits(3.0), regs={24: self.R}, floats={31: 1.5, 30: 2.0}, stored=stored)
        self.assertEqual((m.f[31], m.f[30], m.f[0]), (4.5, 6.0, 7.0))
        m, _ = self.run_site("combat_knock", regs={24: self.R}, floats={31: 1.5, 30: 2.0}, stored=stored)
        self.assertEqual((m.f[31], m.f[30]), (1.5, 2.0))

    def test_the_wall_damage_is_scaled_and_cr0_stays(self):
        m, _ = self.run_site("combat_wall", memmap.MODE_COMBAT_WALL, bits(0.5), floats={28: 8.0, 0: 3.0})
        self.assertEqual(m.f[3], 12.0)
        m, _ = self.run_site("combat_wall", floats={28: 8.0, 0: 3.0})
        self.assertEqual(m.f[3], 24.0)
        at = self.symbols["combat_wall"] - IMAGE
        for k in range(24):                               # no compare of the routine's writes cr0
            w = struct.unpack(">I", self.image[at + 4 * k:at + 4 * k + 4])[0]
            if w >> 26 == 18:                             # its way back: the routine's end
                break
            if w >> 26 in (10, 11):
                self.assertEqual((w >> 23) & 7, 7)
            self.assertNotIn(w >> 26, (28, 29))

    def test_the_bounce_goes_round_one(self):
        stored = {self.R + 0x238: bits(9.0)}
        m, _ = self.run_site("combat_bounce", memmap.MODE_COMBAT_BOUNCE, bits(2.0), regs={31: self.R}, floats={1: 1.5}, stored=stored)
        self.assertEqual((m.f[1], m.f[0]), (2.0, 9.0))
        m, _ = self.run_site("combat_bounce", regs={31: self.R}, floats={1: 1.5}, stored=stored)
        self.assertEqual(m.f[1], 1.5)

    def test_spin_and_side_off_clear_the_flag_before_the_game_tests_it(self):
        stored = {self.M: 0xFFFFFFFF}
        m, _ = self.run_site("combat_spin", memmap.MODE_COMBAT_SPIN, 1, regs={3: self.M, 0: 0xFFFFFFFF}, stored=stored)
        self.assertEqual(get(m.mem, self.M), 0xFFFFFFF7)
        m, _ = self.run_site("combat_spin", regs={3: self.M, 0: 0xFFFFFFFF}, stored=stored)
        self.assertEqual(get(m.mem, self.M), 0xFFFFFFFF)
        m, _ = self.run_site("combat_side", memmap.MODE_COMBAT_SIDE, 1, regs={3: self.M, 5: 0xFFFFFFFF}, stored=stored)
        self.assertEqual((get(m.mem, self.M), m.r[5]), (0xFFFDFFFF, 0xFFFDFFFF))

    def test_a_spinning_machine_is_immune_only_with_the_word(self):
        for name, register, seat_reg in (("combat_immune_a", 24, 27), ("combat_immune_b", 25, 26)):
            for flags, word, equal in ((8, 1, True), (0, 1, False), (8, 0, False)):
                m, _ = self.run_site(name, memmap.MODE_COMBAT_IMMUNE, word, regs={register: self.M, seat_reg: 1}, stored={self.M: flags})
                self.assertEqual(m.cr0[2], equal, (name, flags, word))


class TheDamage(Build):
    """The mode point's loop: `damage` calls the game's own routine with the machine and the amount, for the seat whose
    record asks; the stand-ins are fn_1_24F10, the interpreter and the mode's banners."""
    MANAGER, MACHINES = 0x80D60000, 0x80D70000

    def point(self, seat_flags):
        memory = {}
        put(memory, MODE, 1)
        put(memory, MODES + 4, BLOCK)
        put(memory, self.MANAGER + 0x18, self.MACHINES)
        memory[self.MANAGER + 0x2A] = 3                    # three machines
        for seat, flags in seat_flags.items():
            put(memory, BLOCK + seat * memmap.MODE_SEAT, flags)
            put(memory, BLOCK + memmap.MODE_STATS + seat * memmap.MODE_STAT + memmap.MODE_STAT_DAMAGE,
                struct.unpack(">I", struct.pack(">f", 5.0 + seat))[0])
        hurt = []

        def game(address, cpu):
            if address == TEXT + sites_mode.HURT:
                hurt.append((cpu.r[3], cpu.f[1]))
        m = ModeCpu(self.image, memory, game)
        m.r[31] = self.MANAGER
        m.run(self.symbols["mode_point"])
        return hurt

    def test_the_seat_that_asks_is_hurt_by_the_game_with_its_amount(self):
        self.assertEqual(self.point({1: sites_mode.DAMAGE}), [(self.MACHINES + memmap.MACHINE_SIZE, 6.0)])

    def test_each_seat_with_its_own_amount(self):
        self.assertEqual(self.point({0: sites_mode.DAMAGE, 2: sites_mode.DAMAGE}),
                         [(self.MACHINES, 5.0), (self.MACHINES + 2 * memmap.MACHINE_SIZE, 7.0)])

    def test_no_seat_asks(self):
        self.assertEqual(self.point({0: sites_mode.PEDALS}), [])


class TheRuleHookWrapper(Build):
    """standin_inputs.py from `ps_yes` and `call_orig`: a seat's record against the game's rule hook, run as the image
    holds it; the hook is a stand-in that clears what the game's start hold or lap-1 rule would."""
    SEAT = 5
    M = 0x80D40000            # the seat's machine
    DATA = 0x80D50000         # the stand-in routine's data (r30): +36 0, no AI hold
    MODULE = 0x80600000
    ROW = BLOCK + memmap.MODE_SEATS + SEAT * memmap.MODE_SEAT
    SAVE = BLOCK + memmap.MODE_ROLL_SAVE + SEAT * memmap.MODE_ROLL
    ACCEL, BRAKE, STEER, STRAFE = 0x3F000000, 0x3E800000, 0x3E000000, 0x3D800000

    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        from keystone import Ks, KS_ARCH_PPC, KS_MODE_PPC32, KS_MODE_BIG_ENDIAN
        text = standin_inputs.asm()
        code = bytes(Ks(KS_ARCH_PPC, KS_MODE_PPC32 + KS_MODE_BIG_ENDIAN).asm(text)[0])
        assert cls.image.count(code) == 1
        at = IMAGE + cls.image.index(code)
        cls.labels = {name: at + offset for name, offset in native.label_offsets(text).items()}

    def wrap(self, record=0, flags=0x41, block=BLOCK, hook=None, entry="call_orig"):
        """Through the wrapper: gives the machine's words as the hook saw them and the machine after."""
        memory = {}
        put(memory, MODES + 4, block)
        put(memory, 0x800030C8, self.MODULE)
        put(memory, self.ROW, record)
        put(memory, self.ROW + 4, 0x3F400000)             # the record's accelerator
        put(memory, self.ROW + 8, 0x3DCCCCCD)             # and brake
        put(memory, self.M, flags)
        put(memory, self.M + 4, self.SEAT << 16)          # the seat (lha 4(31))
        for at, value in ((0x1FC, self.STEER), (0x1F8, self.STRAFE), (0x200, self.ACCEL), (0x204, self.BRAKE)):
            put(memory, self.M + at, value)
        seen = []

        def game(address, cpu):
            self.assertEqual((address, cpu.r[3]), (self.MODULE + 0x332C8, self.M))
            seen.append({at: get(cpu.mem, self.M + at) for at in (0, 0x1F8, 0x1FC, 0x200, 0x204)})
            if hook:
                hook(cpu)
        m = ModeCpu(self.image, memory, game)
        m.r[31], m.r[30], m.r[29], m.r[9] = self.M, self.DATA, self.MODULE, self.SEAT
        m.run(self.labels[entry])
        self.assertEqual(len(seen), 1)
        return seen[0], m

    def hold(self, cpu):
        """The game's start hold: controls zero, no boost request."""
        put(cpu.mem, self.M, get(cpu.mem, self.M) & ~0x40)
        for at in (0x1F8, 0x1FC, 0x200, 0x204):
            put(cpu.mem, self.M + at, 0)

    def test_without_a_mode_the_hook_is_the_games(self):
        seen, m = self.wrap(record=0xFF, block=0)
        self.assertEqual(seen[0], 0x41)
        self.assertEqual(seen[0x200], self.ACCEL)
        self.assertEqual(get(m.mem, self.M), 0x41)
        self.assertEqual(get(m.mem, self.SAVE), 0)

    def test_an_empty_record_changes_nothing(self):
        seen, m = self.wrap()
        self.assertEqual((seen[0], seen[0x200], seen[0x204]), (0x41, self.ACCEL, self.BRAKE))
        self.assertEqual(get(m.mem, self.M), 0x41)

    def test_a_seat_handed_to_the_cpu_skips_the_players_controls(self):
        # past `ps_yes` with HAND: straight to the hook, no input row read and the CPU's controls kept
        seen, m = self.wrap(record=standin_inputs.HAND, entry="ps_yes")
        self.assertEqual((seen[0x1FC], seen[0x200], seen[0x204]), (self.STEER, self.ACCEL, self.BRAKE))
        self.assertEqual(get(m.mem, self.M), 0x41)

    def test_the_rolling_start_keeps_the_cpus_steering_through_the_hold(self):
        record = standin_inputs.HAND | standin_inputs.ROLL
        seen, m = self.wrap(record=record, hook=self.hold)
        self.assertEqual((get(m.mem, self.SAVE), get(m.mem, self.SAVE + 4)), (self.STEER, self.STRAFE))
        self.assertEqual((get(m.mem, self.M + 0x1FC), get(m.mem, self.M + 0x1F8)), (self.STEER, self.STRAFE))
        self.assertEqual(get(m.mem, self.M + 0x200), standin_inputs.ROLL_ACCEL)
        self.assertEqual(get(m.mem, self.M + 0x204), 0)
        self.assertEqual(get(m.mem, self.M) & 0x40, 0)              # the hold's refusal stands

    def test_the_rolling_start_clears_a_boost_or_spin_request_the_hook_left(self):
        # the record of mode.cpp's FillRecords until GO: a CPU's request gets through the game's pre-GO hold (mi12's trace)
        record = 2 | standin_inputs.HAND | standin_inputs.ROLL
        _, m = self.wrap(record=record, flags=0x49)
        self.assertEqual(get(m.mem, self.M) & 0x48, 0)               # no boost, no spin
        self.assertEqual(get(m.mem, self.M + 0x200), standin_inputs.ROLL_ACCEL)

    def test_another_seats_save_is_left_alone(self):
        _, m = self.wrap(record=standin_inputs.ROLL)
        for other in (self.SEAT - 1, self.SEAT + 1):
            self.assertEqual(get(m.mem, BLOCK + memmap.MODE_ROLL_SAVE + other * memmap.MODE_ROLL), 0)

    def test_boost_is_asked_before_the_hook_which_may_refuse_it(self):
        seen, m = self.wrap(record=standin_inputs.BOOST, flags=1)
        self.assertEqual(seen[0], 0x41)                              # the hook sees the request
        self.assertEqual(get(m.mem, self.M), 0x41)
        seen, m = self.wrap(record=standin_inputs.BOOST, flags=1, hook=self.hold)
        self.assertEqual(seen[0], 0x41)
        self.assertEqual(get(m.mem, self.M), 1)                      # the game's lap-1 rule or start hold wins

    def test_boost_off_clears_the_request_after_the_hook_even_a_boost_word(self):
        seen, m = self.wrap(record=2)
        self.assertEqual((seen[0], get(m.mem, self.M)), (0x41, 1))
        _, m = self.wrap(record=2 | standin_inputs.BOOST, flags=1)
        self.assertEqual(get(m.mem, self.M), 1)

    def stats(self, mask, values, rebuilt):
        """Through the wrapper with a stat record; the hook rebuilds the fields as fn_1_22E8C does."""
        record = BLOCK + memmap.MODE_STATS + self.SEAT * memmap.MODE_STAT

        def hook(cpu):
            put(cpu.mem, record, mask)
            page = BLOCK + memmap.MODE_STATS_TWO + (self.SEAT << memmap.MODE_STAT_TWO_SHIFT)
            for i, value in values.items():
                if i < memmap.MODE_STATS_FIRST:
                    put(cpu.mem, record + 4 + 4 * i, value)
                else:
                    put(cpu.mem, page + 4 * (i - memmap.MODE_STATS_FIRST), value)
            for field, value in rebuilt.items():
                if field == standin_inputs.BYTE_FIELD:
                    cpu.mem[self.M + field] = value & 0xFF
                else:
                    put(cpu.mem, self.M + field, value)
        return self.wrap(hook=hook)[1]

    def test_the_stat_fields_are_the_cores(self):
        path = os.path.join(here, "..", "..", "core", "src", "mode.h")
        with open(path) as file:
            header = file.read()
        text = re.search(r"STAT_FIELDS = \{([^}]*)\}", header).group(1)
        self.assertEqual(tuple(int(w, 16) for w in text.split(",")), standin_inputs.STAT_FIELDS)
        self.assertEqual(int(re.search(r"STATS = 6 \* PAGE \+ (0x[0-9A-F]+)", header).group(1), 16) + 0x6000,
                         memmap.MODE_STATS)

    def test_a_changed_stat_goes_over_the_rebuild_and_the_others_stay_the_games(self):
        rebuilt = {field: 0x3F000000 + i for i, field in enumerate(standin_inputs.STAT_FIELDS)}
        changed = {2: 0x40000000, 9: 0x40400000}          # max speed and body
        m = self.stats(1 << 2 | 1 << 9, changed, rebuilt)
        self.assert_fields(m, changed, rebuilt)

    def test_every_stat_reaches_its_own_field(self):
        rebuilt = {field: 1 for field in standin_inputs.STAT_FIELDS}
        values = {i: 0x41000000 + i for i in range(len(standin_inputs.STAT_FIELDS))}
        m = self.stats((1 << len(values)) - 1, values, rebuilt)
        self.assert_fields(m, values, rebuilt)

    def test_a_stat_of_the_stats_page_alone_leaves_the_rest_the_games(self):
        for i in range(memmap.MODE_STATS_FIRST, len(standin_inputs.STAT_FIELDS)):
            rebuilt = {field: 0x3F000000 + n for n, field in enumerate(standin_inputs.STAT_FIELDS)}
            changed = {i: 0x40000000 + i}
            self.assert_fields(self.stats(1 << i, changed, rebuilt), changed, rebuilt)

    def test_the_byte_stat_keeps_its_neighbours(self):
        at = self.M + standin_inputs.BYTE_FIELD
        i = standin_inputs.STAT_FIELDS.index(standin_inputs.BYTE_FIELD)
        rebuilt = {0x474: 0x11223300, standin_inputs.BYTE_FIELD: 7}
        m = self.stats(1 << i, {i: 3}, rebuilt)
        self.assertEqual(m.mem[at], 3)
        self.assertEqual([m.mem[at - 3], m.mem[at - 2], m.mem[at - 1]], [0x11, 0x22, 0x33])

    def assert_fields(self, m, values, rebuilt):
        for i, field in enumerate(standin_inputs.STAT_FIELDS):
            want = values.get(i, rebuilt[field])
            if field == standin_inputs.BYTE_FIELD:
                self.assertEqual(m.mem[self.M + field], want & 0xFF)
            else:
                self.assertEqual(get(m.mem, self.M + field), want)

    def test_no_changed_stat_changes_nothing(self):
        rebuilt = {field: 7 for field in standin_inputs.STAT_FIELDS}
        m = self.stats(0, {0: 0x41000000}, rebuilt)
        for field in standin_inputs.STAT_FIELDS:
            self.assertEqual(m.mem[self.M + field] if field == standin_inputs.BYTE_FIELD else get(m.mem, self.M + field), 7)

    def test_another_seats_record_is_not_this_seats(self):
        other = BLOCK + memmap.MODE_STATS + (self.SEAT + 1) * memmap.MODE_STAT

        def hook(cpu):
            put(cpu.mem, other, 1)
            put(cpu.mem, other + 4, 0x41000000)
            put(cpu.mem, self.M + 8, 7)
        _, m = self.wrap(hook=hook)
        self.assertEqual(get(m.mem, self.M + 8), 7)

    def test_pedals_locked_before_the_hook_no_press(self):
        seen, m = self.wrap(record=1, flags=0x2041)
        self.assertEqual((seen[0x200], seen[0x204], seen[0]), (0x3F400000, 0x3DCCCCCD, 0x41))


if __name__ == "__main__":
    unittest.main()
