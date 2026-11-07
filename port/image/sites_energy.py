"""The energy meter under a mode's max energy (piconet-docs research/game-modes/vocabulary.md 4.4). A drawing-only site in
the getter `fn_1_867F8`, which seven places call, all of them drawing and none a step: the meter `fn_1_C8DC0` (0xC8EA4, 0xC8F78,
0xC9158) and its second form `fn_1_C96A4` (0xC9768, 0xC9834, 0xC9B28), and the face board `fn_1_D27B8` (0xD2998).

| Site | Where | What the game did | Now |
|---|---|---|---|
| `meter_energy` | fn_1_867F8 0x86808 | `lfs f1, 0x184(r3)`: the energy, of which the callers take width x energy / 100 and blink under 25 and 15 | the same word while M+0x5DA is 100, or in a plain start; else energy x 100 / M+0x5DA |

So the bar is energy / max and the blink is at 25% and 15% of max. The value goes to the drawing only: it reaches the sprite
pool (`rollback-state.tsv` row sprite-pool, untracked) and the picture, and writes nothing a step reads (handbook
race-map-rollback.md 2, rule 2). At 100, which every machine has in a race not using the words, the routine returns the game's own
load, so nothing differs by a bit.

Registers: the routine writes f0, f1, f2, r12 and cr0 and uses its own stack frame; the word after the site is the getter's `blr`.
f0 and f2 are volatile across a call, and each of the seven callers loads them before reading them [code]."""

GETTER, GETTER_KNOWN = 0x86808, 0xC0230184      # fn_1_867F8: lfs f1, 0x184(r3), r3 = the machine
FULL, ENERGY, STOCK_FULL = 0x5DA, 0x184, 100
HUNDRED = 0x42C8                                # the float 100, its high half


def add(k, r, patch, stock, test, middle):
    """Adds the routine to `r` and the site's entry through `patch`, as sites.add does; `test` is the mode test's text."""
    at = k.TEXT + GETTER
    word = stock(at, GETTER_KNOWN)
    r.add("meter_energy", test + f"""    beq me_stock
    lhz 12, {FULL:#x}(3)
    cmplwi 12, {STOCK_FULL}
    beq me_stock
    stwu 1, -0x20(1)
    stw 12, 0x14(1)
    lis 12, 0x4330
    stw 12, 0x10(1)
    stw 12, 0x18(1)
    li 12, 0
    stw 12, 0x1C(1)
    lfd 1, 0x10(1)
    lfd 0, 0x18(1)
    fsubs 0, 1, 0
    lis 12, {HUNDRED:#x}
    stw 12, 0x14(1)
    lfs 1, 0x14(1)
    lfs 2, {ENERGY:#x}(3)
    fmuls 1, 2, 1
    fdivs 1, 1, 0
    addi 1, 1, 0x20
    @b {at + 4:#x}
me_stock:
    .long {word:#010x}
    @b {at + 4:#x}
""")
    patch(at, word, 0, "meter_energy")
    middle.append((at, ["r12", "cr0"], "meter_energy"))
