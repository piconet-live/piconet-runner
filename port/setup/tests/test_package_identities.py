"""No personal name in a player copy (port/build/package_identities.py, and package.py with it): a copy of made-up
parts packages clean; a made-up identity planted in any file of it, as UTF-8 or UTF-16 text or in a file's name, is
refused with the file and the kind of name, and the name itself is never printed; a build's manifest goes into a
copy without its host and home folders; the read-me says which
kind of copy it is. Every name here is made up: the tests give them to the script as a PC would (a repository's git
user, the login of the environment, a file of more names)."""
import io, json, os, shutil, subprocess, sys, unittest, zipfile
from unittest import mock

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
import test_package as base
import package_identities as identities

FORKS, write, read, sha1 = base.FORKS, base.write, base.read, base.sha1
SCAN = os.path.join(base.root, "port", "build", "package_identities.py")
PERSON, ADDRESS, LOGIN, HOST = "Avery Quillfeather", "avery@quillfeather.example", "aquillfeather", "QUILLBOX-0042"
MADE_UP = ("quillfeather", "avery", "quillbox")          # no output may hold any of these, in any case


class Identities(base.Parts):
    """The made-up parts, on a PC whose user is made up too: the checkout's git user, the login, one more name."""

    def setUp(self):
        super().setUp()
        self.git("repo", "config", "user.name", PERSON)
        self.git("repo", "config", "user.email", ADDRESS)
        self.env.update(LOGNAME=LOGIN, USER=LOGIN, HOME="/home/" + LOGIN)
        self.more = write(self.at("more.tsv"), f"# kind, a tab, the name\nthe made-up PC's host name\t{HOST}\n")

    def package(self, *more, **named):
        return super().package("--more-identities", self.more, *more, **named)

    def silent(self, done):
        """Nothing a run printed holds a made-up name: a refusal can be pasted anywhere."""
        for text in MADE_UP:
            self.assertNotIn(text, (done.stdout + done.stderr).lower())

    def refused_for(self, path, name, kind, form, **named):
        """The copy is refused; the file is printed as the copy names it, with the kind of name; the name is not
        printed, nor any path of this PC (`path`, where the file is); nothing is written."""
        done = self.package(**named)
        self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
        self.assertIn(f"package: identity: {name}: {kind} (as {form})\n", done.stderr)
        self.assertIn("package: REFUSED: ", done.stderr)
        self.assertIn("of the copy would carry a personal identity, which no copy may", done.stderr)
        self.assertTrue(done.stderr.rstrip().endswith(": " + name), done.stderr)
        self.assertNotIn(self.tmp.name, done.stdout + done.stderr)
        self.assertNotIn(os.path.dirname(path), done.stdout + done.stderr)
        self.assertFalse(os.path.isdir(self.at("work", "dist")) and os.listdir(self.at("work", "dist")))
        self.silent(done)


