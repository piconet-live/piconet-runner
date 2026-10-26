#!/usr/bin/env python3
"""package.py: a player copy from named parts: the folder and its zip. port/build/package.sh is the command; it
publishes the launcher and calls this, which checks every part before it writes anything.

A copy is, at its top: one program, named as the application is, and the read-me (from the template in
player-copy/); and folders: the published launcher in its own (the program runs it from there, and the marker in it
tells the launcher that it is a copy and keeps its data here); recomp/ with the runner, the native module, the image
bundle (fzgx/), the emulator's Sys folder as real files, the four VC runtime files and the font files the game
window's strip is drawn in (Fonts/, with their licence); and source/, the builds' manifests, which name the commit
and tree of every source tree the runner and the module were built from, with this script's own record. The runner
is GPL code: the read-me points to its source, the public repositories at this release's tag (SOURCE_LINE).

What it refuses, each with the offending path:
  - a checkout that is not clean (this repository's, or the launcher's, --launcher-repository), or a launcher that
    was not published from the commit the launcher's checkout has checked out; a program that does not start the
    launcher in its folder;
  - a batch file, a script Windows runs on a double-click or a shortcut anywhere (START_FILES), any file at the top
    but the program and the read-me, and any folder at the top but the launcher's, recomp and source;
  - a launcher that was not made for the copy's stack: its own program must carry the stack's name, its address and
    whether it is a test build, as the copy is said to be (--stack-address, --stack-kind);
  - a runner or module whose manifest is missing, not complete, or describes another file (SHA-1); a runner built
    from another commit than the one named; a module that was not built from this image; a bundle whose manifest
    names another image;
  - a Sys folder that is not, file for file, the emulator's own at the commit the runner's manifest names (so a
    file planted there, a missing one and a changed one all stop it);
  - a link of any kind (a junction, a symbolic link) in a part: the copy holds real files only;
  - anything of the game or of a player: a disc image, a file or folder of the extracted game, a `data` folder, a
    save, a replay, a dump, a log, a picture (the emulator's own, checked against its source, are the exception);
  - a personal identity anywhere in it: every file is searched, as UTF-8, UTF-16 and UTF-32 text, for this PC's and
    the repositories' git users' names, gathered when the script runs (package_identities.py; the file and the kind
    of name are printed, never the name); and a source of names that could not be asked (--identity-without). The copy's own papers are written without one: a build's manifest
    goes in without its host name and home folders;
  - an image that would refuse a released build's replays: the table of same races (same-race.json, its
    `released` list) names no row from that build's image to this one (same_race.py), unless
    --old-replays-refused (said on purpose, for a build that is not to play them);
  - an output that exists (REPLACE=1 makes it again), or a place that is not <work>/dist.

The copy is named, dated and stamped by the launcher's commit (its own checkout's, separate from this repository's
since the split); the runner's commit is the one its manifest names (the record's parts.source.runner_commit).
The same parts give the same copy: files in name order, every time stamp the launcher's commit's, no path of the
PC and no clock in the record. Status 0 and the copy's name, or status 1 and "package: REFUSED: why: path"."""
import argparse, hashlib, json, os, re, shutil, subprocess, sys, time, zipfile

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
import manifest                       # pins.tsv (layout), Windows' spelling of a path (win), the module's file name
from package_refusals import Refused, refuse_game_data   # what a copy must never hold: the game's and a player's files
from package_identities import neutral_manifest, refuse_identities   # nor anybody's name
import same_race                         # which released builds' replays the copy would refuse

