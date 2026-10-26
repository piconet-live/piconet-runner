"""The image's list of memory never rolled back (untracked.py) against the list of rollback's state
(port/facts/rollback-state.tsv): every range the image keeps out of a rollback has its row there, and no row says
"kept by the image" that the image does not keep. The core's test holds the undo log to the same rows."""
import os, sys, unittest

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, ".."))
import build  # noqa: F401  (puts the generators' folder on the path)
import native
import untracked

LIST = os.path.join(here, "..", "..", "facts", "rollback-state.tsv")


def rows():
    with open(LIST, encoding="utf-8") as f:
        lines = [line.rstrip("\n").split("\t") for line in f if line.strip() and not line.startswith("#")]
    return [dict(zip(lines[0], line)) for line in lines[1:]]


class TheImagesList(unittest.TestCase):
    def test_the_sprite_pool_is_kept_whole(self):
        # main.rel .bss 0x4C688: 64 entries of 0x80 (fn_1_4DF60's loop), the next object at 0x4E688; the routine
        # forms its bounds from main.rel's .bss in r30
        self.assertEqual(untracked.SPRITES, [(0x4C688, 0x4E688)])
        self.assertIn("    addis 9, 30, 5\n    addi 9, 9, %d\n    addi 10, 9, 8192\n" % (0x4C688 - 0x50000),
                      untracked.asm())

    def test_every_range_the_image_keeps_has_its_row(self):
        kept = [r for r in rows() if r["within"] == "simulation-memory" and r["restored"] == "no"]
        fixed = sorted((int(r["start"], 16), int(r["end"], 16)) for r in kept if r["space"] == "bss")
        # the face board's variables and the file loader's state are typed inside the routine's text
        was = native.ON
        native.ON = True   # the native form's list (the fork's has no MOVIE)
        try:
            text = untracked.asm()
        finally:
            native.ON = was
        face = [(0x7ACD8, 0x7ACD8 + 0x70), (0x384D0, 0x384D0 + 8)]
        self.assertIn("addis 9, 30, 8\n    addi 9, 9, %d\n    addi 10, 9, 0x70\n" % (0x7ACD8 - 0x80000), text)
        self.assertIn("addis 9, 30, 4\n    addi 9, 9, %d\n    addi 10, 9, 8\n" % (0x384D0 - 0x40000), text)
        self.assertEqual(fixed, sorted(untracked.MANAGER + untracked.HUD + untracked.SPRITES + untracked.RUMBLE + untracked.MOVIE + face))
        # the four it finds while the game runs: the two heaps and their descriptors
        found = sorted(r["id"] for r in kept if r["space"] == "found")
        self.assertEqual(found, ["face-board-heap", "face-board-heap-descriptor", "sound-heap", "sound-heap-descriptor"])
        # and that is all the routine writes: its count of ranges
        self.assertIn("    li 0, %d\n    stw 0, 0(12)\n" % len(kept), text)

    def test_what_the_image_keeps_is_paired_with_something(self):
        for r in rows():
            if r["within"] == "simulation-memory" and r["restored"] == "no":
                self.assertNotIn(r["paired"], ("none", "unknown"), r["id"])
                # no step reads it ("writes": a request to the outside that nothing of the race reads back)
                self.assertIn(r["step"], ("no", "writes"), r["id"])


if __name__ == "__main__":
    unittest.main()