class ACleanCopy(Identities):
    def test_made_up_parts_package_clean_and_say_what_was_searched(self):
        done = self.package()
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertIn("package: identities: searched every file as UTF-8, UTF-16 and UTF-32 text for ", done.stdout)
        self.assertIn("package: identities: names login: gathered, 2 names; host: gathered, ", done.stdout)
        self.assertIn("; windows: left out, by the caller's word; git:repo: gathered, 2 names; git:emulator: gathered, none is set; more: gathered, 1 name\n",
                      done.stdout)                                                 # every source, gathered or not
        for kind in ("this PC's login name", "git's user.name (repo)", "a word of git's user.name (repo)", "git's user.email (repo)",
                     "git's user.name (repo), written together", "the made-up PC's host name"):
            self.assertIn(kind, done.stdout)
        self.assertNotIn("identity:", done.stderr)
        self.silent(done)

    def test_the_parts_alone_are_searched_before_the_launcher_is_published(self):
        write(self.at("parts", "vcrt", "msvcp140.dll"), b"MZ built by " + LOGIN.encode())
        done = self.package("--parts-only", launcher=None)
        self.assertEqual(done.returncode, 1)
        self.assertIn("package: identity: recomp/MSVCP140.dll: this PC's login name (as UTF-8 text)", done.stderr)
        self.silent(done)

    def test_the_command_passes_a_copy_and_its_zip_and_refuses_a_planted_name(self):
        self.assertEqual(self.package().returncode, 0)
        copy = self.at("work", "dist", self.name)
        scan = lambda where: subprocess.run([sys.executable, SCAN, where, "--repository", self.at("repo"), "--more", self.more, "--product", "Testnet",
                                             "--without", "windows", "--without", "git:" + os.path.basename(base.root)],
                                            env=self.env, capture_output=True, text=True)
        for where in (copy, copy + ".zip"):
            done = scan(where)
            self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
            self.assertIn(f"identities: clean: {len(self.files())} files, no personal identity: {where}", done.stdout)
        planted = shutil.copytree(copy, self.at("handed-out"))
        write(os.path.join(planted, "recomp", "fzgx", "notes.json"), '{"host": "%s"}' % HOST.lower())
        write(os.path.join(planted, "app", "Some.Library.dll"), b"MZ " + PERSON.encode("utf-16-le"))
        done = scan(planted)
        self.assertEqual(done.returncode, 1)
        self.assertIn("identities: recomp/fzgx/notes.json: the made-up PC's host name (as UTF-8 text)\n", done.stdout)
        self.assertIn("identities: app/Some.Library.dll: git's user.name (repo) (as UTF-16 text)\n", done.stdout)
        self.assertIn(f"identities: REFUSED: 2 of {len(self.files()) + 1} files carry a personal identity: {planted}", done.stdout)
        self.silent(done)


class APlantedIdentity(Identities):
    def test_a_full_name_in_a_library_as_utf8(self):
        path = write(self.at("launcher", "Some.Library.dll"), b"MZ \x01\x00\x12" + PERSON.encode() + b"\x00\x00")
        self.refused_for(path, "app/Some.Library.dll", "git's user.name (repo)", "UTF-8 text")

    def test_a_full_name_in_the_copys_program_as_utf16(self):
        # a program's version resource: CompanyName, as Windows shows it in the file's properties
        path = write(self.at("program.exe"), b"MZ a host ..\x00app\\Launcher.App.dll\x00\x00 .." + "CompanyName\0AVERY QUILLFEATHER\0".encode("utf-16-le"))
        self.refused_for(path, "Testnet Launcher.exe", "git's user.name (repo)", "UTF-16 text")

    def test_a_first_name_alone_in_a_text(self):
        path = write(self.at("launcher", "notes.txt"), "For the source of any part, ask Avery.\r\n")
        self.refused_for(path, "app/notes.txt", "a word of git's user.name (repo)", "UTF-8 text")

    def test_an_address_a_login_in_a_path_and_a_host_name(self):
        for data, kind in ((b"Sender: <AVERY@quillfeather.example>", "git's user.email (repo)"),
                           (b"/home/aquillfeather/projects/x/obj/Release/Some.pdb\x00", "this PC's login name"),
                           (b"made on quillbox-0042 at noon", "the made-up PC's host name"),
                           (b"by AveryQuillfeather", "git's user.name (repo), written together"),
                           (b"avery.quillfeather wrote it", "git's user.name (repo), written together")):
            path = write(self.at("launcher", "Some.Library.dll"), b"MZ a library " + data)
            self.refused_for(path, "app/Some.Library.dll", kind, "UTF-8 text")

    def test_a_name_in_a_files_name(self):
        path = write(self.at("launcher", "aquillfeather-notes.txt"), "nothing\r\n")
        self.refused_for(path, "app/***-notes.txt", "this PC's login name", "the file's name")     # with *** where the name is

    def test_a_first_name_in_the_emulators_own_files_is_the_emulators(self):
        """Sys is the emulator's own, checked against its source file for file: its list of every game's title holds
        most first names. A full name there is refused all the same."""
        for name, data in (("titles.txt", b"R5XE = Avery's Adventure\n"), ("credits.txt", b"thanks to Avery Quillfeather\n")):
            write(self.at("emulator", "Data", "Sys", name), data)
            write(self.at("parts", "Sys", name), data)
        emulator = self.commit("emulator", "the emulator, with a list of titles")
        for part in ("runner", "module"):
            with open(self.at("parts", part, "build-manifest.json")) as f:
                m = json.load(f)
            m["trees"]["recompcore"]["commit"] = emulator
            self.manifest(f"parts/{part}/build-manifest.json", m)
        done = self.package()
        self.assertEqual(done.returncode, 1)
        self.assertIn("package: identity: recomp/Sys/credits.txt: git's user.name (repo) (as UTF-8 text)", done.stderr)
        self.assertNotIn("recomp/Sys/titles.txt", done.stderr)

    def test_every_file_that_carries_one_is_named(self):
        for name in ("a.txt", "b.txt", "c.txt"):
            write(self.at("launcher", name), f"{LOGIN}\r\n")
        done = self.package()
        self.assertEqual([line for line in done.stderr.splitlines() if "identity: app/" in line],
                         [f"package: identity: app/{n}: this PC's login name (as UTF-8 text)" for n in ("a.txt", "b.txt", "c.txt")])
        self.assertIn("3 files of the copy would carry a personal identity", done.stderr)


