#!/usr/bin/env python3
"""The mod in its native form: the PowerPC image that is compiled into the recompiled
game's native module, and the one list of patched words of the game's code.

  build.py OUT [--rel MAIN.REL --dol MAIN.DOL]
      OUT/mod.bin        the image at memmap.IMAGE: the fork's routines (seat routines, frame routine, name tags)
                         and, after 'FZPT', a routine for every site where the fork had a Gecko code
      OUT/patches.json   every patched word: address, the stock game's word, the new word, what it is. The native
                         module is compiled from the game's code with these words in; the host writes the same words
                         (main.dol's into the copy of the executable that is booted, main.rel's when it is linked)
      OUT/symbols.json   the image's routines and the host calls, by name
      OUT/manifest.json  what the host checks: the form, the image's hash, the layout
  --rel   main.rel as the disc has it, unpacked (not linked); --dol the stock executable. With both, every replaced
          word is read from the game and every site in the middle of a function is proven safe (liveness.py); the
          bundle is then "complete". Without them (the repository's tests: no game data there) the bundle is built
          from the generators' own tables, marked incomplete, and a host refuses it.

What changes against the fork's bundle (port/image/fork_bundle.py: the only bundle a race on the JIT can run, the
reference of the gates' JIT races; it lacks the native-only race sites, README.md):
- a `D2`/`C2` insert is a routine in the image, entered by a branch written at the site; a `04`/`14` word is a
  patched word; the two `C0` codes (the seat installer, the shuffle word's hold) run from the mode machine's routine;
- the patches the frame routine installed at run time are static entries of the list;
- every routine tests the mode word (memmap.MODE) and in a plain start does what the stock game does, because the
  patched code is in the native module in every start;
- the host's services are calls to addresses with nothing behind them (memmap.HOSTCALLS): the network hook, the
  frame tick from the mode machine's routine, "main.rel is linked" from the executable's loader.
Nothing here is game data except the replaced words, which the list must name.
The routines of the patched sites and their entries are sites.py's; dol.py reads the executable."""
import argparse, hashlib, json, os, struct, subprocess, sys, tempfile

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.join(here, "..", "..")
sys.path.insert(0, os.path.join(here, "routines"))
sys.path.insert(0, here)
import memmap
import liveness
import relink
import sites
from dol import Dol

TEXT = memmap.REL_TEXT                       # main.rel .text in memory
C = memmap.hi(memmap.CONTROL) - 0x10000
MODE = memmap.MODE - memmap.CONTROL
# The executable's sites (stock words: the generator's own knowledge, checked against --dol)
DOL_PIN = (0x80005940, 0x4800BAE5, 0x38600000)        # `bl OSGetTick` -> `li r3, 0`: main.rel loads at one address
DOL_LINKED = (0x800059A8, 0x819E0034)                 # `lwz r12, 0x34(r30)`: the module's prolog, loaded after OSLink
DOL_SOUND = (0x80067898, 0x9421FFF0)                  # fn_80067898's first instruction: the sound command queue
SHUFFLE_WORD = 0x801A63C0                             # the executable's generator (port/image/routines/setup.py)
# The results screen's pictures. The game's own names, by address:
RESULTS_SITES = (0x3B770, 0x3B77C, 0x3B798)           # fn_1_3B11C's three `bl fn_1_48004` (bitmap groups 0x97, 0x99, a pilot's)
# The main heap's room that must stay after the three files: at the gate's frame the heap has one free block of
# 1,013,952 bytes (the same in an offline Grand Prix race: 1,014,176), the step into the countdown takes 151,456
# of it (four blocks it gives back at the go, and 192 bytes; runs n7m1, n7m2), and the files take 558,720 at most.
RESULTS_RESERVE = 0x50000
CAMERA_INDEXES = (0x725C, 0x6F14, 0x686C)     # `clrlwi r0, r3, 0x18`: fn_1_725C, fn_1_6EC0, fn_1_681C index the cameras
CAMERA_INDEX_WORD = 0x5460063E
SHADOW_PLACED = (0x57174, 0xC01F008C)         # fn_1_56D4C, `lfs f0, 0x8c(r31)`: a machine's shadow has its place
EFFECT_DRAWS = (0x58A60, 0x58AB4, 0x58B70, 0x58C28)   # fn_1_58994's four `bctrl`: an effect's draw (sites_effects.py)
EFFECT_DRAW_WORD = 0x4E800421
SAVEGPR_21, GX_SYNC, DC_FLUSH = 0x80079874, 0x8006FDEC, 0x8000B654   # the executable's
MAIN_HEAP, HEAP_TABLE, LANGUAGE = 0x801A6410, 0x801A6744, 0x801A66B4  # the executable's variables


