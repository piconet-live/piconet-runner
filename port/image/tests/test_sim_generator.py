"""The simulation's own generator (sites_generator.py, frame_replay.py `simgen`), run here as the bytes the image
holds: the step wrapper's seed and shift, the race's two draws' routines, and the generator log's first word. No game
data."""
import os, sys, unittest

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, ".."))
import build, memmap  # noqa: F401  (build puts the generators' folder on the path)
import frame_checksum, frame_replay, sites_generator
from test_race_end import BSS
from test_raceview import Build, Cpu, get, put, CONTROL, IMAGE, RUNNING, STEP, TEXT

EXECUTABLE = memmap.EXECUTABLE_GENERATOR
FLAG, SEED = memmap.RACE + memmap.RACE_SIM_GENERATOR, memmap.RACE + memmap.RACE_CPUSEED
WORD, SHIFT = memmap.SIM_GENERATOR, memmap.SIM_GENERATOR_SHIFT


def fmix(seed, step):
    """MurmurHash3's finaliser of seed ^ step * 0x9E3779B9, as the wrapper works it out."""
    x = (seed ^ step * 0x9E3779B9) & 0xFFFFFFFF
    x ^= x >> 16
    x = x * 0x85EBCA6B & 0xFFFFFFFF
    x ^= x >> 13
    x = x * 0xC2B2AE35 & 0xFFFFFFFF
    return x ^ x >> 16


def assemble(text):
    from keystone import Ks, KS_ARCH_PPC, KS_MODE_PPC32, KS_MODE_BIG_ENDIAN
    return bytes(Ks(KS_ARCH_PPC, KS_MODE_PPC32 + KS_MODE_BIG_ENDIAN).asm(text)[0])


class SimGenerator(Build):
    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        code = assemble(frame_replay._simgen())                     # its own labels only: the step wrapper's part
        assert cls.image.count(code) == 1
        cls.simgen = IMAGE + cls.image.index(code)
        text = "genlog:\n" + frame_checksum.asm().split("genlog:\n")[1]
        code = assemble(text)
        assert cls.image.count(code) == 1
        cls.genlog = IMAGE + cls.image.index(code)

    def wrapper(self, running, flag, seed=0x12345678, step=4700, shift=0x5555):
        m = {}
        put(m, RUNNING, running); put(m, STEP, step); put(m, FLAG, flag); put(m, SEED, seed); put(m, SHIFT, shift)
        cpu = Cpu(self.image, m)
        cpu.r[31], cpu.r[30] = CONTROL, BSS
        cpu.run(self.simgen)
        return cpu

    def test_a_race_with_the_word_seeds_it_from_the_step_and_the_seed(self):
        for seed, step in ((0x12345678, 4700), (0x12345678, 4701), (0, 0), (0xFFFFFFFF, 0x7FFF)):
            cpu = self.wrapper(1, 1, seed, step)
            self.assertEqual(get(cpu.mem, WORD), fmix(seed, step))
            self.assertEqual(get(cpu.mem, SHIFT), (WORD - EXECUTABLE) & 0xFFFFFFFF)
            self.assertEqual(cpu.written, {WORD, SHIFT})
        self.assertNotEqual(fmix(0x12345678, 4700), fmix(0x12345678, 4701))

    def test_without_the_word_or_outside_a_running_race_the_draws_take_the_executables(self):
        for running, flag in ((1, 0), (0, 1), (0, 0)):
            cpu = self.wrapper(running, flag)
            self.assertEqual(get(cpu.mem, SHIFT), 0, (running, flag))
            self.assertEqual(cpu.written, {SHIFT})

    def test_each_draw_takes_the_word_the_shift_names(self):
        for name, offset in sites_generator.SITES:
            site = TEXT + offset
            self.assertEqual(self.patches[site]["original"], f"{sites_generator.KNOWN:08X}")
            for shift, want in ((0, 0x801A0000), ((WORD - EXECUTABLE) & 0xFFFFFFFF, WORD - 0x63C0)):
                m = {}
                put(m, SHIFT, shift)
                seen = []
                cpu = Cpu(self.image, m, game=lambda pc, c: seen.append((pc, c.r[4])))
                cpu.run(self.symbols[name])
                self.assertEqual(seen, [(site + 4, want)], name)       # r4 + 0x63C0: the word drawn from

    def test_the_generator_log_holds_the_word_the_race_draws_from(self):
        entry = memmap.GENERATOR_LOG + 8 * (4700 % memmap.GENERATOR_LOG_STEPS)
        for shift, want in ((0, 0x11112222), ((WORD - EXECUTABLE) & 0xFFFFFFFF, 0x77778888)):
            m = {}
            put(m, RUNNING, 1); put(m, STEP, 4700); put(m, memmap.IMAGE_LOGS, 1); put(m, SHIFT, shift)
            put(m, EXECUTABLE, 0x11112222); put(m, WORD, 0x77778888); put(m, BSS - 0x30000 + 0xBE8, 0x33334444)
            cpu = Cpu(self.image, m)
            cpu.r[31], cpu.r[30] = CONTROL, BSS
            cpu.run(self.genlog)
            self.assertEqual((get(cpu.mem, entry), get(cpu.mem, entry + 4)), (want, 0x33334444))


if __name__ == "__main__":
    unittest.main()
