"""The proof: every value the facts table generates equals the constant in today's sources that it will replace.
Each side is read at test time (places.py), so a change of either fails here. Also: the generated header's layout
namespace opens with the names the core's layout.h had; the manifest holds the same values; checksum.cpp takes the
checksum's skip masks from the header, and they equal the ones netframe.py computes. No game, no build."""
import ast
import csv
import os
import re
import shutil
import subprocess
import sys
import tempfile
import tomllib
import unittest
import warnings

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
import places  # noqa: E402
from places import generate, root  # noqa: E402

CHECKSUM = "port/core/src/checksum.cpp"
MANIFEST = "recomp/manifest.toml"


def read(path):
    with open(os.path.join(root, path), encoding="utf-8", newline="") as f:
        return f.read()


class Proof(unittest.TestCase):
    maxDiff = None

    @classmethod
    def setUpClass(cls):
        cls.rows = generate.load()
        cls.table = generate.values(cls.rows)
        cls.masks = generate.skip_masks(cls.rows)
        cls.made = places.generated_values()

    def test_the_generated_files_are_current(self):
        for path, text in generate.generated().items():
            self.assertEqual(read(path), text, f"{path} is stale: run port/facts/generate.py")

    def test_every_generated_file_says_what_the_table_says(self):
        """Read back from the files, as a compiler would: the header and the module hold every name, the C# class
        its three groups."""
        everything = {**self.table, **self.masks}
        self.assertEqual(self.made["cxx"], {k: v for k, v in everything.items() if self.in_header(k)})
        self.assertEqual(self.made["python"], {k: v for k, v in everything.items() if not k.endswith("_WORD")})
        sharp = {generate.pascal(r.name): r.value for r in self.rows if generate.GROUPS[r.group][1]}
        self.assertEqual(self.made["csharp"], sharp)
        self.assertEqual({r.group for r in self.rows if generate.GROUPS[r.group][1]}, {"limit", "wire", "file"})

    def in_header(self, name):
        owner = next((r for r in self.rows if name in (r.name, r.name + "_AT", r.name + "_WORD")), None)
        return owner is None or generate.GROUPS[owner.group][0] is not None

    def test_every_row_equals_the_constants_it_will_replace(self):
        problems, found = places.check(self.rows)
        self.assertEqual(problems, [])
        # (a row whose source is in an optional part this checkout has not, the gates, is proved where they are)
        self.assertTrue(all(found[r.name] or places.left_out(r.source.rpartition(":")[0]) for r in self.rows if not r.schema))

    def test_a_change_of_either_side_is_found(self):
        """The proof is not blind: a changed number in a source, by name and by pattern, and a changed generated
        value each fail the comparison."""
        made = places.generated_values
        self.addCleanup(places._constants.clear)
        self.addCleanup(setattr, places, "generated_values", made)
        words = "port/core/src/net_internal.h"
        playback, frame = "port/core/src/playback.cpp", "port/core/src/frame.cpp"
        for file, old, new, said in [
                (words, "GO = CONTROL + 40;", "GO = CONTROL + 44;", "says 0x8181002C, the table's GO_AT is 0x81810028"),
                (words, "u32 delay = 2;", "u32 delay = 3;", "says 0x3, the table's INPUT_DELAY is 0x2"),
                (frame, "module + PHASE) == 0x29", "module + PHASE) == 0x2A", "says 0x2A, the table's PHASE_RACE is 0x29"),
                (frame, "AI_DRIVES = {0, 0x80,", "AI_DRIVES = {0, 0x81,", "says 0x81, the table's AI_STEER is 0x80"),
                (playback, "g.Write32(CONTROL + 248, 0);", "g.Write32(SPECTATE, 0);", "has 0 matches")]:
            kept = places.text_of(file)
            self.addCleanup(places._texts.__setitem__, file, kept)
            self.assertIn(old, kept)
            places._texts[file] = kept.replace(old, new)
            places._constants.clear()
            problems, _ = places.check(self.rows)
            self.assertTrue(any(said in p for p in problems), (said, problems[:3]))
            places._texts[file] = kept
            places._constants.clear()
        changed = made()
        changed["cxx"]["SEATS"] += 1
        changed["csharp"]["NameShown"] = 17
        places.generated_values = lambda: changed
        problems, _ = places.check(self.rows)
        self.assertTrue(any("says 0x1E, the table's SEATS is 0x1F" in p for p in problems))
        self.assertTrue(any("net/src/Online.Protocol/PlayerNames.cs" in p and "the table's NAME_SHOWN is 0x11" in p
                            for p in problems))

    def test_the_layout_namespace_opens_with_the_names_layout_h_had(self):
        """The core says layout::NAME by this header since its own layout.h went: the 40 names that file had open the
        namespace, in its order (layout-today.tsv keeps them), but for the three of the replay mode's lineup."""
        with open(generate.LAYOUT_TODAY, encoding="utf-8") as f:
            names = [line.split("\t")[0] for line in f.read().split("\n")[1:] if line.strip()]
        header = read(generate.OUTPUTS["cxx"])
        self.assertEqual(header.count("\nnamespace fzgx::layout\n"), 1)
        block = header[header.index("\nnamespace fzgx::layout\n"):header.index("}  // namespace fzgx::layout")]
        self.assertEqual(re.findall(r"inline constexpr std::uint32_t (\w+) =", block)[:len(names)], names)
        self.assertEqual(len(names), 37)

    @unittest.skipUnless(shutil.which("g++"), "no g++")
    def test_a_compiler_reads_the_header_through_the_cores_own(self):
        """The header compiles as the core is compiled, taken in the way the core takes it (fzgx/guest.h), and a
        second time by its own name: one file, read once."""
        lines = ["#include <cstdint>", '#include "fzgx/guest.h"', f'#include "{os.path.basename(generate.OUTPUTS["cxx"])}"',
                 "static_assert(fzgx::layout::WINDOW == fzgx::layout::IMAGE && fzgx::layout::WINDOW_END == fzgx::layout::SEAT_BLOCK + fzgx::layout::SEAT_BLOCK_SIZE);",
                 "static_assert(fzgx::game::BLOCK_SKIP.size() == 13 && fzgx::game::PROGRESS_SKIP.size() == 4);",
                 "static_assert(sizeof(fzgx::file::REPLAY_MARK) == 5 && fzgx::limits::SEATS == 30);", ""]
        with tempfile.TemporaryDirectory() as folder:
            with open(os.path.join(folder, "facts_check.cpp"), "w") as f:
                f.write("\n".join(lines))
            done = subprocess.run(["g++", "-std=c++20", "-Wall", "-Wextra", "-Werror", "-fsyntax-only",
                                   "-I", os.path.join(root, "port/core/include"),
                                   "-I", os.path.join(root, os.path.dirname(generate.OUTPUTS["cxx"])), f.name],
                                  capture_output=True, text=True)
        self.assertEqual((done.returncode, done.stderr), (0, ""))

    def test_the_manifest_holds_the_same_values(self):
        with open(os.path.join(root, MANIFEST), "rb") as f:
            manifest = tomllib.load(f)
        checked = 0
        for r in self.rows:
            for key in r.manifest:
                self.assertEqual(places.manifest_value(manifest, key), r.value, f"game.tsv:{r.line}: {r.name} and {MANIFEST} {key}")
                checked += 1
        self.assertGreaterEqual(checked, 19)

    def test_the_checksum_takes_its_skip_masks_from_the_header(self):
        """checksum.cpp types no mask any more: it names the generated ones, in the hash and in InChecksum, and the
        generated ones are the map's."""
        text = places.without_comments(read(CHECKSUM))
        self.assertEqual(re.findall(r"\b\w*_SKIP\s*=", text), [])
        for name in ("BLOCK_SKIP", "PROGRESS_SKIP"):
            self.assertEqual(len(places.uses(CHECKSUM, "game::" + name, self.masks[name])), 2, name)
        self.assertEqual([len(self.masks["BLOCK_SKIP"]), len(self.masks["PROGRESS_SKIP"])], [13, 4])
        for kind in ("cxx", "python"):
            self.assertEqual({k: self.made[kind][k] for k in self.masks}, self.masks)

    def test_the_checksums_own_numbers_are_what_they_were(self):
        """The three numbers checksum.cpp writes as what they are, where they stood in hexadecimal: a mask's top bit,
        and FNV-1a's offset basis and prime. A change of one changes every checksum of every race and recording."""
        self.assertEqual({name: places.cxx_value(CHECKSUM, name)[0] for name in ("FIRST_WORD", "FNV_OFFSET_BASIS", "FNV_PRIME")},
                         {"FIRST_WORD": 0x80000000, "FNV_OFFSET_BASIS": 0x811C9DC5, "FNV_PRIME": 0x01000193})

    def test_the_skip_masks_equal_the_ones_netframe_computes(self):
        """netframe.py's own statements (its table, skipmask(), the two calls), run without the rest of the file,
        which needs the assembler."""
        path = places.full("netframe.py")
        wanted = []
        for statement in ast.parse(places.text_of("netframe.py")).body:
            names = {t.id for t in ast.walk(statement) if isinstance(t, ast.Name) and isinstance(t.ctx, ast.Store)}
            if (isinstance(statement, ast.FunctionDef) and statement.name == "skipmask") or (
                    isinstance(statement, ast.Assign) and names & {"rows", "BLOCK", "PROG"}):
                wanted.append(statement)
        self.assertEqual(len(wanted), 4)
        space = {"csv": csv, "os": os, "__file__": path}
        with warnings.catch_warnings():
            warnings.simplefilter("ignore", ResourceWarning)   # its own open() of the map
            exec(compile(ast.Module(wanted, []), path, "exec"), space)
        self.assertEqual({"BLOCK_SKIP": space["BLOCK"], "PROGRESS_SKIP": space["PROG"]}, self.masks)

    def test_the_state_map_is_whole(self):
        """The map is here alone (piconet-docs' research/machine-state-map.md points at it): every row of it."""
        with open(generate.STATE_MAP, newline="") as f:
            self.assertEqual(sum(1 for _ in f), 847)


if __name__ == "__main__":
    unittest.main()