RUNNER_FILE, MODULE_FILE = "moderngekko-run.exe", manifest.MODULE_FILE
BUNDLE_FILES = ("mod.bin", "manifest.json", "patches.json")     # with same-race.json, which the checkout holds
VC_RUNTIME = ("MSVCP140.dll", "MSVCP140_ATOMIC_WAIT.dll", "VCRUNTIME140.dll", "VCRUNTIME140_1.dll")
# the game window: the three files port/core/include/fzgx/strip.h names (the strip and the overlays), and the licence they travel with
FONT_FILES = ("SairaCondensed-ExtraBold.ttf", "SairaCondensed-SemiBold.ttf", "SairaSemiCondensed-Regular.ttf", "OFL.txt")
SYS_IN_TREE = "Data/Sys"              # the emulator's Sys folder in its source tree
LEFT_OUT = (".pdb",)                  # of the published launcher: two native libraries' debug symbols (100 MB)
RECORD = "source/package-manifest.json"
READ_ME = "READ ME.txt"
LAUNCHER_LIBRARY = "Launcher.App.dll"  # the launcher itself: it holds the build's stamp, and the program names it
# What Windows starts or follows on a double-click besides a program; a copy is started by its one program
START_FILES = (".cmd", ".bat", ".lnk", ".url", ".pif", ".scf", ".scr", ".com", ".cpl", ".ps1", ".psm1", ".psd1", ".vbs", ".vbe",
               ".js", ".jse", ".wsf", ".wsh", ".hta", ".msi", ".msp", ".reg", ".jar")
TOP_FOLDERS = ("recomp", "source")    # beside the launcher's own folder, whose name the launcher's source gives
MARKER_TEXT = ("This file marks the folder above as a portable copy: the launcher keeps everything it writes in the\r\n"
               "\"data\" folder beside its program. Leave the file here.\r\n").encode("ascii")
BUILT_IN = "the launcher's built-in one"
def facts(data=None, path=None):
    """SHA-1, git's blob id, size and first bytes of a file (or of bytes), in one reading."""
    size = len(data) if path is None else os.path.getsize(path)
    plain, blob, head = hashlib.sha1(), hashlib.sha1(b"blob %d\0" % size), data[:32] if path is None else None
    if path is None:
        plain.update(data); blob.update(data)
    else:
        with open(path, "rb") as f:
            for block in iter(lambda: f.read(1 << 20), b""):
                head = block[:32] if head is None else head
                plain.update(block); blob.update(block)
    return {"sha1": plain.hexdigest(), "blob": blob.hexdigest(), "bytes": size, "head": head or b""}


def real_file(path, what):
    if os.path.islink(path):
        raise Refused(f"{what} is a link, not a real file", path)
    if not os.path.isfile(path):
        raise Refused(f"{what} is not there", path)
    return path


def walk(folder, what):
    """Every file under a folder as (path, name with /), in name order. A link of any kind stops the copy."""
    if os.path.islink(folder):
        raise Refused(f"{what} is a link (a junction or a symbolic link), not a real folder", folder)
    if not os.path.isdir(folder):
        raise Refused(f"{what} is not there", folder)
    out, todo = [], [folder]
    while todo:
        with os.scandir(todo.pop()) as entries:
            for entry in entries:
                if entry.is_symlink():
                    raise Refused(f"{what} holds a link (a junction or a symbolic link)", entry.path)
                if entry.is_dir(follow_symlinks=False):
                    todo.append(entry.path)
                elif entry.is_file(follow_symlinks=False):
                    out.append((entry.path, os.path.relpath(entry.path, folder).replace(os.sep, "/")))
                else:
                    raise Refused(f"{what} holds something that is neither a file nor a folder", entry.path)
    return sorted(out, key=lambda item: item[1])


def read_json(path, what):
    try:
        with open(real_file(path, what), encoding="utf-8") as f:
            return json.load(f)
    except ValueError:
        raise Refused(f"{what} is not JSON", path)


def git(tree, *arguments):
    """git on a tree, its output as bytes (None: it failed). A tree on a Windows drive is read by Git for Windows,
    as the builds read it."""
    on_windows = re.match(r"/mnt/[a-z]/", os.path.abspath(tree) + "/")
    command = [manifest.layout()["TOOL_GIT"], "-C", manifest.win(tree)] if on_windows else ["git", "-C", tree]
    try:
        done = subprocess.run(command + list(arguments), capture_output=True, stdin=subprocess.DEVNULL, cwd=tree)
    except OSError:
        return None
    return None if done.returncode else done.stdout


def listing(tree, commit, folder):
    """name -> blob id for the files of a folder at a commit. A link or a submodule there stops the copy."""
    out = git(tree, "ls-tree", "-r", "-z", commit, folder + "/")
    if out is None:
        raise Refused(f"no commit {commit[:8]} to read {folder} from", tree)
    files = {}
    for row in filter(None, out.decode("utf-8", "surrogateescape").split("\0")):
        (mode, kind, blob), path = row.split("\t", 1)[0].split(), row.split("\t", 1)[1]
        if kind != "blob" or not mode.startswith("100"):
            raise Refused(f"{folder} at commit {commit[:8]} holds a link or a submodule", path)
        files[path[len(folder) + 1:]] = blob
    if not files:
        raise Refused(f"commit {commit[:8]} has no {folder}", tree)
    return files


