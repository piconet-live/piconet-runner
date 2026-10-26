"""Tests of port/setup and port/build that need no disc, no work folder and no Windows tool."""
import hashlib, json, os, re, shutil, struct, subprocess, sys, tempfile, unittest

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.abspath(os.path.join(here, "..", "..", ".."))
sys.path.insert(0, os.path.join(root, "port", "setup"))
sys.path.insert(0, os.path.join(root, "port", "build"))
import mainrel, manifest, ondemand, tools, work_folder


def sha1(path):
    with open(path, "rb") as f:
        return hashlib.sha1(f.read()).hexdigest()


def pack(items):
    """Amusement Vision's LZSS from a list of items: a byte (a literal) or (ring index, length) (a reference)."""
    body, i = bytearray(), 0
    while i < len(items):
        group, flags, part = items[i:i + 8], 0, bytearray()
        for bit, item in enumerate(group):
            if isinstance(item, int):
                flags |= 1 << bit; part.append(item)
            else:
                where, length = item
                part += bytes([where & 0xFF, ((where >> 4) & 0xF0) | (length - 3)])
        body += bytes([flags]) + part
        i += 8
    size = sum(1 if isinstance(item, int) else item[1] for item in items)
    return struct.pack("<II", len(body), size) + bytes(body)


class MainRel(unittest.TestCase):
    def test_the_cipher_is_its_own_inverse_at_any_length(self):
        for length in (0, 1, 4, 7, 4096, 4099):
            data = bytes((i * 37 + 5) & 0xFF for i in range(length))
            self.assertEqual(mainrel.decrypt(mainrel.decrypt(data)), data)
            if length >= 4:
                self.assertNotEqual(mainrel.decrypt(data), data)

    def test_the_first_key_word(self):
        # the generator's first state from the start value: (0xAF8910BE * 0x9B36BB94 + 0xCD8F3) mod 2^32
        self.assertEqual(mainrel.decrypt(bytes(4)), struct.pack(">I", (0xAF8910BE * 0x9B36BB94 + 0xCD8F3) & 0xFFFFFFFF))

    def test_literals_and_references(self):
        text = b"abcabcabcabc-the ring starts at 0xFEE-"
        items = list(b"abc") + [(0xFEE, 9)] + list(text[12:])      # nine bytes copied from the three just written
        self.assertEqual(mainrel.unpack(pack(items)), text)
        self.assertEqual(mainrel.unpack(pack([(0, 5), 65])), bytes(5) + b"A")   # the ring is zeros before any data

    def test_through_both_steps(self):
        data = bytes(range(256)) * 20
        self.assertEqual(mainrel.unpack(mainrel.decrypt(mainrel.decrypt(pack(list(data))))), data)

    def test_a_wrong_size_is_refused(self):
        bad = bytearray(pack(list(b"hello"))); struct.pack_into("<I", bad, 4, 6)
        with self.assertRaises(ValueError):
            mainrel.unpack(bytes(bad))
        with self.assertRaises(ValueError):
            mainrel.unpack(pack(list(b"hello"))[:-2])


def rel(ident, text, data, bss_size, relocations, bss_align=8, fix_size=None):
    """A small version 3 REL: section 1 text, section 2 data, section 3 .bss. relocations: {target module:
    [(skip, kind, section, addend)]}, each list ended here."""
    header, sections = 0x4C, 4
    at_text = header + sections * 8
    at_data = at_text + len(text)
    at_rel = at_data + len(data)
    lists, table = bytearray(), []
    for target, entries in relocations.items():
        table.append((target, at_rel + len(lists)))
        for entry in entries + [(0, 203, 0, 0)]:
            lists += struct.pack(">HBBI", *entry)
    at_imp = at_rel + len(lists)
    head = struct.pack(">12I", ident, 0, 0, sections, header, 0, 0, 3, bss_size, at_rel, at_imp, len(table) * 8)
    head += bytes(0x40 - len(head)) + struct.pack(">III", 8, bss_align, fix_size if fix_size is not None else at_rel)
    table_bytes = b"".join(struct.pack(">II", t, o) for t, o in table)
    secs = struct.pack(">8I", 0, 0, at_text | 1, len(text), at_data, len(data), 0, bss_size)
    return head + secs + text + data + bytes(lists) + table_bytes, at_text, at_data


