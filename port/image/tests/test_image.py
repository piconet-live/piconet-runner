"""Tests of the native form's generators (port/image): the liveness proof, the Gecko code reader, the image and the
patch list. They need keystone (the main checkout's .venv; scripts/check.sh runs them with it). No game data is in
the repository, so the bundle is built here from the generators' own tables ("incomplete"); with a full work
folder at hand (FZGX_WORK, by default port/setup/work-folder.env's) one more class builds the complete bundle from the
game's files and checks it against them."""
import json, os, struct, sys, tempfile, unittest

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, ".."))
sys.path.insert(0, os.path.join(here, "..", "routines"))
sys.path.insert(0, os.path.join(here, "..", "..", "setup"))
import build, liveness, memmap, relink, work_folder

IMAGE = memmap.IMAGE
word_at = lambda data, address, base=IMAGE: struct.unpack_from(">I", data, address - base)[0]


def target(word, at):
    disp = word & 0x03FFFFFC
    return (at + disp - (0x04000000 if disp & 0x02000000 else 0)) & 0xFFFFFFFF


class Liveness(unittest.TestCase):
    def prove(self, words, resource, start=0x1000):
        code = {start + 4 * i: w for i, w in enumerate(words)}
        return liveness.dead(code.get, start, resource)

    def test_a_write_before_any_read_is_dead(self):
        # li r12, 0; blr
        self.assertIs(self.prove([0x39800000, 0x4E800020], "r12"), True)
        # cmpwi r3, 0 writes CR field 0; beq reads it afterwards
        self.assertIs(self.prove([0x2C030000, 0x41820008, 0x4E800020, 0x4E800020], "cr0"), True)

    def test_a_read_is_named(self):
        # mr r3, r12
        self.assertIn("read at 00001000", self.prove([0x7D836378, 0x4E800020], "r12"))
        # beq reads CR field 0 before anything writes it
        self.assertIn("cr0: read", self.prove([0x41820008, 0x4E800020, 0x4E800020], "cr0"))

    def test_both_ways_of_a_branch_are_followed(self):
        # bne cr1, +8; li r12, 0; mr r3, r12 (reached with r12 unwritten when the branch is taken); blr
        self.assertIn("read at 00001008", self.prove([0x40860008, 0x39800000, 0x7D836378, 0x4E800020], "r12"))

    def test_calls_and_returns(self):
        call, ret = 0x48000101, 0x4E800020          # bl, blr
        self.assertIs(self.prove([call, ret], "r12"), True)        # a call leaves r12 without meaning
        self.assertIs(self.prove([call, ret], "cr0"), True)
        self.assertIs(self.prove([call, ret], "lr"), True)         # and writes LR
        self.assertIn("r3: read", self.prove([call, ret], "r3"))   # but reads its arguments
        self.assertIn("r0: read", self.prove([call, ret], "r0"))   # and a direct call may be a register save helper
        self.assertIs(self.prove([0x4E800421, ret], "r0"), True)   # bctrl: an ordinary function
        self.assertIs(self.prove([ret], "r12"), True)
        self.assertIn("lr: read", self.prove([ret], "lr"))         # a return needs LR
        self.assertIn("cr2", self.prove([ret], "cr2"))             # CR fields 2-4 belong to the caller

    def test_what_cannot_be_decided_is_not_proven(self):
        self.assertIn("not known", self.prove([0x00000000], "r12"))
        self.assertIn("indirect jump", self.prove([0x4E800420], "r12"))             # bctr
        self.assertIn("leaves the image", self.prove([0x60000000], "r12"))          # runs off the end
        loop = [0x60000000] * 700 + [0x4E800020]
        self.assertIn("no answer within", self.prove(loop, "r12"))

    def test_link_register(self):
        # mflr r0 reads LR; mtlr r0 writes it
        self.assertIn("lr: read", self.prove([0x7C0802A6, 0x4E800020], "lr"))
        self.assertIs(self.prove([0x7C0803A6, 0x4E800020], "lr"), True)


