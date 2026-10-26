"""port/build/same_race.py: which released builds' replays an image refuses, on made-up tables; and the
repository's own table, read as the launcher and package.py read it."""
import hashlib, json, os, subprocess, sys, tempfile, unittest

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.abspath(os.path.join(here, "..", "..", ".."))
sys.path.insert(0, os.path.join(root, "port", "build"))
import same_race

OLD, NEW, OTHER = "a" * 40, "b" * 40, "c" * 40


def table(rows=(), released=()):
    return {"version": 1, "rows": [{"recorded": r, "plays_on": p, "evidence": "a run"} for r, p in rows],
            "released": [{"version": v, "image": i} for v, i in released]}


class Refused(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)

    def load(self, content):
        path = os.path.join(self.tmp.name, "same-race.json")
        with open(path, "w") as f:
            f.write(content if isinstance(content, str) else json.dumps(content))
        return same_race.load(path)

    def test_a_released_image_with_no_row_to_the_new_one_is_refused(self):
        pairs, builds = self.load(table(released=[("0.2.0", OLD)]))
        self.assertEqual(same_race.refused(pairs, builds, NEW), [("0.2.0", OLD)])

    def test_a_row_from_it_or_the_same_image_plays(self):
        pairs, builds = self.load(table(rows=[(OLD, NEW)], released=[("0.2.0", OLD), ("0.2.1", OLD), ("0.3.0", NEW)]))
        self.assertEqual(same_race.refused(pairs, builds, NEW), [])
        self.assertEqual(same_race.refused(pairs, builds, NEW.upper()), [])

    def test_a_row_the_other_way_or_to_another_image_does_not(self):
        pairs, builds = self.load(table(rows=[(NEW, OLD), (OLD, OTHER)], released=[("0.2.0", OLD)]))
        self.assertEqual(same_race.refused(pairs, builds, NEW), [("0.2.0", OLD)])

    def test_rows_are_not_followed_through_another_image(self):
        # the table's rule: a row comes from a run, never from reasoning (old plays on other, other on new: not old on new)
        pairs, builds = self.load(table(rows=[(OLD, OTHER), (OTHER, NEW)], released=[("0.1.0", OLD)]))
        self.assertEqual(same_race.refused(pairs, builds, NEW), [("0.1.0", OLD)])

    def test_a_table_without_its_lists_or_with_a_broken_member_is_not_read(self):
        for bad in ({"version": 1}, {"rows": [], "released": [{"version": "0.2.0"}]}, {"rows": [{"recorded": OLD}], "released": []}):
            with self.assertRaises(ValueError):
                self.load(bad)

    def test_the_command_names_each_refused_build_by_the_image_file(self):
        image = os.path.join(self.tmp.name, "mod.bin")
        with open(image, "wb") as f:
            f.write(b"image words")
        ours = hashlib.sha1(b"image words").hexdigest()
        path = os.path.join(self.tmp.name, "t.json")
        for content, status, out in ((table(released=[("0.2.0", OLD)]), 1, f"0.2.0 {OLD}\n"),
                                     (table(rows=[(OLD, ours)], released=[("0.2.0", OLD)]), 0, ""),
                                     ({"version": 1}, 2, "")):
            with open(path, "w") as f:
                json.dump(content, f)
            done = subprocess.run([sys.executable, os.path.join(root, "port", "build", "same_race.py"), "refused", image, path],
                                  capture_output=True, text=True)
            self.assertEqual((done.returncode, done.stdout), (status, out), done.stderr)


class TheRepositorysTable(unittest.TestCase):
    """The table the launcher is given: readable, every released build named, every row two SHA-1s with evidence."""

    def test_it_is_read_and_names_the_released_builds(self):
        pairs, builds = same_race.load()
        self.assertTrue(pairs)
        self.assertIn("0.2.0", [v for v, _ in builds])
        for recorded, plays_on in pairs:
            self.assertRegex(recorded + plays_on, r"^[0-9a-f]{80}$")
        for version, image in builds:
            self.assertRegex(image, r"^[0-9a-f]{40}$", version)

    def test_every_row_says_its_evidence(self):
        with open(same_race.TABLE, encoding="utf-8") as f:
            for row in json.load(f)["rows"]:
                self.assertTrue(str(row.get("evidence", "")).strip(), row)


if __name__ == "__main__":
    unittest.main()
