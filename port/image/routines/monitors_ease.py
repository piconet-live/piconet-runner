"""The hand-back monitors' frame, eased (monitors.py's part: fixed rigidly to the car's movement, the frame looks
stiff). The monitors stay placed in front of the followed racer, but
from a frame that follows the racer's drawn matrix (block +0x14C) softly instead of being that matrix: an eased
position, back vector (the matrix's third column: forward negated) and up vector (its second column), kept in the
monitors' state (memmap.MONITORS +EASED) and moved toward the drawn matrix's every drawn frame by the exponential
step ALPHA = 1 - exp(-1 / (60 TAU)) (TAU seconds at 60 drawn frames a second).

- The back and up vectors are each eased toward the matrix's, then made unit again and square to each other: the
  right vector is up x back made unit, and up is back x right; so the machine's roll and pitch jitter (banking,
  landing bumps, side attacks) and quick swings of its heading reach the monitors softened, and the frame never
  flips. When an eased vector is shorter than MIN (the target turned half round) or the eased up and back are near
  parallel (|up x back|^2 below PARALLEL), or either is more than 135 degrees round from the matrix's (TURNED: a
  half turn would otherwise be a resting point of the easing), both are put back to the matrix's and the frame is
  built again from them.
  A matrix that fails the same tests (not a number) is used as it is, as before the easing.
- The position is eased except along the machine's own forward (the matrix's back column): that part follows at once,
  or at race speed the monitors, 40 units ahead, would fall behind the camera (0.2 s at 1000 km/h is about 55 units).
  What is eased is the rest: a side hit, a bump, a drift's sideways slide.
- The frame snaps to the matrix's (no easing): on the first drawn frame of a hand-back (monitors.py clears +FOLLOW
  for a new one), when the followed racer is not the one the frame was eased for (a viewport or spectate change),
  when the step word (control block +124) is not 0 to GAP steps on from the last drawn frame's (a scrub, a restore,
  frames not drawn in between), and when the eased position is SNAP units or more from the machine's (a respawn, a
  fall-out), or not a number. A rollback's correction (smooth.py moves the drawn matrix through it) is eased.

Picture only: the state is in the data window, no step reads it, no random value, no rule flag. The record's turn
(+0x48, rows: right, up, back, 0) and the points (position + a forward + b up) come from the eased frame.

State (memmap.MONITORS): +FOLLOW the followed racer + 1 the frame is eased for (0: none), +STEP_SEEN the step word at
the last drawn frame, +EASED the position, back and up vectors (36 bytes, floats)."""
import math, os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import memmap

TAU = 0.2                                # seconds: the time constant of the position's and the turn's easing
ALPHA = 1.0 - math.exp(-1.0 / (60.0 * TAU))   # the share of the way eased in a drawn frame (60 a second)
SNAP = 300.0                             # units: a machine this far from the eased frame snaps it (a respawn)
GAP = 30                                 # steps: a drawn frame further from the last one snaps
MIN = 0.01                               # squared length below which an eased vector is not trusted
PARALLEL = 0.04                          # |up x back|^2 below this (about 11.5 degrees apart): near parallel
TURNED = math.cos(math.radians(135))     # an eased vector this far round from the machine's is put back to it
FOLLOW, STEP_SEEN, EASED = 48, 52, 0xC8  # state offsets in memmap.MONITORS (+48 to +55 and +0xC8 to +0xEB)
POS, BACK, UP = EASED, EASED + 12, EASED + 24
# stack of monitors.py's frame: the constants, from K: 0 keep (1 - ALPHA), 4 0.5, 8 SNAP^2, 12 MIN, 16 PARALLEL,
# 20 TURNED
K = 0x40
CONSTS = (1.0 - ALPHA, 0.5, SNAP * SNAP, MIN, PARALLEL, TURNED)
assert 48 <= FOLLOW and STEP_SEEN + 4 <= 0x40 and 0x40 + 0x88 <= EASED and UP + 12 <= 0x100   # the window's room
assert K + 4 * len(CONSTS) <= 0xB0       # below monitors.py's saved registers
TP = (0x158, 0x168, 0x178)   # the drawn matrix's translation (block offsets)
TU = (0x150, 0x160, 0x170)   # its up column
TB = (0x154, 0x164, 0x174)   # its back column


def const(i, v):
    w = struct.unpack(">I", struct.pack(">f", v))[0]
    return f"    lis 0, {w >> 16}\n    ori 0, 0, {w & 0xFFFF}\n    stw 0, {K + 4 * i:#x}(1)\n"


def rsqrt():             # f10 = 1 / sqrt(f9) (f9 > 0): the estimate and two Newton steps; f11 scratch, f12 = 0.5
    step = "    fmuls 11, 10, 10\n    fmuls 11, 11, 9\n    fnmsubs 11, 11, 12, 12\n    fmadds 10, 10, 11, 10\n"
    return "    frsqrte 10, 9\n    frsp 10, 10\n" + step + step


def unit(regs, bad):     # the vector in regs made unit; to `bad` when its squared length is not above f13 or NaN
    x, y, z = regs
    return (f"    fmuls 9, {x}, {x}\n    fmadds 9, {y}, {y}, 9\n    fmadds 9, {z}, {z}, 9\n    fcmpu 0, 9, 13\n"
            f"    ble {bad}\n" + rsqrt() +
            f"    fmuls {x}, {x}, 10\n    fmuls {y}, {y}, 10\n    fmuls {z}, {z}, 10\n")