class OnDemand(unittest.TestCase):
    def test_a_link_like_the_games(self):
        text = struct.pack(">6I", 0x48000001, 0x3C600000, 0x38630000, 0, 0x48000001, 0x3C800000)
        file, at_text, at_data = rel(5, text, bytes(8), 0x40, {
            5: [(0, 202, 1, 0), (0, 10, 1, 0x10),            # bl to text+0x10, from text+0
                (6, 6, 3, 0x8004), (4, 4, 3, 0x8004),        # lis/addi of .bss+0x8004 (the high half carries)
                (2, 1, 2, 4)],                               # a word: data+4
            1: [(0, 202, 1, 0), (0x10, 10, 1, 0x20)],        # bl to another module's section 1 + 0x20
            0: [(0, 202, 1, 0), (0x16, 5, 0, 0x80123456)],   # the executable: an absolute address's high half
        }, bss_align=32, fix_size=0x101)
        base = 0x80600000
        bss = ondemand.fixed_bss(file, base)
        self.assertEqual(bss, 0x80600120)                    # base + 0x101, rounded up to 32
        other = {1: [0, 0x80200000]}
        out = ondemand.link(file, base, bss, other)
        word = lambda i: struct.unpack_from(">I", out, at_text + 4 * i)[0]
        self.assertEqual(word(0), 0x48000011)                # 0x10 ahead, link bit kept
        self.assertEqual(word(1) & 0xFFFF, ((bss + 0x8004 + 0x8000) >> 16) & 0xFFFF)
        self.assertEqual(word(2) & 0xFFFF, (bss + 0x8004) & 0xFFFF)
        self.assertEqual(word(3), base + at_data + 4)
        self.assertEqual(word(4), 0x48000001 | ((0x80200020 - (base + at_text + 0x10)) & 0x03FFFFFC))
        self.assertEqual(word(5) & 0xFFFF, 0x8012)
        address, code = ondemand.text(file, base, other)
        self.assertEqual((address, code), (base + at_text, bytes(out[at_text:at_text + len(text)])))

    def test_a_module_that_is_not_linked_is_refused(self):
        file, _, _ = rel(5, bytes(8), b"", 0, {7: [(0, 202, 1, 0), (0, 1, 1, 0)]})
        with self.assertRaises(ValueError):
            ondemand.link(file, 0x80600000, 0, {})


