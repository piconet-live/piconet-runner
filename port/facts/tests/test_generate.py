"""The generator's own rules: what a table may say, and that a stale generated file is found. No game, no build."""
import os
import sys
import tempfile
import unittest

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(here))
import generate  # noqa: E402

HEAD = "\t".join(generate.COLUMNS)


def row(name, value, group="game", of="", type="u32", source="a/b.cpp:1", meaning="a thing"):
    return "\t".join([name, group, value, of, type, "", source, meaning])


class Table(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.addCleanup(self.folder.cleanup)

    def load(self, *lines):
        path = os.path.join(self.folder.name, "game.tsv")
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write("\n".join([HEAD, *lines]) + "\n")
        return generate.load(path)

    def error(self, *lines):
        with self.assertRaises(generate.FactsError) as raised:
            self.load(*lines)
        return str(raised.exception)

    def test_values_expressions_and_addresses(self):
        rows = self.load(row("BASE", "0x81810000"), "# a comment", "", row("WORD", "40", of="BASE"),
                         row("END", "=BASE + WORD * 2"), row("LIST", "0x30C8", type="ptr"),
                         row("FIELD", "0x49C", of="LIST"), row("MARK", "'FZRP'", type="mark"))
        known = generate.values(rows)
        self.assertEqual(known["WORD"], 40)
        self.assertEqual(known["WORD_AT"], 0x81810028)
        self.assertEqual(known["END"], 0x81810050)
        self.assertNotIn("FIELD_AT", known)          # what a pointer leads to has no fixed address
        self.assertEqual(known["MARK_WORD"], 0x465A5250)
        self.assertEqual([r.shown for r in rows[:5]], ["0x81810000", "40", "0x81810050", "0x30C8", "0x49C"])

    def test_a_value_that_does_not_fit_its_type_names_its_line(self):
        self.assertIn("game.tsv:3: the value 256 (0x100) does not fit the type u8",
                      self.error(row("A", "1"), row("B", "256", type="u8")))
        self.assertIn("game.tsv:2: the value -1", self.error(row("A", "=0 - 1")))
        self.assertIn("game.tsv:2: the value 4294967296", self.error(row("A", "0x100000000")))
        self.assertIn("game.tsv:3:", self.error(row("A", "0xFFFFFFFF"), row("B", "4", of="A")))   # the address
        self.assertIn("game.tsv:2: the value 'FZRPX' is not a text", self.error(row("A", "'FZRPX'", type="mark")))

    def test_a_duplicate_name_names_both_lines(self):
        self.assertIn("game.tsv:4: duplicate name A (line 2)", self.error(row("A", "1"), row("B", "2"), row("A", "3")))
        self.assertIn("game.tsv:4: duplicate name B_AT (line 3)",
                      self.error(row("A", "0x80000000"), row("B_AT", "2"), row("B", "3", of="A")))
        self.assertIn("game.tsv:2: the name BLOCK_SKIP is the generator's", self.error(row("BLOCK_SKIP", "1")))

    def test_an_expression_over_an_unknown_name_names_its_line(self):
        self.assertIn("game.tsv:3: expression over an unknown name C", self.error(row("A", "1"), row("B", "=A + C")))
        self.assertIn("game.tsv:2: expression over an unknown name B", self.error(row("A", "=B + 1"), row("B", "1")))
        self.assertIn("game.tsv:2: cannot read the expression", self.error(row("A", "=__import__('os')")))
        self.assertIn("game.tsv:2: cannot read the expression", self.error(row("A", "1.5")))

    def test_the_other_rules_name_their_line(self):
        self.assertIn("game.tsv:2: 3 columns", self.error("A\tgame\t1"))
        self.assertIn("game.tsv:2: unknown group", self.error(row("A", "1", group="misc")))
        self.assertIn("game.tsv:2: unknown type", self.error(row("A", "1", type="u64")))
        self.assertIn("game.tsv:2: the name 'a'", self.error(row("a", "1")))
        self.assertIn("game.tsv:2: no meaning", self.error(row("A", "1", meaning="")))
        self.assertIn("game.tsv:2: the source 'somewhere'", self.error(row("A", "1", source="somewhere")))
        self.assertIn("game.tsv:2: the base B is not a number in a row above", self.error(row("A", "1", of="B"), row("B", "2")))
        self.assertIn("game.tsv:4: the base B has no address", self.error(
            row("L", "0x30C8", type="ptr"), row("B", "4", of="L"), row("A", "1", of="B")))
        with open(os.path.join(self.folder.name, "bad.tsv"), "w") as f:
            f.write("name\tvalue\n")
        with self.assertRaises(generate.FactsError):
            generate.load(f.name)

    @unittest.skipUnless(os.path.exists(generate.SCHEMA), "no protocol/relay.toml")
    def test_the_wire_rows_are_the_relay_schemas(self):
        """Every wire row but the exit codes (in no schema yet) takes its value from protocol/relay.toml, and the
        values are the protocol's: what game and relay were built with since 0.4.0 (relay version 3)."""
        rows = generate.load()
        wire = {r.name: r for r in rows if r.group == "wire"}
        self.assertEqual({n for n, r in wire.items() if not r.schema}, {"EXIT_RACE_OVER", "EXIT_LEFT_RACING"})
        self.assertTrue(all(r.source.startswith("protocol/relay.toml:") for r in wire.values() if r.schema))
        values = {n: r.value for n, r in wire.items()}
        self.assertEqual({k: values[k] for k in ("RELAY_VERSION", "WIRE_MARK", "HEADER_SIZE", "PACKET_OVER", "LEN_PONG",
                                                 "LEN_PONG_TIMED", "LEN_JOINED", "LEN_INPUT_HEAD", "LEN_OFFICIAL_HEAD",
                                                 "MAX_FRAMES", "AI_STEER", "JOINED_OVER")},
                         {"RELAY_VERSION": 3, "WIRE_MARK": "FZ", "HEADER_SIZE": 4, "PACKET_OVER": 13, "LEN_PONG": 12,
                          "LEN_PONG_TIMED": 16, "LEN_JOINED": 22, "LEN_INPUT_HEAD": 25, "LEN_OFFICIAL_HEAD": 9,
                          "MAX_FRAMES": 20, "AI_STEER": 0x80, "JOINED_OVER": 2})
        record = next(r for r in rows if r.name == "RECORD")   # the game's control record, typed: the schema's too
        self.assertEqual(record.value, generate.schema_names()["relay_record_size"])
        self.assertIn("game.tsv:2: expression over an unknown name relay_nothing",
                      self.error(row("A", "=relay_nothing", group="wire")))
        self.assertIn("game.tsv:2: =relay_version is no text of the relay schema",
                      self.error(row("A", "=relay_version", group="wire", type="mark")))

    def test_the_skip_masks_of_a_small_map(self):
        rows = self.load(row("MACHINE_SIZE", "0x30"), row("MACHINE_PROGRESS", "0x10", type="ptr"), row("PROGRESS_SIZE", "0x8"))
        path = os.path.join(self.folder.name, "map.csv")
        lines = ["region,offset,type,name,category,confidence,changes_in_race,net,mask_7200",
                 "block,0x000,u32,flags,STATE,code,yes,SEND,0", "block,0x004,f32,drawn,STATE,code,yes,VISUAL,0",
                 "block,0x00C,u32,state,STATE,code,yes,SEND,0", "block,0x010,ptr,progress,STATE,code,no,ONCE,0",
                 "block,0x014,u32,more,STATE,code,yes,SEND,0", "block,0x02A,u16,model,POINTER,code,no,ONCE,0",
                 "+0x010,0x000,u32,laps,STATE,code,yes,SEND,0", "+0x010,0x004,u32,shown,STATE,code,yes,VISUAL,0"]
        with open(path, "w") as f:
            f.write("\n".join(lines) + "\n")
        # words 1 and 2 (drawn, to the next row), 4 (a pointer by type), 10 and 11 (0x2A to the end, rounded out)
        self.assertEqual(generate.skip_masks(rows, path), {"BLOCK_SKIP": [0b0110_1000_0011 << 20], "PROGRESS_SKIP": [1 << 30]})
        with open(path, "w") as f:
            f.write("\n".join(lines[:1] + lines[2:]) + "\nblock,0x040,u32,beyond,STATE,code,yes,SEND,0\n")
        with self.assertRaises(generate.FactsError) as raised:
            generate.skip_masks(rows, path)
        self.assertIn("map.csv:6: offset 0x2A", str(raised.exception))


class Check(unittest.TestCase):
    """--check against a copy of the outputs in a folder of the test's own."""

    def setUp(self):
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        self.addCleanup(setattr, generate, "root", generate.root)
        self.addCleanup(setattr, sys, "stderr", sys.stderr)
        generate.root = folder.name
        sys.stderr = open(os.path.join(folder.name, "stderr"), "w+")
        self.addCleanup(sys.stderr.close)

    def said(self):
        sys.stderr.seek(0)
        return sys.stderr.read()

    def test_fresh_files_pass_and_a_changed_or_missing_one_fails(self):
        self.assertEqual(generate.main(["--check"]), 1)                      # nothing written yet
        self.assertEqual(generate.main([]), 0)
        self.assertEqual(generate.main(["--check"]), 0)
        header = os.path.join(generate.root, generate.OUTPUTS["cxx"])
        text = open(header, encoding="utf-8").read()
        with open(header, "w", encoding="utf-8", newline="\n") as f:
            f.write(text.replace("SEATS = 30;", "SEATS = 31;"))
        self.assertEqual(generate.main(["--check"]), 1)
        self.assertIn("stale (run port/facts/generate.py): " + generate.OUTPUTS["cxx"], self.said())
        os.remove(os.path.join(generate.root, generate.OUTPUTS["csharp"]))
        self.assertEqual(generate.main(["--check"]), 1)
        self.assertIn(generate.OUTPUTS["csharp"], self.said().splitlines()[-1])

    def test_an_unknown_argument_is_refused(self):
        self.assertEqual(generate.main(["--force"]), 2)
        self.assertEqual(generate.main(["--value", "NO_SUCH_NAME"]), 1)


if __name__ == "__main__":
    unittest.main()
