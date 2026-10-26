"""The race HUD's banners start in the simulation step that raises them (for the frame routine, netframe.py, which
includes this code and calls `banners_install` on entry every frame).

The HUD entry (0x34 bytes per port from .bss 0x8FDA8) has three banner timers, +0xA, +2 (FINAL LAP, raised by the
lap event fn_1_327F8) and +1, each raised by a registration (fn_1_15E764, fn_1_15E5E4, fn_1_15E540) that sets the
timer to 0xFF and adds its countdown task (fn_1_15E220, task list 13, which leaves 0xFF alone). The HUD drawing
(fn_1_15EFEC, fn_1_15E7F4) then plays the banner's sound and sets the timer to 0x78 when it finds 0xFF, +0xA before +2
before +1 (a lower one waits at 0xFF while one before it is up), and the task counts it down. Both are presentation,
run in a frame's last step only: a rollback from before the lap line took the banner back to 0xFF, the re-simulation
raised it again and the drawing restarted it with its sound (FINAL LAP three times in one race), and one from
after it set the countdown back a rollback's depth (the banner stayed up). Now each registration marks its countdown
task (the newest task of list 13) to run in every step (flag bit 1), and the countdown task starts the banner itself
when it finds 0xFF and no banner before it up: the sound, behind the drawing's own check (fn_1_3F864), and 0x78. The
banner is then a function of the race's steps, the same after a re-simulation, whose replayed steps play no sound
(mute.py). Control block +500: the main.rel address the sites were patched for (follow.installer)."""
from follow import installer, s16

# (text offset, original word, mask, helper)
SITES = [
    (0x15E7D4, 0x380000FF, 0xFFFFFFFF, "a"),    # fn_1_15E764: li r0, 0xFF (stored at +0xA by the next instruction)
    (0x15E658, 0x98A40002, 0xFFFFFFFF, "2"),    # fn_1_15E5E4: stb r5, 2(r4)
    (0x15E5B4, 0x98A40001, 0xFFFFFFFF, "1"),    # fn_1_15E540: stb r5, 1(r4)
    (0x15E244, 0x4182000C, 0xFFFFFFFF, "t"),    # fn_1_15E220: beq (0xFF: no countdown)
]
assert 0x160000 - 0x1D04 == 0xDC + 0x15E220 and 0x40000 - 0x6C0 == 0xDC + 0x3F864 and 0xA0000 + 0x2E60 == 0xDC + 0xA2D84
assert 0x250000 - 0x6218 == 0x1BA040 + 0x8FDA8

def flag(name):
    """Marks the newest task of list 13 to run in every step if it is a banner countdown (uses r9-r12)."""
    return f"""    lis 12, 0x8000
    lwz 12, 0x30C8(12)
    addis 11, 12, 0x1C
    addi 11, 11, -0x5FC0
    lwz 11, 0xDAC(11)
    lwz 11, 0x4C(11)
    lwz 11, 8(11)
    cmpwi 11, 0
    beq bf{name}_end
    lwz 10, 0(11)
    addis 9, 12, 0x16
    addi 9, 9, -0x1D04
    cmpw 10, 9
    bne bf{name}_end
    lwz 10, 0x14(11)
    ori 10, 10, 1
    stw 10, 0x14(11)
bf{name}_end:
"""


def sound(s):
    return f"""    lis 3, {s16(s >> 16)}
    ori 3, 3, {s & 0xFFFF}
    addis 12, 30, 0xA
    addi 12, 12, 0x2E60
    mtctr 12
    bctrl
"""


# The countdown task's helper, in place of its `beq` past the countdown when the timer (r4; r3 points at it) is 0xFF:
# the countdown then stores r4 - 1. A banner with none before it up starts (r4 = 0x79: 0x78); otherwise r4 = 0x100
# keeps it waiting at 0xFF.
START = f"""    cmplwi 4, 0xFF
    bnelr
    stwu 1, -32(1)
    mflr 0
    stw 0, 36(1)
    stw 31, 28(1)
    stw 30, 24(1)
    stw 29, 20(1)
    mr 31, 3
    lis 30, 0x8000
    lwz 30, 0x30C8(30)
    addis 9, 30, 0x25
    addi 9, 9, -0x6218
    subf 9, 9, 31
    li 10, 0x34
    divwu 11, 9, 10
    mullw 11, 11, 10
    subf 29, 11, 9
    subf 9, 29, 31
    li 4, 0x100
    cmpwi 29, 0xA
    beq bt_start
    lbz 0, 0xA(9)
    cmpwi 0, 0
    bne bt_done
    cmpwi 29, 2
    beq bt_start
    lbz 0, 2(9)
    cmpwi 0, 0
    bne bt_done
bt_start:
    addis 12, 30, 0x4
    addi 12, 12, -0x6C0
    mtctr 12
    bctrl
    cmpwi 3, 0
    beq bt_quiet
    cmpwi 29, 0xA
    bne bt_not_a
{sound(0xA9061A00)}{sound(0xA9092800)}    b bt_quiet
bt_not_a:
    cmpwi 29, 2
    bne bt_not_2
{sound(0xA9061700)}    b bt_quiet
bt_not_2:
{sound(0xA9061600)}bt_quiet:
    li 4, 0x79
bt_done:
    mr 3, 31
    lwz 29, 20(1)
    lwz 30, 24(1)
    lwz 31, 28(1)
    lwz 0, 36(1)
    mtlr 0
    addi 1, 1, 32
    blr
"""

HELPERS = {   # the registrations: the instruction replaced, after marking the task
    "a": flag("a") + "    li 0, 0xFF\n    blr\n",
    "2": flag("2") + "    li 0, 0xFF\n    stb 0, 2(31)\n    mr 3, 29\n    blr\n",
    "1": flag("1") + "    li 0, 0xFF\n    stb 0, 1(31)\n    mr 3, 29\n    blr\n",
    "t": START,
}


def asm():
    """The helpers and `banners_install` (r31 = the control block; called with bl; uses r0, r3-r12 and 32(r1))."""
    return installer("banners_install", "b", SITES, HELPERS, 500)
