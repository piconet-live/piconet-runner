"""The finish and the results list, per player: the image's routine that watches the local racer's finish
(frame_hud.py `finwatch`) and the helpers of the picture's sites (follow.py: the countdown's six readers, the
results list's row test), run here as the bytes the image holds, against a made-up game: two machines, the race's
flags, the control block. No game data."""
import os, struct, sys, tempfile, unittest

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, ".."))
import build, memmap
import frame_hud, native
from test_race_end import Machine, signed, BSS, MODULE, MACHINES, FINISHED, OUT, POINTER

IMAGE, CONTROL, TEXT = memmap.IMAGE, memmap.CONTROL, memmap.REL_TEXT
KIND, END, COUNT = memmap.FINISH_KIND, memmap.FINISH_END, memmap.FINISH_COUNT
SESSION, STEP, SPECTATOR, KEPT = CONTROL + 116, CONTROL + 124, CONTROL + 56, CONTROL + 276
OVER = memmap.END_OVER                                  # the race-over test's "over": read here, never written
KEPT_RECORD, KEPT_SPEED, KEPT_PLACE = CONTROL + 0x400, CONTROL + 0x5FC, CONTROL + 0x600
PHASE, RULES, GAMES_COUNT, SLOT_SPEED = BSS + 0x3C2A, BSS + 0x3C30, BSS + 0x50EC, BSS + 0x26304
RECORDS = 0x80A00000
SEAT = IMAGE + 0x14
COUNT_SITES = {0x15E10C: 3, 0x15E140: 3, 0x15E164: 3, 0x15E190: 3, 0x15C4AC: 0, 0x14A094: 0}   # site: its register
ROW_SITE, MACHINE_SITE, PLACE_SITE = 0x145C3C, 0x15C760, 0x15C750
STATE_SITES = {0x14557C: (6, 7, 4), 0x145608: (15, 3, 4)}   # site: the registers of its `lbzx dest, base, index`
LIMIT = memmap.RACE + memmap.RACE_FINISH_LIMIT
LIST_SITES = (0x14A3E4, 0x14A474)


def game(flags=(0, 0), step=1000, session=1, phase=0x29, rules=0, spectator=0, games=0xFFFF, slot=0, record=1,
         place=(1, 2), control=None):
    """Memory of a race: one machine block for each of `flags`, each with a progress record filled with `record`."""
    words = {0x800030C8: MODULE, SESSION: session, STEP: step, SPECTATOR: spectator, RULES: rules,
             POINTER: MACHINES, SLOT_SPEED: slot}
    words.update(control or {})
    memory = {}
    for i, f in enumerate(flags):
        machine = MACHINES + i * 0x620
        words.update({machine: f, machine + 0x49C: RECORDS + i * 0x200, machine + 0xD8: 0x44000000 + i})
        memory[machine + 0x482] = place[i]
        memory.update({RECORDS + i * 0x200 + k: (record + i + k) & 0xFF for k in range(0x1FC)})
    for address, value in words.items():
        memory.update({address + i: b for i, b in enumerate(value.to_bytes(4, "big"))})
    memory.update({PHASE: phase >> 8, PHASE + 1: phase & 0xFF, GAMES_COUNT: games >> 8, GAMES_COUNT + 1: games & 0xFF})
    return memory


