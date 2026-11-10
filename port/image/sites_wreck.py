"""A wreck slides before its restore (the owner, 2026-11-09: "the game's physics take over right up until the point
where the machine would have been KO'd or retired anyways, and when that happens, do a restore"): the words of the
patch list that make a seat with restores (rule 0x8000, or flag 0x1 from a mode's `respawns`) move as the game without
Restore moves it while it is broken down, and start its restore at the step that game would have taken it out. Gated
on the race block's word memmap.RACE_WRECK_SLIDE, which the core writes: 1 in every race and practice from this build
(but a mode's `wreck_slide off`), 0 in every recording made before, which plays as it was raced. Native form only.
sites.py calls add() after sites_mode.add().

What the game does (fn_1_24A4C, a machine's own part of the step, called for each machine at fn_1_850C4 0x85260):
- 0x24CB0: `mr r3, r29; li r4, 1; bl fn_1_248C0` asks whether the machine's restore is due. fn_1_248C0 says yes for a
  machine with flag 0x1 that is destroyed (0x800), fallen (0x40000000) or broken down (0x80): so with Restore a
  breakdown starts the restore (M+0x214 = 180 or 270) at its own step, and the wreck never slides;
- 0x24D40, when it said no: with any of flags 0x08000201 the wreck's own routine fn_1_2B684 is not run (a retired
  machine, a CPU, a machine with Restore); without them a broken-down machine is the wreck fn_1_2B684 moves.
Flag 0x1 is read nowhere else in the step (the explosion effect's two reads, fn_1_6F90C 0x6F948 and fn_1_702A0
0x7035C, the verdict's site and the restore's exit fn_1_2BF8C 0x2C470 apart).

| Site | Where | What the game did | Now, in a race with RACE_WRECK_SLIDE 1 |
|---|---|---|---|
| `wreck_slide` | fn_1_24A4C 0x24CB8 | `bl fn_1_248C0` | of a machine with flag 0x1 that is broken down, not finished and not in a restore, the verdict of the game without Restore is asked (outtest.clause, restore=False: destroyed, fallen, or the breakdown's own out: at rest below 1.0, a CPU, race type 5). Out: nothing, so fn_1_248C0 starts the restore, with the game's own length. Racing: flag 0x1 is cleared for the rest of fn_1_24A4C, so fn_1_248C0 says no and 0x24D40 runs the wreck's routine, as without Restore |
| `wreck_step` | fn_1_850C4 0x85260 | `bl fn_1_24A4C` | a machine that had flag 0x1 at the call has it again after it: outside fn_1_24A4C a sliding wreck is a machine with Restore as before (the verdict never books it out, the stand-in never retires it, the mode's `respawns` words find their flag) |

The restore so starts one step after the step whose verdict would have booked the machine out without Restore (the
verdict is asked after the machines' step, fn_1_32A40; fn_1_24A4C before it), as the game's own Restore starts one
step after a fall or an explosion that happens after fn_1_24A4C. A finished machine is not touched: the game gives it
flag 0x1 at the line and restores a broken finisher, with or without Restore."""
import memmap
import outtest

SLIDE, DUE = 0x24CB8, 0x248C0     # fn_1_24A4C: bl fn_1_248C0 (r3 the machine, r4 1)
STEP, MACHINE = 0x85260, 0x24A4C  # fn_1_850C4: bl fn_1_24A4C (r3 = r27 the machine)
RESTORING = 0x214                 # M+0x214: the restore's countdown (u16)
assert memmap.MACHINE_FLAG_RESTORE == 1 and memmap.MACHINE_FLAG_BROKEN_DOWN == 0x80


def add(k, r, patch, stock, test, la, middle):
    """Adds the routines to `r` and the sites' entries through `patch`, as sites.add does; `test` is the mode test's
    text."""
    on = la(12, memmap.RACE + memmap.RACE_WRECK_SLIDE) + "    lwz 12, 0(12)\n    cmpwi 12, 0\n"
    at, due = k.TEXT + SLIDE, k.TEXT + DUE
    word = stock(at, k.branch(at, due, True))
    r.add("wreck_slide", test + "    beq ws_stock\n" + on + f"""    beq ws_stock
    lwz 12, 0(3)
    andi. 0, 12, {memmap.MACHINE_FLAG_RESTORE}
    beq ws_stock
    andi. 0, 12, {memmap.MACHINE_FLAG_BROKEN_DOWN}
    beq ws_stock
    andis. 0, 12, {memmap.MACHINE_FLAG_FINISHED >> 16}
    bne ws_stock
    lhz 0, {RESTORING:#x}(3)
    cmpwi 0, 0
    bne ws_stock
""" + la(11, memmap.REL_BSS) + outtest.clause(3, 11, 0, 12, "ws_stock", "ws_slide", restore=False) + f"""ws_slide:
    lwz 12, 0(3)
    rlwinm 12, 12, 0, 0, 30
    stw 12, 0(3)
ws_stock:
    @bl {due:#x}
    @b {at + 4:#x}
""")
    patch(at, word, 0, "wreck_slide")
    middle.append((at, ["r0", "r11", "r12", "cr0"], "wreck_slide"))

    at, machine = k.TEXT + STEP, k.TEXT + MACHINE
    word = stock(at, k.branch(at, machine, True))
    r.add("wreck_step", test + "    beq wt_stock\n" + on + f"""    beq wt_stock
    lwz 12, 0(3)
    andi. 0, 12, {memmap.MACHINE_FLAG_RESTORE}
    beq wt_stock
    @bl {machine:#x}
    lwz 12, 0(27)
    ori 12, 12, {memmap.MACHINE_FLAG_RESTORE}
    stw 12, 0(27)
    @b {at + 4:#x}
wt_stock:
    @bl {machine:#x}
    @b {at + 4:#x}
""")
    patch(at, word, 0, "wreck_step")
    middle.append((at, ["r0", "r11", "r12", "cr0"], "wreck_step"))
