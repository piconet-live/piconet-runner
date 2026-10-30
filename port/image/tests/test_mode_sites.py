"""The game mode's sites (sites_mode.py), run here as the bytes the image holds against a made-up machine: the speed
hold (`mode_hold`, with the game's square root as a stand-in), the test of a seat that may be held (`mode_held`), and
the verdict's own Restore (`respawns`). No game data."""
import math, os, struct, sys, unittest

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, ".."))
from test_raceview import Build, Cpu, get, put, TEXT
import build
import memmap
import sites_mode

M, RECORD = 0x80D00000, 0x80D10000


def real(memory, address):
    return struct.unpack(">f", get(memory, address).to_bytes(4, "big"))[0]


def put_real(memory, address, value):
    put(memory, address, int.from_bytes(struct.pack(">f", value), "big"))


def single(x):
    return struct.unpack(">f", struct.pack(">f", x))[0]


class ModeSites(Build):
    def call(self, symbol, memory, **registers):
        def game(address, cpu):                       # the game's square root, f1 -> f1
            self.assertEqual(address, sites_mode.SQRT)
            cpu.f[1] = math.sqrt(cpu.f[1])
        m = Cpu(self.image, memory, game)
        m.f = [0.0] * 32
        for name, value in registers.items():
            m.r[int(name[1:])] = value
        m.run(self.symbols[symbol])
        return m

    def machine(self, p=(3.0, 4.0, 0.0), weight=2.0, held=2160.0, flags=0):
        memory = {}
        put(memory, M, flags)
        for k, v in enumerate(p):
            put_real(memory, M + 0x94 + 4 * k, v)
        put_real(memory, M + 8, weight)
        for at, v in ((0xF4, 0.0), (0x104, 0.6), (0x114, 0.8)):   # the nose: the orientation's column 2
            put_real(memory, M + at, v)
        put(memory, RECORD, sites_mode.HELD)
        put_real(memory, RECORD + 12, held)
        return memory

    def test_the_sites_are_the_known_words(self):
        for site, known, name in ((sites_mode.POINT, sites_mode.POINT_KNOWN, "mode_point"),
                                  (sites_mode.SPEED, sites_mode.SPEED_KNOWN, "mode_speed"),
                                  (sites_mode.VERDICT, sites_mode.VERDICT_KNOWN, "respawns")):
            p = self.patches[TEXT + site]
            self.assertEqual(int(p["original"], 16), known, name)
            self.assertEqual(int(p["new"], 16), build.branch(TEXT + site, self.symbols[name]), name)

    def test_the_hold_scales_the_momentum_to_the_speed(self):
        # v 2160 km/h = 10 m a step; W 2: |P| becomes 20 along (3, 4, 0); the base speed 10; the read-out v again
        memory = self.machine()
        memory = self.call("mode_hold", memory, r3=M, r4=RECORD, r5=1).mem
        self.assertEqual([real(memory, M + 0x94 + 4 * k) for k in range(3)], [12.0, 16.0, 0.0])
        self.assertEqual(real(memory, M + 0x224), 10.0)
        self.assertEqual(real(memory, M + 0x17C), 2160.0)

    def test_the_step_hold_leaves_the_read_out(self):
        memory = self.machine()
        put_real(memory, M + 0x17C, 123.0)
        memory = self.call("mode_hold", memory, r3=M, r4=RECORD, r5=0).mem
        self.assertEqual(real(memory, M + 0x17C), 123.0)
        self.assertEqual(real(memory, M + 0x94), 12.0)

    def test_a_machine_at_rest_is_pushed_along_its_nose(self):
        memory = self.machine(p=(0.0, 0.0, 0.0), held=1080.0)          # 5 m a step, W 2: T = 10
        memory = self.call("mode_hold", memory, r3=M, r4=RECORD, r5=1).mem
        self.assertEqual([real(memory, M + 0x94 + 4 * k) for k in range(3)],
                         [single(-0.0 * 10.0), single(-0.6 * 10.0), single(-0.8 * 10.0)])
        self.assertEqual(real(memory, M + 0x17C), single(216.0 * single(math.sqrt(single(36.0) + single(64.0)) / 2.0)))

    def test_only_a_seat_racing_is_held(self):
        for flags, held in ((0, 1), (0x8000, 0), (0x80, 0), (0x800, 0), (0x10000, 0), (0x08000000, 0),
                            (0x40000000, 0), (0x40 | 0x2 | 0x1, 1)):
            memory = self.machine(flags=flags)
            self.assertEqual(self.call("mode_held", memory, r3=M, r4=RECORD).r[3], held, hex(flags))
        memory = self.machine()
        put(memory, RECORD, sites_mode.PEDALS | sites_mode.BOOST_OFF)   # no hold this step
        self.assertEqual(self.call("mode_held", memory, r3=M, r4=RECORD).r[3], 0)

    def test_the_verdict_takes_the_rule_or_the_machines_own_restore(self):
        for rule, flags, restore in ((0, 0, False), (0x8000, 0, True), (0, 1, True), (0x8000, 1, True),
                                     (0x7FFF, 0x7FFE, False)):
            memory = {}
            put(memory, memmap.MODE, 1)
            put(memory, M, flags)
            m = Cpu(self.image, memory, lambda address, cpu: None)
            m.r[0], m.r[30] = rule, M
            m.run(self.symbols["respawns"])
            self.assertEqual(m.cr0[2], not restore, (hex(rule), hex(flags)))   # beq: not put back


if __name__ == "__main__":
    unittest.main()
