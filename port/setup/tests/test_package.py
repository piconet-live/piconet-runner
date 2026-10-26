"""port/build/package.py on made-up parts: what a copy holds, that the same parts give the same zip, and every
refusal with its offending path. No real part, no work folder, no Windows tool: the "published launcher" is a folder
of small files, the copy's program a small file that names the launcher's place as a host file does, the emulator's
source and the checkout are repositories made here, a junction is a symbolic link."""
import hashlib, json, os, shutil, subprocess, sys, tempfile, unittest, zipfile

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.abspath(os.path.join(here, "..", "..", ".."))
PACKAGE = os.path.join(root, "port", "build", "package.py")
sys.path.insert(0, os.path.join(root, "port", "build"))
import manifest
FORKS, IMAGE = manifest.layout()["REPO_FORKS"], manifest.layout()["REPO_IMAGE"]      # where pins.tsv says they are
DISC = bytes(28) + b"\xc2\x33\x9f\x3d" + bytes(64)          # a GameCube disc image's mark, under any name


def sha1(data):
    return hashlib.sha1(data).hexdigest()


def write(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data if isinstance(data, bytes) else data.encode())
    return path


def read(path):
    with open(path, "rb") as f:
        return f.read()


class Parts(unittest.TestCase):
    """A checkout, an emulator's source tree and every part of a copy, all made up and all in order."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.at = lambda *names: os.path.join(self.tmp.name, *names)
        clean = {k: v for k, v in os.environ.items() if not k.startswith("GIT_")}       # a hook's own must not reach these
        clean["PATH"] = os.pathsep.join(p for p in clean.get("PATH", "").split(os.pathsep) if not p.startswith("/mnt/"))   # no Windows side: its user is not asked
        self.env = {**clean, "GIT_CONFIG_GLOBAL": os.devnull, "GIT_CONFIG_NOSYSTEM": "1", "GIT_AUTHOR_NAME": "Test",
                    "GIT_AUTHOR_EMAIL": "t@example.com", "GIT_COMMITTER_NAME": "Test", "GIT_COMMITTER_EMAIL": "t@example.com",
                    "GIT_AUTHOR_DATE": "2026-10-11T12:00:01+0000", "GIT_COMMITTER_DATE": "2026-10-11T12:00:01+0000"}
        # the emulator's source, with a picture of its own in Sys
        self.sys_files = {"GameSettings/GFZ.ini": b"# GFZE01\n", "Themes/Clean/icon.png": b"\x89PNG the emulator's own"}
        for name, data in self.sys_files.items():
            write(self.at("emulator", "Data", "Sys", name), data)
            write(self.at("parts", "Sys", name), data)
        self.emulator = self.commit("emulator", "the emulator")
        # the checkout: the commit the runner is built from, then a later commit the launcher is published from
        write(self.at("repo", FORKS, "README.md"), "as the runner was built\n")
        write(self.at("repo", *IMAGE.split("/"), "same-race.json"), '{"version": 1, "rows": [], "released": []}\n')
        self.runner_commit = self.commit("repo", "the runner's commit")
        write(self.at("repo", FORKS, "README.md"), "changed after the runner was built\n")
        self.head = self.commit("repo", "the launcher's commit")
        self.build = self.git("repo", "rev-parse", "--short", "HEAD")
        # the image bundle, the module built from it, the runner
        mod = write(self.at("parts", "bundle", "mod.bin"), b"image words")
        write(self.at("parts", "bundle", "patches.json"), "[]\n")
        self.manifest("parts/bundle/manifest.json", {"complete": True, "form": "native", "image_sha1": sha1(read(mod))})
        write(self.at("parts", "module", "gGFZE01_recomp.dll"), b"MZ the module")
        self.manifest("parts/module/build-manifest.json", self.built("module", "gGFZE01_recomp.dll", inputs={
            "bundle": {n: sha1(read(self.at("parts", "bundle", n))) for n in ("mod.bin", "patches.json", "manifest.json")}}))
        write(self.at("parts", "runner", "moderngekko-run.exe"), b"MZ the runner")
        self.manifest("parts/runner/build-manifest.json", self.built("runner", "moderngekko-run.exe"))
        for name in ("msvcp140.dll", "msvcp140_atomic_wait.dll", "vcruntime140.dll", "vcruntime140_1.dll", "concrt140.dll"):
            write(self.at("parts", "vcrt", name), b"MZ " + name.encode())
        for name in ("SairaCondensed-ExtraBold.ttf", "SairaCondensed-SemiBold.ttf", "SairaSemiCondensed-Regular.ttf", "OFL.txt", "Another.ttf"):
            write(self.at("parts", "fonts", name), b"a font: " + name.encode())
        # the published launcher
        write(self.at("launcher", "Launcher.App.exe"), b"MZ the launcher")
        write(self.at("launcher", "Launcher.App.dll"), b"MZ ..\x00 0.9.0+" + self.build.encode() + b"\x00 ..")
        write(self.at("launcher", "Some.Library.dll"), b"MZ a library")
        write(self.at("launcher", "libSome.pdb"), b"debug symbols")
        # the copy's program, as the publish writes it: a host file with the launcher's place under its own folder
        write(self.at("program.exe"), b"MZ a host ..\x00app\\Launcher.App.dll\x00\x00 ..")
        self.name = f"Testnet-0.9.0-{self.build}"

    def tearDown(self):
        self.tmp.cleanup()

    def git(self, where, *arguments):
        return subprocess.run(["git", "-C", self.at(where), *arguments], env=self.env, check=True, capture_output=True, text=True).stdout.strip()

    def commit(self, where, message):
        if not os.path.isdir(self.at(where, ".git")):
            self.git(where, "init", "-q", "-b", "main")
        self.git(where, "add", "-A")
        self.git(where, "commit", "-q", "-m", message)
        return self.git(where, "rev-parse", "HEAD")

    def manifest(self, name, content):
        write(self.at(*name.split("/")), json.dumps(content))

    def built(self, kind, file, **more):
        return {"kind": kind, "name": "t1", "complete": True, "missing": [], "repository": {"commit": self.runner_commit},
                "trees": {"recompcore": {"commit": self.emulator, "tree": "e" * 40}},
                "outputs": {file: {"sha1": sha1(read(self.at("parts", kind, file)))}}, **more}

    def change(self, name, **values):
        """One entry of a part's manifest set to another value."""
        with open(self.at(*name.split("/"))) as f:
            m = json.load(f)
        m.update(values)
        self.manifest(name, m)

    def package(self, *more, **named):
        a = {"name": "Testnet", "stack": "T1", "repository": self.at("repo"), "work": self.at("work"),
             "runner": self.at("parts", "runner", "moderngekko-run.exe"), "runner-commit": self.runner_commit[:8],
             "module": self.at("parts", "module", "gGFZE01_recomp.dll"), "bundle": self.at("parts", "bundle"),
             "sys": self.at("parts", "Sys"), "sys-source": self.at("emulator"), "vcrt": self.at("parts", "vcrt"),
             "fonts": self.at("parts", "fonts"),
             "launcher": self.at("launcher"), "program": self.at("program.exe"), "program-name": "Testnet Launcher",
             "program-folder": "app", "marker": "portable-copy.txt",
             "identity-without": "windows",          # these parts are made up on a PC with no Windows side asked (the PATH above)
             **{k.replace("_", "-"): v for k, v in named.items()}}
        options = [x for k, v in a.items() if v is not None for x in ("--" + k, v)]
        return subprocess.run([sys.executable, PACKAGE, *options, *more], env=self.env, capture_output=True, text=True)

    def refused(self, why, path, *more, **named):
        """The copy is refused for this reason, names the offending path, and nothing is written."""
        done = self.package(*more, **named)
        self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
        self.assertIn("package: REFUSED: ", done.stderr)
        self.assertIn(why, done.stderr)
        self.assertTrue(done.stderr.rstrip().endswith(": " + path), done.stderr)
        self.assertEqual(os.listdir(self.at("work", "dist")) if os.path.isdir(self.at("work", "dist")) else [], [])

    def files(self):
        out = []
        for at, _, names in os.walk(self.at("work", "dist", self.name)):
            out += [os.path.relpath(os.path.join(at, n), self.at("work", "dist", self.name)).replace(os.sep, "/") for n in names]
        return sorted(out)


