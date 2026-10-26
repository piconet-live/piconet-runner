"""The rank icons (1st, 2nd, 3rd above machines; fn_1_12999C, rankicons.py, at its two port reads 0x129AA8 and
0x129AC0): the helpers run here as the bytes the image holds, against a made-up camera block. An icon is held back
(0) for the racer the HUD follows as fn_1_6B48(0) reads it (the chase camera's racer, or the TV cameras' racer in
use in camera modes 9 and 10), and for the local seat when the HUD follows none; let through (-1) for every other
racer. The first read counts each racer's frames at memmap.ICONS in a game told to keep the logs, and writes
nothing otherwise; the second keeps r3 (fn_1_5910's answer). No game data."""
import os, struct, sys, tempfile, unittest

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, ".."))
import build, memmap, rankicons
from test_race_end import Machine, signed, BSS, MODULE

IMAGE, TEXT = memmap.IMAGE, memmap.REL_TEXT
SEAT = IMAGE + 0x14
CAMERA, CHASE, LIVE = BSS + 0xF68, BSS + 0xF6C, BSS + 0xF70
BLOCK, RECORDS, TV = 0x80A00000, 0x80A10000, 0x80A20000
MACHINE = 0x80900000
LOCAL = 4


class Run(Machine):
    def other(self, pc, w):
        if w >> 26 == 31 and (w >> 1) & 0x3FF == 954:                         # extsb
            s, a = (w >> 21) & 31, (w >> 16) & 31
            self.r[a] = signed(self.r[s] & 0xFF, 8) & 0xFFFFFFFF
            return pc + 4
        if w >> 26 == 31 and (w >> 1) & 0x3FF == 124:                         # nor (not)
            s, a, b = (w >> 21) & 31, (w >> 16) & 31, (w >> 11) & 31
            self.r[a] = ~(self.r[s] | self.r[b]) & 0xFFFFFFFF
            return pc + 4
        return super().other(pc, w)


def memory(mode=0, chase=7, tv=9, block=True, none=False, logs=0, counts=None):
    words = {0x800030C8: MODULE, CAMERA: BLOCK if block else 0, CHASE: RECORDS, LIVE: TV,
             BLOCK: 0x80000000 if none else 0, memmap.IMAGE_LOGS: logs}
    words.update(counts or {})
    m = {}
    for address, value in words.items():
        m.update({address + k: b for k, b in enumerate(value.to_bytes(4, "big"))})
    m[BLOCK + 0x48] = mode & 0xFF
    m.update({RECORDS + 2: chase >> 8, RECORDS + 3: chase & 0xFF, TV + 6: tv >> 8, TV + 7: tv & 0xFF})
    return m


class TheRankIcons(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.dir = tempfile.TemporaryDirectory()
        b = build.build(cls.dir.name)
        patches = {int(p["address"], 16): p for p in b["patches"]}
        image = bytearray(b["image"])
        struct.pack_into(">I", image, SEAT - IMAGE, LOCAL)
        cls.image = bytes(image)
        cls.helpers = []
        for site in rankicons.SITES:
            p = patches[TEXT + site]
            assert p["original"] == "881E0475", p                               # lbz r0, 0x475(r30)
            entry = build.branch_target(int(p["new"], 16), TEXT + site)
            bne = struct.unpack_from(">I", cls.image, entry + 12 - IMAGE)[0]
            assert bne >> 16 == 0x4082, p                                       # the mode test, then the helper
            cls.helpers.append(entry + 12 + signed(bne & 0xFFFC))

    @classmethod
    def tearDownClass(cls):
        cls.dir.cleanup()

    def held(self, racer, site=0, **kw):
        """Whether the icon of `racer` is held back by the helper of `site`; nothing written without the logs."""
        m = Run(self.image, memory(**kw))
        m.mem.update({MACHINE + 4: racer >> 8, MACHINE + 5: racer & 0xFF})
        m.r[30], m.r[3] = MACHINE, 0x1234
        m.run(self.helpers[site])
        self.assertEqual(m.r[3], 0x1234)
        if not kw.get("logs"):
            self.assertEqual(m.written, set())
        self.assertIn(m.r[0], (0, 0xFFFFFFFF))
        return m.r[0] == 0, m

    def test_the_chase_cameras_racer_is_held_back_and_the_local_seat_not(self):
        for site in (0, 1):
            self.assertTrue(self.held(7, site, chase=7)[0])
            self.assertFalse(self.held(LOCAL, site, chase=7)[0])
            self.assertFalse(self.held(3, site, chase=7)[0])

    def test_racing_holds_back_the_local_seat_alone(self):
        for racer in range(30):
            self.assertEqual(self.held(racer, chase=LOCAL)[0], racer == LOCAL)

    def test_the_tv_cameras_racer_in_modes_9_and_10(self):
        for mode in (9, 10):
            self.assertTrue(self.held(9, mode=mode, chase=7, tv=9)[0])
            self.assertFalse(self.held(7, mode=mode, chase=7, tv=9)[0])
        for mode in (0, 5, 8, 12, 0xFF):
            self.assertTrue(self.held(7, mode=mode, chase=7, tv=9)[0])
            self.assertFalse(self.held(9, mode=mode, chase=7, tv=9)[0])

    def test_no_racer_followed_gives_the_local_seat(self):
        for kw in ({"mode": 11}, {"none": True}, {"block": False}):
            self.assertTrue(self.held(LOCAL, chase=7, **kw)[0], kw)
            self.assertFalse(self.held(7, chase=7, **kw)[0], kw)

    def test_the_first_read_counts_with_the_logs(self):
        counts = {memmap.ICONS + 7 * 4: 5, memmap.ICONS + 120 + 3 * 4: 2}
        held, m = self.held(7, logs=1, chase=7, counts=counts)
        self.assertTrue(held)
        self.assertEqual(m.written, {memmap.ICONS + 7 * 4})
        self.assertEqual(m.read(memmap.ICONS + 7 * 4, 4), 6)
        held, m = self.held(3, logs=1, chase=7, counts=counts)
        self.assertFalse(held)
        self.assertEqual(m.written, {memmap.ICONS + 120 + 3 * 4})
        self.assertEqual(m.read(memmap.ICONS + 120 + 3 * 4, 4), 3)
        self.assertEqual(self.held(7, site=1, logs=1, chase=7)[1].written, set())   # the second read counts nothing


if __name__ == "__main__":
    unittest.main()
