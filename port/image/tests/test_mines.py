"""Mines that come back (sites_mines.py), run here as the bytes the image holds against a made-up course: the per-step
pass's site, with the game's instructions after it as a stand-in that does what the disassembly says of them (a
hit becomes spent), step after step; and the countdowns' block taken from a made-up heap. No game data."""
import os, struct, sys, unittest

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, ".."))
from test_raceview import Build, Cpu, get, put, STACK, TEXT
import build
import memmap
import sites_mines

MODE, RULE, MINES = memmap.MODE, memmap.RACE + memmap.RACE_MINE_REGEN, memmap.MINES
HIT, SPENT = sites_mines.HIT, sites_mines.SPENT
HEADER, OBJECTS, COLLIDER, BLOCK = 0x80D00000, 0x80D10000, 0x80D20000, 0x80D30000
OWNER = 0x80D28000                # a placed object's +8: its collider at +0xC
START = 20                       # the walk's first object, as on Serial Gaps
ALLOC, FREE = TEXT + 0x45D0, TEXT + 0x46B4
BSS = memmap.REL_BSS


class MinesComeBack(Build):
    def course(self, rule=12, mode=1, block=BLOCK, collider=0x4000):
        memory = {}
        put(memory, MODE, mode)
        put(memory, RULE, rule)
        put(memory, MINES + 4, block)
        put(memory, sites_mines.HEADER, HEADER)
        put(memory, HEADER + 0x50, START)
        put(memory, COLLIDER, collider)
        return memory

    def mine(self, k):
        return OBJECTS + (START + k) * 0x40

    def step(self, memory, k=0, collider=COLLIDER, index=None):
        """One pass of fn_1_9AF80 over object START + k: the site, then the game's own conversion of a hit (0x9B0E0
        to 0x9B0F8) as the stand-in where the routine leaves. Gives the memory after it."""
        obj = self.mine(k)
        left = []

        def game(address, cpu):
            left.append(address)
            if address == TEXT + sites_mines.BACK and cpu.r[3] & HIT:
                put(cpu.mem, obj, (cpu.r[3] & ~HIT & 0xFFFFFFFF) | SPENT)
        m = Cpu(self.image, memory, game)
        m.r[28], m.r[26], m.r[5], m.r[4] = obj, START + k if index is None else index, collider, 0x4444
        m.run(self.symbols["mine_regen"])
        self.assertEqual((left, m.r[4], m.r[5]), ([TEXT + sites_mines.BACK], 0x4444, collider))
        return m.mem

    def hit(self, memory, k=0):
        put(memory, self.mine(k), get(memory, self.mine(k)) | HIT)

    def test_the_site_is_the_passes_turn_from_hit_to_spent(self):
        p = self.patches[TEXT + sites_mines.SITE]
        self.assertEqual(int(p["new"], 16), build.branch(TEXT + sites_mines.SITE, self.symbols["mine_regen"]))
        self.assertEqual(int(p["original"], 16), sites_mines.KNOWN)          # lwz r3, 0(r28)

    def test_a_mine_comes_back_n_steps_after_it_went_off(self):
        for n in (12, 3, 2, 300):
            memory = self.course(n)
            put(memory, self.mine(0), 0x0002E013)
            self.hit(memory)
            memory = self.step(memory)                                   # step s: it goes off
            self.assertEqual(get(memory, self.mine(0)), 0x0002E013 | SPENT, n)
            self.assertEqual(get(memory, BLOCK + memmap.MINE_COUNTS, 2), n - 1)
            for s in range(1, n - 1):                                    # gone in steps s + 1 to s + N - 1
                memory = self.step(memory)
                self.assertEqual(get(memory, self.mine(0)), 0x0002E013 | SPENT, (n, s))
            memory = self.step(memory)                                   # the pass of s + N - 1: armed for s + N
            self.assertEqual(get(memory, self.mine(0)), 0x0002E013, n)
            self.assertEqual([get(memory, BLOCK + o) for o in (0, 4, 8)], [n, 1, 1], n)
            memory = self.step(memory)                                   # armed, it stays so
            self.assertEqual(get(memory, self.mine(0)), 0x0002E013, n)

    def test_a_rule_of_one_step_never_leaves_a_mine_spent(self):
        memory = self.course(1)
        put(memory, self.mine(0), 0x000AE00F | HIT)
        memory = self.step(memory)
        self.assertEqual(get(memory, self.mine(0)), 0x000AE00F)
        self.assertEqual([get(memory, BLOCK + o) for o in (4, 8)], [1, 1])

    def test_each_mine_counts_alone(self):
        memory = self.course(5)
        put(memory, self.mine(3), HIT)
        memory = self.step(memory, 3)
        for _ in range(2):
            memory = self.step(memory, 3)
        put(memory, self.mine(7), HIT)
        memory = self.step(memory, 7)
        self.assertEqual([get(memory, BLOCK + memmap.MINE_COUNTS + 2 * k, 2) for k in (3, 7)], [2, 4])
        memory = self.step(memory, 3)
        memory = self.step(memory, 3)
        self.assertEqual((get(memory, self.mine(3)), get(memory, self.mine(7))), (0, SPENT))

    def test_without_the_rule_the_game_does_as_it_did(self):
        # the rule at 0, a plain start, no block, an object that is no mine, a mine past the table: the game's own
        # conversion, and nothing else written
        cases = [dict(rule=0), dict(mode=0), dict(block=0), dict(collider=0x8000)]
        for case in cases:
            memory = self.course(**case)
            put(memory, self.mine(0), 0x0002E013 | HIT)
            after = self.step(memory)
            self.assertEqual(get(after, self.mine(0)), 0x0002E013 | SPENT, case)
            changed = {a for a in after if after[a] != memory.get(a, 0)}
            self.assertEqual(changed, {self.mine(0)}, case)
            for _ in range(20):
                after = self.step(after)
            self.assertEqual(get(after, self.mine(0)), 0x0002E013 | SPENT, case)
        memory = self.course()
        put(memory, self.mine(0), HIT)
        after = self.step(memory, index=START + memmap.MINE_TABLE)
        self.assertEqual({a for a in after if after[a] != memory.get(a, 0)}, {self.mine(0)})
        memory = self.course()
        put(memory, self.mine(0), HIT)
        after = self.step(memory, collider=0)
        self.assertEqual({a for a in after if after[a] != memory.get(a, 0)}, {self.mine(0)})

    # --- the block, on the black frame before the start gate
    def heap(self, rule=12, largest=0x60000):
        memory = self.course(rule, block=0)
        put(memory, BSS + 0x960, 2, 2)
        put(memory, BSS + 0x3C2A, 0x27, 2)
        put(memory, BSS + 0x26C28, 1)
        put(memory, build.MAIN_HEAP, 1)
        put(memory, build.HEAP_TABLE, 0x80E00000)
        put(memory, 0x80E00000 + 12 + 4, 0x80E10000)                     # heap 1's free list: two cells
        put(memory, 0x80E10000 + 4, 0x80E20000)
        put(memory, 0x80E10000 + 8, 0x1000)
        put(memory, 0x80E20000 + 8, largest)
        for at in range(BLOCK, BLOCK + sites_mines.SIZE):
            memory[at] = 0xFF
        return memory

    def preload(self, memory):
        calls = []

        def game(address, cpu):
            calls.append((address, cpu.r[3], cpu.r[4]))
            cpu.r[3] = {1: 0x80F00000, 2: BLOCK}.get(len(calls), 0) if address == ALLOC else 0
        m = Cpu(self.image, memory, game)
        m.r[24], m.r[31] = 0x2424, 0x3131
        m.run(self.symbols["mines_preload"])
        self.assertEqual((m.r[24], m.r[31], m.r[1]), (0x2424, 0x3131, 0x80C00000))
        return calls, m.mem

    def test_the_block_is_taken_at_the_top_of_the_largest_free_block(self):
        calls, memory = self.preload(self.heap())
        size = sites_mines.SIZE
        self.assertEqual(calls, [(ALLOC, 1, 0x60000 - size - 0x60), (ALLOC, 1, size), (FREE, 1, 0x80F00000)])
        self.assertEqual((get(memory, MINES), get(memory, MINES + 4)), (1, BLOCK))
        self.assertEqual({memory[a] for a in range(BLOCK, BLOCK + size)}, {0})
        self.assertEqual(self.preload(memory)[0], [])                    # once

    def test_no_block_without_the_rule_or_the_room(self):
        for memory in (self.heap(rule=0), self.heap(largest=build.RESULTS_RESERVE)):
            calls, memory = self.preload(memory)
            self.assertEqual((calls, get(memory, MINES), get(memory, MINES + 4)), ([], 3, 0))
        memory = self.heap()
        put(memory, BSS + 0x3C2A, 0x28, 2)                               # not the gate's frame: not yet
        calls, memory = self.preload(memory)
        self.assertEqual((calls, get(memory, MINES)), ([], 0))

    # --- the fade-in: the course objects' draw reads the flags and the fade through the routines, which write nothing
    OWN = 0.0625                                         # a record's own fade (the stock read's): no c / W of a test

    def draw(self, memory, record):
        """The draw's two reads of `record` (r15): 0x9C014's routine, then 0x9C03C's with the registers it left (the
        draw's six instructions between write none of r11 and the float registers; f1 is the draw's again at
        0x9C038). Gives (flags, fade)."""
        memory = dict(memory)
        put(memory, record + 0x2C, struct.unpack(">I", struct.pack(">f", self.OWN))[0])
        left = []
        m = Cpu(self.image, memory, lambda address, cpu: left.append(address))
        m.r[15], m.r[4], m.r[5] = record, 0x4444, 0x5555
        m.f = [7.0] * 32
        m.run(self.symbols["mine_shown"])
        flags = m.r[0]
        m.r[0] = m.r[3] = 0x3333                                          # what the draw writes between
        m.run(self.symbols["mine_fade"])
        self.assertEqual(left, [TEXT + sites_mines.SHOWN_SITE + 4, TEXT + sites_mines.FADE_SITE + 4])
        written = {a for a in m.mem if m.mem[a] != memory.get(a, 0) and not STACK - 64 <= a < STACK}
        self.assertEqual(written, set())
        kept = (m.r[1], m.r[4], m.r[5], m.r[15], m.f[3], m.f[31])
        self.assertEqual(kept, (STACK, 0x4444, 0x5555, record, 7.0, 7.0))
        return flags, m.f[0]

    def drawn(self, memory, k=0):
        """What the draw is given of mine k: (its flags have "spent", the fade)."""
        flags, fade = self.draw(memory, self.mine(k))
        return bool(flags & SPENT), fade

    def placed(self, memory, *ks):
        """The course's objects at [header +0x54], and mines ks with their collider ([[record +8] +0xC])."""
        put(memory, HEADER + 0x54, OBJECTS)
        put(memory, OWNER + 0xC, COLLIDER)
        for k in ks:
            put(memory, self.mine(k) + 8, OWNER)
        return memory

    def test_the_draw_sites_are_the_flags_and_the_fade(self):
        for site, name, known in ((sites_mines.SHOWN_SITE, "mine_shown", 0x800F0000),
                                  (sites_mines.FADE_SITE, "mine_fade", 0xC00F002C)):
            p = self.patches[TEXT + site]
            self.assertEqual(int(p["new"], 16), build.branch(TEXT + site, self.symbols[name]))
            self.assertEqual(int(p["original"], 16), known)

    def test_a_mine_fades_in_over_its_countdowns_last_part(self):
        for n in (12, 15, 16, 300, 2):
            memory = self.placed(self.course(n), 0)
            put(memory, self.mine(0), 0x0002E013)
            self.assertEqual(self.drawn(memory), (False, self.OWN), n)   # armed: the game's own
            self.hit(memory)
            seen = []
            while not seen or get(memory, self.mine(0)) & SPENT:             # to the step it is armed again
                memory = self.step(memory)
                seen.append(self.drawn(memory))
            w = min(n, sites_mines.FADE_STEPS)
            spent = [(True, self.OWN)] * (n - w)                         # the spent model, the record's fade
            fading = [(False, single(c / w)) for c in range(w - 1, 0, -1)]
            self.assertEqual(seen, spent + fading + [(False, self.OWN)], n)   # then armed, fully shown
            self.assertEqual(get(memory, self.mine(0)), 0x0002E013, n)

    def test_the_draw_is_the_games_own_for_every_other_record(self):
        last = sites_mines.TABLE - 1
        memory = self.placed(self.course(), 0, last)
        for k in (0, last):
            put(memory, self.mine(k), SPENT)
            put(memory, BLOCK + memmap.MINE_COUNTS + 2 * k, 3, 2)
        self.assertEqual([self.drawn(memory, k) for k in (0, last)], [(False, 0.25)] * 2)
        for case in [dict(rule=0), dict(mode=0), dict(block=0), dict(collider=0x8000)]:
            other = dict(memory)
            other.update(self.course(**case))
            self.assertEqual(self.drawn(other), (True, self.OWN), case)
        for record in (self.mine(0) + 4, self.mine(-1), self.mine(sites_mines.TABLE)):   # no mine of the table
            other = dict(memory)
            put(other, record, SPENT)
            put(other, record + 8, OWNER)
            self.assertEqual(self.draw(other, record), (SPENT, self.OWN), hex(record))
        for at, value in ((OWNER + 0xC, 0), (BLOCK + memmap.MINE_COUNTS, 0)):
            other = dict(memory)                                         # no collider; spent with no countdown
            put(other, at, value, 2 if at == BLOCK + memmap.MINE_COUNTS else 4)
            self.assertEqual(self.drawn(other), (True, self.OWN), hex(at))


def single(x):
    return struct.unpack(">f", struct.pack(">f", x))[0]

if __name__ == "__main__":
    unittest.main()
