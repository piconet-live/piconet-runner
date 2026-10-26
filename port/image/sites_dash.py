"""The dash plate's boost as data: one word of
the patch list, so that the length of a dash plate's boost is int(A + B x D) with A and B the two floats of the
machine's seat (memmap.SEAT_BLOCK: the seat's constants, +SEAT_DASH_BASE and +SEAT_DASH_SCALE), D the machine's boost
duration (its block +0x234, seconds). The core writes every seat's A and B for every race and replay: its stat
pack's constants DashPlateBase and DashPlateScale, or the game's own 0 and 30 (port/core/src/const_ranges.h). GX Unleashed 5.0.4 is A = 30, B = 15 (0.5 s plus a quarter
of the boost's duration), which its own main.rel computes with the same two instructions. Native form only: the
fork's image does not have it, and its runs are the stock game's formula. sites.py calls add().

The site is in machine_boost_update (fn_1_26F34), the branch a dash plate takes (state bit 0x00400000), r31 the pool
lbl_1_rodata_840, r30 the machine:

    .text+0x271A8  lfs   f1, 0x258(r31)   ; 60.0
    .text+0x271AC  lfs   f0, 0x234(r30)   ; D
    .text+0x271B0  lfs   f2, 0x1C(r31)    ; 0.5
    .text+0x271BC  fmuls f0, f1, f0       ; 60 x D
    .text+0x271C4  fmuls f0, f2, f0       ; 0.5 x (60 x D)    <- the site
    .text+0x271C8  fctiwz f0, f0          ; -> r29: the countdown +0x18A = max(+0x18A, r29)

| Site | Where | What the game did | Now, in a race or replay of ours |
|---|---|---|---|
| `dash_plate` | fn_1_26F34 0x271C4 | `fmuls f0, f2, f0`: frames = int(0.5 x single(60 x D)) | f0 = A + single(B x D) of the machine's seat (its racer index, block +4) (`fmuls`, then the double `fadd` GX Unleashed uses), which `fctiwz` cuts as before |

Why the stock A = 0, B = 30 give the game's bits (to be shown by the replay set as well): D is the bits of 0 or a
normal number (the stat ranges refuse denormals, -0 and any negative D), so 60 x D rounded to single, halved, is
exact and equals 30 x D rounded to single (a power of two moves no rounding while nothing overflows: D < 4.26); and
0.0 + x is x for every x that is not -0, and x is never -0 here (D >= 0). The machine's +0x234 is not written
between 0x271AC and the site (four float instructions), so the reload reads the D the game read.

Registers: the routine writes f0 (the result the game's next word reads), f2, r11, r12 and cr0. A racer index
outside the 30 seats (none is) takes the game's own instruction. f2's last read was the
replaced word; the instructions to the call at 0x271D4 read neither f2 nor f1, and a call leaves the float registers
f0 to f13 without meaning (the liveness proof covers r11, r12 and cr0; the float registers are this paragraph's).

**The energy boost's length** in the same shape: an energy boost lasts int(A + B x D)
frames, A and B the seat's BoostLengthBase and BoostLengthScale (+SEAT_BOOST_BASE, +SEAT_BOOST_SCALE), where the game
has int(60 x D). The site, in fn_1_26F34's boost start (r31 the pool, r30 the machine):

    .text+0x272C4  lfs   f2, 0x258(r31)   ; 60.0
    .text+0x272C8  lfs   f1, 0x234(r30)   ; D
    .text+0x272CC  lfs   f0, 0x68(r31)
    .text+0x272D0  fmuls f1, f2, f1       ; 60 x D        <- the site
    .text+0x272D4  fcmpo cr0, f27, f0
    .text+0x272D8  fctiwz f1, f1          ; -> the countdowns +0x18A and +0x18B (bytes)

| Site | Where | What the game did | Now, in a race or replay of ours |
|---|---|---|---|
| `boost_length` | fn_1_26F34 0x272D0 | `fmuls f1, f2, f1`: frames = int(single(60 x D)) | f1 = A + single(B x D) of the machine's seat (`fmuls`, then the double `fadd`) |

The stock A = 0, B = 60 give the game's bits: f1 holds D (0x272C8, nothing writes it before the site), B x D is the
game's own single product, and 0.0 + x is x for x not -0 (D >= 0). Registers: f2 (60.0) is read by no instruction
before the call at 0x27344 (whose arguments are integers) and means nothing after it; cr0 is written at 0x272D4 before
any read; r11 and r12 are the liveness proof's. The packs' readers refuse an A and B with which a machine's count could
pass 255 (the bytes' most), for the longest boost duration the pack's car data gives a machine or a custom one."""
import memmap
from sites_seats import seat_constants

SITE, KNOWN = 0x271C4, 0xEC020032          # fmuls f0, f2, f0
DURATION = 0x234                           # the machine's boost duration, as fn_1_26F34 reads it at 0x271AC
STOCK_BASE, STOCK_SCALE = 0.0, 30.0        # A and B of the game's own formula
BOOST_SITE, BOOST_KNOWN = 0x272D0, 0xEC220072   # fmuls f1, f2, f1: the energy boost's 60 x D
BOOST_BASE, BOOST_SCALE = 0.0, 60.0        # the energy boost's A and B, the game's own


def add(k, r, patch, stock, test, la, middle):
    """Adds the routine to `r` and the site's entry through `patch`, as sites.add does; `test` is the mode test's
    text, `la` loads an address."""
    at = k.TEXT + SITE
    word = stock(at, KNOWN)
    r.add("dash_plate", test + """    beq dp_stock
""" + seat_constants(30, "dp_stock") + f"""    lfs 0, {DURATION:#x}(30)
    lfs 2, {memmap.SEAT_DASH_SCALE}(12)
    fmuls 0, 2, 0
    lfs 2, {memmap.SEAT_DASH_BASE}(12)
    fadd 0, 2, 0
    @b {at + 4:#x}
dp_stock:
    .long {word:#010x}
    @b {at + 4:#x}
""")
    patch(at, word, 0, "dash_plate")
    middle.append((at, ["r11", "r12", "cr0"], "dash_plate"))

    # the energy boost's length (f1 = D, the machine r30)
    at = k.TEXT + BOOST_SITE
    word = stock(at, BOOST_KNOWN)
    r.add("boost_length", test + """    beq bl_stock
""" + seat_constants(30, "bl_stock") + f"""    lfs 2, {memmap.SEAT_BOOST_SCALE}(12)
    fmuls 1, 2, 1
    lfs 2, {memmap.SEAT_BOOST_BASE}(12)
    fadd 1, 2, 1
    @b {at + 4:#x}
bl_stock:
    .long {word:#010x}
    @b {at + 4:#x}
""")
    patch(at, word, 0, "boost_length")
    middle.append((at, ["r11", "r12", "cr0"], "boost_length"))
