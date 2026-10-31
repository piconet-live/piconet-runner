"""The combat values (piconet-docs research/game-modes/vocabulary.md section 1, design 5): each site reads the live word
in the mode's block (port/core/src/mode.h COMBAT, memmap.MODE_COMBAT), which a setup word sets at the mode's first run and
race.X changes from the next step. A word of 0 is the game's own: a race with no mode, a mode that does not name the value,
and every step before the mode's first run take the game's own instruction, so the race is the stock one bit for bit.
Native form only: the fork's image does not have it. sites.py calls add() after sites_mode.add().

| Site | Where | What the game did | Now, with the word |
|---|---|---|---|
| `combat_factor_a`, `_b` | fn_1_239C4 0x24588, 0x24674, a pair's damage (`lfs f0, 0x128(r31)`: 0.002, .rodata 0x968, read nowhere else) | 0.002 | 0.002 x race.damage |
| `combat_cap` | fn_1_24F10 0x24F50, a hit's cap for a player (`lfs f0, 0x178(r6)`: 20, .rodata 0x9B8, which nine loads share) | 20 | 20 x race.damage, when the caller is a contact's (the two calls in fn_1_239C4: fn_1_24F10 is a leaf, so the link register still names its caller) |
| `combat_knock` | fn_1_239C4 0x242DC, after the pair's scales are chosen (f31, f30: 1.0, 1.5 attacking, 2.2) | the scales | each x race.knockback |
| `combat_wall` | fn_1_266A8 0x269D4, a wall's damage (`fmuls f3, f28, f0`: x body) | the damage | x race.wall_damage, before the cap. The condition in cr0 is live here: this routine tests with cr7 |
| `combat_bounce` | fn_1_266A8 0x26BC4, all three branches of the bounce's e joined (f1) | e | 1 + race.wall_bounce x (e - 1) |
| `combat_spin` | fn_1_287E8 0x28840, the test of the spin's flag 0x8 | a spin goes on | race.spin_attacks off: the flag cleared first, CPUs too |
| `combat_side` | fn_1_287E8 0x28908, the test of the side attack's flag 0x20000 (r5, the flags) | a side attack goes on | race.side_attacks off: the flag cleared first |
| `combat_immune_a`, `_b` | fn_1_239C4 0x245BC, 0x246A8, whether the machine (r24, r25) takes the pair's damage | as the game says | race.spin_immunity on and the machine spinning: not |

Everything written is the machine's (SIM, in the checksum) and every word read is the block's, under the undo log."""
import memmap

SPIN_FLAG = 8
FACTORS = ((0x24588, 0xC01F0128, "combat_factor_a"), (0x24674, 0xC01F0128, "combat_factor_b"))
CAP, CAP_KNOWN = 0x24F50, 0xC0060178
CONTACT_CALLS = (0x245D8, 0x246C4)                 # fn_1_239C4: bl fn_1_24F10
KNOCK, KNOCK_KNOWN = 0x242DC, 0xC0180094
WALL, WALL_KNOWN = 0x269D4, 0xEC7C0032
BOUNCE, BOUNCE_KNOWN = 0x26BC4, 0xC01F0238
SPIN, SPIN_KNOWN = 0x28840, 0x54000739
SIDE, SIDE_KNOWN = 0x28908, 0x54A0039D
IMMUNE = ((0x245BC, 0x2C1B0000, 24, "combat_immune_a"), (0x246A8, 0x2C1A0000, 25, "combat_immune_b"))
C = memmap.MODE_COMBAT
assert C + memmap.MODE_COMBAT_SIZE < 0x8000


