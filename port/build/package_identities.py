#!/usr/bin/env python3
"""package_identities.py: no personal name in anything handed out. A copy holds the product's name and nobody's.

  package_identities.py COPY [--repository DIR ...] [--more FILE] [--without SOURCE ...]     COPY: a folder, or its zip

Two things, both for port/build/package.py, and the command above for a copy that is already made:

  What a copy is written with (neutral_manifest): a build's manifest goes in without its host name and with a home
  folder's path cut down to "~". The manifest's hashes are untouched.

  What a copy is refused for (refuse_identities, scan): every file, read as bytes whatever its kind and searched
  as UTF-8, UTF-16 and UTF-32 text (both byte orders), every file's name, a zip's comments, and the files of a zip
  inside the copy, for the names of this PC and of the repositories' git users. Whose names, and how they are
  gathered when the script runs and written nowhere: port/build/package_names.py. A full name is searched whole,
  written together and word by word; an address whole and by the name before its @. And one thing whatever the
  PC is: in the copy's own papers (its top and source/), a home folder's path.

**It refuses when it could not gather.** Every source of names is listed in what it prints as gathered, left out
by the caller's word (--without SOURCE: windows, git:NAME, ...) or NOT GATHERED; with one that is not gathered no
copy passes.
A refusal names the file as the copy holds it, the kind of name and how it was written. **It never prints the
text it found**, nor the text it searched for, nor a path of this PC (a file whose own name holds a name is printed
with *** there): the output of a refused copy can be pasted anywhere. A name of fewer than 4 characters and a name
every PC has (root, user) are not searched, and the output says so. One word of a full name (a first name alone)
and the name before an address's @ are searched everywhere but in the emulator's own Sys folder, whose list of
every game's title holds most first names: the full name, the login, the host and the address are searched there
too. A name none of the sources holds is not searched: give it with --more.
**The game's own names are the game's** (game_names): a pilot's or a machine's name as the launcher's source lists
it (Pilots.Names, GameMachines.Names) is left out of the search where it stands as a whole string of a program, in
exactly the game's spelling: a .NET string literal (UTF-16, with its length byte before it) or a quoted UTF-8
string. So the pilot "Pico" passes in the launcher's pilot list, while the same word anywhere else (in a sentence, in
another case, inside a longer string) is searched as ever. Both lists are read from this script's own checkout,
which is the one package.sh publishes from; a list that cannot be read, or is not 41 names, refuses the copy.
Status 0: clean. Status 1: refused."""
import argparse, io, json, os, re, sys, zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from package_names import gather, not_gathered
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "setup"))
import parts                          # the other parts' checkouts (port/setup/pins.tsv)

SHORTEST = 4                          # shorter names are not searched (said in the output): they match by chance
WHOLE = 6                             # a name shorter than this counts only as a whole word
GENERIC = {"root", "user", "users", "admin", "administrator", "ubuntu", "debian", "localhost", "runner", "test",
           "tester", "home", "public", "default", "owner", "noreply", "none", "unknown", "build", "builder", "wsl",
           "desktop", "laptop", "example", "example.com", "name", "git", "mail", "info"}
EMULATORS_OWN = "recomp/Sys/"         # in a copy that is already made (package.py knows each file's part itself)
OWN_PAPERS = ("source/",)             # with the files at a copy's top: what package.py itself writes
HOME = re.compile(rb"(/home/|/Users/|/mnt/[a-z]/Users/|[A-Za-z]:[\\/]{1,4}Users[\\/]{1,4})[^\\/\"'<>|:*?\t\r\n]+")   # a Windows folder's name may hold spaces
SHARED_HOMES = ("public", "default", "all users", "shared")      # with GENERIC: a home folder that is nobody's
UTF8, UTF16, UTF32 = "UTF-8 text", "UTF-16 text", "UTF-32 text"
CODECS = {UTF8: ("utf-8",), UTF16: ("utf-16-le", "utf-16-be"), UTF32: ("utf-32-le", "utf-32-be")}
NESTED = 3                            # a zip in a zip in a zip is opened; what is deeper is searched as it lies


def neutral_manifest(data):
    """A build's manifest as a copy holds it: without the host it was made on, a home folder's path cut down to
    "~". Everything a manifest proves (commits, trees, hashes, switches, tools) is as the build wrote it."""
    m = json.loads(data)
    left = ["made.host"] if (m.get("made") or {}).pop("host", None) is not None else []

    def without_homes(value):
        if isinstance(value, dict):
            return {k: without_homes(v) for k, v in value.items()}
        if isinstance(value, list):
            return [without_homes(v) for v in value]
        if isinstance(value, str):
            cut = HOME.sub(lambda m: m.group(0) if nobodys(m.group(0)) else b"~", value.encode("utf-8", "surrogateescape")).decode("utf-8", "surrogateescape")
            if cut != value and "home folders" not in left:
                left.append("home folders")
            return cut
        return value

    m = without_homes(m)
    if left:
        m.setdefault("made", {})["left_out_of_a_copy"] = left
    return (json.dumps(m, indent=1, sort_keys=True) + "\n").encode()