def cross(d, a, b):      # d = a x b (registers, d apart from a and b)
    return "".join(f"    fmuls {d[i]}, {a[(i + 1) % 3]}, {b[(i + 2) % 3]}\n"
                   f"    fnmsubs {d[i]}, {a[(i + 2) % 3]}, {b[(i + 1) % 3]}, {d[i]}\n" for i in range(3))


def copy(dst, src):      # state words at dst from the machine block's (r24) src offsets
    return "".join(f"    lwz 0, {s:#x}(24)\n    stw 0, {dst + 4 * i}(27)\n" for i, s in enumerate(src))


def asm():
    """Run with r27 = memmap.MONITORS, r28 = the record, r24 = the followed racer's machine block, r22 = that racer
    + 1, r30 = .rodata 0x78D0 (+0x18: 0.0). Writes the eased state and the record's turn; uses r0, r9-r11, r21,
    f0-f13 and the stack's K area. Ends at `mn_eased_done`."""
    turned = "".join(f"    lfs 0, {dst:#x}(27)\n    lfs 1, {src[0]:#x}(24)\n    fmuls 2, 0, 1\n"
                     f"    lfs 0, {dst + 4:#x}(27)\n    lfs 1, {src[1]:#x}(24)\n    fmadds 2, 0, 1, 2\n"
                     f"    lfs 0, {dst + 8:#x}(27)\n    lfs 1, {src[2]:#x}(24)\n    fmadds 2, 0, 1, 2\n"
                     f"    fcmpu 0, 2, 3\n    blt mn_reset\n" for dst, src in ((BACK, TB), (UP, TU)))
    ease = "".join(f"    lfs 0, {dst + 4 * i}(27)\n    lfs 1, {s:#x}(24)\n    fsubs 0, 0, 1\n"
                   f"    fmadds 0, 0, 13, 1\n    stfs 0, {dst + 4 * i}(27)\n"
                   for dst, src in ((BACK, TB), (UP, TU)) for i, s in enumerate(src))
    error = "".join(f"    lfs {i}, {POS + 4 * i}(27)\n    lfs 3, {s:#x}(24)\n    fsubs {i}, {i}, 3\n"
                    for i, s in enumerate(TP))
    place = "".join(f"    lfs 1, {s:#x}(24)\n    fmadds {i + 6}, {i + 6}, 13, 1\n"
                    f"    stfs {i + 6}, {POS + 4 * i}(27)\n" for i, s in enumerate(TP))
    turn = "".join(f"    stfs {6 + i}, {0x48 + 16 * i}(28)\n    stfs {3 + i}, {0x4C + 16 * i}(28)\n"
                   f"    stfs {i}, {0x50 + 16 * i}(28)\n    stfs 9, {0x54 + 16 * i}(28)\n" for i in range(3))
    raw = "".join(f"    lwz 0, {0x14C + 16 * i + 4 * j:#x}(24)\n    stw 0, {0x48 + 16 * i + 4 * j}(28)\n"
                  for i in range(3) for j in range(3)) + \
        "".join(f"    stfs 9, {0x54 + 16 * i}(28)\n" for i in range(3))
    return "".join(const(i, v) for i, v in enumerate(CONSTS)) + f"""    li 21, 0
    lis 9, {memmap.hi(memmap.CONTROL)}
    lwz 10, 124(9)
    lwz 11, {STEP_SEEN}(27)
    stw 10, {STEP_SEEN}(27)
    subf 11, 11, 10
    cmplwi 11, {GAP}
    bgt mn_snap
    lwz 0, {FOLLOW}(27)
    cmpw 0, 22
    bne mn_snap
""" + error + f"""    fmuls 4, 0, 0
    fmadds 4, 1, 1, 4
    fmadds 4, 2, 2, 4
    lfs 5, {K + 8:#x}(1)
    fcmpu 0, 4, 5
    blt mn_ease
mn_snap:
    stw 22, {FOLLOW}(27)
""" + copy(POS, TP) + """    b mn_reset
mn_ease:
""" + "".join(f"    lfs {3 + i}, {s:#x}(24)\n" for i, s in enumerate(TB)) + f"""    fmuls 9, 0, 3
    fmadds 9, 1, 4, 9
    fmadds 9, 2, 5, 9
    fnmsubs 6, 9, 3, 0
    fnmsubs 7, 9, 4, 1
    fnmsubs 8, 9, 5, 2
    lfs 13, {K:#x}(1)
""" + place + f"    lfs 3, {K + 20:#x}(1)\n" + turned + ease + """    b mn_frame
mn_reset:
    cmpwi 21, 0
    bne mn_raw
    li 21, 1
""" + copy(BACK, TB) + copy(UP, TU) + f"""mn_frame:
    lfs 12, {K + 4:#x}(1)
    lfs 13, {K + 12:#x}(1)
""" + "".join(f"    lfs {i}, {BACK + 4 * i}(27)\n    lfs {3 + i}, {UP + 4 * i}(27)\n" for i in range(3)) + \
        unit((0, 1, 2), "mn_reset") + unit((3, 4, 5), "mn_reset") + cross((6, 7, 8), (3, 4, 5), (0, 1, 2)) + \
        f"    lfs 13, {K + 16:#x}(1)\n" + unit((6, 7, 8), "mn_reset") + cross((3, 4, 5), (0, 1, 2), (6, 7, 8)) + \
        "".join(f"    stfs {i}, {BACK + 4 * i}(27)\n    stfs {3 + i}, {UP + 4 * i}(27)\n" for i in range(3)) + \
        "    lfs 9, 0x18(30)\n" + turn + """    b mn_eased_done
mn_raw:
    lfs 9, 0x18(30)
""" + raw + "mn_eased_done:\n"