class Pins(unittest.TestCase):
    pins = manifest.layout()

    def test_every_path_of_the_table_is_there(self):
        for key, value in self.pins.items():
            # optional: the gates (private), and the service's checkout (private; read only for a launcher made for
            # one of our online stacks, README.md)
            if key.startswith("REPO_") and (key not in ("REPO_GATES", "REPO_SERVICE") or os.path.exists(os.path.join(root, value))):
                self.assertTrue(os.path.exists(os.path.join(root, value)), f"{key}={value}")

    def test_pins_are_whole_hashes(self):
        for key, value in self.pins.items():
            if key.startswith("SHA1_"):
                self.assertRegex(value, r"^[0-9a-f]{40}$", key)

    def test_the_source_trees_are_pinned_in_forks_only(self):
        # forks/upstream.tsv is the one table of URL, base, head and tree id: no row here repeats it
        for key in self.pins:
            self.assertNotRegex(key, r"_(URL|BASE|TREE|COMMIT)$", key)
        with open(os.path.join(root, "port", "setup", "trees.sh")) as f:
            trees = f.read()
        self.assertIn("apply.sh", trees)
        self.assertIn('check.sh" --trees', trees)
        self.assertNotRegex(trees, r"[0-9a-f]{40}")

    def test_the_table_as_the_shell_reads_it(self):
        read = lambda name: subprocess.run(["bash", "-c", f'. "$0"; printf %s "${name}"', os.path.join(root, "port", "setup", "lib.sh")],
                                           check=True, capture_output=True, text=True).stdout
        for name in ("TOOL_GIT", "VERSION_VS", "VERSION_LLVM", "DISC_DEFAULT", "REPO_FORKS"):
            self.assertEqual(read(name), self.pins[name], name)       # brackets and spaces arrive as written

    def test_the_table_pins_versions_and_names_no_tools_place_and_no_disc(self):
        with open(os.path.join(root, "port", "setup", "pins.tsv")) as f:
            rows = dict(line.rstrip("\n").split("\t")[:2] for line in f if line.strip() and not line.startswith("#"))
        for name in ("TOOL_LLVM_WIN", "TOOL_VCVARS_WIN", "TOOL_PYTHON_WIN"):
            self.assertNotIn(name, rows)                                # found by port/setup/tools.py, or a PC's own row
        self.assertEqual(rows["DISC_DEFAULT"], "")                      # the user's own setting: no default
        for name in ("VERSION_VS", "VERSION_LLVM", "VERSION_PYTHON", "VERSION_CMAKE", "VERSION_NINJA"):
            self.assertTrue(rows[name], name)

    @unittest.skipUnless(shutil.which("wslpath"), "needs WSL's wslpath")
    def test_install_into_another_folder(self):
        tools = manifest.windows_tools(manifest.layout())
        if not all(tools[name] for name in ("TOOL_VCVARS_WIN", "TOOL_LLVM_WIN", "TOOL_PYTHON_WIN")):
            self.skipTest("the Windows build tools are not all here (port/setup/tools.py --say)")
        install = os.path.join(root, "port", "setup", "install.sh")
        with tempfile.TemporaryDirectory() as work:
            env = {k: v for k, v in os.environ.items() if k != "RECOMP_KIT"}    # the harness: the gates' copy, where they are
            # the busy check and the quiet check are a PC's own scripts (PC_SCRIPTS, lib.sh): stand-ins here
            pc = os.path.join(work, "pc-scripts")
            os.makedirs(os.path.join(pc, "none"))
            for name in ("busy-check.sh", "quiet.sh"):
                with open(os.path.join(pc, name), "w") as f:
                    f.write("#!/bin/bash\necho none\n")
            env["PC_SCRIPTS"] = pc
            fork = os.path.join(work, "DolRecomp", "tools", "fastlookup.py")       # the recompiler fork's copy (its tools/)
            os.makedirs(os.path.dirname(fork)); open(fork, "wb").close()
            first = subprocess.run([install, work], check=True, capture_output=True, text=True, env=env).stdout
            self.assertEqual(sha1(fork), sha1(os.path.join(work, "scripts", "fastlookup.py")))   # installed byte for byte
            viewer = os.path.isfile(os.path.join(root, "scripts", "progress-viewer.ps1"))   # this PC's, in the monorepo only
            kit = os.path.isfile(os.path.join(harness := os.path.join(root, "port", "gates", "vendor", "harness", "win"), "launch.ps1"))
            self.assertEqual(first.count("wrote"), 19 + 2 * viewer + 3 * kit)
            if viewer:
                with open(os.path.join(work, "scripts", "progress-viewer.bat")) as f:      # the window that shows a long run, and its
                    self.assertIn('-File "%~dp0progress-viewer.ps1"', f.read())           # starter, which names it beside itself
                self.assertTrue(os.path.isfile(os.path.join(work, "scripts", "progress-viewer.ps1")))
            else:
                self.assertIn("progress-viewer.ps1 in this checkout", first)
                self.assertFalse(os.path.exists(os.path.join(work, "scripts", "progress-viewer.ps1")))
            for name in ("launch.ps1", "time-limit.ps1", "show.ps1") if kit else ():   # the harness's Windows scripts, as they are
                self.assertEqual(open(os.path.join(harness, name), "rb").read(), open(os.path.join(work, "kit-win", name), "rb").read(), name)
            self.assertEqual(os.path.exists(os.path.join(work, "kit-win")), kit)   # the gates' (optional: private)
            self.assertEqual(subprocess.run([install, work, "--check"], capture_output=True, env=env).returncode, 0)
            gone = subprocess.run([install, work, "--check"], capture_output=True, text=True,
                                  env=dict(env, RECOMP_KIT=os.path.join(work, "no-kit"))).stdout
            self.assertIn("no harness at " + os.path.join(work, "no-kit", "tools", "harness", "win") + " (the gates, pins.tsv REPO_GATES, or RECOMP_KIT): kit-win is left out", gone)
            alone = subprocess.run([install, work, "--check"], capture_output=True, text=True,
                                   env=dict(env, PC_SCRIPTS=os.path.join(pc, "none"))).stdout
            self.assertIn("install: no busy check and quiet check (busy-check.sh, quiet.sh) in this PC's own scripts", alone)
            for name, said in manifest.scripts(work, manifest.layout()).items():
                self.assertTrue(said["is_tracked_file"], name)         # the build's files (the superbuild's, fastlookup.py) as tracked
            for name in ("build-mg.bat", "build-module.bat", "build-dolrecomp-llvm.bat"):   # none: the build is CMake's
                self.assertFalse(os.path.exists(os.path.join(work, "scripts", name)), name)
            import tomllib
            windows = subprocess.run(["wslpath", "-w", work], check=True, capture_output=True, text=True).stdout.strip()
            for name in ("machine-gate-n.toml", "machine-n.toml", "machine-first.toml"):
                with open(os.path.join(work, "port", name), "rb") as f:
                    self.assertNotRegex(f.read(), b"@(WORK|LLVM)", name)
                    f.seek(0)
                    machine = tomllib.load(f)
                self.assertEqual((machine["work"], machine["work_win"]), (os.path.realpath(work), windows), name)
                self.assertEqual(machine["post_emit"][0][1], os.path.realpath(work) + "/scripts/fastlookup.py")
                self.assertEqual(machine["recompiler"], "work/dolrecomp-port/dolrecomp.exe")
                llvm = tools["TOOL_LLVM_WIN"]                        # where port/setup/tools.py finds LLVM
                self.assertEqual(machine["llvm_bin_win"], llvm.replace("\\", "/") + "/bin")
                self.assertEqual(machine["llvm_bin"], "/mnt/" + llvm[0].lower() + llvm[2:].replace("\\", "/") + "/bin")
                self.assertTrue(machine["vcvars"].endswith("vcvars64.bat") and "\\" in machine["vcvars"])
            for name in ("port/Dolphin.ini.gate-nocheats", "port/Dolphin.ini.pristine", "scratch/Dolphin.ini.single"):
                with open(os.path.join(work, name)) as f:
                    text = f.read()
                self.assertIn("CustomRTCValue = 0x386d4380", text, name)     # the fixed clock: another value is another race
                self.assertIn("Overclock = 6.0", text, name)
            # the gate scenes' user folder: gate-native.sh copies a configuration into it, so it must be there
            with open(os.path.join(work, "scratch", "Dolphin.ini.single"), "rb") as a, \
                    open(os.path.join(work, "user", "Config", "Dolphin.ini"), "rb") as b:
                self.assertEqual(a.read(), b.read())
            for name in ("config.ini", "Config/GFX.ini", "Config/GCPadNew.ini"):
                self.assertTrue(os.path.isfile(os.path.join(work, "user", name)), name)
            # beside the busy check, the folders work-folder.env resolves to, as fixed lines the reader takes
            beside = work_folder.read(os.path.join(work, "port", "scratch", "rp", "work-folder.env"), local=None)
            self.assertEqual(beside, {"PORT_WORK_DEFAULT": work_folder.WORK_DEFAULT, "PORT_LOCKS_DEFAULT": work_folder.LOCKS_DEFAULT})
            # the busy check is installed when absent and never overwritten; a changed build script is reported
            check = os.path.join(work, "port", "scratch", "rp", "chk.sh")
            with open(check, "w") as f:
                f.write("#!/bin/bash\nexit 3\n")
            with open(os.path.join(work, "scripts", "fastlookup.py"), "a") as f:
                f.write("# changed\n")
            again = subprocess.run([install, work], check=True, capture_output=True, text=True).stdout
            self.assertIn("kept      port/scratch/rp/chk.sh (never overwritten); it DIFFERS", again)
            with open(check) as f:
                self.assertEqual(f.read(), "#!/bin/bash\nexit 3\n")
            with open(check, "w") as f:
                f.write("x")
            with open(os.path.join(work, "scripts", "fastlookup.py"), "a") as f:
                f.write("# changed\n")
            report = subprocess.run([install, work, "--check"], capture_output=True, text=True)
            self.assertEqual(report.returncode, 1)
            self.assertIn("DIFFERENT port/scratch/rp/chk.sh", report.stdout)
            self.assertIn("DIFFERENT scripts/fastlookup.py", report.stdout)

    def test_the_shared_work_folder_is_only_checked(self):
        self.assertNotIn("SHARED_WORK", self.pins)                  # work-folder.env is the one place that names it
        # a PC's own work folder (~/.config/piconet/work-folder.env) is shared: setup.sh and install.sh refuse it
        with tempfile.TemporaryDirectory() as home, tempfile.TemporaryDirectory() as work:
            os.makedirs(os.path.join(home, ".config", "piconet"))
            with open(os.path.join(home, ".config", "piconet", "work-folder.env"), "w") as f:
                f.write("PORT_WORK_DEFAULT=/mnt/q/shared\n")
            # (a work folder named in the caller's environment would be the scripts' choice over the PC's file)
            clean = {k: v for k, v in os.environ.items() if k not in ("PORT_WORK", "PORT_WORK_WIN", "PORT_LOCKS")}
            env = dict(clean, HOME=home)
            for script, args in (("install.sh", ["/mnt/q/shared"]), ("setup.sh", ["/mnt/q/shared"]), ("setup.sh", [])):
                done = subprocess.run([os.path.join(root, "port", "setup", script), *args], capture_output=True, text=True, env=env)
                self.assertEqual(done.returncode, 1, script)
                self.assertIn("shared work folder", done.stderr, script)
            # install.sh writes it only when told that it is the shared one; the word is refused for any other folder
            done = subprocess.run([os.path.join(root, "port", "setup", "install.sh"), work, "--shared"], capture_output=True, text=True, env=env)
            self.assertEqual((done.returncode, os.listdir(work)), (1, []))
            self.assertIn("--shared is for this PC's shared work folder", done.stderr)
            # without a PC's file the default is the checkout's own build/work: no shared folder, so no --shared
            done = subprocess.run([os.path.join(root, "port", "setup", "install.sh"), work_folder.WORK_CHECKOUT, "--shared"],
                                  capture_output=True, text=True, env=dict(clean, HOME=work))
            self.assertEqual(done.returncode, 1)
            self.assertIn("--shared is for this PC's shared work folder", done.stderr)
            self.assertFalse(os.path.exists(work_folder.WORK_CHECKOUT))

    def test_a_staged_launcher_names_its_module_and_its_bundle(self):
        # no default: a module and a bundle are always named
        stage = os.path.join(root, "port", "build", "stage-launcher.sh")
        with tempfile.TemporaryDirectory() as work:
            env = {k: v for k, v in os.environ.items() if k not in ("MODULE", "BUNDLE", "GAME", "RUNNER")}
            env.update(PORT_WORK=work, PORT_WORK_WIN="C:\\w")
            for more, word in (({}, "MODULE=cg-NAME"), ({"MODULE": "cg-x"}, "BUNDLE=<folder>")):
                done = subprocess.run([stage], env=dict(env, **more), capture_output=True, text=True, cwd=root)
                self.assertEqual((done.returncode, os.listdir(work)), (1, []), done.stderr)
                self.assertIn(word, done.stderr)


