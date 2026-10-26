"""The local racer's rumble and player sounds (seatport.py), run here as the bytes the image holds against a made-up
game: the helpers its sites call (the port read before each rumble request, fn_1_F7338's lookup of the port's
machine, the player index read before each player sound and the flags mask after it) and the finish's stop in
frame_hud.py `finwatch`. No game data."""
import os, sys, unittest

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, ".."))
import build, memmap
import seatport
from test_raceview import Build, Cpu as RaceviewCpu, get, put, LOCAL, TEXT
from test_finish import game, MACHINES, FINISHED
from test_race_end import BSS, MODULE

CONTROL, IMAGE = memmap.CONTROL, memmap.IMAGE
RUMBLES, SOUNDS, STOPS = (CONTROL + w for w in (seatport.RUMBLES, seatport.SOUNDS, seatport.STOPS))
RESIM = CONTROL + seatport.RESIM
RUMBLE_ON, REQUEST, OPTIONS = BSS + 0x7F028, BSS + 0x7F02C, MODULE + seatport.DATA + 0x3E8F0
OTHER = 2                                             # another seat than this game's (LOCAL)
KEEP = {r: 0x1000 + r for r in range(32)}             # every register's value before a helper runs


class Cpu(RaceviewCpu):
    """test_raceview.py's, and `nor` (the counters add 1 with `nor` and `neg`)."""

    def other(self, pc, w):
        if w >> 26 == 31 and (w >> 1) & 0x3FF == 124 and IMAGE <= pc < IMAGE + len(self.image):
            self.r[(w >> 16) & 31] = ~(self.r[(w >> 21) & 31] | self.r[(w >> 11) & 31]) & 0xFFFFFFFF
            return pc + 4
        return super().other(pc, w)


def machine(seat):
    return MACHINES + seat * 0x620


class TheSeatsPort(Build):
    def run_site(self, site, regs, memory=None, game=None):
        m = Cpu(self.image, memory or {}, game)
        for r, v in {**KEEP, **regs}.items():
            if r != 1:
                m.r[r] = v
        m.run(self.helper(site))
        return m

    def machine_regs(self, reg, seat):
        memory = {}
        put(memory, machine(seat) + 4, seat, 2)
        return memory, {reg: machine(seat)}

    def test_the_sites_are_the_games_words(self):
        self.assertEqual(len(seatport.SITES), 47)
        for offset, word, mask, _ in seatport.SITES:
            self.assertEqual(int(self.patches[TEXT + offset]["original"], 16), word)
            self.assertEqual(mask, 0xFFFFFFFF)
        # every port read is `lbz rD, 0x474(rM)` and every player index read `lbz r0, 0x475(rM)` of its helper's rM
        for offset, word, _, helper in seatport.RUMBLE_SITES:
            self.assertEqual(word & 0xFC00FFFF, 0x88000474)
            self.assertEqual(helper, f"p{word >> 16 & 31}_{word >> 21 & 31}")
        for offset, word, reg, mask in seatport.SOUND_SITES:
            self.assertEqual((word, 0 < mask - offset < 0x20), (0x88000475 | reg << 16, True))

    def test_only_the_local_racer_has_a_port(self):
        for offset, word, _, _ in seatport.RUMBLE_SITES + [s[:3] + (None,) for s in seatport.SOUND_SITES]:
            reg, dest = word >> 16 & 31, word >> 21 & 31
            for seat, port in ((LOCAL, 0), (OTHER, 0xFFFFFFFF), (0, 0xFFFFFFFF)):
                memory, regs = self.machine_regs(reg, seat)
                m = self.run_site(offset, regs, memory)
                self.assertEqual(m.r[dest], port, (hex(offset), seat))
                self.assertEqual(m.written, set())
                self.assertEqual([m.r[r] for r in range(32) if r not in (1, 12, dest)],
                                 [{**KEEP, **regs}[r] for r in range(32) if r not in (1, 12, dest)])

    def test_port_0_is_the_local_seats_machine_and_counted(self):
        site = seatport.LOOKUP_SITE[0]
        called = []
        counted = {}
        put(counted, RUMBLES, 41)
        m = self.run_site(site, {3: 0}, counted, game=lambda a, cpu: called.append(a))
        self.assertEqual((m.r[3], called, get(m.mem, RUMBLES)), (LOCAL, [], 42))
        self.assertEqual(m.written, {RUMBLES})
        # any other port (a real one outside a race of ours, or -1) is the game's own lookup's, not counted
        def lookup(address, cpu):
            called.append((address, cpu.r[3]))
            cpu.r[3] = 7
        memory = {}
        put(memory, 0x800030C8, MODULE)
        for port in (0xFFFFFFFF, 2):
            called.clear()
            m = self.run_site(site, {3: port}, memory, lookup)
            self.assertEqual((called, m.r[3], get(m.mem, RUMBLES)), ([(TEXT + 0x866B4, port)], 7, 0))

    def test_the_mask_is_a_players_and_counts_first_runs_only(self):
        site = seatport.SOUND_SITES[0][3]
        counted = {}
        put(counted, SOUNDS, 41)
        m = self.run_site(site, {3: 0x04000000, 4: 0x04000000}, counted)   # racing, driven by the stand-in's CPU
        self.assertEqual((m.r[0], get(m.mem, SOUNDS), m.written), (0x200, 42, {SOUNDS}))
        m = self.run_site(site, {3: 0x04000000, 4: 0x04000000 | FINISHED})   # finished: a CPU's, as in versus
        self.assertEqual(m.r[0], 0x04000200)
        memory = {}
        put(memory, RESIM, 1)
        m = self.run_site(site, {4: 0}, memory)                          # a step run again: the sound is dropped
        self.assertEqual((m.r[0], m.written), (0x200, set()))
        self.assertEqual([m.r[r] for r in range(2, 32) if r not in (4, 12)], [KEEP[r] for r in range(2, 32) if r not in (4, 12)])

    def test_the_player_sound_plays_for_the_local_racer_only(self):
        # the site's own test, from the +0x475 read to the sound: -1 skips; then flags & mask skips
        def plays(seat, flags):
            memory, regs = self.machine_regs(30, seat)
            index = self.run_site(0x2C030, regs, memory).r[0]
            if index == 0xFFFFFFFF:
                return False
            mask = self.run_site(0x2C048, {3: 0x04000000, 4: flags}).r[0]
            return flags & mask == 0
        self.assertEqual([plays(s, 0x04000000) for s in (0, OTHER, LOCAL)], [False, False, True])
        self.assertFalse(plays(LOCAL, 0x04000000 | FINISHED))
        self.assertFalse(plays(LOCAL, 0x200))


