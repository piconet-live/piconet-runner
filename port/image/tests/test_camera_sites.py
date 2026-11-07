"""The cameras' words of the patch list (sites.py), run here as the bytes the image holds: the slot the chase camera
indexes its record by, and the index of the three routines that read a camera record for a machine's slot. The
game's code a routine leaves for is a stand-in. No game data."""
import os, sys, unittest

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, ".."))
import build, memmap
from test_raceview import Build, Cpu, get, put, TEXT

MODE = memmap.MODE
RECORD = 0x1FC                                   # bytes of a camera record; the game has four


class TheCamerasWords(Build):
    def leave(self, routine, mode, **registers):
        """Runs a routine entered by a plain branch until it leaves for the game: where, and the registers."""
        left, memory = [], {}
        put(memory, MODE, mode)
        m = Cpu(self.image, memory, lambda address, cpu: left.append(address))
        for name, value in registers.items():
            m.r[int(name[1:])] = value
        m.run(self.symbols[routine])
        return left, m

    def test_the_chase_camera_indexes_record_0_for_every_seat(self):
        # fn_1_AA54 (0xAA84) and fn_1_ABAC (0xAC08) ask fn_1_86634 for the followed machine's viewport slot, which
        # is 0xFF for every machine but seat 0's; fn_1_713C and fn_1_715C then write the record by it. In a race
        # and in a replay the slot is 0: the one screen's record, for whichever seat this game follows
        for offset in (0xAA84, 0xAC08):
            for mode in (1, 2):
                left, m = self.leave(f"camera_{offset:x}", mode, r3=0xFF)
                self.assertEqual((left, m.r[3], m.written), ([TEXT + offset + 4], 0, set()), (offset, mode))
        self.assertEqual(set(self.patches) & {TEXT + 0xAA84, TEXT + 0xAC08}, {TEXT + 0xAA84, TEXT + 0xAC08})
        # the writers are reached from those two routines alone: their callers are in fn_1_AA54 and fn_1_ABAC
        self.assertEqual(build.CAMERA_INDEXES, (0x725C, 0x6F14, 0x686C))

    def test_a_machine_without_a_slot_reads_the_one_screens_record(self):
        # fn_1_725C, fn_1_6EC0 and fn_1_681C: `clrlwi r0, r3, 0x18`, then record r0. With slot 0xFF they read
        # 0x1FA04 bytes past the four records. In a race or a replay the index is 0 whenever it is not one of the four
        # (the view, fn_1_725C, has its own: test_a_machine_without_a_slot_is_in_no_first_person_view)
        for offset in build.CAMERA_INDEXES[1:]:
            name = f"camera_index_{offset:x}"
            self.assertEqual(int(self.patches[TEXT + offset]["original"], 16), build.CAMERA_INDEX_WORD)
            for mode in (1, 2):
                for slot, index in ((0xFF, 0), (0xFFFFFFFF, 0), (4, 0), (0x80, 0), (0, 0), (1, 1), (2, 2), (3, 3)):
                    left, m = self.leave(name, mode, r3=slot)
                    self.assertEqual((left, m.r[0], m.r[3], m.written), ([TEXT + offset + 4], index, slot, set()), (name, slot))
                    self.assertLess(m.r[0] * RECORD, 4 * RECORD)
            left, m = self.leave(name, 0, r3=0xFF)                   # a plain start: the game's own instruction
            self.assertEqual((left, m.r[0]), ([TEXT + offset + 4], 0xFF))

    def test_a_machine_without_a_slot_is_in_no_first_person_view(self):
        # fn_1_725C: `clrlwi r0, r3, 0x18`; a slot of the four goes on to the record, as the game does. Any other slot
        # returns 1 at once: its callers ask only "view 0?" of the machine (and the one that asks "view 1?", fn_1_86A2C,
        # has compared the slot with the viewport's first)
        offset = build.CAMERA_INDEXES[0]
        self.assertEqual(int(self.patches[TEXT + offset]["original"], 16), build.CAMERA_INDEX_WORD)
        for mode in (1, 2):
            for slot, index in ((0, 0), (1, 1), (2, 2), (3, 3)):
                left, m = self.leave("camera_view", mode, r3=slot)
                self.assertEqual((left, m.r[0], m.r[3], m.written), ([TEXT + offset + 4], index, slot, set()), slot)
            for slot in (0xFF, 0xFFFFFFFF, 4, 0x80):
                left, m = self.leave("camera_view", mode, r3=slot)
                self.assertEqual((left, m.r[3], m.written), ([], 1, set()), slot)
        left, m = self.leave("camera_view", 0, r3=0xFF)              # a plain start: the game's own instruction
        self.assertEqual((left, m.r[0]), ([TEXT + offset + 4], 0xFF))


if __name__ == "__main__":
    unittest.main()