class Tools(unittest.TestCase):
    """port/setup/tools.py: a PC's own row is taken as written and its version checked; what is missing or of another
    version is one line that says what is needed and how to point at it. Made-up tools (scripts named as the .exe)."""

    def tool(self, folder, name, says):
        os.makedirs(os.path.dirname(os.path.join(folder, name)), exist_ok=True)
        with open(os.path.join(folder, name), "w") as f:
            f.write(f"#!/bin/sh\necho '{says}'\n")
        os.chmod(os.path.join(folder, name), 0o755)

    def test_a_pcs_llvm_of_another_version_or_without_lib_cmake_is_refused_with_how_to_point_at_one(self):
        lay = {"VERSION_LLVM": "20.1.8"}
        with tempfile.TemporaryDirectory() as llvm:
            self.assertIsNone(tools.llvm(dict(lay, TOOL_LLVM_WIN=llvm))[0])
            self.tool(llvm, "bin/clang.exe", "clang version 19.1.0")
            found, said = tools.llvm(dict(lay, TOOL_LLVM_WIN=llvm))
            self.assertEqual((found, said.split(": LLVM 20.1.8 for")[0]), (None, f"LLVM 19.1.0 at {llvm}, but 20.1.8 is needed"))
            self.assertIn("TOOL_LLVM_WIN (its folder)", said)
            self.assertIn("~/.config/piconet/pins.tsv", said)
            self.tool(llvm, "bin/clang.exe", "clang version 20.1.8")
            self.assertIn("has no lib/cmake/llvm", tools.llvm(dict(lay, TOOL_LLVM_WIN=llvm))[1])
            os.makedirs(os.path.join(llvm, "lib", "cmake", "llvm"))
            self.assertEqual(tools.llvm(dict(lay, TOOL_LLVM_WIN=llvm)), (llvm, None))

    def test_a_pcs_python_and_visual_studio_rows_are_checked(self):
        with tempfile.TemporaryDirectory() as python:
            self.tool(python, "python.exe", "3.12 " + python)
            found, said = tools.python({"VERSION_PYTHON": "3.10", "TOOL_PYTHON_WIN": python})
            self.assertEqual(found, None)
            self.assertTrue(said.startswith(f"Python 3.12 at TOOL_PYTHON_WIN {python}, but 3.10 is needed"), said)
            self.tool(python, "python.exe", "3.10 " + python)
            self.assertEqual(tools.python({"VERSION_PYTHON": "3.10", "TOOL_PYTHON_WIN": python}), (python, None))
            found, said = tools.visual_studio({"VERSION_VS": "[17.0,18.0)", "TOOL_VCVARS_WIN": python + "/vcvars64.bat"})
            self.assertEqual(found, None)
            self.assertIn("Visual Studio 2022 Build Tools (version [17.0,18.0)) with the C++ workload", said)
            self.assertIn("TOOL_VCVARS_WIN (its vcvars64.bat)", said)

    def test_the_shell_gets_the_tools_or_one_line_each_and_status_1(self):
        with tempfile.TemporaryDirectory() as home:
            os.makedirs(os.path.join(home, ".config", "piconet"))
            with open(os.path.join(home, ".config", "piconet", "pins.tsv"), "w") as f:
                f.write(f"name\tvalue\tnote\nTOOL_LLVM_WIN\t{home}/none\t\nTOOL_VCVARS_WIN\t{home}/none.bat\t\n"
                        f"TOOL_PYTHON_WIN\t{home}/none\t\n")
            done = subprocess.run(["bash", "-c", '. "$0"; find_tools; echo "status $? ${TOOL_LLVM_WIN:-}"', os.path.join(root, "port", "setup", "lib.sh")],
                                  capture_output=True, text=True, env=dict(os.environ, HOME=home))
            self.assertTrue(done.stdout.startswith("status 1 "), done.stdout)
            lines = done.stderr.splitlines()
            self.assertEqual([line.split(" ")[1] for line in lines], ["no", "no", "no"], done.stderr)
            for line, name in zip(lines, ("TOOL_VCVARS_WIN", "TOOL_LLVM_WIN", "TOOL_PYTHON_WIN")):
                self.assertTrue(line.startswith("tools: ") and name in line and "~/.config/piconet/pins.tsv" in line, line)