class Plan:
    """What the copy will hold: name -> where it comes from (a file, or bytes made here) and its facts."""
    def __init__(self):
        self.files, self.names = {}, {}

    def add(self, name, part, path=None, data=None):
        if name.lower() in self.names:
            raise Refused(f"two parts write the same file ({self.names[name.lower()]} and {part})", name)
        self.names[name.lower()] = part
        self.files[name] = {"part": part, "path": path, "data": data, "own": False, **facts(data, path)}

    def add_folder(self, to, part, folder, leave_out=()):
        left = []
        for path, name in walk(folder, part):
            if name.lower().endswith(leave_out):
                left.append(name)
            else:
                self.add(to + name, part, path)
        return left


def checkout(repository):
    """The commit checked out, which must be clean: this repository's (the table of same races is read from it) or the
    launcher's (the launcher is published from it, so nothing may differ from it)."""
    status = git(repository, "status", "--porcelain")
    if status is None:
        raise Refused("not a git checkout", repository)
    if status.strip():
        raise Refused("the checkout is not clean (commit first: the copy is named by its commit)",
                      status.decode("utf-8", "replace").splitlines()[0][3:])
    text = lambda *a: git(repository, *a).decode().strip()
    return {"commit": text("rev-parse", "HEAD"), "build": text("rev-parse", "--short", "HEAD"),
            "date": text("show", "-s", "--format=%cs", "HEAD"), "time": int(text("show", "-s", "--format=%ct", "HEAD"))}


def built(path, kind, name, what):
    """A build's manifest, which must be complete and describe the file beside it (port/build/manifest.py)."""
    real_file(path, what)
    at = os.path.join(os.path.dirname(path), "build-manifest.json")
    m = read_json(at, f"{what}'s manifest")
    if m.get("kind") != kind:
        raise Refused(f"{what}'s manifest is a {m.get('kind')}'s", at)
    if m.get("complete") is not True or m.get("missing"):
        raise Refused(f"{what}'s manifest is not complete (a test build, not one anyone ships)", at)
    file = facts(path=path)
    if (m.get("outputs", {}).get(name) or {}).get("sha1") != file["sha1"]:
        raise Refused(f"{what} is not the file its manifest describes (SHA-1 {file['sha1'][:8]})", path)
    return m, at, file