def nobodys(home):
    """Whether a home folder's path is an account's that every PC may have (/home/build, C:\\Users\\Public)."""
    return re.split(rb"[\\/]", home)[-1].decode("utf-8", "replace").lower() in (*GENERIC, *SHARED_HOMES)


# each list: the part whose checkout holds it (port/setup/pins.tsv's row: "." while every part is in this repository),
# the file in that checkout, the class
GAME_LISTS = (("REPO_LAUNCHER", "net/src/Launcher.Core/CustomParts.cs", "Pilots"),          # the 41 pilots
              ("REPO_PROTOCOL", "net/src/Online.Protocol/GameMachines.cs", "GameMachines"))    # the 41 machines
GAME_COUNT = 41


def game_names(root=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")):
    """The game's own names, from the launcher's and the protocol's sources (their checkouts: port/setup/pins.tsv):
    each list is the class's `Names { get; } = [ ... ];`."""
    names = []
    for part, path, owner in GAME_LISTS:
        try:
            with open(parts.path_in(part, path, root), encoding="utf-8") as f:
                text = f.read()
        except OSError as e:
            raise ValueError(f"the game's names cannot be read from {path} ({e.strerror})") from e
        found = re.search(r"\bclass " + owner + r"\b.*?\bNames\s*\{\s*get;\s*\}\s*=\s*\[(.*?)\];", text, re.S)
        listed = re.findall(r'"((?:[^"\\]|\\.)*)"', found.group(1)) if found else []
        if len(listed) != GAME_COUNT:
            raise ValueError(f"{path}: {owner}.Names is not a list of {GAME_COUNT} names ({len(listed)} found)")
        names += listed
    return tuple(dict.fromkeys(names))


def without_game_names(data, names):
    """The bytes with each of the game's own names blanked where it is a whole string of a program, in exactly its
    spelling: a .NET string literal (its UTF-16 text between its length byte and its last byte, 0 or 1) or a quoted
    UTF-8 string. Anywhere else the name is left for the search."""
    for name in names:
        wide = name.encode("utf-16-le")
        if len(wide) + 1 < 0x80:              # a longer literal's length takes two bytes; the game's names are short
            data = re.sub(re.escape(bytes([len(wide) + 1]) + wide) + b"(?=[\x00\x01])",
                          lambda m: m.group(0)[:1] + bytes(len(wide)), data)
        plain = name.encode("utf-8")
        data = data.replace(b'"' + plain + b'"', b'"' + bytes(len(plain)) + b'"')
    return data


class Search:
    """One text to look for, as the bytes of the ways to write it in one kind of text. A short one counts only as a
    whole word. `alone`: one word of a longer name, which by itself is anybody's (a first name): it is not searched
    in files that are another project's own and checked as such (the emulator's Sys folder holds a list of every
    game's title)."""
    def __init__(self, kind, form, text, word):
        self.kind, self.form, self.word, self.alone = kind, form, word or len(text) < WHOLE, word
        self.unit = {UTF8: 1, UTF16: 2, UTF32: 4}[form]
        self.needles = sorted({(s.encode(codec).lower(), codec) for s in (text, text.lower(), text.upper(), text.title()) for codec in CODECS[form]})

    def is_in(self, low):
        """Whether the lower-cased bytes hold it. As a whole word: no letter or digit on either side. In UTF-8 any
        other byte of plain text's range is an edge, a NUL and a length byte among them (a program's own strings
        lie so), so four letters turn up by chance in a program's bytes now and then: a named limit."""
        for needle, codec in self.needles:
            big = codec.endswith("-be")
            twin = needle[self.unit - 1:] + needle[:self.unit - 1] if big else b""      # the same text, low byte first
            at = low.find(needle)
            while at >= 0:
                # text written low byte first reads as the other order one place on: the low-byte-first search judges it
                if not (big and low.startswith(twin, at + self.unit - 1)) and \
                        (not self.word or (self.edge(low, at - self.unit, big) and self.edge(low, at + len(needle), big))):
                    return True
                at = low.find(needle, at + 1)
        return False

    def edge(self, low, at, big):
        if at < 0 or at + self.unit > len(low):
            return True
        if self.unit == 1:
            return low[at] < 128 and not chr(low[at]).isalnum()
        c = int.from_bytes(low[at:at + self.unit], "big" if big else "little")
        return not (c < 128 and chr(c).isalnum()) and not 0xC0 <= c < 0x250


def searches(identities, allowed=()):
    """Every Search for the identities, and the names that are not searched, by kind and reason (never the name).
    A name of several words is searched whole, written together (firstlast, first.last) and word by word; an
    address whole and by the name before its @ (a word alone, and that name: Search.alone). `allowed`: what a copy
    may say (the product's own name)."""
    out, skipped, seen, allowed = [], [], {}, {a.lower() for a in allowed}

    def add(kind, text, word=False):
        if text.lower() in allowed:
            return
        if text.lower() in seen:              # one name, however many places gave it: the first's kind
            for search in seen[text.lower()] if word else ():
                search.alone = True           # a login that is also somebody's first name is a first name
            return
        if len(text) < SHORTEST or text.lower() in GENERIC:
            return skipped.append((kind, "fewer than %d characters" % SHORTEST if len(text) < SHORTEST else "a name every PC has"))
        seen[text.lower()] = [Search(kind, form, text, word) for form in (UTF8, UTF16, UTF32)]
        out.extend(seen[text.lower()])

    for kind, text in identities:
        words = [w for w in re.split(r"[\s,;]+", text) if w]
        if len(words) > 1:
            add(kind, " ".join(words))
            for joint in ("", ".", "_", "-"):
                add(f"{kind}, written together", joint.join(words))
            for word in words:
                add(f"a word of {kind}", word.strip(".()[]<>\"'"), word=True)
        else:
            add(kind, text)
            if re.fullmatch(r"[^@\s]+@[^@\s]+", text):
                add(f"the name before the @ of {kind}", text.split("@")[0], word=True)
    return out, sorted(set(skipped))


def structural(name, data):
    """What no copy's own papers hold, whoever's PC made it: a home folder's path."""
    if "/" in name and not name.startswith(OWN_PAPERS):
        return []
    out = []
    if any(not nobodys(found.group(0)) for found in HOME.finditer(data)):
        out.append(("a home folder's path", UTF8))
    return out


def opened(name, data, others, depth=0):
    """The file, and what a zip holds when the file is one (a package, an archive in the copy): its comment, and
    each of its files with its own comment and extra field, by the name `archive!/file`; a zip inside it too,
    NESTED deep. A file that only begins like a zip is searched as it lies."""
    yield name, data, others
    if data[:4] != b"PK\x03\x04" or depth >= NESTED:
        return
    try:
        with zipfile.ZipFile(io.BytesIO(data)) as z:
            if z.comment:
                yield f"{name} (the zip's comment)", z.comment, others
            for info in z.infolist():
                if info.comment or info.extra:
                    yield f"{name}!/{info.filename} (its comment in the zip)", info.comment + b"\n" + info.extra, others
                if not info.is_dir():
                    yield from opened(f"{name}!/{info.filename}", z.read(info), others, depth + 1)
    except (zipfile.BadZipFile, RuntimeError, NotImplementedError, OSError, EOFError, ValueError):
        return


def scan(files, sought, game=()):
    """files: (name, bytes, whether it is another project's own file, checked) in turn; game: the game's own names
    (game_names), which are not a person's where they stand as a program's whole string.
    -> [(name, the name as it may be printed, kind, how it was written)], a kind once a file. A file whose own name
    holds an identity is printed with *** in its place."""
    out = []
    for name, data, others in (part for file in files for part in opened(*file)):
        low, found, shown = without_game_names(data, game).lower(), [], name
        for s in sought:
            if s.form == UTF8 and s.is_in(name.encode("utf-8", "surrogateescape").lower()):
                found.append((s.kind, "the file's name"))
                for needle, _ in s.needles:
                    shown = re.sub(re.escape(needle.decode("utf-8", "surrogateescape")), "***", shown, flags=re.IGNORECASE)
        found += [(s.kind, s.form) for s in sought if not (others and s.alone) and s.is_in(low)]
        found += structural(name, data)
        out += [(name, shown, kind, form) for kind, form in dict.fromkeys(found)]
    return out


def said(sought, skipped, sources):
    """What was looked for and where the names came from: kinds and sources only, never a name."""
    kinds = list(dict.fromkeys(s.kind for s in sought))
    lines = ["identities: names " + "; ".join(f"{source}: {state}, {words}" for source, state, words in sources)]
    lines += [f"identities: searched every file as UTF-8, UTF-16 and UTF-32 text for {len(kinds)} kinds of name: " + "; ".join(kinds)]
    lines += [f"identities: not searched: {kind} ({why})" for kind, why in skipped]
    return lines


UNGATHERED = ("the names no copy may hold could not all be gathered: {}. A copy is not passed on names that were never asked for; a source "
              "that this PC does not have is left out by its name (--without SOURCE; package.sh: WITHOUT_IDENTITIES)")


def refuse_identities(plan, repositories, product, more=None, refused=Exception, without=()):
    """For package.py, before anything is written: every file of the plan. Raises `refused` with the first
    offending file as the copy names it (never a path of this PC), after a line for every file that carries an
    identity (the file, the kind, never the text); and when a source of names could not be asked."""
    identities, sources = gather(repositories, more, without)
    sought, skipped = searches(identities, allowed=(product,))
    for line in said(sought, skipped, sources):
        print("package: " + line)
    if not_gathered(sources):
        raise refused(UNGATHERED.format(not_gathered(sources)))
    try:
        game = game_names()
    except ValueError as e:
        raise refused(f"the game's own names could not be read: {e}")

    def contents():
        for name, file in sorted(plan.files.items()):
            if file["data"] is not None:
                yield name, file["data"], file["own"]
            else:
                with open(file["path"], "rb") as f:
                    yield name, f.read(), file["own"]

    found = scan(contents(), sought, game)
    for _, shown, kind, form in found[:200]:
        print(f"package: identity: {shown}: {kind} (as {form})", file=sys.stderr)
    if found:
        files = list(dict.fromkeys(name for name, _, _, _ in found))
        name, shown, kind, form = found[0]
        part = plan.files[name.split("!/")[0].split(" (the zip's comment)")[0]]["part"]
        raise refused(f"{len(files)} file{'s' if len(files) > 1 else ''} of the copy would carry a personal identity, which no copy "
                      f"may ({kind}, as {form}; {part})", shown)
    return len(plan.files)


def read(copy):
    """(name, bytes, whether it is the emulator's own) for every file of a copy's folder or of its zip, names with /
    and without the copy's own folder. The emulator's own: the files of recomp/Sys, which package.py checks against
    the emulator's source, file for file, when it makes a copy; this command takes the folder's word for it."""
    if os.path.isdir(copy):
        for at, folders, names in os.walk(copy):
            folders.sort()
            for name in sorted(names):
                path = os.path.join(at, name)
                with open(path, "rb") as f:
                    inside = os.path.relpath(path, copy).replace(os.sep, "/")
                    yield inside, f.read(), inside.startswith(EMULATORS_OWN)
        return
    with zipfile.ZipFile(copy) as z:
        names = [n for n in z.namelist() if not n.endswith("/")]
        top = {n.split("/")[0] for n in names}
        cut = len(top.pop()) + 1 if len(top) == 1 and all("/" in n for n in names) else 0
        if z.comment:
            yield "(the zip's comment)", z.comment, False
        for info in z.infolist():
            if info.comment or info.extra:
                yield f"{info.filename[cut:]} (its comment in the zip)", info.comment + b"\n" + info.extra, False
        for name in sorted(names):
            yield name[cut:], z.read(name), name[cut:].startswith(EMULATORS_OWN)


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("copy", help="a copy's folder, or its zip")
    p.add_argument("--repository", action="append", default=[], help="a repository whose git user is searched for too")
    p.add_argument("--more", help="a file of more names: a name a line, or kind<TAB>name")
    p.add_argument("--without", action="append", default=[], help="a source of names this PC does not have, left out knowingly (windows, git:NAME)")
    p.add_argument("--product", action="append", default=[], help="a name a copy may say")
    a = p.parse_args()
    if not os.path.exists(a.copy):
        sys.exit(f"identities: no copy at {a.copy}")
    here = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
    identities, sources = gather([here] + a.repository, a.more, a.without)
    sought, skipped = searches(identities, allowed=a.product)
    print("\n".join(said(sought, skipped, sources)))
    if not_gathered(sources):
        print("identities: REFUSED: " + UNGATHERED.format(not_gathered(sources)))
        return 1
    count = 0

    def counted():
        nonlocal count
        for item in read(a.copy):
            count += 1
            yield item

    found = scan(counted(), sought, game_names())
    for _, shown, kind, form in found:
        print(f"identities: {shown}: {kind} (as {form})")
    files = len(dict.fromkeys(name for name, _, _, _ in found))
    print(f"identities: REFUSED: {files} of {count} files carry a personal identity: {a.copy}" if found
          else f"identities: clean: {count} files, no personal identity: {a.copy}")
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main())