class ACopy(Parts):
    def test_it_holds_the_program_the_read_me_the_parts_and_the_source(self):
        done = self.package()
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertEqual(self.files(), sorted([
            "Testnet Launcher.exe", "READ ME.txt",
            "app/Launcher.App.dll", "app/Launcher.App.exe", "app/Some.Library.dll",          # the .pdb is left out
            "app/portable-copy.txt",
            "recomp/moderngekko-run.exe", "recomp/gGFZE01_recomp.dll",
            "recomp/fzgx/mod.bin", "recomp/fzgx/manifest.json", "recomp/fzgx/patches.json", "recomp/fzgx/same-race.json",
            "recomp/Sys/GameSettings/GFZ.ini", "recomp/Sys/Themes/Clean/icon.png",
            "recomp/MSVCP140.dll", "recomp/MSVCP140_ATOMIC_WAIT.dll", "recomp/VCRUNTIME140.dll", "recomp/VCRUNTIME140_1.dll",
            "recomp/Fonts/SairaCondensed-ExtraBold.ttf", "recomp/Fonts/SairaCondensed-SemiBold.ttf",       # the strip's three,
            "recomp/Fonts/SairaSemiCondensed-Regular.ttf", "recomp/Fonts/OFL.txt",                    # and their licence
            "source/runner-build-manifest.json", "source/module-build-manifest.json", "source/package-manifest.json"]))
        copy = lambda *names: read(self.at("work", "dist", self.name, *names))
        # the source: the read-me points to the public repositories at the release's tag; the record names each tree
        self.assertIn(b"github.com/piconet-live", copy("READ ME.txt"))
        record = json.loads(copy("source", "package-manifest.json"))
        self.assertEqual(record["parts"]["source"], {"runner_commit": self.runner_commit,
                                                     "trees": {"recompcore": {"commit": self.emulator, "tree": "e" * 40}}})
        self.assertEqual(copy("recomp", "MSVCP140.dll"), b"MZ msvcp140.dll")
        self.assertIn(f"package: {self.name}: ", done.stdout)
        self.assertIn("the server's address is the launcher's built-in one", done.stdout)

    def test_the_read_me_names_the_version_the_game_build_and_the_stack(self):
        self.assertEqual(self.package().returncode, 0)
        text = read(self.at("work", "dist", self.name, "READ ME.txt"))
        self.assertTrue(text.startswith(b"\xef\xbb\xbfTestnet test build, 11 October 2026\r\n"))
        said = text.decode("utf-8-sig")
        self.assertIn(f"Version 0.9.0 · build {self.build}\r\n", said)
        hashes = [sha1(read(self.at("parts", *p)))[:8] for p in (("bundle", "mod.bin"), ("module", "gGFZE01_recomp.dll"), ("runner", "moderngekko-run.exe"))]
        self.assertIn("Game build " + " / ".join(hashes) + " (image / module / runner)\r\n", said)
        self.assertIn("Server: stack T1 (address: the launcher's built-in one)\r\n", said)
        self.assertNotIn("@", said)
        self.assertNotIn("\n", said.replace("\r\n", ""))
        self.assertIn('  2. Start "Testnet Launcher", the program in this folder. On the first screen choose your .iso', said)
        self.assertNotIn(".exe\"", said)                                         # named as Explorer shows it
        self.assertNotIn(".cmd", said)
        # somebody else's work in what a server may switch on is credited where a player reads it
        self.assertIn("Credits: the GX Unleashed stats, which a session's server may switch on, are by AKC12, used with permission.\r\n", said)

    def test_the_read_me_says_the_release_stage_after_the_version(self):
        self.assertEqual(self.package(stage="Beta").returncode, 0)
        said = read(self.at("work", "dist", self.name, "READ ME.txt")).decode("utf-8-sig")
        self.assertIn(f"Version 0.9.0 Beta · build {self.build}\r\n", said)
        record = json.loads(read(self.at("work", "dist", self.name, "source", "package-manifest.json")))
        self.assertEqual(record["version"], "0.9.0")                      # the version stays three numbers

    def test_the_top_is_one_program_the_read_me_and_folders(self):
        self.assertEqual(self.package().returncode, 0)
        copy = self.at("work", "dist", self.name)
        self.assertEqual(sorted(os.listdir(copy)), ["READ ME.txt", "Testnet Launcher.exe", "app", "recomp", "source"])
        self.assertEqual(sorted(n for n in os.listdir(copy) if os.path.isfile(os.path.join(copy, n))), ["READ ME.txt", "Testnet Launcher.exe"])
        self.assertFalse([n for n in self.files() if n.lower().endswith((".cmd", ".bat", ".lnk"))])    # no batch file, no shortcut
        self.assertEqual(read(os.path.join(copy, "Testnet Launcher.exe")), read(self.at("program.exe")))  # the host file, untouched

    def test_the_marker_is_beside_the_launchers_libraries_and_nothing_makes_a_data_folder(self):
        self.assertEqual(self.package().returncode, 0)
        copy = self.at("work", "dist", self.name)
        marker = read(os.path.join(copy, "app", "portable-copy.txt"))
        self.assertTrue(marker.isascii() and marker.endswith(b"\r\n"))
        self.assertIn(b"portable copy", marker)
        self.assertFalse(os.path.exists(os.path.join(copy, "data")))            # the launcher makes it, at a player's
        self.assertFalse(os.path.exists(os.path.join(copy, "portable-copy.txt")))   # and the top stays clean

    def test_the_record_lists_every_file_and_the_parts(self):
        self.assertEqual(self.package().returncode, 0)
        with open(self.at("work", "dist", self.name, "source", "package-manifest.json")) as f:
            record = json.load(f)
        self.assertEqual(sorted(record["files"]), [n for n in self.files() if n != "source/package-manifest.json"])
        for name, file in record["files"].items():
            data = read(self.at("work", "dist", self.name, *name.split("/")))
            self.assertEqual((file["sha1"], file["bytes"]), (sha1(data), len(data)), name)
        self.assertEqual(record["stack"], {"name": "T1", "server_address": "the launcher's built-in one"})
        self.assertEqual((record["version"], record["build"], record["launcher_commit"]), ("0.9.0", self.build, self.head))
        self.assertEqual(record["program"], "Testnet Launcher.exe")
        self.assertEqual(record["parts"]["source"]["runner_commit"], self.runner_commit)
        self.assertEqual(record["left_out_of_the_launcher"], ["libSome.pdb"])
        self.assertNotIn(self.tmp.name, json.dumps(record))                     # no path of the PC

    def test_the_zip_is_the_folder_and_the_same_parts_give_the_same_zip(self):
        self.assertEqual(self.package().returncode, 0)
        archive = self.at("work", "dist", self.name + ".zip")
        with zipfile.ZipFile(archive) as z:
            self.assertEqual(sorted(z.namelist()), sorted(f"{self.name}/{n}" for n in self.files()))
            for name in self.files():
                self.assertEqual(z.read(f"{self.name}/{name}"), read(self.at("work", "dist", self.name, *name.split("/"))))
        first, listed = read(archive), self.files()
        again = self.package()
        self.assertEqual(again.returncode, 1)
        self.assertIn("there already (REPLACE=1 makes it again)", again.stderr)
        os.utime(self.at("parts", "runner", "moderngekko-run.exe"), (1, 1))      # the parts' own dates do not count
        self.assertEqual(self.package("--replace").returncode, 0)
        self.assertEqual(read(archive), first)
        self.assertEqual(self.files(), listed)

    def stamp(self, stack="T1", address="wss://t1.example/ws", kind="test"):
        """The launcher's own part as its build stamps a stack into it: three attributes of a key and a value, each
        with its length before it (as a build with -p:ServerStack, -p:StackServer and -p:StackKind writes them)."""
        said = b"".join(b"\x01\x00" + bytes([len(k)]) + k.encode() + bytes([len(v)]) + v.encode() + b"\x00\x00"
                        for k, v in (("ServerStack", stack), ("StackServer", address), ("StackKind", kind)) if v is not None)
        write(self.at("launcher", "Launcher.Core.dll"), b"MZ the launcher's own part .." + said + b"..")

    def test_a_copy_for_a_stack_holds_a_launcher_made_for_it_and_names_the_address(self):
        self.stamp()
        done = self.package(stack_address="wss://t1.example/ws", stack_kind="test")
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertIn(b"Server: stack T1 (address: wss://t1.example/ws)", read(self.at("work", "dist", self.name, "READ ME.txt")))
        with open(self.at("work", "dist", self.name, "source", "package-manifest.json")) as f:
            self.assertEqual(json.load(f)["stack"], {"name": "T1", "server_address": "wss://t1.example/ws", "kind": "test"})

    def test_a_launcher_that_was_not_made_for_the_copys_stack_is_refused(self):
        """A copy named for stack B whose launcher was published without the stack's properties plays on the first
        server under B's name; one stamped as a test build must not go out as the public one."""
        for stamped, why in (({"stack": None, "address": None, "kind": None}, "its ServerStack, StackServer, StackKind say otherwise, or nothing"),
                             ({"stack": "T2"}, "its ServerStack says otherwise, or nothing"),
                             ({"address": "wss://t1.example/ws2"}, "its StackServer says otherwise, or nothing"),
                             ({"address": "wss://t1.example/w"}, "its StackServer says otherwise, or nothing"),
                             ({"kind": "public"}, "its StackKind says otherwise, or nothing")):
            self.stamp(**stamped)
            self.refused("the launcher there was not made for stack T1 (test; wss://t1.example/ws): " + why, self.at("launcher"),
                         stack_address="wss://t1.example/ws", stack_kind="test")
        self.stamp()
        self.refused("a copy made for a stack is a test build or the public one", "release", stack_address="wss://t1.example/ws", stack_kind="release")
        self.refused("a copy made for a stack is a test build or the public one", "no kind", stack_address="wss://t1.example/ws")
        os.remove(self.at("launcher", "Launcher.Core.dll"))
        self.refused("the launcher's own part", self.at("launcher", "Launcher.Core.dll"), stack_address="wss://t1.example/ws", stack_kind="test")

    def test_the_parts_alone_are_checked_without_a_launcher(self):
        done = self.package("--parts-only", launcher=None)
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertIn("the parts are in order", done.stdout)
        self.assertFalse(os.path.exists(self.at("work", "dist")))