class WrittenWithoutOne(Identities):
    def test_a_builds_manifest_goes_in_without_its_host_and_home_folders(self):
        for part, file in (("runner", "moderngekko-run.exe"), ("module", "gGFZE01_recomp.dll")):
            with open(self.at("parts", part, "build-manifest.json")) as f:
                m = json.load(f)
            m["made"] = {"host": HOST, "work": "/mnt/q/work", "at": "2026-10-11T12:00:00+0000"}
            m["switches"] = {"file": f"/home/{LOGIN}/checkout/port/build/x.env", "values": {"A": "1"}}
            m["tools"] = {"cmake": "C:\\Users\\Avery Quillfeather\\tools\\cmake.exe", "shared": "C:\\Users\\Public\\x", "ci": "/home/build/x"}
            self.manifest(f"parts/{part}/build-manifest.json", m)
        as_built = sha1(read(self.at("parts", "runner", "build-manifest.json")))
        done = self.package()
        self.assertEqual(done.returncode, 0, done.stderr)
        with open(self.at("work", "dist", self.name, "source", "runner-build-manifest.json")) as f:
            held = json.load(f)
        self.assertEqual(held["made"], {"work": "/mnt/q/work", "at": "2026-10-11T12:00:00+0000", "left_out_of_a_copy": ["made.host", "home folders"]})
        self.assertEqual(held["switches"], {"file": "~/checkout/port/build/x.env", "values": {"A": "1"}})
        self.assertEqual(held["tools"], {"cmake": "~\\tools\\cmake.exe", "shared": "C:\\Users\\Public\\x", "ci": "/home/build/x"})
        self.assertEqual(held["outputs"], {"moderngekko-run.exe": {"sha1": sha1(read(self.at("parts", "runner", "moderngekko-run.exe")))}})
        with open(self.at("work", "dist", self.name, "source", "package-manifest.json")) as f:
            self.assertEqual(json.load(f)["parts"]["runner"]["manifest_as_built_sha1"], as_built)     # the build's own file, by its hash

    def test_a_manifest_with_nothing_to_leave_out_is_the_builds_content(self):
        self.assertEqual(self.package().returncode, 0)
        with open(self.at("work", "dist", self.name, "source", "module-build-manifest.json")) as f, open(self.at("parts", "module", "build-manifest.json")) as g:
            self.assertEqual(json.load(f), json.load(g))