def parts(a, plan):
    """The game's side of a copy: the runner, the module, the image bundle, Sys, the VC runtime, the source."""
    runner, runner_at, runner_file = built(a.runner, "runner", RUNNER_FILE, "the runner")
    commit = str(runner["repository"]["commit"])
    if len(a.runner_commit) < 7 or not commit.startswith(a.runner_commit.lower()):
        raise Refused(f"the runner was built from commit {commit[:8]}, not from {a.runner_commit}", runner_at)
    module, module_at, module_file = built(a.module, "module", MODULE_FILE, "the module")
    image = read_json(os.path.join(a.bundle, "manifest.json"), "the bundle's manifest")
    plan.add("recomp/" + RUNNER_FILE, "the runner", a.runner)
    plan.add("recomp/" + MODULE_FILE, "the module", a.module)
    for name in BUNDLE_FILES:
        plan.add("recomp/fzgx/" + name, "the image bundle", real_file(os.path.join(a.bundle, name), "the bundle's " + name))
        if ((module.get("inputs") or {}).get("bundle") or {}).get(name) != plan.files["recomp/fzgx/" + name]["sha1"]:
            raise Refused(f"the module was not built from this image ({name} is another)", os.path.join(a.bundle, name))
    image_sha1 = plan.files["recomp/fzgx/mod.bin"]["sha1"]
    if image.get("complete") is not True or image.get("form") != "native" or image.get("image_sha1") != image_sha1:
        raise Refused("the bundle's manifest is not a complete native image's, or names another image than mod.bin",
                      os.path.join(a.bundle, "manifest.json"))
    lay = manifest.layout()
    table = os.path.join(a.repository, lay["REPO_IMAGE"], "same-race.json")
    plan.add("recomp/fzgx/same-race.json", "the image bundle", real_file(table, "the table of same races"))
    try:
        lost = same_race.refused(*same_race.load(table), image_sha1)
    except (OSError, ValueError) as why:
        raise Refused(f"the table of same races cannot be read ({why})", table)
    if lost and not a.old_replays_refused:
        raise Refused("this image would refuse the replays of released builds (" + ", ".join(f"{v}, image {i[:8]}" for v, i in lost)
                      + "): play them back on it, add the rows, or OLD_REPLAYS_REFUSED=1 on purpose", table)
    emulator_sys(a, plan, runner["trees"]["recompcore"]["commit"])
    for name in VC_RUNTIME:
        found = [n for n in os.listdir(a.vcrt) if n.lower() == name.lower()] if os.path.isdir(a.vcrt) else []
        plan.add("recomp/" + name, "the VC runtime", real_file(os.path.join(a.vcrt, found[0] if found else name), "the VC runtime's " + name))
        if plan.files["recomp/" + name]["head"][:2] != b"MZ":
            raise Refused("the VC runtime's file is no Windows library", os.path.join(a.vcrt, name))
    for name in FONT_FILES:
        plan.add("recomp/Fonts/" + name, "the game window's fonts", real_file(os.path.join(a.fonts, name), "the game window's " + name))
    records = {}
    for part, at in (("runner", runner_at), ("module", module_at)):      # as the build wrote it, but for the PC's and its user's names
        with open(at, "rb") as f:
            records[part] = facts(f.read())["sha1"]
            f.seek(0)
            plan.add(f"source/{part}-build-manifest.json", "the source", data=neutral_manifest(f.read()))
    return {"runner": {"sha1": runner_file["sha1"], "built_from": commit, "manifest_as_built_sha1": records["runner"]},
            "module": {"sha1": module_file["sha1"], "name": module.get("name"), "built_from": module["repository"]["commit"],
                       "manifest_as_built_sha1": records["module"],
                       "same_trees_as_the_runner": module.get("trees") == runner.get("trees")},
            "image": {"sha1": image_sha1}, "emulator": {"commit": runner["trees"]["recompcore"]["commit"]},
            "vc_runtime": {n: plan.files["recomp/" + n]["sha1"] for n in VC_RUNTIME},
            "source": {"runner_commit": commit, "trees": {k: {"commit": t.get("commit"), "tree": t.get("tree")} for k, t in runner["trees"].items()}},
            "game_build": "/".join(x[:8] for x in (image_sha1, module_file["sha1"], runner_file["sha1"]))}


def emulator_sys(a, plan, commit):
    """Sys, file for file the emulator's own at the commit the runner's manifest names: nothing more, nothing less."""
    want = listing(a.sys_source, commit, SYS_IN_TREE)
    plan.add_folder("recomp/Sys/", "the emulator's Sys folder", a.sys)
    for name, blob in want.items():
        file = plan.files.get("recomp/Sys/" + name)
        if file is None:
            raise Refused(f"Sys lacks a file of the emulator's (commit {commit[:8]})", os.path.join(a.sys, name))
        if file["blob"] != blob:
            raise Refused(f"a file in Sys is not the emulator's own (commit {commit[:8]})", file["path"])
        file["own"] = True
    for name, file in plan.files.items():
        if name.startswith("recomp/Sys/") and not file["own"]:
            raise Refused(f"a file in Sys is not in the emulator's source (commit {commit[:8]})", file["path"])


def launcher(a, plan, at):
    """The published launcher, in its folder. Its own file says which commit it was built from: the one the launcher's
    checkout has checked out (at, from --launcher-repository)."""
    with open(real_file(os.path.join(a.launcher, LAUNCHER_LIBRARY), "the launcher's program"), "rb") as f:
        stamps = set(re.findall(rb"(\d+\.\d+\.\d+)\+([0-9a-f]{7,40})(?![0-9a-f])", f.read()))
    versions = sorted({v.decode() for v, c in stamps if c.decode() == at["build"]})
    if len(versions) != 1:
        said = ", ".join(sorted(f"{v.decode()}+{c.decode()}" for v, c in stamps)) or "no build"
        raise Refused(f"the launcher there was not published from the commit checked out ({at['build']}); it says {said}", a.launcher)
    real_file(os.path.join(a.launcher, "Launcher.App.exe"), "the launcher")
    return versions[0], plan.add_folder(a.program_folder + "/", "the launcher", a.launcher, LEFT_OUT)