class NothingOfTheGame(Parts):
    def test_a_disc_image_by_its_name(self):
        self.refused("a disc image must not be in a copy", write(self.at("launcher", "F-Zero GX (USA).iso"), b"x"))

    def test_a_disc_image_under_another_name(self):
        self.refused("a disc image must not be in a copy", write(self.at("launcher", "notes.dat"), DISC))

    def test_a_data_folder(self):
        self.refused("a data folder must not be in a copy", write(self.at("launcher", "data", "launcher.txt"), b"x"))

    def test_an_extracted_game(self):
        self.refused("an extracted game folder must not be in a copy", write(self.at("launcher", "recomp", "game", "sys", "x.txt"), b"x"))
        shutil.rmtree(self.at("launcher", "recomp"))
        self.refused("a file of the extracted game must not be in a copy", write(self.at("launcher", "main.dol"), b"x"))

    def test_a_save_a_replay_a_dump_a_log_and_a_picture(self):
        for name, why in (("8P-GFZE-f_zero.dat.gci", "a save"), ("race.fzrp", "a replay"), ("ram.dmp", "a dump or a log"),
                          ("prepare-log.txt.log", "a dump or a log"), ("shot.png", "a picture, a video or a sound"),
                          ("state.s01", "a saved state")):
            path = write(self.at("launcher", name), b"x")
            self.refused(why + " must not be in a copy", path)
            os.remove(path)
        self.refused("a replay must not be in a copy", write(self.at("launcher", "notes.txt"), b"FZRP\x02"))

    def test_a_picture_planted_in_sys(self):
        # the emulator's own pictures pass (ACopy); one that its source does not hold is refused
        self.refused("a file in Sys is not in the emulator's source", write(self.at("parts", "Sys", "Themes", "Clean", "shot.png"), b"x"))

    def test_a_changed_or_missing_file_of_sys(self):
        self.refused("a file in Sys is not the emulator's own", write(self.at("parts", "Sys", "GameSettings", "GFZ.ini"), b"# changed\n"))
        os.remove(self.at("parts", "Sys", "GameSettings", "GFZ.ini"))
        self.refused("Sys lacks a file of the emulator's", self.at("parts", "Sys", "GameSettings/GFZ.ini"))