class TheReadMe(Identities):
    def test_it_names_no_person_and_asks_nobody_by_name(self):
        self.assertEqual(self.package().returncode, 0)
        said = read(self.at("work", "dist", self.name, "READ ME.txt")).decode("utf-8-sig")
        self.assertIn("at github.com/piconet-live, at the tag v0.9.0:\r\n", said)

    def test_a_test_build_says_so_and_the_players_build_does_not(self):
        base.ACopy.stamp(self, kind="test")
        self.assertEqual(self.package(stack_address="wss://t1.example/ws", stack_kind="test").returncode, 0)
        said = read(self.at("work", "dist", self.name, "READ ME.txt")).decode("utf-8-sig")
        self.assertTrue(said.startswith("Testnet test build, 11 October 2026\r\n"))
        self.assertIn("\r\nThis is a test build. Players are put in the same lobby only when their game build is the same: the three\r\nnumbers above.\r\n", said)
        base.ACopy.stamp(self, kind="public")
        self.assertEqual(self.package("--replace", stack_address="wss://t1.example/ws", stack_kind="public").returncode, 0)
        said = read(self.at("work", "dist", self.name, "READ ME.txt")).decode("utf-8-sig")
        self.assertTrue(said.startswith("Testnet, 11 October 2026\r\n"))
        self.assertNotIn("test build", said.lower())
        self.assertIn("\r\nPlayers are put in the same lobby only when their game build is the same: the three\r\nnumbers above.\r\n", said)


class NamesThatCouldNotBeGathered(Identities):
    """The refusal does not pass a copy on names it never asked for."""

    def refused_ungathered(self, source, why, *more, **named):
        done = self.package(*more, **named)
        self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
        self.assertIn(f"; {source}: NOT GATHERED, {why}", done.stdout)
        self.assertIn(f"package: REFUSED: the names no copy may hold could not all be gathered: {source} ({why}). A copy is not passed on names "
                      "that were never asked for", done.stderr)
        self.assertFalse(os.path.isdir(self.at("work", "dist")) and os.listdir(self.at("work", "dist")))

    def test_no_windows_side_refuses_unless_it_is_left_out_by_name(self):
        self.refused_ungathered("windows", "no Windows side was found (powershell.exe)", **{"identity-without": None})
        self.assertEqual(self.package().returncode, 0)                           # left out knowingly (Parts.package)

    def test_a_windows_side_that_does_not_answer_refuses_and_one_that_does_gives_five_kinds(self):
        fake = write(self.at("bin", "powershell.exe"), "#!/bin/sh\nexit 1\n")
        os.chmod(fake, 0o755)
        self.env["PATH"] = self.at("bin") + os.pathsep + self.env["PATH"]
        self.refused_ungathered("windows", "Windows did not answer (powershell.exe: its user, display name, user folder and computer)",
                                **{"identity-without": None})
        answer = json.dumps({"user": "aquil", "folder": "C:\\Users\\Avery Quillfeather", "computer": "QUILLBOX-0042",
                             "display": "A. V. Quillfeather", "short": "AVERYQ~1"})
        write(fake, f"#!/bin/sh\ncat <<'ANSWER'\n{answer}\nANSWER\n")
        done = self.package(**{"identity-without": None})
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn("; windows: gathered, 5 names; ", done.stdout)
        for kind in ("the Windows user's display name", "the Windows user folder's short name", "the Windows computer's name"):
            self.assertIn(kind, done.stdout)
        shutil.rmtree(self.at("work"))
        path = write(self.at("launcher", "Some.Library.dll"), b"MZ C:\\Users\\AVERYQ~1\\AppData\\x.pdb\x00")
        self.refused_for(path, "app/Some.Library.dll", "the Windows user folder's short name", "UTF-8 text", **{"identity-without": None})
        self.silent(done)

    def test_a_repository_that_cannot_be_asked_refuses(self):
        self.refused_ungathered("git:nowhere", "no such folder", "--identity-repository", self.at("nowhere"))
        os.makedirs(self.at("plain-folder"))
        self.refused_ungathered("git:plain-folder", "no git repository there, or git did not answer", "--identity-repository", self.at("plain-folder"))
        self.assertEqual(self.package("--identity-repository", self.at("nowhere"), "--identity-without", "git:nowhere").returncode, 0)

    def test_a_plain_line_of_the_callers_file_is_a_name_and_a_line_that_cannot_be_read_refuses(self):
        write(self.more, f"Quillfeather-the-Third\n\n# a comment\nthe made-up PC's host name\t{HOST}\n")
        path = write(self.at("launcher", "notes.txt"), "by quillfeather-the-third\r\n")
        self.refused_for(path, "app/notes.txt", "a name the caller's file adds", "UTF-8 text")
        os.remove(path)
        write(self.more, f"{HOST}\na kind with no name\t\n")
        self.refused_ungathered("more", "line 2 of the file of more names has a kind and no name")
        write(self.more, b"\xff\xfe not text of this kind\n")
        self.refused_ungathered("more", "the file of more names cannot be read (UnicodeDecodeError)")

    def test_a_refusal_prints_no_path_of_this_pc(self):
        """The file is where the login's own folder is: the refusal names it as the copy holds it."""
        mine = shutil.copytree(self.at("launcher"), self.at("home", LOGIN, "published", "launcher"))
        path = write(os.path.join(mine, "Some.Library.dll"), b"MZ " + PERSON.encode())
        self.refused_for(path, "app/Some.Library.dll", "git's user.name (repo)", "UTF-8 text", launcher=mine)