class TheFinish(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.dir = tempfile.TemporaryDirectory()
        b = build.build(cls.dir.name)
        cls.symbols, cls.patches = b["symbols"], {int(p["address"], 16): p for p in b["patches"]}
        image = bytearray(b["image"])
        struct.pack_into(">I", image, SEAT - IMAGE, 1)          # this game's seat: 1, so that seat 0 is another's
        cls.image = bytes(image)
        from keystone import Ks, KS_ARCH_PPC, KS_MODE_PPC32, KS_MODE_BIG_ENDIAN
        text = frame_hud.asm()
        code = bytes(Ks(KS_ARCH_PPC, KS_MODE_PPC32 + KS_MODE_BIG_ENDIAN).asm(text)[0])
        assert cls.image.count(code) == 1
        cls.finwatch = IMAGE + cls.image.index(code) + native.label_offsets(text)["finwatch"]
        cls.end = IMAGE + cls.image.index(code) + len(code)

    @classmethod
    def tearDownClass(cls):
        cls.dir.cleanup()

    def watch(self, memory):
        m = Machine(self.image, memory)
        m.r[31], m.r[30] = CONTROL, BSS
        m.run(self.finwatch)
        return m

    def after(self, m, **changes):
        """The next boundary: the memory `m` left, with the words of `changes` (step, and the local machine's flags)."""
        memory = dict(m.mem)
        for address, value in ((STEP, changes.get("step")), (MACHINES + 0x620, changes.get("flags")),
                               (RULES, changes.get("rules")), (SPECTATOR, changes.get("spectator"))):
            if value is not None:
                memory.update({address + i: b for i, b in enumerate(value.to_bytes(4, "big"))})
        return self.watch(memory)

    def words(self, m):
        return m.read(KIND, 4), m.read(END, 4), m.read(COUNT, 4)

    def helper(self, site):
        """The helper a site's entry leaves for outside a plain start: the mode test, then `bne` to it."""
        p = self.patches[TEXT + site]
        entry = build.branch_target(int(p["new"], 16), TEXT + site)
        bne = struct.unpack_from(">I", self.image, entry + 12 - IMAGE)[0]
        self.assertEqual(bne >> 16, 0x4082, p)
        return entry + 12 + signed(bne & 0xFFFC)

    def test_while_the_local_machine_races_the_picture_has_the_games_countdown(self):
        # seat 0, another player's, has finished and the game counts for it: nothing of this game's starts
        for games in (0xFFFF, 480, 131, 0):
            m = self.watch(game(flags=(FINISHED, 0), games=games))
            self.assertEqual(self.words(m), (0, 0, games))
            self.assertEqual(m.written, {COUNT})

    def test_the_countdown_starts_at_the_local_machines_own_finish(self):
        m = self.watch(game(flags=(0, FINISHED), step=1000))
        self.assertEqual(self.words(m), (1, 1480, 480))
        # counted by the session's step word, as the game counts its own: one a step, to 0, and nothing else moves;
        # run out before the results it stands at 0, where the banner, with versus's threshold, draws the place
        # settled (follow.py, the place site), as long as the results wait for the other players
        for step, left in ((1001, 479), (1180, 300), (1479, 1), (1480, 0), (1481, 0), (5000, 0)):
            n = self.after(m, step=step)
            self.assertEqual(self.words(n), (1, 1480, left), step)
            self.assertEqual(n.written, {COUNT})

    def test_a_machine_that_is_out_gets_the_shorter_countdown(self):
        for flag in (0x10000000, OUT, 0x800):
            m = self.watch(game(flags=(0, flag), step=200))
            self.assertEqual(self.words(m), (2, 500, 300), flag)
        # run out, an out machine's countdown stays 0: the retired banner stays up
        self.assertEqual([self.words(self.after(m, step=s))[2] for s in (499, 500, 501, 900)], [1, 0, 0, 0])
        # broken down, it coasts over the line: the finish's own countdown, from that step
        self.assertEqual(self.words(self.after(m, step=260, flags=OUT | FINISHED)), (1, 740, 480))

    def test_a_rollback_that_takes_the_finish_back_takes_the_countdown_back(self):
        # a finish seen on predicted inputs...
        m = self.watch(game(flags=(0, FINISHED), step=1000))
        self.assertEqual(self.words(m), (1, 1480, 480))
        # ...then the hook puts the memory back to step 996, where the machine still races: the game's countdown
        back = self.after(m, step=996, flags=0)
        self.assertEqual(self.words(back), (0, 1480, 0xFFFF))
        # the steps run again with the official inputs finish it three steps later
        again = self.after(back, step=1003, flags=FINISHED)
        self.assertEqual(self.words(again), (1, 1483, 480))
        # a rollback that does not reach the finish step changes nothing: the machine is finished at its boundary
        kept = self.after(again, step=1004)
        self.assertEqual(self.words(kept), (1, 1483, 479))
        self.assertEqual(kept.written, {COUNT})

    def test_once_the_results_are_on_the_countdown_is_over(self):
        m = self.watch(game(flags=(0, FINISHED), step=1000))
        self.assertEqual(self.words(self.after(m, step=1100, rules=0x1000)), (1, 1480, 0))
        self.assertEqual(self.words(self.after(m, step=1600, rules=0x1000)), (1, 1480, 0))
        self.assertEqual(self.words(self.after(m, step=1100, rules=0x0800)), (1, 1480, 380))

    def test_a_machine_still_racing_when_the_results_come_on_reads_no_countdown(self):
        # the host's rule ended the race with the local machine racing. Seat 0, another player's, has not finished
        # either: the game's own countdown reads "not started", and the results pages take no button while the
        # countdown they read is not 0. Once the results are on it is 0 here, as for a finished racer
        for games in (0xFFFF, 300, 0):
            m = self.watch(game(flags=(0, 0), games=games, rules=0x1000))
            self.assertEqual(self.words(m), (0, 0, 0), games)
            self.assertEqual(m.written, {COUNT})
        # before the results the game's own, as ever
        self.assertEqual(self.words(self.watch(game(flags=(0, 0), games=300, rules=0x0800))), (0, 0, 300))

    def test_outside_a_sessions_race_nothing_is_local(self):
        started = {KIND: 1, END: 1480}
        for case in ({"session": 0}, {"phase": 0x28}):
            m = self.watch(game(flags=(0, FINISHED), games=77, control=started, **case))
            self.assertEqual(self.words(m), (0, 1480, 77), case)
        # a spectator: what was started runs out, and nothing new starts
        m = self.watch(game(flags=(0, 0), step=1400, spectator=1, control=started))
        self.assertEqual(self.words(m), (1, 1480, 80))
        self.assertEqual(m.written, {COUNT})
        self.assertEqual(self.words(self.watch(game(flags=(0, FINISHED), spectator=1, games=5))), (0, 0, 5))

    # --- the local racer's own results, kept for the results board

    def kept(self, m):
        return (m.read(KEPT, 4), bytes(m.mem.get(KEPT_RECORD + k, 0) for k in range(0x1FC)), m.read(KEPT_SPEED, 4),
                m.read(KEPT_PLACE, 1))

    def test_the_results_are_kept_as_they_stand_after_the_finish_step(self):
        m = self.watch(game(flags=(0, FINISHED), place=(1, 7)))
        record = bytes((2 + k) & 0xFF for k in range(0x1FC))
        self.assertEqual(self.kept(m), (1, record, 0x44000001, 7))
        # a place past the thirtieth is kept as the thirtieth, as the game keeps it
        self.assertEqual(self.kept(self.watch(game(flags=(0, FINISHED), place=(1, 31))))[3], 30)
        # the steps after it keep nothing again, whatever the record holds by then
        memory = dict(m.mem)
        memory.update({RECORDS + 0x200 + k: 0xEE for k in range(0x1FC)})
        n = self.after(Machine(self.image, memory), step=1001)
        self.assertEqual((self.kept(n), n.written), ((1, record, 0x44000001, 7), {COUNT}))
        # a machine that is out has no results of its own to keep
        out = self.watch(game(flags=(0, OUT)))
        self.assertEqual((self.kept(out)[0], out.written), (0, {KIND, END, KEPT, COUNT}))

    def test_a_machine_still_racing_when_the_race_ends_keeps_its_row_as_it_stands(self):
        # the race-over test has said "over" (on official steps only: nothing is simulated again from here): the
        # racing machine's record, max speed and place are kept as a finish keeps them, with no countdown
        over = {OVER: 1}
        m = self.watch(game(flags=(FINISHED, 0), place=(1, 23), control=over))
        record = bytes((2 + k) & 0xFF for k in range(0x1FC))
        self.assertEqual(self.kept(m), (1, record, 0x44000001, 23))
        self.assertEqual(self.words(m), (0, 0, 0xFFFF))
        self.assertEqual(m.written - {KEPT_RECORD + 4 * k for k in range(127)}, {KEPT, KEPT_SPEED, KEPT_PLACE, COUNT})
        # once: the machine races on behind the results, and what it comes to there is not its row
        memory = dict(m.mem)
        memory.update({RECORDS + 0x200 + k: 0xEE for k in range(0x1FC)})
        memory[MACHINES + 0x620 + 0x482] = 20
        n = self.after(Machine(self.image, memory), step=1001, rules=0x1000)
        self.assertEqual((self.kept(n), n.written), ((1, record, 0x44000001, 23), {COUNT}))
        # not before "over" is said, whatever else stands
        self.assertEqual(self.kept(self.watch(game(flags=(FINISHED, 0), rules=0x1000)))[0], 0)
        # not for a machine that is out, nor for a spectator, nor outside a session's race
        for case in ({"flags": (FINISHED, OUT)}, {"spectator": 1}, {"session": 0}, {"phase": 0x28}):
            self.assertEqual(self.kept(self.watch(game(**{"flags": (FINISHED, 0), "control": over, **case})))[0], 0, case)
        # the game that follows seat 0 fills the slot itself when the results begin: not kept over it
        self.assertEqual(self.kept(self.watch(game(flags=(FINISHED, 0), slot=0x44000001, control=over)))[0], 0)
        # and a finish on the very step keeps the finish's results, with its countdown
        m = self.watch(game(flags=(0, FINISHED), place=(1, 7), control=over))
        self.assertEqual((self.kept(m)[0], self.kept(m)[3], self.words(m)), (1, 7, (1, 1480, 480)))

    def test_a_finish_run_again_keeps_the_official_results(self):
        # kept on a prediction; the rollback that takes the finish back drops them...
        m = self.watch(game(flags=(0, FINISHED), step=1000, place=(1, 7)))
        back = self.after(m, step=996, flags=0)
        self.assertEqual(self.kept(back)[0], 0)
        # ...and the finish step run again, with another place and other lap times, keeps those
        memory = dict(back.mem)
        memory.update({RECORDS + 0x200 + k: 0xEE for k in range(0x1FC)})
        memory[MACHINES + 0x620 + 0x482] = 9
        again = self.after(Machine(self.image, memory), step=1003, flags=FINISHED)
        self.assertEqual(self.kept(again), (1, b"\xEE" * 0x1FC, 0x44000001, 9))

    def test_the_games_own_results_are_not_kept_twice(self):
        # the game that follows seat 0: the simulation fills the slot in the finish step itself
        m = self.watch(game(flags=(0, FINISHED), slot=0x44000001))
        self.assertEqual((self.words(m), self.kept(m)[0]), ((1, 1480, 480), 0))
        self.assertEqual(m.written, {KIND, END, KEPT, COUNT})

    def test_what_was_kept_goes_when_the_race_is_not_a_sessions(self):
        m = self.watch(game(flags=(0, FINISHED), session=0, control={KIND: 1, END: 1480, KEPT: 1}))
        self.assertEqual(self.kept(m)[0], 0)
        m = self.watch(game(flags=(0, 0), spectator=1, control={KIND: 1, END: 1480, KEPT: 1}))
        self.assertEqual(self.kept(m)[0], 1)

    def test_every_step_boundary_of_a_frame_is_watched(self):
        words = [struct.unpack_from(">I", self.image, o)[0] for o in range(0, len(self.image), 4)]
        to = lambda o: IMAGE + o * 4 + signed(words[o] & 0x03FFFFFC, 26) if words[o] >> 26 == 18 and words[o] & 3 == 1 else None
        calls = [o for o in range(len(words)) if to(o) == self.finwatch]
        self.assertEqual(len(calls), 3)
        first, extra, last = calls
        # after the network hook of the frame's first step and of every extra step: a rollback is made in the hook
        self.assertIsNotNone(to(first - 1))
        self.assertEqual(to(first - 1), to(extra - 1))
        hook = (to(first - 1) - IMAGE) // 4
        self.assertEqual(words[hook + 2:hook + 4], [0x3D800000 | memmap.hi(memmap.HOST_NET),      # lis, ori: the
                                                    0x618C0000 | memmap.lo(memmap.HOST_NET)])     # host call
        # and after the frame's last step, before the HUD pass, which writes what was kept
        hudstate = self.finwatch - native.label_offsets(frame_hud.asm())["finwatch"]
        self.assertEqual(to(last + 1), hudstate)

    def test_the_six_picture_readers_take_the_local_countdown(self):
        for site, register in COUNT_SITES.items():
            m = Machine(self.image, game(control={COUNT: 0x1234}))
            m.r[3] = m.r[0] = 0x5555
            m.run(self.helper(site))
            self.assertEqual(m.r[register], 0x1234, hex(site))
            self.assertEqual(m.written, set())
        self.assertEqual(len({self.helper(site) for site in COUNT_SITES}), 2)

    def test_the_results_list_begins_at_the_local_racers_place(self):
        # the results pages asked fn_1_8623C for the racer whose place the list begins at: racer 0, always
        for site in LIST_SITES:
            m = Machine(self.image, game())
            m.r[3] = 0
            self.assertEqual((m.run(self.helper(site)), m.written), (1, set()), hex(site))

    def test_the_banner_compares_the_local_racers_times(self):
        # fn_1_15C6C0 asked for the machine whose number is the port's (0): the local seat's instead, and, as the
        # game's own routine answers, none when there are none
        m = Machine(self.image, game())
        m.r[3] = 0
        self.assertEqual((m.run(self.helper(MACHINE_SITE)), m.written), (MACHINES + 0x620, set()))
        m = Machine(self.image, game(control={POINTER: 0}))
        self.assertEqual(m.run(self.helper(MACHINE_SITE)), 0)

    def test_the_banner_shows_the_place_from_versuss_threshold(self):
        # fn_1_15C6C0 takes r28 = 0 with rule flag 0x04000000 (every race of ours), 300 without (versus): FINISH!
        # from 480 to r28, the place below it. The site gives versus's 300 and writes nothing
        self.assertEqual(int(self.patches[TEXT + PLACE_SITE]["original"], 16), 0x3B800000)    # li r28, 0
        m = Machine(self.image, game())
        m.r[28] = 0
        m.run(self.helper(PLACE_SITE))
        self.assertEqual((m.r[28], m.written), (300, set()))

    def test_the_results_list_says_disqualified_for_a_machine_the_rule_ended_the_race_for(self):
        # the list copies each racer's state into the row it draws: the game's 0xFF reads RUNNING, 0xFE DISQUALIFIED
        # (fn_1_148950; the game writes 0xFE itself for a machine that went out). A machine still racing when the
        # host's rule ended the race keeps 0xFF in the game's record: its row is given 0xFE
        records, lap = 0x80C00000, 3
        for site, (dest, base, index) in STATE_SITES.items():
            for state, limit, over, row in ((0xFF, 5400, 1, 0xFE),      # the rule ended the race: DISQUALIFIED
                                            (0xFF, 0, 1, 0xFF),         # a race without the rule: the stock list
                                            (0xFF, 5400, 0, 0xFF),      # not over (the list is not drawn then)
                                            (0xFE, 5400, 1, 0xFE),      # went out: the game's own word, kept
                                            (0xFE, 0, 1, 0xFE),
                                            (7, 5400, 1, 7), (0, 5400, 1, 0), (29, 0, 0, 29)):   # a place: its time
                m = Machine(self.image, {**game(control={OVER: over, LIMIT: limit}), records + lap: state})
                m.r[base], m.r[index], m.r[dest] = records, lap, 0x5555
                m.run(self.helper(site))
                self.assertEqual((m.r[dest], m.written), (row, set()), (hex(site), state, limit, over))
                # nothing but the row's register and r12 is touched: not the record, not the registers it read
                self.assertEqual((m.r[base], m.r[index], m.mem[records + lap]), (records, lap, state))
        self.assertEqual(len({self.helper(site) for site in STATE_SITES}), 2)

    def test_only_the_picture_names_the_finish_words(self):
        names = {KIND - CONTROL: "kind", END - CONTROL: "end", COUNT - CONTROL: "count"}
        named = [(IMAGE + o, w >> 26, names[w & 0xFFFF]) for o in range(0, len(self.image), 4)
                 for w in [struct.unpack_from(">I", self.image, o)[0]]
                 if (w >> 26 == 14 or 32 <= w >> 26 <= 47) and w & 0xFFFF in names]
        helpers = {self.helper(site) + 4 for site in COUNT_SITES}
        outside = [n for n in named if not self.finwatch <= n[0] < self.end and n[0] not in helpers]
        self.assertEqual(outside, [])
        # of the race's end the watch reads one word, "over" (twice: a racer's row, an out racer's), and writes none
        ends = {a - CONTROL: n for a, n in ((memmap.END_OFFICIAL, "official"), (memmap.END_ASKED, "asked"), (OVER, "over"))}
        self.assertEqual([(w >> 26, ends[w & 0xFFFF]) for o in range(self.finwatch - IMAGE, self.end - IMAGE, 4)
                          for w in [struct.unpack_from(">I", self.image, o)[0]]
                          if (w >> 26 == 14 or 32 <= w >> 26 <= 47) and w & 0xFFFF in ends], [(32, "over")] * 2)
        self.assertEqual(sorted(n[1:] for n in named if n[0] in helpers), [(32, "count"), (32, "count")])
        # and the game's own countdown is only read: the simulation counts it
        stores = [o for o in range(0, len(self.image), 4) for w in [struct.unpack_from(">I", self.image, o)[0]]
                  if w >> 26 in (36, 37, 38, 39, 44, 45) and w & 0xFFFF == 0x50EC]
        self.assertEqual(stores, [])

    def test_the_results_list_lights_the_local_seats_row(self):
        row = 0x80B00000
        for racer in range(30):
            memory = {row + 0x4C + i: b for i, b in enumerate(racer.to_bytes(4, "big"))}
            m = Machine(self.image, memory)
            m.r[30], m.r[0], m.r[3] = row, racer, BSS + 0x3C30
            m.run(self.helper(ROW_SITE))
            self.assertEqual((m.r[0], m.written), (racer, set()))
            # the game's own test on what the helper gave (fn_1_145B58, 0x145C40 on): signed racer < number
            x = m.r[4] ^ racer
            lit = (((signed(x, 32) >> 1) - (x & m.r[4])) & 0xFFFFFFFF) >> 31
            self.assertEqual(lit, 1 if racer == 1 else 0, racer)


if __name__ == "__main__":
    unittest.main()
