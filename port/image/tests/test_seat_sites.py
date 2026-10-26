"""A stat pack a seat (sites_seats.py, and custom_step's stock path in sites_custom.py), run here as the bytes the image
holds: a seat whose row is on takes its row at racer creation and in every step, every other seat the game's own
copy; each machine reads its own seat's energy use and speed cap. No game data."""
import os, struct, sys, unittest

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, ".."))
from test_raceview import Build, Cpu, get, put, STACK, TEXT
import memmap
import sites_seats

MACHINE, DEST = 0x80D00000, 0x80D10000
CONSTANTS, ROWS = sites_seats.CONSTANTS, sites_seats.ROWS


def seats(on=(), mode=1):
    """Memory with every seat's constants and row made up (seat k: energy 0.1 + k, cap 1000 + k, row words k << 16 | i),
    and the rows of the seats in `on` turned on."""
    memory = {}
    put(memory, memmap.MODE, mode)
    for k in range(30):
        c = CONSTANTS + k * memmap.SEAT_CONSTANT
        put(memory, c + memmap.SEAT_ENERGY_USE, struct.unpack(">I", struct.pack(">f", 0.1 + k))[0])
        put(memory, c + memmap.SEAT_SPEED_CAP, struct.unpack(">Q", struct.pack(">d", 1000.0 + k))[0], 8)
        for i in range(sites_seats.WORDS):
            put(memory, ROWS + k * memmap.SEAT_ROW + 4 * i, k << 16 | i)
        put(memory, memmap.SEAT_BLOCK + k, 1 if k in on else 0, 1)
    return memory


class TheSeatsOwn(Build):
    def leave(self, name, memory, regs, floats=False):
        left = []
        m = Cpu(self.image, memory, lambda address, cpu: left.append(address))
        if floats:
            m.f = [0.0] * 32
        for reg, value in regs.items():
            m.r[reg] = value
        m.run(self.symbols[name])
        return m, left

    def test_creation_takes_an_on_seats_row(self):
        for seat, on in ((5, True), (6, False)):
            memory = seats(on=(5,))
            m, left = self.leave("seat_row_create", memory, {24: seat, 0: 3})
            if on:
                self.assertEqual(left, [TEXT + 0x83398])
                self.assertEqual([get(m.mem, STACK + 0x2C + 4 * i) for i in range(45)], [5 << 16 | i for i in range(45)])
            else:
                self.assertEqual((left, m.r[3]), ([TEXT + 0x83250], 3 * 0xB4))   # the replaced mulli, then on
        # a plain start: the game's own copy, whatever the seat's byte
        m, left = self.leave("seat_row_create", seats(on=(5,), mode=0), {24: 5, 0: 3})
        self.assertEqual(left, [TEXT + 0x83250])

    def test_every_step_takes_an_on_seats_row(self):
        # a stock machine (no custom entry): the row to r5 and r3 = r5; another seat: the game's fn_1_82EDC
        memory = seats(on=(9,))
        put(memory, MACHINE + 4, 9, 2)
        m, left = self.leave("custom_step", memory, {27: MACHINE, 5: DEST})
        self.assertEqual((left, m.r[3]), ([TEXT + 0x3364C + 4], DEST))
        self.assertEqual([get(m.mem, DEST + 4 * i) for i in range(45)], [9 << 16 | i for i in range(45)])
        put(memory, MACHINE + 4, 10, 2)
        m, left = self.leave("custom_step", memory, {27: MACHINE, 5: DEST})
        self.assertEqual(left[0], TEXT + 0x82EDC)

    def test_each_machine_reads_its_seats_energy_use_and_speed_cap(self):
        for seat in (0, 13, 29):
            memory = seats()
            put(memory, MACHINE + 4, seat, 2)
            m, left = self.leave("seat_energy", memory, {30: MACHINE}, floats=True)
            self.assertEqual((left, m.f[1]), ([TEXT + 0x27424], struct.unpack(">f", struct.pack(">f", 0.1 + seat))[0]))
            m, left = self.leave("seat_speed_cap", memory, {30: MACHINE}, floats=True)
            self.assertEqual((left, m.f[0]), ([TEXT + 0x29574], 1000.0 + seat))


if __name__ == "__main__":
    unittest.main()