class WorkFolder(unittest.TestCase):
    """work-folder.env, the one place that says where the work folder and the PC's locks are."""
    file = os.path.join(root, "port", "setup", "work-folder.env")

    def shell(self, script, **env):
        clean = {k: v for k, v in os.environ.items() if k not in ("PORT_WORK", "PORT_LOCKS")}
        return subprocess.run(["bash", "-euc", script, self.file], env=dict(clean, **env), check=True, capture_output=True,
                              text=True).stdout

    def test_the_shell_and_python_read_the_same_folders(self):
        read = self.shell('. "$0"; printf "%s %s" "$PORT_WORK_DEFAULT" "$PORT_LOCKS_DEFAULT"')
        self.assertEqual(read, f"{work_folder.WORK_DEFAULT} {work_folder.LOCKS_DEFAULT}")
        # without a PC's own file, both are build/work in this checkout, the shell's and Python's the same
        self.assertEqual(work_folder.WORK_CHECKOUT, os.path.join(root, "build", "work"))
        with tempfile.TemporaryDirectory() as home:
            read = self.shell('cd /; . "$0"; printf "%s %s %s" "$PORT_WORK_CHECKOUT" "$PORT_WORK_DEFAULT" "$PORT_LOCKS_DEFAULT"', HOME=home)
        self.assertEqual(read, " ".join([work_folder.WORK_CHECKOUT] * 3))
        self.assertEqual(work_folder.read(local=None), dict.fromkeys(work_folder.NAMES, work_folder.WORK_CHECKOUT))

    def test_the_environment_still_decides(self):
        script = '. "$0"; W=${PORT_WORK:-$PORT_WORK_DEFAULT}; L=${PORT_LOCKS:-$PORT_LOCKS_DEFAULT}; printf "%s %s" "$W" "$L"'
        self.assertEqual(self.shell(script), f"{work_folder.WORK_DEFAULT} {work_folder.LOCKS_DEFAULT}")
        self.assertEqual(self.shell(script, PORT_WORK="/mnt/x/mine"), f"/mnt/x/mine {work_folder.LOCKS_DEFAULT}")
        self.assertEqual(self.shell(script, PORT_LOCKS="/mnt/x/locks"), f"{work_folder.WORK_DEFAULT} /mnt/x/locks")

    def test_the_file_holds_only_comments_and_its_two_folders(self):
        # the busy check sources it: a command, a quote or a third name there is refused by the reader
        for bad in ("PORT_WORK_DEFAULT=/mnt/x/a\n", "PORT_WORK_DEFAULT=/mnt/x/a\nPORT_LOCKS_DEFAULT=/mnt/x/a\necho hello\n",
                    "PORT_WORK_DEFAULT=/mnt/x/a\nPORT_LOCKS_DEFAULT=\"/mnt/x/a\"\n",
                    "PORT_WORK_DEFAULT=/mnt/x/a\nPORT_LOCKS_DEFAULT=/mnt/x/a; rm -rf /\n",
                    "PORT_WORK_DEFAULT=/mnt/x/a\nPORT_WORK_DEFAULT=/mnt/x/b\nPORT_LOCKS_DEFAULT=/mnt/x/a\n"):
            with tempfile.NamedTemporaryFile("w", suffix=".env") as f:
                f.write("# a comment\n" + bad)
                f.flush()
                with self.assertRaises(ValueError, msg=bad):
                    work_folder.read(f.name)
        self.assertEqual(sorted(work_folder.read()), ["PORT_LOCKS_DEFAULT", "PORT_WORK_DEFAULT"])
        # the tracked file's own four lines are taken only as they are
        with open(self.file) as f:
            tracked = f.read()
        for was, now in (("/build/work", "/build/work; echo hello"), ("$PORT_WORK_CHECKOUT\n", "/mnt/x/a\n")):
            with tempfile.NamedTemporaryFile("w", suffix=".env") as f:
                f.write(tracked.replace(was, now, 1))
                f.flush()
                with self.assertRaises(ValueError, msg=now):
                    work_folder.read(f.name)

    def test_a_pcs_own_file_is_read_over_the_defaults_and_holds_only_folders_too(self):
        # this PC's folders are in ~/.config/piconet/work-folder.env, outside every checkout: the shell (the file's
        # last line) and the reader take it over the tracked defaults, one name or both; without it, the defaults
        defaults = work_folder.read(local=None)
        self.assertEqual(set(defaults.values()), {work_folder.WORK_CHECKOUT})   # no PC's folder in the tracked file
        script = '. "$0"; printf "%s %s" "$PORT_WORK_DEFAULT" "$PORT_LOCKS_DEFAULT"'
        with tempfile.TemporaryDirectory() as home:
            self.assertEqual(self.shell(script, HOME=home), f"{defaults['PORT_WORK_DEFAULT']} {defaults['PORT_LOCKS_DEFAULT']}")
            own = os.path.join(home, ".config", "piconet", "work-folder.env")
            os.makedirs(os.path.dirname(own))
            with open(own, "w") as f:
                f.write("# this PC's\nPORT_WORK_DEFAULT=/mnt/q/mine\n")
            self.assertEqual(self.shell(script, HOME=home), f"/mnt/q/mine {defaults['PORT_LOCKS_DEFAULT']}")
            self.assertEqual(work_folder.read(local=own)["PORT_WORK_DEFAULT"], "/mnt/q/mine")
            with open(own, "a") as f:
                f.write("echo hello\n")
            with self.assertRaises(ValueError):
                work_folder.read(local=own)

    def test_lib_sh_reads_a_pcs_own_pins_over_the_tables(self):
        script = '. "$0"; printf "%s|%s" "$DISC_DEFAULT" "$REPO_FORKS"'
        lib = os.path.join(root, "port", "setup", "lib.sh")
        clean = {k: v for k, v in os.environ.items() if k not in ("PORT_WORK", "PORT_LOCKS")}
        with tempfile.TemporaryDirectory() as home:
            run = lambda: subprocess.run(["bash", "-euc", script, lib], env=dict(clean, HOME=home), check=True,
                                         capture_output=True, text=True).stdout
            disc, forks = run().split("|")
            self.assertEqual(disc, "")                                     # no default disc: the user's own setting
            os.makedirs(os.path.join(home, ".config", "piconet"))
            with open(os.path.join(home, ".config", "piconet", "pins.tsv"), "w") as f:
                f.write("name\tvalue\tnote\nDISC_DEFAULT\t/mnt/q/my disc.iso\tmine\n")
            self.assertEqual(run(), f"/mnt/q/my disc.iso|{forks}")