def add(k, r, patch, stock, test, la):
    """Adds the routines to `r` and an entry for each site through `patch`; gives the sites in the middle of a function,
    as sites.add wants them."""
    TEXT = k.TEXT
    middle = []

    def base():
        return la(12, memmap.MODES) + "    lwz 12, 4(12)\n"

    def guarded(word, label, compare="cmpwi 12, 0", beq="beq"):
        """Through to what follows when the mode's live word at `word` is not 0; `label` otherwise."""
        cmplwi = compare.replace("cmpwi", "cmplwi")
        return base() + f"""    {cmplwi}
    {beq} {label}
    lwz 12, {C + word:#x}(12)
    {compare}
    {beq} {label}
"""

    def site(name, offset, known, body, dead=("r12", "cr0")):
        at = TEXT + offset
        word = stock(at, known)
        r.add(name, body(word, at))
        patch(at, word, 0, name)
        middle.append((at, list(dead), name))

    def tested(label, body):
        return test + f"    beq {label}\n" + body

    for offset, known, name in FACTORS:
        site(name, offset, known, lambda word, at, name=name: tested(f"{name}_stock", guarded(memmap.MODE_COMBAT_FACTOR, f"{name}_stock") + base() + f"""    lfs 0, {C + memmap.MODE_COMBAT_FACTOR:#x}(12)
    @b {at + 4:#x}
{name}_stock:
    .long {word:#010x}
    @b {at + 4:#x}
"""))

    # a contact's call only: the link register, in a leaf, names the caller; r0 is written again at 0x24F64 before a read
    calls = "".join(la(0, TEXT + call + 4) + """    cmplw 12, 0
    beq combat_cap_contact
""" for call in CONTACT_CALLS)
    site("combat_cap", CAP, CAP_KNOWN, lambda word, at: tested("combat_cap_stock", guarded(memmap.MODE_COMBAT_CAP, "combat_cap_stock") + "    mflr 12\n" + calls + f"""    b combat_cap_stock
combat_cap_contact:
""" + base() + f"""    lfs 0, {C + memmap.MODE_COMBAT_CAP:#x}(12)
    @b {at + 4:#x}
combat_cap_stock:
    .long {word:#010x}
    @b {at + 4:#x}
"""), ("r0", "r12", "cr0"))

    site("combat_knock", KNOCK, KNOCK_KNOWN, lambda word, at: tested("combat_knock_stock", guarded(memmap.MODE_COMBAT_KNOCK, "combat_knock_stock") + base() + f"""    lfs 0, {C + memmap.MODE_COMBAT_KNOCK:#x}(12)
    fmuls 31, 31, 0
    fmuls 30, 30, 0
combat_knock_stock:
    .long {word:#010x}
    @b {at + 4:#x}
"""))

    # cr0 holds the rule's test for the next branch: cr7 here, the mode test's text in it too
    site("combat_wall", WALL, WALL_KNOWN, lambda word, at: test.replace("cmpwi 12, 0", "cmpwi 7, 12, 0") + f"""    .long {word:#010x}
    beq 7, combat_wall_out
""" + guarded(memmap.MODE_COMBAT_WALL, "combat_wall_out", "cmpwi 7, 12, 0", "beq 7,") + base() + f"""    lfs 0, {C + memmap.MODE_COMBAT_WALL:#x}(12)
    fmuls 3, 3, 0
combat_wall_out:
    @b {at + 4:#x}
""", ("r12", "cr7"))

    site("combat_bounce", BOUNCE, BOUNCE_KNOWN, lambda word, at: tested("combat_bounce_stock", guarded(memmap.MODE_COMBAT_BOUNCE, "combat_bounce_stock") + base() + f"""    lfs 0, {C + memmap.MODE_COMBAT_BOUNCE:#x}(12)
    lfs 2, {C + memmap.MODE_COMBAT_ONE:#x}(12)
    fsubs 1, 1, 2
    fmuls 1, 1, 0
    fadds 1, 1, 2
combat_bounce_stock:
    .long {word:#010x}
    @b {at + 4:#x}
"""))

    site("combat_spin", SPIN, SPIN_KNOWN, lambda word, at: tested("combat_spin_stock", guarded(memmap.MODE_COMBAT_SPIN, "combat_spin_stock") + f"""    rlwinm 0, 0, 0, 29, 27
    stw 0, 0(3)
combat_spin_stock:
    .long {word:#010x}
    @b {at + 4:#x}
"""))

    site("combat_side", SIDE, SIDE_KNOWN, lambda word, at: tested("combat_side_stock", guarded(memmap.MODE_COMBAT_SIDE, "combat_side_stock") + f"""    rlwinm 5, 5, 0, 15, 13
    stw 5, 0(3)
combat_side_stock:
    .long {word:#010x}
    @b {at + 4:#x}
"""))

    # not taking it: the compare's "equal", which the next branch leaves to the end of the damage
    for offset, known, machine, name in IMMUNE:
        site(name, offset, known, lambda word, at, machine=machine, name=name: tested(f"{name}_stock", guarded(memmap.MODE_COMBAT_IMMUNE, f"{name}_stock") + f"""    lwz 12, 0({machine})
    andi. 12, 12, {SPIN_FLAG}
    beq {name}_stock
    cmpw 12, 12
    @b {at + 4:#x}
{name}_stock:
    .long {word:#010x}
    @b {at + 4:#x}
"""))
    return middle
