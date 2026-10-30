"""The game mode's race-side sites (sites_mode.py), run here as the bytes the image holds against made-up machines:
the pair gate (a pair the mode keeps apart is not answered and keeps the game's immunity bits; an answered pair goes
into the touching rows), the timer (the game's countdown skipped under a clock limit) and the explosion that keeps
the motion. The game's own code at the calls is a stand-in that does what the disassembly says of it. No game data."""
import os, struct, sys, unittest

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, ".."))
from test_raceview import Build, Cpu, get, put, TEXT
from test_race_end import signed
import memmap
import sites_mode

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


if __name__ == "__main__":
    unittest.main()