def branch(at, to, link=False):
    disp = (to - at) & 0xFFFFFFFF
    assert disp < 0x02000000 or disp >= 0xFE000000, f"branch from {at:08X} to {to:08X} is out of reach"
    return 0x48000000 | (disp & 0x03FFFFFC) | (1 if link else 0)


def branch_target(word, at):
    assert word >> 26 == 18 and not word & 2, f"{word:08X} is not a relative branch"
    disp = word & 0x03FFFFFC
    return (at + disp - (0x04000000 if disp & 0x02000000 else 0)) & 0xFFFFFFFF


def gecko(lines):
    """A generator's Gecko code lines as (kind, address, words): inserts ("insert": a D2 or C2 code's instructions,
    without the terminator Gecko turns into the branch back), words ("word": a 04 or 14 code) and the routines of C0
    codes ("routine"). An address is absolute: a D2/14 code's offset counts from main.rel's header."""
    rows = [tuple(int(w, 16) for w in line.split()) for line in lines if line.strip() and not line.startswith("$")]
    out, i = [], 0
    while i < len(rows):
        a, b = rows[i]
        kind = a >> 24
        if kind in (0xC2, 0xD2, 0xC0):
            words = [w for row in rows[i + 1:i + 1 + b] for w in row]
            i += 1 + b
            if kind == 0xC0:
                out.append(("routine", 0, words))
                continue
            assert words[-1] == 0, "an insert ends with the word Gecko replaces by its branch back"
            words = words[:-1]
            if words[-1] == 0x60000000:
                words = words[:-1]             # the padding nop before it
            out.append(("insert", (memmap.REL if kind == 0xD2 else 0x80000000) + (a & 0xFFFFFF), words))
            continue
        if kind in (0x04, 0x14):
            out.append(("word", (memmap.REL if kind == 0x14 else 0x80000000) + (a & 0xFFFFFF), [b]))
        i += 1                                 # 48/DE/E0/E2/3A lines: Gecko's own bookkeeping
    return out


class Routines:
    """The routines after 'FZPT', assembled as one text. `@b X` and `@bl X` are branches to an absolute address
    (keystone has no symbols outside its text): X is a number or a name of `symbols`."""

    def __init__(self, base, symbols):
        self.base, self.symbols, self.text, self.names = base, dict(symbols), "", []

    def add(self, name, text):
        self.names.append(name)
        self.text += f"{name}:\n{text.rstrip()}\n"

    def words(self, name, words):
        return "".join(f"    .long {w:#010x}\n" for w in words)

    def build(self):
        from keystone import Ks, KS_ARCH_PPC, KS_MODE_PPC32, KS_MODE_BIG_ENDIAN
        import native
        offsets = native.label_offsets(self.text.replace("@b ", "nop ").replace("@bl ", "nop "))
        out, at = [], 0
        for line in self.text.split("\n"):
            s = line.strip()
            if s.startswith("@b ") or s.startswith("@bl "):
                op, target = s.split(None, 1)
                to = self.symbols[target] if target in self.symbols else \
                    self.base + offsets[target] if target in offsets else int(target, 0)
                line = f"    .long {branch(self.base + at, to, op == '@bl'):#010x}"
            if s and not s.endswith(":"):
                at += 4
            out.append(line)
        code = bytes(Ks(KS_ARCH_PPC, KS_MODE_PPC32 + KS_MODE_BIG_ENDIAN).asm("\n".join(out))[0])
        assert len(code) == at, "the line count does not give the assembled size"
        return code, {name: self.base + offsets[name] for name in self.names}


