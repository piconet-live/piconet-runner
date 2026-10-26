"""The seats' custom machines (sites_custom.py), run here as the bytes the image holds against a made-up race block:
each site reads the seat's own entry, a seat without one (and a plain start) does what the game did, and the
game's routines a site calls are stand-ins that note their arguments. No game data."""
import os, sys, unittest

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, ".."))
from test_raceview import Build, Cpu, get, put, TEXT
import memmap
import sites_custom

MODE = memmap.MODE
TABLE = memmap.RACE + memmap.RACE_CUSTOM
SEAT_PARTS, SUM, STATS, NAME = TEXT + 0x14F38C, TEXT + 0x14F6F8, TEXT + 0x82EDC, TEXT + 0x833DC
RACER, ENTRY, RECORD, STACK = 0x80C10000, 0x80C20000, 0x80C30000, 0x80C00000
CUSTOM = 7                      # the made-up race's custom seat
PARTS = (15, 43, 58)
PILOT = 6
COLOURS = (0xFF000001, 0, 0x0000FF03)


class TheSeatsCustomMachines(Build):
    def race(self, mode=1):
        memory = {}
        put(memory, MODE, mode)
        e = TABLE + CUSTOM * memmap.CUSTOM_ENTRY
        put(memory, e, 0xC0000000)
        for i, (part, colour) in enumerate(zip(PARTS, COLOURS)):
            put(memory, e + 4 + 8 * i, part, 1)
            put(memory, e + 8 + 8 * i, colour)
        put(memory, e + 28, PILOT, 1)
        return memory

    def run_site(self, routine, memory, **registers):
        """The routine entered by a plain branch, to where it leaves for the game: the calls it made on the way
        (address and r3 to r7 at the call) and where it left."""
        calls, left = [], []

        def game(address, cpu):
            if address in (SEAT_PARTS, SUM, STATS):
                calls.append((address, tuple(cpu.r[3:8])))
            else:
                left.append(address)
        m = Cpu(self.image, memory, game)
        for name, value in registers.items():
            m.r[int(name[1:])] = value
        m.run(self.symbols[routine])
        return calls, left, m

    def entry(self, seat=CUSTOM):
        return TABLE + seat * memmap.CUSTOM_ENTRY

    def test_the_table_is_the_race_blocks_tail(self):
        self.assertEqual((memmap.RACE_CUSTOM, memmap.RACE_SIZE, sites_custom.RECORD), (320, 1280, 0x81A0))
        # a made-up record's part entries are the entry's: +0x81A4 the body's id, +0x81A8 its colour
        self.assertEqual(self.entry() - sites_custom.RECORD + 0x81A4, self.entry() + 4)

    def test_the_seat_entry_takes_the_seats_parts(self):
        for seat, mode, called in ((CUSTOM, 1, True), (CUSTOM, 2, True), (CUSTOM, 0, False), (3, 1, False), (30, 1, False)):
            memory = self.race(mode)
            put(memory, ENTRY + 2, memmap.CUSTOM_ID, 2)
            put(memory, RECORD + 8, 0x5A, 1)
            calls, left, m = self.run_site("custom_seat", memory, r21=seat, r29=ENTRY, r24=RECORD, r4=0x44, r10=0x1010)
            self.assertEqual(left, [TEXT + 0x319F0], (seat, mode))
            self.assertEqual(m.r[0], 0x5A)                                    # the instruction it replaced
            expected = [(SEAT_PARTS, (ENTRY, self.entry() - 0x81A0))] if called else []
            self.assertEqual([(a, r[:2]) for a, r in calls], expected, (seat, mode))
            self.assertEqual((m.r[4], m.r[10], m.r[1]), (0x44, 0x1010, STACK))   # kept over the call
        # a stock machine's seat is the game's own, entry or not
        memory = self.race()
        put(memory, ENTRY + 2, 6, 2)
        calls, left, _ = self.run_site("custom_seat", memory, r21=CUSTOM, r29=ENTRY, r24=RECORD)
        self.assertEqual((calls, left), ([], [TEXT + 0x319F0]))

    def test_the_results_record_takes_the_parts_and_the_pilot(self):
        for seat, mode, written in ((CUSTOM, 1, True), (3, 1, False), (CUSTOM, 0, False)):
            memory = self.race(mode)
            calls, left, m = self.run_site("custom_result", memory, r22=seat, r20=RECORD, r25=0x1000)
            self.assertEqual((left, m.r[25]), ([TEXT + 0x31C00], 0x10A0))
            got = [get(m.mem, RECORD + o, 1) for o in (0x7C, 0x7D, 0x7E, 0x80)]
            self.assertEqual(got, [*PARTS, PILOT] if written else [0, 0, 0, 0], (seat, mode))

    def test_the_stats_are_the_seats_sum(self):
        # creation (reached for an id of 0x29 or more): the seat's sum into sp + 0x2C, then fn_1_2277C's call
        calls, left, _ = self.run_site("custom_create", self.race(), r24=CUSTOM, r22=0)
        self.assertEqual((calls, left), ([(SUM, (*PARTS, STACK + 0x2C, 0))], [TEXT + 0x83398]))
        for seat, mode in ((3, 1), (CUSTOM, 0)):
            calls, left, m = self.run_site("custom_create", self.race(mode), r24=seat, r22=0xFFFF)
            self.assertEqual((calls, left, m.r[0]), ([], [TEXT + 0x83360], 0xFFFFFFFF))
        # every step: the seat's sum into sp + 8 in place of fn_1_82EDC; a stock seat gets fn_1_82EDC as it was called
        for mode in (1, 2):
            memory = self.race(mode)
            put(memory, RACER + 4, CUSTOM, 2)
            calls, left, _ = self.run_site("custom_step", memory, r27=RACER, r3=0xFFFFFFFF, r4=0x32, r5=STACK + 8, r7=0)
            self.assertEqual((calls[0][0], calls[0][1][:4], left), (SUM, (*PARTS, STACK + 8), [TEXT + 0x33650]))
        memory = self.race()
        put(memory, RACER + 4, 3, 2)
        calls, left, _ = self.run_site("custom_step", memory, r27=RACER, r3=0xFFFFFFFF, r4=6, r5=STACK + 8, r7=0)
        self.assertEqual((calls, left), ([(STATS, (0xFFFFFFFF, 6, STACK + 8, 0, 0))], [TEXT + 0x33650]))

    def test_the_name_is_the_seats_parts(self):
        calls, left, m = self.run_site("custom_name", self.race(), r24=CUSTOM)
        self.assertEqual((left, tuple(m.r[3:8])), ([NAME], (*PARTS, STACK + 0xC, 0)))
        calls, left, m = self.run_site("custom_name", self.race(), r24=29)
        self.assertEqual((left, m.r[3]), ([TEXT + 0x833B4], 0x10000))

    def test_the_paint_and_the_results_page_read_the_seats_entry(self):
        record = self.entry() - 0x81A0
        memory = self.race()
        put(memory, RACER + 4, CUSTOM, 2)
        put(memory, RACER + 6, memmap.CUSTOM_ID, 2)
        _, left, m = self.run_site("custom_paint", memory, r29=RACER, r28=5)
        self.assertEqual((left, m.r[3], m.r[28]), ([TEXT + 0x84E00], record, 0))
        _, left, m = self.run_site("custom_record", memory, r3=RACER)
        self.assertEqual((left, m.r[3]), ([TEXT + 0x84780], record))
        put(memory, RACER + 4, 3, 2)                                          # a seat without an entry
        _, left, m = self.run_site("custom_paint", memory, r29=RACER)
        self.assertEqual((left, m.r[3]), ([TEXT + 0x84DC0], memmap.CUSTOM_ID))
        _, left, m = self.run_site("custom_record", memory, r3=RACER)
        self.assertEqual((left, m.r[3]), ([TEXT + 0x84744], memmap.CUSTOM_ID))

    def test_a_parts_colour_is_the_entrys(self):
        record = self.entry() - 0x81A0
        # the colour of part i (r30 = record + 8 i): the entry's; 0, the part's own (the game's defaults)
        for part, colour in enumerate(COLOURS):
            _, left, m = self.run_site("custom_colour", self.race(), r29=record, r30=record + 8 * part)
            self.assertEqual(left, [TEXT + (0x151D08 if colour else 0x151CF0)], part)
            self.assertEqual(m.r[3], colour)
        # a game record (the four at .bss 0x5480): the game's own test of it
        _, left, m = self.run_site("custom_colour", self.race(), r29=0x80381CC0, r30=0x80381CC0)
        self.assertEqual(left, [TEXT + 0x151CD8])
        _, left, _ = self.run_site("custom_colour", self.race(0), r29=record, r30=record)
        self.assertEqual(left, [TEXT + 0x151CD8])

    def test_the_sites_are_patched(self):
        for offset in (0x319EC, 0x31BFC, 0x8335C, 0x833B0, 0x3364C, 0x84DBC, 0x151CD4, 0x84740):
            self.assertIn(TEXT + offset, self.patches, hex(offset))


if __name__ == "__main__":
    unittest.main()