class StartedByItsProgram(Parts):
    def test_a_batch_file_a_script_or_a_shortcut_anywhere(self):
        for name in ("Play.cmd", "start.BAT", "Testnet.lnk", "site.url", "run.ps1", "run.vbs", "run.js", "run.wsf", "setup.msi",
                     "keys.reg", "sub/folder/Start.PS1"):
            path = write(self.at("launcher", *name.split("/")), b"@echo off\r\n")
            self.refused("a batch file, a script or a shortcut must not be in a copy, which is started by its program (the launcher)", path)
            os.remove(path)
        self.assertEqual(self.package().returncode, 0)                           # and none of them left: a copy again

    def test_a_folder_at_the_top_that_a_copy_is_not_made_of(self):
        # the copy's parts go into the launcher's folder, recomp and source; a part given another place is refused
        done = self.package(**{"program-folder": "bin"})
        self.assertEqual(done.returncode, 1)                                     # (the program names app, not bin)
        self.assertIn("starts the launcher in bin\\", done.stderr)
        write(self.at("program.exe"), b"MZ a host ..\x00recomp\\Launcher.App.dll\x00\x00 ..")
        self.refused("the launcher's folder is its own: it is none of recomp, source and data", "recomp", **{"program-folder": "recomp"})
        import importlib.util
        spec = importlib.util.spec_from_file_location("package", PACKAGE)
        package = importlib.util.module_from_spec(spec); spec.loader.exec_module(package)
        plan = package.Plan()
        for name in ("Testnet Launcher.exe", "READ ME.txt", "app/Launcher.App.dll", "recomp/x.bin", "source/y.txt"):
            plan.add(name, "a part", data=b"MZ")
        package.clean_top(plan, "Testnet Launcher.exe", "app")                   # what a copy is made of passes
        plan.add("tools/helper.txt", "another part", data=b"x")
        with self.assertRaises(package.Refused) as refused:
            package.clean_top(plan, "Testnet Launcher.exe", "app")
        self.assertEqual(str(refused.exception), "a folder at the copy's top that a copy is not made of (another part): tools/helper.txt")
        path = write(self.at("parts", "Sys", "run.bat"), b"@echo off\r\n")
        self.refused("a file in Sys is not in the emulator's source", path)      # whatever its kind: Sys is the emulator's own