class Manifest(unittest.TestCase):
    def test_the_switches_file(self):
        s = manifest.switches(os.path.join(root, manifest.layout()["REPO_SWITCHES"]))
        self.assertEqual(len(s["values"]), 24)                                     # n6's 23 and the lanes' record
        self.assertEqual((s["values"]["DOLRECOMP_CODEGEN_REV"], s["values"]["DOLRECOMP_MEM2_TRACK"]),
                         (manifest.layout()["RELEASE_CODEGEN_REV"], "1"))              # the module the last review proved
        self.assertEqual((s["values"]["DOLRECOMP_PS_MADD"], s["values"]["DOLRECOMP_ENTRY_QUERY"]), ("guarded", "off"))
        self.assertEqual(s["values"]["DOLRECOMP_CLOSED_ROOTS"].count(","), 15)
        self.assertEqual(manifest.switches("/nowhere/x.env")["sha1"], None)

    def test_what_is_not_established_is_named(self):
        self.assertEqual(manifest.unset({"a": 1, "b": {"c": None, "d": [1, None]}, "e": "none"}), ["b.c", "b.d[1]"])

    def base(self):
        return {"kind": "module", "name": "t", "missing": [], "complete": False,
                "repository": {"commit": "c" * 40, "branch": "main", "differs_from_commit": []},
                "trees": {"dolrecomp": {"commit": "d" * 40, "tree": "e" * 40, "uncommitted": []},
                          "recompcore_dolrecomp": {"commit": "f" * 40, "tree": "0" * 40, "uncommitted": []}},
                "outputs": {"gGFZE01_recomp.dll": {"sha1": "1" * 40}}, "made": {"at": "now"}}

    def finish(self, m):
        with tempfile.TemporaryDirectory() as folder:
            to = os.path.join(folder, "build-manifest.json")
            with open(os.devnull, "w") as quiet:
                out, sys.stdout = sys.stdout, quiet
                try:
                    status = manifest.finish(m, to)
                finally:
                    sys.stdout = out
            with open(to) as f:
                return status, json.load(f)

    def test_complete_only_when_everything_is_there(self):
        status, m = self.finish(self.base())
        self.assertEqual((status, m["complete"], m["missing"]), (0, True, []))
        for spoil, word in ((lambda m: m["outputs"]["gGFZE01_recomp.dll"].update(sha1=None), "outputs.gGFZE01_recomp.dll.sha1"),
                            (lambda m: m["repository"].update(differs_from_commit=["port/core/x.cpp"]), "repository"),
                            (lambda m: m["trees"]["dolrecomp"].update(uncommitted=["a.c"]), "trees.dolrecomp"),
                            (lambda m: m["trees"]["recompcore_dolrecomp"].update(uncommitted=["src/x.c"]), "trees.recompcore_dolrecomp"),
                            (lambda m: m["missing"].append("inputs changed while the build ran: main.rel"), "inputs changed")):
            m = self.base(); spoil(m)
            status, m = self.finish(m)
            self.assertEqual((status, m["complete"]), (1, False))
            self.assertTrue(any(word in line for line in m["missing"]), word)

    def test_compare_leaves_out_where_and_when(self):
        one, two = manifest.flat(self.base()), manifest.flat(self.base())
        two["made.at"] = "later"
        self.assertEqual([k for k in one if one[k] != two[k] and not k.startswith("made.")], [])


if __name__ == "__main__":
    unittest.main()
