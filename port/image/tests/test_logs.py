"""The image's three test logs are kept only in a game the host has told to keep them (memmap.IMAGE_LOGS, set
for a launch file that asks for a dump): a players' game writes none of their regions. Here the generator log
(frame_checksum.py `genlog`); the spectator's log is in test_spectator_out.py, the smoothing's in test_smooth.py
and test_shadow.py. No game data."""
import os, sys, unittest

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, ".."))
import build, memmap  # noqa: F401  (build puts the generators' folder on the path)
import frame_checksum
from test_race_end import BSS
from test_raceview import Build, Cpu, get, put, CONTROL, IMAGE, RUNNING, STEP

EXECUTABLE = memmap.EXECUTABLE_GENERATOR
EFFECTS = BSS - 0x30000 + 0xBE8                   # main.rel .data 0x1D628, as the routine reaches it from .bss
LOGS = ((memmap.SPEC_LOG, memmap.SPEC_LOG + 8 * memmap.SPEC_LOG_ENTRIES),
        (memmap.GENERATOR_LOG, memmap.GENERATOR_LOG + 8 * memmap.GENERATOR_LOG_STEPS),
        (memmap.SMOOTH_LOG, memmap.SMOOTH_LOG + 8 * memmap.SMOOTH_LOG_FRAMES))


class TheLogsSwitch(Build):
    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        from keystone import Ks, KS_ARCH_PPC, KS_MODE_PPC32, KS_MODE_BIG_ENDIAN
        text = "genlog:\n" + frame_checksum.asm().split("genlog:\n")[1]
        code = bytes(Ks(KS_ARCH_PPC, KS_MODE_PPC32 + KS_MODE_BIG_ENDIAN).asm(text)[0])
        assert cls.image.count(code) == 1
        cls.genlog = IMAGE + cls.image.index(code)

    def step(self, logs, step=4700):
        m = {}
        put(m, RUNNING, 1); put(m, STEP, step); put(m, memmap.IMAGE_LOGS, logs)
        put(m, EXECUTABLE, 0x11112222); put(m, EFFECTS, 0x33334444)
        cpu = Cpu(self.image, m)
        cpu.r[31], cpu.r[30] = CONTROL, BSS
        cpu.run(self.genlog)
        return cpu

    def test_the_generator_log_is_kept_only_when_told(self):
        entry = memmap.GENERATOR_LOG + 8 * (4700 % memmap.GENERATOR_LOG_STEPS)
        cpu = self.step(1)
        self.assertEqual((get(cpu.mem, entry), get(cpu.mem, entry + 4), cpu.written), (0x11112222, 0x33334444, {entry, entry + 4}))
        cpu = self.step(0)                                           # a players' game
        self.assertEqual(cpu.written, set())

    def test_the_logs_do_not_overlap_and_the_switch_is_the_control_blocks(self):
        spans = sorted(LOGS)
        self.assertTrue(all(a[1] <= b[0] for a, b in zip(spans, spans[1:])), spans)
        self.assertTrue(CONTROL <= memmap.IMAGE_LOGS < CONTROL + 0x400)
        self.assertTrue(all(not lo <= memmap.IMAGE_LOGS < hi for lo, hi in LOGS))


if __name__ == "__main__":
    unittest.main()
