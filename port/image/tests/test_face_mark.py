"""The mark on the top six portraits (sites_faces.py), run here as the bytes the image holds against a made-up camera
block: at fn_1_C8294's read of a row's mark, r0 is 0 for the row of the racer the HUD follows and -1 for the others on
the race's board, the local seat's row on the results page, and the game's own word in a plain start. It writes
nothing and reads no game generator. No game data."""
import os, struct, sys, tempfile, unittest

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, ".."))
import build, memmap, sites_faces
from test_rank_icons import Run, memory, SEAT, IMAGE, TEXT, LOCAL

ENTRY = 0x80B00000
GENERATOR = 0x801A63C0
MARK = -1 & 0xFFFFFFFF


class Back(Exception):
    """The routine went back to the game, after the site."""


class Reads(Run):
    def read(self, address, size):
        self.__dict__.setdefault("reads", set()).add(address)
        return super().read(address, size)

    def other(self, pc, w):
        if pc == TEXT + sites_faces.SITE + 4:
            raise Back
        return super().other(pc, w)


class TheMark(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.dir = tempfile.TemporaryDirectory()
        b = build.build(cls.dir.name)
        cls.patches = {int(p["address"], 16): p for p in b["patches"]}
        cls.entry = b["symbols"]["face_mark"]
        image = bytearray(b["image"])
        struct.pack_into(">I", image, SEAT - IMAGE, LOCAL)
        cls.image = bytes(image)

    @classmethod
    def tearDownClass(cls):
        cls.dir.cleanup()

    def mark(self, racer, board=1, stored=0x77, plain=False, **kw):
        """r0 after the site for the row of `racer`, whose entry's +0x18 holds `stored`."""
        m = Reads(self.image, memory(counts={memmap.MODE: 0 if plain else 1}, **kw))
        for address, value in ((ENTRY + 4, racer), (ENTRY + 0x18, stored)):
            m.mem.update({address + k: b for k, b in enumerate(value.to_bytes(4, "big"))})
        m.r[31], m.r[15] = ENTRY, board
        m.r[3], m.r[4] = 0x1234, 0x5678
        with self.assertRaises(Back):
            m.run(self.entry)
        self.assertEqual(m.written, set())
        self.assertNotIn(GENERATOR, m.reads)
        self.assertEqual((m.r[3], m.r[4], m.r[31], m.r[15]), (0x1234, 0x5678, ENTRY, board))
        return m.r[0]

    def test_the_site_is_the_reads_word(self):
        p = self.patches[TEXT + sites_faces.SITE]
        self.assertEqual(p["original"], "801F0018")          # lwz r0, 0x18(r31)
        self.assertEqual(build.branch_target(int(p["new"], 16), TEXT + sites_faces.SITE), self.entry)

    def test_racing_marks_the_local_seat_alone_whatever_its_number(self):
        for racer in range(30):
            self.assertEqual(self.mark(racer, chase=LOCAL), 0 if racer == LOCAL else MARK, racer)

    def test_racer_0_is_not_marked_for_another_seat(self):
        self.assertEqual(self.mark(0, chase=LOCAL, stored=0), MARK)   # the game's +0x18 said racer 0

    def test_a_watcher_marks_the_racer_watched(self):
        self.assertEqual(self.mark(7, chase=7), 0)
        self.assertEqual(self.mark(LOCAL, chase=7), MARK)
        for mode in (9, 10):
            self.assertEqual(self.mark(9, mode=mode, chase=7, tv=9), 0)
            self.assertEqual(self.mark(7, mode=mode, chase=7, tv=9), MARK)

    def test_no_racer_followed_marks_the_local_seat(self):
        for kw in ({"mode": 11}, {"none": True}, {"block": False}):
            self.assertEqual(self.mark(LOCAL, chase=7, **kw), 0, kw)
            self.assertEqual(self.mark(7, chase=7, **kw), MARK, kw)

    def test_the_results_page_marks_the_local_seat(self):
        self.assertEqual(self.mark(LOCAL, board=0, chase=7), 0)
        self.assertEqual(self.mark(7, board=0, chase=7), MARK)

    def test_a_plain_start_reads_the_entry(self):
        for stored in (0, 3, MARK):
            self.assertEqual(self.mark(5, stored=stored, chase=5, plain=True), stored)


if __name__ == "__main__":
    unittest.main()