class GeckoLines(unittest.TestCase):
    def test_inserts_words_and_routines(self):
        rows = build.gecko(["$A code", "48000000 800030C8", "DE000000 80008180",
                            "D2000FA8 00000002", "38600001 60000000", "9421FFF0 00000000",
                            "14035E68 60830000", "E0000000 80008000",
                            "C2067898 00000001", "9421FFF0 00000000",
                            "C0000000 00000001", "4E800020 00000000", "041A63C0 00001234"])
        self.assertEqual(rows, [("insert", memmap.REL + 0xFA8, [0x38600001, 0x60000000, 0x9421FFF0]),
                                ("word", memmap.REL + 0x35E68, [0x60830000]),
                                ("insert", 0x80067898, [0x9421FFF0]),
                                ("routine", 0, [0x4E800020, 0]),
                                ("word", 0x801A63C0, [0x1234])])

    def test_branch_words(self):
        self.assertEqual(build.branch(0x801BB608, 0x8180369C), 0x49648094)
        self.assertEqual(build.branch(0x1000, 0x0FF0, True), 0x4BFFFFF1)
        self.assertEqual(build.branch_target(0x4807BBB1, 0x801C51C0), 0x80240D70)
        with self.assertRaises(AssertionError):
            build.branch(0x80000000, 0x83000000)


