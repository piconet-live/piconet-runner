"""`seat.look = ghost`'s sites (sites_ghostdraw.py, mode_look's ghost path), run here as the bytes the image holds against
made-up memory: the machine's draw sets the seat aside, a silent seat is out of hearing, a ghost has no shadow's picture,
the task is made only under the window's flag. The task's own drawing has no test here: it is for rounds by eye. No game data."""
import os, sys, unittest

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, ".."))
from test_raceview import Build, get, put, TEXT
from test_mode_sites import ModeCpu
import memmap
import sites_ghostdraw as ghost
import sites_modehud as hud

BLOCK = 0x80D00000
MACHINE, CAR, RECORD = 0x80C00000, 0x80C10000, 0x80C20000


class TheGhostDraw(Build):
    def run_routine(self, name, memory, stop_at=(), **registers):
        reached = []
        memory.setdefault(memmap.MODE, 1)
        m = ModeCpu(self.image, memory, lambda address, cpu: reached.append(address))
        for key, value in registers.items():
            m.r[int(key[1:])] = value
        m.run(self.symbols[name])
        return m, reached, m.mem

    def look(self, look, seat=3):
        memory = {}
        put(memory, memmap.MODES + 4, BLOCK)
        memory[memmap.MODE_HUD_LOOKS + seat] = look
        memory[MACHINE + 4], memory[MACHINE + 5] = 0, seat
        return self.run_routine("mode_look", memory, r3=CAR, r4=2, r29=MACHINE)

    def test_a_ghost_is_set_aside_and_not_drawn_at_cardisp(self):
        _, reached, memory = self.look(1)
        self.assertNotIn(TEXT + hud.DRAW_MACHINE, reached)
        self.assertEqual(get(memory, memmap.MODE_HUD_GHOSTS + 8 * 3), CAR)
        self.assertEqual(memory[memmap.MODE_HUD_GHOSTS + 8 * 3 + 4], 2)

    def test_a_normal_machine_is_the_games_call(self):
        _, reached, memory = self.look(0)
        self.assertIn(TEXT + hud.DRAW_MACHINE, reached)
        self.assertEqual(get(memory, memmap.MODE_HUD_GHOSTS + 8 * 3), 0)

    def pilot(self, look, block=BLOCK, mode=1, seat=3):
        memory = {memmap.MODE_HUD_LOOKS + seat: look, MACHINE + 4: 0, MACHINE + 5: seat}
        put(memory, memmap.MODE, mode)
        put(memory, memmap.MODES + 4, block)
        _, reached, _ = self.run_routine("ghost_pilot", memory, r3=CAR, r4=0, r29=MACHINE)
        return TEXT + ghost.PILOT_DRAW in reached

    def test_a_ghost_or_invisible_seat_has_no_pilot(self):
        self.assertFalse(self.pilot(1))
        self.assertFalse(self.pilot(2))

    def test_any_other_seat_keeps_the_games_pilot(self):
        self.assertTrue(self.pilot(0))
        self.assertTrue(self.pilot(3))
        self.assertTrue(self.pilot(1, block=0))
        self.assertTrue(self.pilot(1, mode=0))

    def voice(self, mask, seat, mode=1):
        memory = {}
        put(memory, memmap.MODE, mode)
        put(memory, memmap.MODE_HUD_SILENT, mask)
        _, reached, _ = self.run_routine("ghost_voice", memory, r27=seat)
        return TEXT + ghost.VOICE_FAR in reached

    def test_a_silent_seat_is_out_of_hearing(self):
        self.assertTrue(self.voice(1 << 3, 3))
        self.assertFalse(self.voice(1 << 3, 4))
        self.assertFalse(self.voice(0, 3))
        self.assertFalse(self.voice(1 << 3, 3, mode=0))

    def shadow(self, look, block=BLOCK):
        memory = {}
        put(memory, memmap.MODES + 4, block)
        put(memory, RECORD + 0x48, MACHINE)
        memory[MACHINE + 4], memory[MACHINE + 5] = 0, 3
        memory[memmap.MODE_HUD_LOOKS + 3] = look
        _, reached, _ = self.run_routine("ghost_shadow", memory, r3=RECORD)
        return TEXT + ghost.SHADOW + 4 in reached

    def test_a_ghost_has_no_shadows_picture(self):
        self.assertFalse(self.shadow(1))
        self.assertTrue(self.shadow(0))
        self.assertTrue(self.shadow(2))
        self.assertTrue(self.shadow(1, block=0))

    def make(self, flags):
        memory = {}
        put(memory, memmap.MODE_HUD_FLAGS, flags)
        _, reached, _ = self.run_routine("ghost_make", memory, r3=1, r4=2)
        return [a for a in reached if a in (TEXT + ghost.GHOSTS_FN, TEXT + ghost.TASK_NEW, TEXT + ghost.MAKE + 4)]

    def test_the_task_is_made_only_under_the_flag(self):
        self.assertEqual(self.make(0), [TEXT + ghost.GHOSTS_FN, TEXT + ghost.MAKE + 4])
        self.assertEqual(self.make(memmap.MODE_POINTS_PAGE), [TEXT + ghost.GHOSTS_FN, TEXT + ghost.MAKE + 4])
        self.assertEqual(self.make(memmap.MODE_GHOST_TASK), [TEXT + ghost.GHOSTS_FN, TEXT + ghost.TASK_NEW, TEXT + ghost.MAKE + 4])

    def test_the_task_and_the_sites_are_in_the_image(self):
        self.assertIn("ghost_disp", self.symbols)
        for site in (ghost.MAKE, ghost.VOICE, ghost.SHADOW):
            self.assertIn(TEXT + site, self.patches)


if __name__ == "__main__":
    unittest.main()