class TheFinishStop(Build):
    def watch(self, flags, rumble_on=1, options=1, contact=0, request=0):
        memory = game(flags=(0,) * LOCAL + (flags,), step=1000, place=tuple(range(1, LOCAL + 2)))
        memory[RUMBLE_ON], memory[OPTIONS], memory[REQUEST] = rumble_on, options, request
        put(memory, machine(LOCAL) + 0x58C, contact)
        put(memory, STOPS, 6)
        m = Cpu(self.image, memory)
        m.r[31], m.r[30] = CONTROL, BSS
        m.run(self.symbols_finwatch())
        return m

    def symbols_finwatch(self):
        from keystone import Ks, KS_ARCH_PPC, KS_MODE_PPC32, KS_MODE_BIG_ENDIAN
        import frame_hud, native
        text = frame_hud.asm()
        code = bytes(Ks(KS_ARCH_PPC, KS_MODE_PPC32 + KS_MODE_BIG_ENDIAN).asm(text)[0])
        return memmap.IMAGE + self.image.index(code) + native.label_offsets(text)["finwatch"]

    def request(self, m):
        return m.mem.get(REQUEST, 0), get(m.mem, REQUEST + 2, 2), get(m.mem, STOPS)

    def test_the_local_finish_stops_pad_0s_rumble_hard(self):
        self.assertEqual(self.request(self.watch(FINISHED, request=4)), (5, 0, 7))

    def test_under_the_games_conditions_only(self):
        for changes in ({"rumble_on": 0}, {"options": 2}, {"contact": 0x10}):
            self.assertEqual(self.request(self.watch(FINISHED, request=4, **changes)), (4, 0, 6), changes)
        # still racing, or out: no stop (versus asks for it at the finish only)
        self.assertEqual(self.request(self.watch(0, request=4)), (4, 0, 6))
        self.assertEqual(self.request(self.watch(0x800, request=4)), (4, 0, 6))


if __name__ == "__main__":
    unittest.main()
