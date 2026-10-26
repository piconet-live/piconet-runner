"""Pictures between two steps, for a display faster than the game (high refresh rate; sites.py calls add() after the
cameras' words). Native form only: the fork's image does not have it.

The site is fn_1_37AA8's `bl fn_1_3CC4` at main.rel .text 0x37C18: every step of the pass is run (the frame routine
at 0x37B38 is behind it) and nothing of the pass is drawn yet. The routine tells the host (memmap.HOST_HFR, r3 the
operation) that the pass's steps are done, and whether pictures between may be drawn this pass: not while the game
is paused (fn_1_3EFA8), while the camera object updates itself (fn_1_660C) or while the main loop's capture path is
on (.data 0x3D648). The host reads the machines and the chase camera then and asks the emulator's GPU thread to draw
the frame of the step before again, corrected, for each picture between (port/core/src/hfr.cpp,
VideoCommon/FrameRedraw.h); nothing of the game runs again and nothing of its memory is written. Then the game's
own call, as in the stock game. In a plain start (mode 0) only the game's call.

The host call's operations (r3; r4 its argument; the host's answer in r3, unused; no other register changes):
0 the plan: r4 not 0 when nothing may be drawn between this pass;
5 the pass is done: the host keeps this pass's state for the next pass's request.
(Operations 1 to 4, frames between drawn with the game's own draw code, are not used.)"""

PLAN, DONE = 0, 5
SITE, TARGET = 0x37C18, 0x3CC4                 # `bl fn_1_3CC4`
DATA = 0x16D600                                # main.rel .data from its header
CAPTURE = DATA + 0x3D648                       # the main loop's capture path


def ha(a):
    """`lis` half of an address whose low half is used as a signed offset."""
    h = ((a + 0x8000) >> 16) & 0xFFFF
    return h - 0x10000 if h >= 0x8000 else h


def lo(a):
    l = a & 0xFFFF
    return l - 0x10000 if l >= 0x8000 else l


def add(k, r, patch, stock, test, middle, entry=True):
    """Adds the routine to `r` and its entry through `patch`, as sites.add does (`middle`: its list of sites in the
    middle of a function; this site is a call's, so nothing is added there). `entry` False: the site's entry is
    another routine's, which goes on to this one (sites_present.py's `present_draw`, when the frame draws)."""
    TEXT, REL, HOST = k.TEXT, k.TEXT - 0xDC, "host_hfr"
    t = lambda off: f"{TEXT + off:#x}"
    rel = lambda off: REL + off
    word = stock(TEXT + SITE, k.branch(TEXT + SITE, TEXT + TARGET, True))
    assert k.branch_target(word, TEXT + SITE) == TEXT + TARGET
    # fn_1_3CC4's arguments and fn_1_37AA8's own registers are kept around the calls
    saves = "".join(f"    stw {n}, {0x08 + 4 * (n - 3):#x}(1)\n" for n in range(3, 11)) + \
        "".join(f"    stfd {n}, {0x28 + 8 * (n - 1):#x}(1)\n" for n in range(1, 9))
    loads = saves.replace("stw ", "lwz ").replace("stfd ", "lfd ")
    r.add("hfr", test + f"""    beq hfr_stock
    stwu 1, -0xB0(1)
    stmw 25, 0x8C(1)
{saves}    @bl {t(0x3EFA8)}
    mr 27, 3
    @bl {t(0x660C)}
    or 27, 27, 3
    lis 12, {ha(rel(CAPTURE))}
    lwz 0, {lo(rel(CAPTURE))}(12)
    or 4, 27, 0
    li 3, {PLAN}
    @bl {HOST}
    li 3, {DONE}
    li 4, 0
    @bl {HOST}
{loads}    lmw 25, 0x8C(1)
    addi 1, 1, 0xB0
hfr_stock:
    @bl {TEXT + TARGET:#x}
    @b {TEXT + SITE + 4:#x}
""")
    if entry:
        patch(TEXT + SITE, word, 0, "hfr")
