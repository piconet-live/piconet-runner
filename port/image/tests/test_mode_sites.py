"""The game mode's race-side sites (sites_mode.py), run here as the bytes the image holds against made-up machines:
the pair gate (a pair the mode keeps apart is not answered and keeps the game's immunity bits; an answered pair goes
into the touching rows), the timer (the game's countdown skipped under a clock limit) and the explosion that keeps
the motion. The game's own code at the calls is a stand-in that does what the disassembly says of it. No game data."""
import os, struct, sys, unittest

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, ".."))
from test_raceview import Build, Cpu, get, put, IMAGE, TEXT, native
from test_race_end import signed
import memmap
import sites_mode
import standin_inputs

MODE, MODES = memmap.MODE, memmap.MODES
BLOCK = 0x80D00000
MI, MJ = 0x80D10000, 0x80D20000           # the pair's machines
I, J = 3, 7                               # their seats (r28, r27)


class ModeCpu(Cpu):
    """rlwnm. (the pair gate's bit test) beside what Cpu reads."""

    def other(self, pc, w):
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

    def test_pedals_locked_before_the_hook_no_press(self):
        seen, m = self.wrap(record=1, flags=0x2041)
        self.assertEqual((seen[0x200], seen[0x204], seen[0]), (0x3F400000, 0x3DCCCCCD, 0x41))


if __name__ == "__main__":
    unittest.main()