def program(a, plan):
    """The copy's one program and its marker. The program is the launcher's host file as the publish wrote it for a
    copy (Launcher.App.csproj, WriteCopyProgram): it names the launcher under the program's folder, and one that does
    not would start nothing. The marker beside the launcher's libraries tells the launcher it is a copy."""
    for said, what in ((a.program_name, "the program's name"), (a.program_folder, "the launcher's folder"), (a.marker, "the marker's name")):
        if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9 ._-]*", said) or said.endswith((" ", ".")):
            raise Refused(f"{what} is letters, digits, spaces, '.', '_' and '-'", said)
    if a.program_folder.lower() in (*TOP_FOLDERS, "data"):
        raise Refused("the launcher's folder is its own: it is none of recomp, source and data", a.program_folder)
    name = a.program_name + ".exe"
    plan.add(name, "the copy's program", real_file(a.program, "the copy's program"))
    with open(a.program, "rb") as f:
        host = f.read()
    if host[:2] != b"MZ" or f"{a.program_folder}\\{LAUNCHER_LIBRARY}".encode() + b"\0" not in host:
        raise Refused(f"the copy's program is no Windows program that starts the launcher in {a.program_folder}\\", a.program)
    plan.add(f"{a.program_folder}/{a.marker}", "the copy's marker", data=MARKER_TEXT)
    return name


def clean_top(plan, name, folder):
    """A copy is started by its program: nothing else stands at its top but the read-me and the folders a copy is
    made of, and no batch file, script or shortcut is anywhere in it."""
    for dest, file in sorted(plan.files.items()):
        if dest.lower().endswith(START_FILES):
            raise Refused(f"a batch file, a script or a shortcut must not be in a copy, which is started by its program ({file['part']})", file["path"] or dest)
        if "/" not in dest and dest not in (name, READ_ME):
            raise Refused(f"a file at the copy's top that is neither its program nor its read-me ({file['part']})", file["path"] or dest)
        if "/" in dest and dest.split("/")[0] not in (folder, *TOP_FOLDERS):
            raise Refused(f"a folder at the copy's top that a copy is not made of ({file['part']})", file["path"] or dest)


def made_for(a):
    """The published launcher carries the stack the copy is said to be for: the name, the address and the kind, as its
    build stamped them (net/src/Launcher.Core/Launcher.Core.csproj; each an attribute of a key and a value, both with
    their lengths before them). A launcher published without them, or for another stack, would be handed out under
    this one's name and play on another server."""
    if a.stack_kind not in ("test", "public"):
        raise Refused("a copy made for a stack is a test build or the public one (--stack-kind test|public)", a.stack_kind or "no kind")
    with open(real_file(os.path.join(a.launcher, "Launcher.Core.dll"), "the launcher's own part"), "rb") as f:
        program = f.read()

    def says(key, value):
        key, value = key.encode(), value.encode()
        return len(value) < 128 and bytes([len(key)]) + key + bytes([len(value)]) + value + b"\x00\x00" in program

    wrong = [key for key, value in (("ServerStack", a.stack), ("StackServer", a.stack_address), ("StackKind", a.stack_kind))
             if not says(key, value)]
    if wrong:
        raise Refused(f"the launcher there was not made for stack {a.stack} ({a.stack_kind}; {a.stack_address}): its "
                      f"{', '.join(wrong)} {'say' if len(wrong) > 1 else 'says'} otherwise, or nothing", a.launcher)


def templates(plan, values):
    """The read-me from player-copy/: @NAME@ and the like filled in, Windows line ends, the mark of UTF-8."""
    for path, name in walk(os.path.join(here, "player-copy"), "the templates"):
        if name == "README.md":
            continue
        with open(path, encoding="utf-8") as f:
            text = re.sub(r"@([A-Z_]+)@", lambda m: values.get(m.group(1), m.group(0)), f.read())
        if re.search(r"@[A-Z_]+@", text):
            raise Refused(f"a template names a value this script does not know ({re.search(r'@[A-Z_]+@', text).group(0)})", path)
        plan.add(name, "the read-me", data=text.replace("\r\n", "\n").replace("\n", "\r\n").encode("utf-8-sig"))


