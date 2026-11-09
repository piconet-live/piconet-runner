"""A mode's pause (frame_replay.py `manager`, frame_entry.py `extra_on` and `done_ret`), run here as the bytes the image
holds: a step whose mode block says "paused" stands (the mode point's host call in place of the simulation manager,
STOOD 1), any other runs (STOOD 0, the simulation manager); and the race clock is counted only for a step that ran,
the frame's last step's count being the game's own after the routine returns. No game data."""
import os, sys, unittest

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, ".."))
import build, memmap  # noqa: F401  (build puts the generators' folder on the path)
import frame_entry, frame_replay, native, netframe
from test_race_end import BSS, MODULE
from test_raceview import Build, Cpu, get, put, CONTROL, STEP

BLOCK = 0x80500000                                   # the mode's block (memmap.MODES + 4 names it)
SESSION, RUNNING = CONTROL + 116, CONTROL + 120
PHASE, CLOCK = BSS + 0x3C2A, BSS + 0x50A4
SIM_MANAGER = MODULE + 0x4158                        # fn_1_4158, the simulation manager the routine calls


class ThePause(Build):
    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        from keystone import Ks, KS_ARCH_PPC, KS_MODE_PPC32, KS_MODE_BIG_ENDIAN
        code = bytes(Ks(KS_ARCH_PPC, KS_MODE_PPC32 + KS_MODE_BIG_ENDIAN).asm(frame_replay._stand())[0])
        assert cls.image.count(code) == 1
        offsets = native.label_offsets(netframe.asm)                   # the frame routine's text, where the test is
        base = memmap.IMAGE + cls.image.index(code) - offsets["m_flag"] - 4
        cls.at = {name: base + offsets[name] for name in ("manager", "done_ret", "extra_on", "extra_clock", "dr_clock")}
        word = lambda a: int.from_bytes(cls.image[a - memmap.IMAGE:a - memmap.IMAGE + 4], "big")
        assert word(cls.at["manager"]) == 0x7C0802A6                    # mflr 0: the labels are where the text says
        assert word(cls.at["done_ret"]) == 0x801F0000 | (memmap.STOOD - CONTROL)   # lwz 0, STOOD(31)

    def manager(self, block, paused, stood=0xAA):
        """`manager` from its first instruction to its return: where it went, and the STOOD word."""
        m, went = {}, []
        put(m, 0x800030C8, MODULE); put(m, SESSION, 0); put(m, RUNNING, 1); put(m, STEP, 5000)
        put(m, memmap.MODES + 4, block); put(m, memmap.STOOD, stood)
        if block:
            put(m, block + memmap.MODE_PAUSED, paused)
        cpu = Cpu(self.image, m, game=lambda pc, c: went.append(pc))
        cpu.r[31], cpu.r[30] = CONTROL, BSS
        cpu.run(self.at["manager"], limit=400)
        return went, get(cpu.mem, memmap.STOOD)

    def test_a_paused_block_stands_the_step_and_runs_the_mode(self):
        went, stood = self.manager(BLOCK, 1)
        self.assertEqual((went, stood), ([memmap.HOST_MODE], 1))      # no simulation: the mode's run instead

    def test_otherwise_the_step_runs(self):
        for block, paused in ((BLOCK, 0), (0, 1)):
            went, stood = self.manager(block, paused)
            self.assertEqual((went[-1:], stood), ([SIM_MANAGER], 0), (block, paused))
            self.assertNotIn(memmap.HOST_MODE, went)                  # (the mode point is the race pass's)

    def clock(self, entry, stood, phase=0x29, clock=1000):
        m = {}
        put(m, 0x800030C8, MODULE); put(m, memmap.STOOD, stood); put(m, PHASE, phase, 2); put(m, CLOCK, clock)
        image = bytearray(self.image)                                  # the clock's part alone: a return where it ends
        for end in ("extra_clock", "dr_clock"):
            image[self.at[end] - memmap.IMAGE:self.at[end] - memmap.IMAGE + 4] = (0x4E800020).to_bytes(4, "big")
        cpu = Cpu(bytes(image), m, game=lambda pc, c: None)
        cpu.r[31], cpu.r[30] = CONTROL, BSS
        cpu.run(entry, limit=40)
        return get(cpu.mem, CLOCK)

    def test_the_clock_counts_a_step_that_ran(self):
        # extra_on counts the step just run, before the next: not when it stood, nor outside the race phase
        e = self.at["extra_on"]
        self.assertEqual([self.clock(e, 0), self.clock(e, 1), self.clock(e, 0, phase=0x28)], [1001, 1000, 1000])

    def test_the_frames_last_step_that_stood_takes_back_the_games_count(self):
        # done_ret: the game's frame function then adds one in the race phase below the top (main.rel 0x37BAC)
        d, top = self.at["done_ret"], frame_entry.CLOCK_CAP
        self.assertEqual([self.clock(d, 0), self.clock(d, 1), self.clock(d, 1, phase=0x28), self.clock(d, 1, clock=top)],
                         [1000, 999, 1000, top])


if __name__ == "__main__":
    unittest.main()
