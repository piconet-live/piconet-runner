"""The start countdown's movie request (sites_countdown.py): in a race of ours, Starter's `bl ARQPostRequest` posts
only in a step simulated for the first time (control block +488 clear); in a plain start it is the game's call."""
import os, sys, unittest

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, ".."))
import build  # noqa: F401  (puts the generators' folder on the path)
import memmap
import sites_countdown


class Routines:
    def __init__(self):
        self.text = {}

    def add(self, name, text):
        self.text[name] = text


class K:
    TEXT = memmap.REL_TEXT

    @staticmethod
    def branch(site, target, link):
        return 0x48000000 | ((target - site) & 0x03FFFFFC) | (1 if link else 0)

    @staticmethod
    def branch_target(word, site):
        off = word & 0x03FFFFFC
        return (site + (off - 0x04000000 if off & 0x02000000 else off)) & 0xFFFFFFFF


class TheCountdownsRequest(unittest.TestCase):
    def test_the_site_and_its_routine(self):
        r, patched, middle = Routines(), [], []
        site = K.TEXT + sites_countdown.SITE
        stock = lambda address, known: known
        sites_countdown.add(K, r, lambda a, w, n, name: patched.append((a, w, name)), stock, "    test\n", middle, True)
        self.assertEqual(patched, [(site, K.branch(site, 0x800205A0, True), "starter_arq")])
        self.assertEqual(middle, [(site, ["r12", "cr0"], "starter_arq")])
        text = r.text["starter_arq"]
        # a plain start posts; in a race, a step simulated before (+488) goes back without the call
        self.assertIn("beq starter_arq_post", text)
        self.assertIn("lwz 12, 488(12)\n    cmpwi 12, 0\n    bne starter_arq_back", text)
        self.assertIn("starter_arq_post:\n    @bl 0x800205a0\nstarter_arq_back:\n    @b %#x" % (site + 4), text)


if __name__ == "__main__":
    unittest.main()
