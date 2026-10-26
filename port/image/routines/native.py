"""The native form of the mod: what the generators change when FZGX_NATIVE=1.

On the recompiled game the mod image is compiled into the native module together with every patch of the game's code,
so the same patched code runs in every start, a plain one included. Each routine at a patched site therefore tests
the mode word (memmap.MODE: 0 a plain start, 1 a race, a replay's playback included) and in mode 0 does
exactly what the instruction it replaced did. The patches themselves become a static list (port/image/build.py):
nothing installs them at run time: the installers left in the frame routine keep their guard word and write nothing
(follow.installer leaves their site code out).

  FZGX_NATIVE=1            the generators build the native form (default: the fork's, byte for byte as before)
  FZGX_CLEAN_REL=FILE      main.rel as the stock game has it in memory, from its header on, linked at memmap.REL
                           (port/image/relink.py makes it): the replaced instructions are read from it. Without it
                           a site whose table entry is masked gets a placeholder and the build is marked incomplete.
This module reads no environment: modbuild.py, the program port/image/build.py runs, reads both at its start and hands
them on (to netframe.py as arguments), and each program calls use() before a generator asks.
"""
import struct
import memmap

ON = False                                     # the native form is built: use() says
CLEAN_REL = None                               # the clean image's file, or None: use() says
C = memmap.hi(memmap.CONTROL) - 0x10000        # lis takes a signed immediate
MODE = memmap.MODE - memmap.CONTROL
SITES = []                                     # every installer site of the frame routine, in order (dicts)
_clean = None


def use(on, clean_rel=None):
    """What the program read: whether the native form is built, and the clean image's file (or None)."""
    global ON, CLEAN_REL
    ON, CLEAN_REL = on, clean_rel


def clean():
    """The clean image (bytes) or None."""
    global _clean
    if _clean is None and CLEAN_REL:
        _clean = open(CLEAN_REL, "rb").read()
    return _clean


def original(text_off, word, mask=0xFFFFFFFF):
    """The stock game's word at a main.rel .text offset, and whether it is known for certain: read from the clean
    image and checked against the generator's table (under the table's mask), or the table's own word."""
    image = clean()
    if image is None:
        return word, mask == 0xFFFFFFFF
    real = struct.unpack_from(">I", image, 0xDC + text_off)[0]
    assert real & mask == word & mask, f"main.rel .text {text_off:#x} holds {real:08X}, the table says {word:08X}/{mask:08X}"
    return real, True


def instructions(asm):
    """The instruction and data lines of an assembly text (4 bytes each), without labels and blank lines."""
    out = []
    for line in asm.split("\n"):
        line = line.split("#")[0].strip()
        if line and not line.endswith(":"):
            out.append(line)
    return out


def label_offsets(asm):
    """Every label's offset in the assembled text. PowerPC instructions and .long words are 4 bytes each, so the
    offsets follow from counting lines (keystone gives no symbol table)."""
    offsets, at = {}, 0
    for line in asm.split("\n"):
        line = line.split("#")[0].strip()
        if not line:
            continue
        if line.endswith(":"):
            offsets[line[:-1]] = at
        else:
            at += 4
    return offsets


def mode_test(cr=0):
    """r12 = the mode word; CR field `cr` compares it with 0. Uses r12 and that field only."""
    return f"    lis 12, {C}\n    lwz 12, {MODE}(12)\n    cmpwi {cr}, 12, 0\n"


def test_field(word):
    """The CR field a site's mode test may use: field 0, unless the replaced instruction is a conditional branch
    (it reads a field that must then survive the test): field 7."""
    return 7 if word >> 26 == 16 else 0


def stock(word, text_off, complete):
    """What a helper called with `bl` from main.rel .text `text_off` does in mode 0: the replaced instruction `word`,
    then back to the instruction after the site. r12 is free (the mode test used it)."""
    op = word >> 26
    if op == 18 and word & 1 and not word & 2:           # bl target: the same call, as a tail jump (returns to the site)
        disp = word & 0x03FFFFFC
        disp -= 0x04000000 if disp & 0x02000000 else 0
        target = (memmap.REL_TEXT + text_off + disp) & 0xFFFFFFFF if complete else 0
        return (f"    lis 12, {memmap.hi(target) - (0x10000 if target & 0x80000000 else 0)}\n"
                f"    ori 12, 12, {memmap.lo(target)}\n    mtctr 12\n    bctr\n")
    if op == 16:                                         # a conditional branch: taken, the return address moves on
        assert not word & 3, f"{word:08X}: a conditional call or an absolute branch is not handled"
        disp = word & 0xFFFC
        disp -= 0x10000 if disp & 0x8000 else 0
        return (f"    .long {(word & 0xFFFF0000) | 8:#010x}\n    blr\n"
                f"    mflr 12\n    addi 12, 12, {disp - 4}\n    mtlr 12\n    blr\n")
    assert op not in (17, 18, 19), f"{word:08X} at .text {text_off:#x}: a branch that is not handled"
    return f"    .long {word:#010x}\n    blr\n"