def read(path, mode="rb"):
    with open(path, mode) as f:
        return f.read()


def write(path, data):
    binary = isinstance(data, (bytes, bytearray))
    with open(path, "wb" if binary else "w", **({} if binary else {"newline": "\n"})) as f:   # LF on every platform
        f.write(data)


def run(script, *args, env=None):
    result = subprocess.run([sys.executable, script, *args], capture_output=True, text=True,
                            env=dict(os.environ, **(env or {})))
    if result.returncode:
        sys.exit(f"{os.path.basename(script)} failed:\n{result.stderr}")
    return result.stdout


def build(out, rel_file=None, dol_file=None, given_commit=None):
    os.makedirs(out, exist_ok=True)
    complete = bool(rel_file and dol_file)
    env = {"FZGX_NATIVE": "1"}
    clean = dol = None
    if complete:
        clean, _ = relink.relocate(read(rel_file), memmap.REL, memmap.REL_BSS)
        tmp = tempfile.NamedTemporaryFile(suffix=".bin", delete=False)
        tmp.write(clean); tmp.close()
        env["FZGX_CLEAN_REL"] = tmp.name
        dol = Dol(read(dol_file))

    def stock(address, known=None):
        """The stock game's word at an address, read from the game; without the game, what the generators know."""
        if not complete:
            return known
        word = dol.word(address) if address < memmap.REL else struct.unpack_from(">I", clean, address - memmap.REL)[0]
        assert known is None or word == known, f"{address:08X} holds {word:08X}, the generators expect {known:08X}"
        return word

    try:
        routines = os.path.join(here, "routines")
        image_file = os.path.join(out, "mod.bin")
        hooks = gecko(run(os.path.join(routines, "modbuild.py"), image_file, "0", "0", "1", "0", "2", env=env).split("\n"))
        layout = json.loads(read(image_file + ".layout.json", "r"))
        os.remove(image_file + ".layout.json")
        codes = {name: gecko(run(os.path.join(routines, script), *args, env=env).split("\n")) for name, script, args in (
            ("race", "direct.py", ("--params",)), ("seats", "seats.py", ("same:0", "--params")))}
        mute = gecko(run(os.path.join(routines, "mute.py"), env=env).split("\n"))
    finally:
        if complete:
            os.unlink(env["FZGX_CLEAN_REL"])

    image = bytearray(read(image_file))
    while len(image) % 8:
        image += b"\0"
    base = memmap.IMAGE + len(image) + 4            # the routines start after the section's mark
    patches, notes = [], []

    def patch(address, original, new, what):
        patches.append({"address": f"{address:08X}", "original": None if original is None else f"{original:08X}",
                        "new": f"{new:08X}", "what": what})

    symbols = {"host_net": memmap.HOST_NET, "host_tick": memmap.HOST_TICK, "host_linked": memmap.HOST_LINKED,
               "host_hfr": memmap.HOST_HFR, "host_mode": memmap.HOST_MODE,
               "seat_slot": layout["slot"], "seat_routine": layout["routine"], "race_over": layout["over"],
               "rank_refs": layout["refs"], "frame_routine": layout["frame"], "name_tags": layout["tags"]}
    r = Routines(base, symbols)
    middle = sites.add(sys.modules[__name__], r, patch, stock, complete, codes, hooks, mute)

    code, where = r.build()
    symbols.update(where)
    for p in patches:
        if p["new"] == "00000000":
            site = int(p["address"], 16)
            p["new"] = f"{branch(site, where[p['what']]):08X}"
    patch(DOL_PIN[0], stock(DOL_PIN[0], DOL_PIN[1]), DOL_PIN[2], "pin")

    # --- the sites the frame routine's installers patched: static entries, each a call to its entry
    for site in layout["sites"]:
        address = TEXT + site["text"]
        patch(address, site["original"] if site["complete"] else None, branch(address, site["address"], True), site["helper"])
        middle.append((address, ["r12", f"cr{site['field']}", "lr"], site["helper"]))
        symbols.setdefault(site["label"], site["address"])

    image += b"FZPT" + code
    struct.pack_into(">II", image, 4, 2, len(image))        # version 2: the native form; the size
    assert memmap.IMAGE + len(image) <= min(memmap.CONTROL, memmap.SMOOTH_LOG)   # the tests' log has the upper half
    write(image_file, image)

    # --- every site in the middle of a function: the routine's extra registers must be dead after it
    if complete:
        def read_word(address):
            if memmap.REL_TEXT <= address < memmap.REL_TEXT + 0x15F62C:
                return struct.unpack_from(">I", clean, address - memmap.REL)[0]
            return dol.word(address) if dol.holds(address) else None
        by_address = {int(p["address"], 16): p for p in patches}
        for address, resources, name in middle:
            word = int(by_address[address]["original"], 16)
            starts = [address + 4]
            if word >> 26 == 16:                             # a replaced conditional branch: both ways on
                disp = word & 0xFFFC
                starts.append(address + disp - (0x10000 if disp & 0x8000 else 0))
            redone = liveness.decode(word, address)[1]       # what the replaced instruction writes, the routine's
            for resource in resources:                       # stock path writes again by running it last
                if resource in redone:
                    continue
                for start in starts:
                    proof = liveness.dead(read_word, start, resource)
                    if proof is not True:
                        sys.exit(f"build.py: site {address:08X} ({name}): {proof}")
        notes.append(f"{len(middle)} sites in the middle of a function proven safe")

    patches.sort(key=lambda p: p["address"])
    assert len({p["address"] for p in patches}) == len(patches), "a word is patched twice"
    commit = given_commit or subprocess.run(["git", "-C", root, "describe", "--always", "--dirty"], capture_output=True, text=True).stdout.strip()
    sha1 = hashlib.sha1(image).hexdigest()
    hexmap = lambda d: {k: f"{v:08X}" for k, v in sorted(d.items())}
    write(os.path.join(out, "patches.json"), json.dumps(
        {"version": 1, "complete": complete, "image_sha1": sha1, "main_rel": f"{memmap.REL:08X}", "patches": patches}, indent=1))
    write(os.path.join(out, "symbols.json"), json.dumps(hexmap(symbols), indent=1))
    write(os.path.join(out, "manifest.json"), json.dumps(
        {"version": 2, "form": "native", "complete": complete, "commit": commit, "image": memmap.IMAGE,
         "image_sha1": sha1, "slot": layout["slot"], "race": memmap.RACE, "names": memmap.NAMES,
         "name_size": memmap.NAME_SIZE, "mode": memmap.MODE, "host_net": memmap.HOST_NET,
         "host_tick": memmap.HOST_TICK, "host_linked": memmap.HOST_LINKED, "main_rel": memmap.REL}, indent=1, sort_keys=True))
    print(f"native bundle: {len(image)} bytes of image ({len(code)} of routines), {len(patches)} patched words "
          f"({sum(int(p['address'], 16) < memmap.REL for p in patches)} in the executable), "
          f"{'complete' if complete else 'INCOMPLETE (no game files given)'}, {commit}" + "".join(f"; {n}" for n in notes))
    return {"image": bytes(image), "patches": patches, "symbols": symbols, "complete": complete}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("out")
    parser.add_argument("--rel", help="main.rel as the disc has it, unpacked")
    parser.add_argument("--dol", help="the stock executable (main.dol)")
    parser.add_argument("--commit-file", help="a file that names the checkout's commit as the manifest names it (empty or "
                        "missing: git describe here); the build gives it when the checkout's git is not the one that runs this")
    a = parser.parse_args()
    if bool(a.rel) != bool(a.dol):
        sys.exit("build.py: --rel and --dol go together")
    named = open(a.commit_file).read().strip() if a.commit_file and os.path.isfile(a.commit_file) else ""
    build(a.out, a.rel, a.dol, named or None)
