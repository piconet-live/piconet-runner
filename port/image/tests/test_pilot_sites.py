"""The seats' pilots in the race's pictures (sites_pilot.py), run here as the bytes the image holds against a made-up
race block and car: each site gives the game's pilot lookup the seat's own pilot for a custom seat, and the id as it
was for a seat without an entry, a stock machine and a plain start. The game's routines a site calls are stand-ins
that note their arguments. No game data."""
import os, sys, unittest

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, ".."))
from test_raceview import Build, Cpu, put, TEXT
import memmap
import sites_pilot

TABLE = memmap.RACE + memmap.RACE_CUSTOM
PICTURE, MODEL, PILOT_OF = TEXT + 0x12C930, TEXT + 0x12CCB0, TEXT + 0x12C7B8
CAR, RACER = 0x80C10000, 0x80C20000
CUSTOM, PILOT = 7, 0x1D           # the made-up race's custom seat, and its pilot: stock machine 29's
ID = memmap.CUSTOM_ID


class TheSeatsPilots(Build):
    def race(self, mode=1, seat=CUSTOM, machine=ID):
        """A race (mode 1), a replay (2) or a plain start (0) with a custom entry for seat CUSTOM, and the car CAR of
        `seat` racing `machine`."""
        memory = {}
        put(memory, memmap.MODE, mode)
        e = TABLE + CUSTOM * memmap.CUSTOM_ENTRY
        put(memory, e, 0xC0000000)
        put(memory, e + 28, PILOT, 1)
        put(memory, CAR + 0x328, machine, 1)
        put(memory, CAR + 0x32C, RACER)
        put(memory, RACER + 4, seat, 2)
        return memory

    def run_site(self, routine, memory, **registers):
        """The site's routine entered by a plain branch, to where it leaves for the game: the calls it made on the
        way (address and r3, r4 at the call), where it left, and the machine."""
        calls, left = [], []

        def game(address, cpu):
            if address in (PICTURE, MODEL, PILOT_OF):
                calls.append((address, cpu.r[3], cpu.r[4]))
            else:
                left.append(address)
        m = Cpu(self.image, memory, game)
        for name, value in registers.items():
            m.r[int(name[1:])] = value
        m.run(self.symbols[routine])
        return calls, left, m

    def cases(self):
        """(memory, the pilot the lookup must be given): the custom seat in a race and a replay; a seat without an
        entry, a stock machine on the custom seat's entry, and a plain start keep the id."""
        return [(self.race(1), PILOT), (self.race(2), PILOT), (self.race(1, seat=3), ID), (self.race(1, seat=30), ID),
                (self.race(1, machine=6), 6), (self.race(0), ID)]

    def test_the_cars_id_load_gives_the_seats_pilot(self):
        for offset in sites_pilot.CARS:
            for memory, pilot in self.cases():
                _, left, m = self.run_site(f"pilot_car_{offset:x}", memory, r3=CAR)
                self.assertEqual((left, m.r[3]), ([TEXT + offset + 4], pilot), hex(offset))
        for memory, pilot in self.cases():
            machine = memory[CAR + 0x328]
            _, left, m = self.run_site("pilot_name", memory, r28=machine, r29=CAR, r3=0)
            self.assertEqual((left, m.r[3], m.r[28]), ([TEXT + 0x142788], pilot, machine))

    def test_the_pilots_models_take_the_seats_pilot(self):
        for memory, pilot in self.cases():
            machine = memory[CAR + 0x328]
            calls, left, m = self.run_site("pilot_model_a7320", memory, r3=machine, r28=CAR)
            self.assertEqual((calls, left), ([(PICTURE, pilot, 0)], [TEXT + 0xA7324]))
            calls, left, m = self.run_site("pilot_model_a7338", memory, r3=machine, r4=2, r28=CAR)
            self.assertEqual((calls, left, m.r[4]), ([(MODEL, pilot, 2)], [TEXT + 0xA733C], 2))
            for offset, car in ((0xA74B4, 23), (0xA7614, 24)):
                _, left, m = self.run_site(f"pilot_models_{offset:x}", memory, r27=0xFFFFFF00 | machine, **{f"r{car}": CAR})
                self.assertEqual((left, m.r[27]), ([TEXT + offset + 4], pilot), hex(offset))

    def test_the_face_board_takes_the_seats_pilot(self):
        for offset, register in sites_pilot.FACES:
            for seat, mode, machine, pilot in ((CUSTOM, 1, ID, PILOT), (CUSTOM, 2, ID, PILOT), (3, 1, ID, ID),
                                               (CUSTOM, 1, 6, 6), (CUSTOM, 0, ID, ID)):
                calls, left, m = self.run_site(f"pilot_face_{offset:x}", self.race(mode), r3=machine, **{f"r{register}": seat})
                self.assertEqual((calls, left), ([(PILOT_OF, pilot, 0)], [TEXT + offset + 4]), (hex(offset), seat, mode))
                self.assertEqual(m.r[register], seat)

    def test_the_sites_are_patched(self):
        sites = [*sites_pilot.CARS, 0x142784, 0xA7320, 0xA7338, 0xA74B4, 0xA7614, *(o for o, _ in sites_pilot.FACES)]
        self.assertEqual(len(sites), 22)
        for offset in sites:
            self.assertIn(TEXT + offset, self.patches, hex(offset))


if __name__ == "__main__":
    unittest.main()