class WhatIsSearched(unittest.TestCase):
    """The script's own parts, without a copy."""

    def kinds(self, sought):
        return list(dict.fromkeys(s.kind for s in sought))

    def test_the_list_is_gathered_from_the_pc_and_the_callers_file(self):
        with base.tempfile.TemporaryDirectory() as tmp:
            more = write(os.path.join(tmp, "more.tsv"), f"a nickname\tQuillfeather-the-Third\n# a comment\tnot a name\n{HOST}\n")
            with mock.patch.dict(os.environ, {"LOGNAME": LOGIN, "USER": LOGIN, "HOME": "/home/quillhome", "PATH": "/usr/bin:/bin"}):
                found, sources = identities.gather([os.path.join(tmp, "no-such-repository")], more)
        # (asked one by one, and never the whole list: a failure here must not print this PC's own names)
        has = lambda kind, text: (kind, text) in found
        self.assertTrue(has("this PC's login name", LOGIN))
        self.assertTrue(has("the name of the login's home folder", "quillhome"))
        self.assertTrue(has("a nickname", "Quillfeather-the-Third"))
        self.assertTrue(has("a name the caller's file adds", HOST))
        self.assertFalse(any(text == "not a name" for _, text in found))
        self.assertEqual(sum(kind == "this PC's host name" for kind, _ in found) >= 1, True)
        self.assertEqual([(source, state) for source, state, _ in sources],
                         [("login", "gathered"), ("host", "gathered"), ("windows", "NOT GATHERED"), ("git:no-such-repository", "NOT GATHERED"),
                          ("more", "gathered")])
        self.assertEqual(identities.not_gathered(sources), "windows (no Windows side was found (powershell.exe)); git:no-such-repository (no such folder)")

    def test_a_short_name_and_a_name_every_pc_has_are_not_searched_and_that_is_said_without_the_name(self):
        sought, skipped = identities.searches([("a login", "avq"), ("another login", "root"), ("a host", "QUILLBOX-0042"), ("the product", "Testnet")],
                                              allowed=("Testnet",))
        self.assertEqual(self.kinds(sought), ["a host"])
        self.assertEqual(skipped, [("a login", "fewer than 4 characters"), ("another login", "a name every PC has")])
        said = "\n".join(identities.said(sought, skipped, [("host", "gathered", "1 name")]))
        self.assertIn("identities: not searched: a login (fewer than 4 characters)", said)
        self.assertNotIn("avq", said)
        self.assertNotIn("quillbox", said.lower())

    def test_a_short_name_counts_as_a_whole_word_and_a_long_one_anywhere(self):
        sought, _ = identities.searches([("a login", "avery"), ("a host", "quillbox")])
        found = lambda data: [(kind, form) for _, _, kind, form in identities.scan([("app/x", data, False)], sought)]
        self.assertEqual(found(b"/home/avery/x"), [("a login", "UTF-8 text")])
        self.assertEqual(found(b"slavery, avery1 and xavery"), [])                 # inside another word: not the name
        self.assertEqual(found(b"\x00\xffAVERY\x8a\x01"), [])                      # five letters among a program's bytes
        self.assertEqual(found(b"\x05avery\x00\x07"), [("a login", "UTF-8 text")])   # a program's own string: a length before it, a NUL after
        self.assertEqual(found("Avery".encode("utf-16-be")), [("a login", "UTF-16 text")])        # the other byte order, to the file's end
        self.assertEqual(found("an avery!".encode("utf-32-le")), [("a login", "UTF-32 text")])
        self.assertEqual(found("an avery!".encode("utf-32-be")), [("a login", "UTF-32 text")])
        self.assertEqual(found("Xavery".encode("utf-32-le")), [])
        self.assertEqual(found(b"myquillboxes"), [("a host", "UTF-8 text")])
        self.assertEqual(found("x Avery y".encode("utf-16-le")), [("a login", "UTF-16 text")])
        self.assertEqual(found("x Avery y".encode("utf-16-be")), [("a login", "UTF-16 text")])
        self.assertEqual(found("Xavery".encode("utf-16-le")), [])

    def test_a_login_that_is_also_a_first_name_is_searched_as_a_first_name(self):
        sought, _ = identities.searches([("a login", "avery"), ("a full name", PERSON)])
        scan = lambda others: [kind for _, _, kind, _ in identities.scan([("recomp/Sys/titles.txt", b"Avery's Adventure", others)], sought)]
        self.assertEqual(scan(False), ["a login"])
        self.assertEqual(scan(True), [])

    def test_an_address_is_searched_whole_and_by_the_name_before_its_at(self):
        sought, _ = identities.searches([("an address", "avery.q@quillfeather.example")])
        found = lambda data, others=False: [kind for _, _, kind, _ in identities.scan([("app/x", data, others)], sought)]
        self.assertEqual(found(b"mail AVERY.Q@quillfeather.example"), ["an address", "the name before the @ of an address"])
        self.assertEqual(found(b"user avery.q, at another place"), ["the name before the @ of an address"])
        self.assertEqual(found(b"xavery.qx"), [])
        self.assertEqual(found(b"user avery.q", others=True), [])                 # as a first name: not in the emulator's own files

    def test_a_zip_in_the_copy_is_opened_and_its_comments_are_read(self):
        sought, _ = identities.searches([("a host", "quillbox")])
        inner, outer = io.BytesIO(), io.BytesIO()
        with zipfile.ZipFile(inner, "w", zipfile.ZIP_DEFLATED) as z:
            z.writestr("lib/notes.txt", "made on QUILLBOX, " * 40)
        with zipfile.ZipFile(outer, "w", zipfile.ZIP_DEFLATED) as z:
            z.writestr("pkg/inner.nupkg", inner.getvalue())
            z.writestr("pkg/plain.txt", "nothing " * 40)
            z.comment = b"packed on quillbox"
        found = [(name, form) for name, _, _, form in identities.scan([("app/outer.zip", outer.getvalue(), False)], sought)]
        self.assertEqual(found, [("app/outer.zip", "UTF-8 text"), ("app/outer.zip (the zip's comment)", "UTF-8 text"),
                                 ("app/outer.zip!/pkg/inner.nupkg!/lib/notes.txt", "UTF-8 text")])
        self.assertEqual(identities.scan([("app/not-a.zip", b"PK\x03\x04 and nothing of a zip after it", False)], sought), [])
        with base.tempfile.TemporaryDirectory() as tmp:                             # and a copy's own zip: its comment too
            path = os.path.join(tmp, "copy.zip")
            with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
                z.writestr("Copy-1/READ ME.txt", "nothing " * 40)
                z.comment = b"from QuillBox"
            self.assertEqual([name for name, _, _ in identities.read(path)], ["(the zip's comment)", "READ ME.txt"])

    def test_what_no_copys_own_papers_hold_whoever_made_it(self):
        named = lambda name, data: [kind for kind, _ in identities.structural(name, data)]
        self.assertEqual(named("source/m.json", b'{"file": "/home/somebody/x"}'), ["a home folder's path"])
        self.assertEqual(named("READ ME.txt", b"see C:\\Users\\Some Body\\x"), ["a home folder's path"])
        self.assertEqual(named("source/m.json", b'{"file": "/home/build/x", "other": "C:\\\\Users\\\\Public\\\\y"}'), [])
        self.assertEqual(named("app/Some.Library.dll", b"/home/vsts/work/1/s/x.cs"), [])     # another project's build, not the copy's papers