class Bundle(unittest.TestCase):
    """The bundle built without the game's files."""

    @classmethod
    def setUpClass(cls):
        cls.dir = tempfile.TemporaryDirectory()
        cls.b = build.build(cls.dir.name)
        cls.image, cls.symbols = cls.b["image"], cls.b["symbols"]
        cls.patches = {int(p["address"], 16): p for p in cls.b["patches"]}

    @classmethod
    def tearDownClass(cls):
        cls.dir.cleanup()

    def test_files(self):
        manifest = json.loads(build.read(os.path.join(self.dir.name, "manifest.json"), "r"))
        listed = json.loads(build.read(os.path.join(self.dir.name, "patches.json"), "r"))
        self.assertEqual((manifest["form"], manifest["complete"], listed["complete"]), ("native", False, False))
        self.assertEqual(manifest["image_sha1"], listed["image_sha1"])
        self.assertEqual(build.read(os.path.join(self.dir.name, "mod.bin")), self.image)
        self.assertEqual(len(listed["patches"]), len(self.patches))        # no address twice

    def test_the_image(self):
        image = self.image
        self.assertEqual(image[:4], b"FZMD")
        self.assertEqual(struct.unpack_from(">II", image, 4), (2, len(image)))
        self.assertEqual(image[0x10:0x14], b"FZSI")                         # the slot the host writes
        for mark in (b"FZFR", b"FZNT", b"FZPT"):
            self.assertEqual(image.count(mark), 1)
        self.assertLessEqual(IMAGE + len(image), memmap.CONTROL)
        self.assertEqual(len(image) % 4, 0)

    def test_host_calls_have_nothing_behind_them(self):
        for name in ("host_net", "host_tick", "host_linked"):
            self.assertGreaterEqual(self.symbols[name], memmap.INPUTS_END)   # outside the data window and the image
        # the network hook is a host call's address, no longer the image's own word
        words = [struct.unpack_from(">I", self.image, o)[0] for o in range(0, len(self.image), 4)]
        lis, ori = 0x3D800000 | memmap.hi(memmap.HOST_NET), 0x618C0000 | memmap.lo(memmap.HOST_NET)
        self.assertTrue(any(a == lis and b == ori for a, b in zip(words, words[1:])))
        old = (0x3D800000 | memmap.hi(IMAGE), 0x618C000C)
        self.assertFalse(any((a, b) == old for a, b in zip(words, words[1:])))

    def test_every_patch_enters_the_image(self):
        self.assertEqual(len(self.patches), 317)   # 317: the portraits' mark, the leader's gap popup in the step, and the mode words' ten since 305; 305: mode_monitors; 286: the simulation's own generator (2), the mode's sites from the board, display and race words; 250: the game mode's displays' three (sites_modehud.py); 247: the game mode's pair gate and timer; 245: the game mode's three (sites_mode.py); 242: the machine setting's curve (54 and its 2 calls), the boost length
        executable = [a for a in self.patches if a < memmap.REL]
        # the last: frame begin's copy to the display buffer (sites_present.py)
        self.assertEqual(sorted(executable), [0x80005940, 0x800059A8, 0x80067898, 0x8006FDA4])
        entered = set()
        for address, p in self.patches.items():
            new = int(p["new"], 16)
            if p["what"] == "pin":
                self.assertEqual(new, 0x38600000)
                continue
            self.assertEqual(new >> 26, 18)                                  # a branch
            to = target(new, address)
            self.assertTrue(IMAGE + 0x10 < to < IMAGE + len(self.image), p)
            entered.add(to)
            installer_site = p["what"].split("_")[0] in ("fo", "bo", "eo", "vo", "oso", "pno", "spo") or p["what"] == "smooth_hook"
            self.assertEqual(new & 1, 1 if installer_site else 0, p)         # the fork's helpers are called; routines are entered
            if not installer_site:
                self.assertEqual(to, self.symbols[p["what"]])
        # every routine entered by a plain branch goes back to the instruction after its site, or, where the site's
        # own word was a plain branch (true_aspect, sites_view.py), to that branch's target
        back = set()
        start = self.image.index(b"FZPT") + 4
        for o in range(start, len(self.image), 4):
            w = struct.unpack_from(">I", self.image, o)[0]
            if w >> 26 == 18 and not w & 3:
                back.add(target(w, IMAGE + o))
        for address, p in self.patches.items():
            if p["what"] != "pin" and not int(p["new"], 16) & 1:
                original = int(p["original"] or "0", 16)                     # (None: a word the build did not read)
                plain = original >> 26 == 18 and not original & 3
                self.assertIn(target(original, address) if plain else address + 4, back, p)

    def test_routines_test_the_mode_word_first(self):
        test = (0x3D800000 | memmap.hi(memmap.CONTROL), 0x818C0000 | (memmap.MODE - memmap.CONTROL))
        # every start; a plain word; tests its own word (smooth.py); reached only after mine_shown, which tests it;
        # the simulation's own generator (sites_generator.py), whose shift word is 0 in a plain start: the stock word
        untested = {"linked", "pin", "smooth_hook", "mine_fade", "simgen_cpu", "simgen_hazard"}
        for address, p in self.patches.items():
            # a `bl mode_sort` first, which tests the mode's own flag word (sites_modehud.py), and then the stock call
            if p["what"] in untested or p["what"].startswith(("mode_sort_", "mode_clock_")):
                continue
            to = target(int(p["new"], 16), address)
            # the draw skip's routines (sites_present.py) load the word into r11 and keep r12 the control block;
            # the leader's state (sites_cpu.py) into r3, the register dead where it goes back to
            rd = 11 if p["what"].startswith("present_") else 3 if p["what"] == "cpu_leader" else 12
            ra = 3 if rd == 3 else 12
            expect = (test[0] - (12 - ra) * 0x200000, test[1] - (12 - rd) * 0x200000 - (12 - ra) * 0x10000)
            self.assertEqual((word_at(self.image, to), word_at(self.image, to + 4)), expect, p)
            compare = word_at(self.image, to + 8)
            self.assertEqual(compare & 0xFC7FFFF8, 0x2C000000 | rd << 16, p)  # cmpwi crN, rD, 0 or 2

    def test_installer_sites(self):
        sites = [p for p in self.patches.values() if int(p["new"], 16) & 1]
        # follow 44, banners 4, effects 1, the racing view 4, other sounds 6, names 4, smoothing 1, the seat's port 47
        self.assertEqual(len(sites), 112)
        # the installers left in the frame routine compare a site with its stock word before writing: with the
        # site already patched they write nothing
        for p in sites:
            if p["original"] is not None:
                self.assertNotEqual(p["original"], p["new"])

    def test_incomplete_without_the_game(self):
        unknown = [p for p in self.patches.values() if p["original"] is None]
        # two masked sites of follow.py (a relocated half), the two volume calls of othersound.py, the two camera calls
        self.assertEqual(len(unknown), 6, unknown)

    def test_the_entry_table(self):
        # the routines entered through a pointer or a plain branch are call targets somewhere in the image
        called = set()
        for o in range(0, len(self.image), 4):
            w = struct.unpack_from(">I", self.image, o)[0]
            if w >> 26 == 18 and w & 3 == 1:
                called.add(target(w, IMAGE + o))
        for name in ("seat_routine", "race_over", "rank_refs", "frame_routine", "name_tags", "mode_machine",
                     "boot_race", "seats", "sound_queue", "rumble", "cpu_seed",
                     "frame", "tags", "linked", "results_preload", "results_load", "results_3b770"):
            self.assertIn(self.symbols[name], called, name)

    def test_the_results_pictures(self):
        # fn_1_3B11C's three loads: in a plain start the game's own call and back; otherwise the image's loader,
        # which leaves for the game's own (the disc) whenever it has no packed file for the group
        image, text = self.image, memmap.REL_TEXT
        game_load, worker = text + 0x48004, self.symbols["results_load"]
        for offset in build.RESULTS_SITES:
            site = text + offset
            p = self.patches[site]
            self.assertEqual(int(p["original"], 16), build.branch(site, game_load, True))
            entry = target(int(p["new"], 16), site)
            self.assertEqual(word_at(image, entry + 12) >> 16, 0x4082)                       # bne: not a plain start
            self.assertEqual(word_at(image, entry + 16), build.branch(entry + 16, game_load, True))
            self.assertEqual(word_at(image, entry + 20), build.branch(entry + 20, site + 4))
            self.assertEqual(word_at(image, entry + 24), build.branch(entry + 24, worker, True))
            self.assertEqual(word_at(image, entry + 28), build.branch(entry + 28, site + 4))
        # the worker's ways out: fn_1_48004 itself, a return (the group is loaded), fn_1_47AD4's second half
        end = self.symbols["results_3b770"]
        words = {a: word_at(image, a) for a in range(worker, end, 4)}
        jumps = {target(w, a) for a, w in words.items() if w >> 26 == 18 and not w & 3}
        self.assertEqual(jumps, {game_load, text + 0x47D58})
        self.assertEqual(words[end - 4], build.branch(end - 4, game_load))
        # the files are read from the frame routine's wrapper, never in a plain start: the call is after its `bne`
        frame, preload = self.symbols["frame"], self.symbols["results_preload"]
        on = frame + 12 + (word_at(image, frame + 12) & 0xFFFC)
        # (the mines' countdowns first, sites_mines.py: they then lie at the very top of the heap's free block)
        self.assertEqual(word_at(image, on), build.branch(on, self.symbols["mines_preload"], True))
        on += 4
        # (then the game mode's block, sites_mode.py, below the mines' when the race has a mode)
        self.assertEqual(word_at(image, on), build.branch(on, self.symbols["mode_preload"], True))
        on += 4
        # (then the story's pictures a mode's displays ask for, sites_modehud.py, once the block is taken)
        self.assertEqual(word_at(image, on), build.branch(on, self.symbols["mode_story"], True))
        on += 4
        self.assertEqual(word_at(image, on), build.branch(on, preload, True))
        self.assertEqual(word_at(image, on + 4), build.branch(on + 4, self.symbols["monitors_preload"], True))
        self.assertEqual(word_at(image, on + 8), build.branch(on + 8, self.symbols["frame_routine"], True))
        calls = [a for a in range(self.symbols["mode_machine"], IMAGE + len(image), 4)
                 if word_at(image, a) == build.branch(a, preload, True)]
        self.assertEqual(len(calls), 2)                                  # this one and the entry table's
        self.assertEqual(calls[0], on)
        self.assertGreaterEqual(calls[1], self.symbols["entry_table"])
        # it tests its own word and the game's (the mode, the phase, the intro's count) before anything else
        self.assertEqual(word_at(image, preload + 4), 0x800C0000 | (memmap.PRELOAD - memmap.CONTROL))   # lwz r0, state(r12)
        self.assertEqual(word_at(image, preload + 12), 0x4C820020)                                        # bnelr

    def test_no_site_of_the_retired_replay_mode(self):
        # a replay plays as the race: the replay mode's boot, its data and three words, and the eight stores of a
        # machine's viewport slot it left out (fn_1_6BC0, fn_1_DA6C) are the stock game's again, and no routine
        # tests for the mode word 2
        text = memmap.REL_TEXT
        for offset in (0x3696C, 0x35D8C, 0x373D4, 0x320D4,
                       0x6C88, 0x6C98, 0xDAFC, 0xDB48, 0xDBBC, 0xDC08, 0xDC88, 0xDCD4):
            self.assertNotIn(text + offset, self.patches, hex(offset))
        for name in ("boot_replay", "replay_data", "camera_mark_dcd4", "replay_word_35d8c"):
            self.assertNotIn(name, self.symbols)

    def test_the_forks_bundle_is_untouched(self):
        # without FZGX_NATIVE the generators build the fork's image: the network hook is the image's word at +0x0C
        out = os.path.join(self.dir.name, "fork.bin")
        build.run(os.path.join(here, "..", "routines", "modbuild.py"), out, "0", "0", "1", "0", "2")
        image = build.read(out)
        self.assertEqual(struct.unpack_from(">I", image, 4)[0], 1)
        self.assertNotIn(b"FZPT", image)
        words = [struct.unpack_from(">I", image, o)[0] for o in range(0, len(image), 4)]
        self.assertTrue(any((a, b) == (0x3D800000 | memmap.hi(IMAGE), 0x618C000C) for a, b in zip(words, words[1:])))
        self.assertFalse(os.path.exists(out + ".layout.json"))