class NoLink(Parts):
    def test_a_junction_inside_sys(self):
        os.makedirs(self.at("elsewhere"))
        os.symlink(self.at("elsewhere"), self.at("parts", "Sys", "Load"))
        self.refused("the emulator's Sys folder holds a link (a junction or a symbolic link)", self.at("parts", "Sys", "Load"))

    def test_sys_that_is_itself_a_junction(self):
        os.symlink(self.at("parts", "Sys"), self.at("parts", "SysLink"))
        self.refused("the emulator's Sys folder is a link (a junction or a symbolic link), not a real folder",
                     self.at("parts", "SysLink"), sys=self.at("parts", "SysLink"))

    def test_a_link_in_the_launcher_and_a_part_that_is_a_link(self):
        os.symlink(self.at("parts", "bundle"), self.at("launcher", "fzgx"))
        self.refused("the launcher holds a link", self.at("launcher", "fzgx"))
        os.remove(self.at("launcher", "fzgx"))
        os.symlink(self.at("parts", "runner", "moderngekko-run.exe"), self.at("parts", "run.exe"))
        self.refused("the runner is a link, not a real file", self.at("parts", "run.exe"), runner=self.at("parts", "run.exe"))


class PartsThatBelongTogether(Parts):
    def test_a_module_built_from_another_image(self):
        mod = write(self.at("parts", "bundle", "mod.bin"), b"another image")
        self.change("parts/bundle/manifest.json", image_sha1=sha1(read(mod)))
        self.refused("the module was not built from this image (mod.bin is another)", mod)

    def test_a_bundle_whose_manifest_names_another_image(self):
        self.change("parts/bundle/manifest.json", image_sha1="0" * 40)
        self.change("parts/module/build-manifest.json", inputs={"bundle": {
            n: sha1(read(self.at("parts", "bundle", n))) for n in ("mod.bin", "patches.json", "manifest.json")}})
        self.refused("names another image than mod.bin", self.at("parts", "bundle", "manifest.json"))

    def test_a_runner_that_is_not_the_file_its_manifest_describes(self):
        self.refused("the runner is not the file its manifest describes", write(self.at("parts", "runner", "moderngekko-run.exe"), b"MZ rebuilt"))

    def test_a_runner_built_from_another_commit_than_the_one_named(self):
        self.refused(f"the runner was built from commit {self.runner_commit[:8]}, not from {self.head[:8]}",
                     self.at("parts", "runner", "build-manifest.json"), **{"runner-commit": self.head[:8]})

    def test_a_manifest_that_is_not_complete_or_not_there(self):
        self.change("parts/module/build-manifest.json", complete=False, missing=["repository: files differ from the commit"])
        self.refused("the module's manifest is not complete", self.at("parts", "module", "build-manifest.json"))
        os.remove(self.at("parts", "module", "build-manifest.json"))
        self.refused("the module's manifest is not there", self.at("parts", "module", "build-manifest.json"))

    def test_a_launcher_published_from_another_commit(self):
        write(self.at("launcher", "Launcher.App.dll"), b"MZ 0.9.0+0123abc ..")
        self.refused(f"was not published from the commit checked out ({self.build}); it says 0.9.0+0123abc", self.at("launcher"))

    def test_a_program_that_does_not_start_the_launcher_in_its_folder(self):
        # the launcher's ordinary host file names the library beside itself: at the top it would find nothing
        plain = write(self.at("plain.exe"), b"MZ a host ..\x00Launcher.App.dll\x00\x00 ..")
        self.refused("the copy's program is no Windows program that starts the launcher in app\\", plain, program=plain)
        other = write(self.at("other.exe"), b"MZ a host ..\x00bin\\Launcher.App.dll\x00\x00 ..")
        self.refused("the copy's program is no Windows program that starts the launcher in app\\", other, program=other)
        self.refused("the copy's program is not there", self.at("none.exe"), program=self.at("none.exe"))

    def test_a_name_that_is_no_plain_file_name(self):
        self.refused("the program's name is letters, digits, spaces, '.', '_' and '-'", "..\\Testnet", **{"program-name": "..\\Testnet"})
        self.refused("the launcher's folder is letters, digits, spaces, '.', '_' and '-'", "app/x", **{"program-folder": "app/x"})

    def test_a_checkout_that_is_not_clean(self):
        write(self.at("repo", "new.txt"), "not committed\n")
        self.refused("the checkout is not clean", "new.txt")

    def test_a_vc_runtime_file_that_is_missing(self):
        os.remove(self.at("parts", "vcrt", "vcruntime140_1.dll"))
        self.refused("the VC runtime's VCRUNTIME140_1.dll is not there", self.at("parts", "vcrt", "VCRUNTIME140_1.dll"))


    def test_a_font_of_the_game_windows_strip_that_is_missing(self):
        os.remove(self.at("parts", "fonts", "OFL.txt"))
        self.refused("the game window's OFL.txt is not there", self.at("parts", "fonts", "OFL.txt"))