def remove(path):
    """Removes an earlier output of this script and nothing else: its zip, or a folder with its record and no link."""
    if os.path.islink(path):
        raise Refused("a link where the copy goes: remove it by hand (cmd /c rmdir on the link itself)", path)
    if os.path.isfile(path):
        os.remove(path)
    elif os.path.isdir(path):
        if not os.path.isfile(os.path.join(path, *RECORD.split("/"))):
            raise Refused("a folder where the copy goes that this script did not make (no record in it): remove it by hand", path)
        walk(path, "the earlier copy")
        shutil.rmtree(path)


def write(plan, folder, name, stamp):
    """The folder, then the zip from the folder as it was written: every file read back and compared on the way."""
    partial, archive = folder + ".partial", folder + ".zip"
    order = [RECORD] + sorted(n for n in plan.files if n != RECORD)        # the record first: a cut-off run is removable
    for dest in order:
        file, to = plan.files[dest], os.path.join(partial, *dest.split("/"))
        os.makedirs(os.path.dirname(to), exist_ok=True)
        if file["data"] is None:
            shutil.copyfile(file["path"], to)
        else:
            with open(to, "wb") as f:
                f.write(file["data"])
        os.utime(to, (stamp, stamp))
    there = {n for _, n in walk(partial, "the copy as written")}
    if there != set(plan.files):
        raise Refused("the folder as written is not the plan's", sorted(there ^ set(plan.files))[0])
    when = time.gmtime(stamp - stamp % 2)[:6]
    with zipfile.ZipFile(archive + ".partial", "w", zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        for dest in sorted(plan.files):
            info = zipfile.ZipInfo(f"{name}/{dest}", when)
            info.compress_type, info.create_system, info.external_attr = zipfile.ZIP_DEFLATED, 0, 0x20
            read = hashlib.sha1()
            with open(os.path.join(partial, *dest.split("/")), "rb") as f, z.open(info, "w") as out:
                for block in iter(lambda: f.read(1 << 20), b""):
                    read.update(block); out.write(block)
            if read.hexdigest() != plan.files[dest]["sha1"]:
                raise Refused("a file changed while it was copied", plan.files[dest]["path"] or dest)
    for tries in range(10, -1, -1):       # Windows may still hold a file it is scanning: the rename waits for it
        try:
            os.rename(partial, folder)
            break
        except PermissionError:
            if not tries:
                raise
            time.sleep(2)
    os.rename(archive + ".partial", archive)
    return archive, facts(path=archive)


def package(a):
    checkout(a.repository)                         # the runner's own checkout: clean (its commit is the manifest's)
    at, plan = checkout(a.launcher_repository), Plan()    # the launcher's: the copy's name, date, stamps and record
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]*", a.stack):
        raise Refused("a stack's name is letters, digits, '.', '_' and '-'", a.stack)
    made = parts(a, plan)
    if not made["module"]["same_trees_as_the_runner"]:
        print("package: note: the module was built from other source trees than the runner; the read-me points to the runner's (the GPL part)")
    whose = [a.repository, a.launcher_repository, a.sys_source, *a.identity_repository]         # the repositories whose git users are searched for
    if a.parts_only:
        refuse_game_data(plan)
        refuse_identities(plan, whose, a.name, a.more_identities, Refused, a.identity_without)
        return print(f"package: the parts are in order: game build {made['game_build']} (image/module/runner), {len(plan.files)} files")
    version, left = launcher(a, plan, at)
    started_by = program(a, plan)
    if a.stack_address:
        made_for(a)
    name = f"{a.name}-{version}-{at['build']}"
    day = time.strptime(at["date"], "%Y-%m-%d")
    address = a.stack_address or BUILT_IN
    # as the launcher's BuildInfo.Text says it: "Version 0.5.0 Beta · build 3934d68" (the stage is Branding.Stage, which
    # package.sh reads from the launcher's source; the version stays the program's three numbers, launcher() above)
    stage = f" {a.stage}" if a.stage else ""
    templates(plan, {"NAME": a.name, "PROGRAM": a.program_name, "VERSION": version, "VERSION_TEXT": f"Version {version}{stage} · build {at['build']}",
                     "DATE": f"{day.tm_mday} {time.strftime('%B %Y', day)}", "STACK": a.stack, "STACK_ADDRESS": address,
                     # the kind of copy: the players' build says nothing, every other copy that it is a test build
                     "BUILD_KIND": "" if a.stack_kind == "public" else " test build",
                     "TEST_BUILD": "" if a.stack_kind == "public" else "This is a test build. ",
                     **dict(zip(("IMAGE", "MODULE", "RUNNER"), made["game_build"].split("/")))})
    refuse_game_data(plan)
    clean_top(plan, started_by, a.program_folder)
    refuse_identities(plan, whose, a.name, a.more_identities, Refused, a.identity_without)
    record = {"schema": 1, "name": name, "program": started_by, "version": version, "build": at["build"], "launcher_commit": at["commit"],
              "date": at["date"], "parts": made,
              "stack": {"name": a.stack, "server_address": address, **({"kind": a.stack_kind} if a.stack_address else {})},
              "game_build": made.pop("game_build"), "left_out_of_the_launcher": left,
              "files": {n: {"sha1": f["sha1"], "bytes": f["bytes"]} for n, f in sorted(plan.files.items())}}
    plan.add(RECORD, "the record", data=(json.dumps(record, indent=1, sort_keys=True) + "\n").encode())
    dist = os.path.join(os.path.abspath(a.work), "dist")
    if a.name.lower() in [part.lower() for part in dist.split(os.sep)]:
        raise Refused(f"a folder named {a.name} is a release folder: a copy is made under the work folder and copied there by hand", dist)
    folder = os.path.join(dist, name)
    for old in (folder + ".partial", folder + ".zip.partial") + ((folder, folder + ".zip") if a.replace else ()):
        remove(old)
    for old in (folder, folder + ".zip"):
        if os.path.lexists(old):
            raise Refused("there already (REPLACE=1 makes it again)", old)
    archive, zipped = write(plan, folder, name, at["time"])
    size = sum(f["bytes"] for f in plan.files.values())
    print(f"package: {name}: {record['game_build']} (image/module/runner), {len(plan.files)} files, {size / 1e6:.0f} MB")
    print(f"package: stack {a.stack}; the server's address is {address}")
    print(f"package: {folder}")
    print(f"package: {archive} ({zipped['bytes'] / 1e6:.0f} MB, SHA-1 {zipped['sha1']})")


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    for option in ("name", "stack", "repository", "launcher-repository", "work", "runner", "runner-commit", "module",
                   "bundle", "sys", "sys-source", "vcrt", "fonts"):
        p.add_argument("--" + option, required=True)
    for option in ("launcher", "program", "program-name", "program-folder", "marker", "stack-address", "stack-kind"):
        p.add_argument("--" + option)
    p.add_argument("--replace", action="store_true"); p.add_argument("--parts-only", action="store_true")
    p.add_argument("--old-replays-refused", action="store_true", help="on purpose: this build does not play the released builds' replays that same-race.json does not name for its image")
    p.add_argument("--identity-repository", action="append", default=[], help="one more repository whose git user no copy may name")
    p.add_argument("--more-identities", help="a file of more names no copy may hold: a name a line, or kind<TAB>name (never a tracked file)")
    p.add_argument("--identity-without", action="append", default=[], help="a source of names this PC does not have, left out knowingly")
    p.add_argument("--stage", default="", help="the release's stage after the version in the read-me (the launcher's Branding.Stage, \"Beta\"); empty: none")
    a = p.parse_args()
    if a.stage and not re.fullmatch(r"[A-Za-z][A-Za-z0-9 ]*[A-Za-z0-9]|[A-Za-z]", a.stage):
        p.error(f"--stage is a word or a few, letters, digits and spaces (got {a.stage!r})")
    if not a.parts_only and not all((a.launcher, a.program, a.program_name, a.program_folder, a.marker)):
        p.error("--launcher FOLDER --program FILE --program-name NAME --program-folder NAME --marker NAME (the published "
                "launcher, the copy's program and what they are called), or --parts-only")
    try:
        package(a)
    except Refused as why:
        print(f"package: REFUSED: {why}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