WORK = os.environ.get("FZGX_WORK", work_folder.WORK_DEFAULT)
REL, DOL = os.path.join(WORK, "derived", "main.rel"), os.path.join(WORK, "extracted", "GFZE01", "sys", "main.dol.orig")


@unittest.skipUnless(os.path.exists(REL) and os.path.exists(DOL), "the game's files are not at hand")
class CompleteBundle(unittest.TestCase):
    """The bundle built from the game's files (never in the repository)."""

    @classmethod
    def setUpClass(cls):
        cls.dir = tempfile.TemporaryDirectory()
        cls.b = build.build(cls.dir.name, REL, DOL)
        cls.clean, _ = relink.relocate(build.read(REL), memmap.REL, memmap.REL_BSS)
        cls.dol = build.Dol(build.read(DOL))

    @classmethod
    def tearDownClass(cls):
        cls.dir.cleanup()

    def test_every_original_is_the_games_word(self):
        self.assertTrue(self.b["complete"])
        for p in self.b["patches"]:
            address, original = int(p["address"], 16), int(p["original"], 16)
            stock = self.dol.word(address) if address < memmap.REL else word_at(self.clean, address, memmap.REL)
            self.assertEqual(original, stock, p)
            self.assertNotEqual(p["new"], p["original"])

    def test_the_installers_write_nothing_and_every_site_has_its_entry(self):
        # The native form's installers keep their guard word and have no site code (follow.installer): a site whose
        # table entry is masked ("any bl") would take the list's own `bl` for the stock call and write over it, and
        # the piece of the game around it would leave native code. So the image writes code in one place only
        # (one `icbi`): the smoothing installer's, which compares its site exactly and finds it patched.
        image = self.b["image"]
        words = [struct.unpack_from(">I", image, o)[0] for o in range(0, len(image), 4)]
        self.assertEqual(sum(w == 0x7C004FAC for w in words), 1)        # icbi 0, r9: a write of code
        # Every site the installers patched on the fork is a `bl` of the list to an entry of its own: the mode test
        # (lis, lwz, cmpwi) and a conditional branch to the fork's helper, inside the image. The entries are placed
        # by counting lines (native.label_offsets); read back from the image they must be where the list points.
        mode = memmap.MODE - memmap.CONTROL
        entries = 0
        for p in self.b["patches"]:
            address, new = int(p["address"], 16), int(p["new"], 16)
            if new >> 26 != 18 or not new & 1 or p["what"] == "smooth_hook":
                continue
            entry = target(new, address)
            self.assertEqual(word_at(image, entry) >> 16, 0x3D80, p)                 # lis r12
            self.assertEqual(word_at(image, entry + 4), 0x818C0000 | mode, p)        # lwz r12, MODE(r12)
            self.assertEqual(word_at(image, entry + 8) & 0xFC7FFFFF, 0x2C0C0000, p)  # cmpwi crN, r12, 0
            bc = word_at(image, entry + 12)
            self.assertEqual(bc >> 26, 16, p)
            disp = ((bc & 0xFFFC) ^ 0x8000) - 0x8000
            self.assertTrue(IMAGE <= entry + 12 + disp < IMAGE + len(image), p)
            entries += 1
        self.assertEqual(entries, 111)

    def test_the_stock_path_is_the_replaced_instruction(self):
        image = self.b["image"]
        for p in self.b["patches"]:
            new, address, original = int(p["new"], 16), int(p["address"], 16), int(p["original"], 16)
            if not new & 1 or p["what"] == "smooth_hook":
                continue
            stock = word_at(image, target(new, address) + 16)       # after lis, lwz, cmpwi, bne
            if original >> 26 == 18:                                 # a call: its target, loaded for a jump
                to = target(original, address)
                self.assertEqual(stock, 0x3D800000 | (to >> 16), p)
            elif original >> 26 == 16:                               # a conditional branch: the same condition
                self.assertEqual(stock & 0xFFFF0000, original & 0xFFFF0000, p)
            else:
                self.assertEqual(stock, original, p)

    def test_the_recompilers_input_holds_the_patched_game(self):
        import gameinput
        patches = [(int(p["address"], 16), int(p["original"], 16), int(p["new"], 16), p["what"]) for p in self.b["patches"]]
        stock = build.read(DOL)
        dol = gameinput.patched_dol(stock, patches)
        self.assertEqual(len(dol), len(stock))
        changed = [o for o in range(0, len(dol), 4) if dol[o:o + 4] != stock[o:o + 4]]
        self.assertEqual(len(changed), 4)                           # the executable's four words, nothing else
        self.assertEqual(gameinput.patched_dol(dol, patches), dol)  # a copy that has them already is accepted
        out = gameinput.combined(stock, build.read(REL), self.b["image"], patches)
        sections = {a: (o, size) for a, o, size in gameinput.sections(out)}
        self.assertEqual(sections[memmap.REL_TEXT][1], 0x15F62C)
        self.assertEqual(sections[IMAGE][1], len(self.b["image"]))
        o, size = sections[IMAGE]
        self.assertEqual(out[o:o + size], self.b["image"])
        o, size = sections[memmap.REL_TEXT]
        text = out[o:o + size]
        differing = {memmap.REL_TEXT + i for i in range(0, size, 4)
                     if text[i:i + 4] != self.clean[0xDC + i:0xDC + i + 4]}
        self.assertEqual(differing, {a for a, _, _, _ in patches if a >= memmap.REL})
        for a, _, new, _ in patches:
            if a >= memmap.REL:
                self.assertEqual(word_at(text, a, memmap.REL_TEXT), new)
        # a word that is neither stock nor patched stops the build
        broken = bytearray(stock)
        struct.pack_into(">I", broken, 0x100, 0)
        first = min(a for a, _, _, _ in patches)
        offset = next(o + first - a for a, o, size in gameinput.sections(stock) if a <= first < a + size)
        struct.pack_into(">I", broken, offset, 0x12345678)
        with self.assertRaises(SystemExit):
            gameinput.patched_dol(bytes(broken), patches)

    def test_relinking_gives_the_dump_of_the_running_game(self):
        dump = os.path.join(WORK, "derived", "mainrel-ram-801ba660.bin")
        if not os.path.exists(dump):
            self.skipTest("no dump of main.rel from memory")
        ram = build.read(dump)
        differing = [o for o in range(0xDC, 0xDC + 0x15F62C, 4) if ram[o:o + 4] != self.clean[o:o + 4]]
        # the dump was taken with two of the older Gecko codes in: fn_1_ECC's first word, fn_1_F47AC's first three
        self.assertEqual(differing, [0xFA8, 0xF4888, 0xF488C, 0xF4890])


if __name__ == "__main__":
    unittest.main()