class ReleasedBuildsReplays(Parts):
    """A copy whose image would refuse a released build's replays (same-race.json, port/build/same_race.py)."""

    def table(self, rows, released):
        table = write(self.at("repo", *IMAGE.split("/"), "same-race.json"), json.dumps({"version": 1, "rows": [
            {"recorded": r, "plays_on": p, "evidence": "a run"} for r, p in rows], "released": [
            {"version": v, "image": i} for v, i in released]}))
        self.commit("repo", "the table")
        return table

    def test_an_image_with_no_row_from_a_released_build_is_refused(self):
        table = self.table([], [("0.2.0", "a" * 40)])
        self.refused("this image would refuse the replays of released builds (0.2.0, image aaaaaaaa)", table, "--parts-only")

    def test_a_row_from_it_or_the_switch_on_purpose_lets_it_through(self):
        ours = sha1(read(self.at("parts", "bundle", "mod.bin")))
        self.table([("a" * 40, ours)], [("0.2.0", "a" * 40), ("0.2.1", "a" * 40)])
        done = self.package("--parts-only")
        self.assertEqual(done.returncode, 0, done.stderr)
        self.table([], [("0.2.0", "a" * 40)])
        done = self.package("--parts-only", "--old-replays-refused")
        self.assertEqual(done.returncode, 0, done.stderr)

    def test_a_table_that_cannot_be_read(self):
        table = write(self.at("repo", *IMAGE.split("/"), "same-race.json"), '{"version": 1}\n')
        self.commit("repo", "the table")
        self.refused("the table of same races cannot be read", table, "--parts-only")


class WhereItIsWritten(Parts):
    def test_never_into_a_release_folder_of_its_name(self):
        work = self.at("Testnet", "work")
        done = self.package(work=work)
        self.assertEqual(done.returncode, 1)
        self.assertIn("a folder named Testnet is a release folder", done.stderr)
        self.assertFalse(os.path.exists(work))

    def test_replace_removes_only_a_copy_this_script_made(self):
        theirs = write(self.at("work", "dist", self.name, "kept.txt"), b"somebody's")
        done = self.package("--replace")
        self.assertEqual(done.returncode, 1)
        self.assertIn("a folder where the copy goes that this script did not make", done.stderr)
        self.assertEqual(read(theirs), b"somebody's")


if __name__ == "__main__":
    unittest.main()