class TheGamesOwnNames(unittest.TestCase):
    """A pilot's or a machine's name is the game's, not a person's, where it is a whole string of the launcher's
    program in the game's own spelling (the pilot "Pico" beside a user whose address is pico@...); the same word
    anywhere else, and the person's own name, are refused as ever."""

    def test_the_lists_are_the_launchers_own(self):
        names = identities.game_names()
        self.assertEqual(len(names), 82)
        for name in ("Pico", "Captain Falcon", "Gomar & Shioh", "Blue Falcon", "Rainbow Phoenix"):
            self.assertIn(name, names)

    def test_pico_in_the_pilot_list_passes_and_a_person_named_pico_does_not(self):
        sought, _ = identities.searches([("the Windows user's name", "Pico Quillfeather"),
                                         ("git's user.email", "pico@quillfeather.example")])
        game = identities.game_names()
        literal = lambda text: bytes([len(text.encode("utf-16-le")) + 1]) + text.encode("utf-16-le") + b"\x00"   # .NET's #US heap
        pilots = b"".join(literal(n) for n in ("Samurai Goroh", "Pico", "Captain Falcon"))
        found = lambda data: [kind for _, _, kind, _ in identities.scan([("app/Launcher.Core.dll", data, False)], sought, game)]
        self.assertEqual(found(pilots), [])
        self.assertEqual(found(b'{"pilot": "Pico"}'), [])
        self.assertNotEqual([k for _, _, k, _ in identities.scan([("app/x", pilots, False)], sought)], [])   # refused without them
        # "pico" alone in another string, in another case or inside a longer string: a person's, refused
        self.assertIn("a word of the Windows user's name", found(pilots + literal("pico")))
        self.assertIn("a word of the Windows user's name", found(pilots + literal("Made by Pico")))
        self.assertIn("a word of the Windows user's name", found(b'{"author": "pico"}'))
        # the Windows user's name, anywhere
        self.assertIn("the Windows user's name, written together", found(pilots + literal("PicoQuillfeather")))
        self.assertIn("the Windows user's name", found(pilots + literal("Pico Quillfeather")))

    def test_a_list_that_cannot_be_read_or_is_short_refuses(self):
        with self.assertRaises(ValueError):
            identities.game_names(root=os.path.join(here, "no-such-folder"))


if __name__ == "__main__":
    unittest.main()
