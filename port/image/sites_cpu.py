"""Computer drivers race at their own pace, with no rubber band. In a race of
ours whose race block says the band is off
(memmap.RACE_RUBBER_BAND_ON 0: every race and practice from 0.4.0, and their replays) no driver paces against
anyone: every driver's accelerator and boost chance are its level's middle values, whatever the player seats do.
With the word 1 (a replay recorded before 0.4.0, which raced with the band: the core reads it from the replay's
header, files_replay.cpp) and in a plain start every word below does what the game did, so every older replay
plays as it was raced. Native form only: the fork's image (the JIT reference) keeps the band. sites.py calls add().

The band, as the code has it (cpu-level 3, Master, the level our races use; .rodata 0x8031EED8 + 8 x level):
- The pre-pass fn_1_C42F4 (priority 2, once a step) copies the references through four getters into the drivers'
  globals (.bss 0x7AC48): +0x49 best reference rank, +0x4A worst, +0x4C and +0x50 their progress (status +0xF8).
  In our image the references are the player seats (standin_race.py `refs`).
- The driver fn_1_C4ABC (priority 3, each machine) picks a state from its own rank r (status +0x115, 0 = leading)
  at 0xC53CC: 0 when r <= best, 2 when r >= worst, else 1 (a change waits 60 steps unless the block's flag
  0x02000000 asks for it at once). Nothing else writes 0 or 2: every other state change (3 to 6 and back) goes
  to 1, and so does a new driver block (fn_1_C489C).
- Its accelerator (+0x0C, the jump table at 0xC64DC): state 1 is D+8 (0.96 at level 3); state 0 is D+8 moved
  towards D+0xC (0.70) as the driver's lead over the best reference grows from 50 to 350 units, and 1.0 for the
  machine that holds the best reference's own rank (in the stock game the human's own, which runs no driver);
  state 2 is D+8 moved towards D+4 (1.03) as its gap behind the worst reference grows from 50 to 350. Within 50
  units both give D+8 exactly: D+8 is the band's centre. D = .bss 0x7AC64, from the level's row (/100).
- Its boost chance (f28, 0xC63CC): x 0.8 x 0.8 in state 0, x 0.8 in state 1, x 1 in state 2.
- Nothing else of the driver tells states 0, 1 and 2 apart; the level's other constants (D+0x10 to D+0x20:
  steering, strafe, boost scale) apply in every state and are not touched here.

Now, in a race of ours with the band off (the word 1: both sites take their stock way):

| Site | Where | What the game did | Now |
|---|---|---|---|
| `cpu_refs` | fn_1_C42F4 0xC46F4 | `bl fn_1_8C3EC`: the four getters (references' ranks and progress) into +0x49..+0x53 | +0x49 = 0, +0x4A = 0xFF, no getter is called (+0x4C and +0x50 keep what they hold and nothing reads them) |
| `cpu_leader` | fn_1_C4ABC 0xC53DC | `li r4, 0`: state 0 for a driver at or ahead of the best reference | `li r4, 1` |

With best 0 and worst 0xFF every driver but the leader is between the references (state 1); the leader (rank 0) is
"at the best reference's rank" and takes 0xC53DC, which now gives it state 1 too. So every driver is in state 1:
accelerator D+8 and boost chance x 0.8, the values the band gives a driver racing among the players. No driver
reads a player's progress any more, nor any other machine's rank or progress for pace, and a late player seat's
input moves no other driver's accelerator. The site in the driver is
reached once a step (by the leader's driver), so the other 29 driver calls stay in their closed bodies.

The other form ("full accelerator", the stock game's value for the best reference's own machine): every driver
at 1.0, by a third site at 0xC6574 (`lfs f0, 8(r3)`, state 1's D+8) loading the pool's 1.0 (`lfs f0, 0x728(r29)`)
in a race. Not built: 1.0 is above D+8 at every level and below D+4 at levels 2 and 3, a pace the band gives no
driver for long, and it would flatten the levels' accelerators to one value.

Registers: `cpu_refs` writes r12, cr0 and (its stock way, a call) LR, `cpu_leader` r3 and cr0 (its mode test: r3,
the driver's rank there, is written again at 0xC53FC before it is read; r12 is not proven dead); each is proven dead where the routine goes back to (build.py from the site's next word; here for the other way back, when the game is read)."""
import liveness
import memmap

REFS_SITE = 0xC46F4          # fn_1_C42F4: bl fn_1_8C3EC, the first of the four getters
REFS_GETTER = 0x8C3EC        # fn_1_8C3EC: the best reference's rank
REFS_PAST = 0xC4714          # after the fourth getter's store: bl fn_1_8DB94
LEADER_SITE, LEADER_KNOWN = 0xC53DC, 0x38800000   # fn_1_C4ABC: li r4, 0 (state 0)
LEADER_BACK = 0xC53E0        # b 0xC53FC: the state chosen is in r4
BEST, WORST = 0x49, 0x4A     # the drivers' globals (r29 in fn_1_C42F4: .bss 0x7AC48)
BEST_RANK, WORST_RANK = 0, 0xFF
STATE = 1
BAND = memmap.RACE + memmap.RACE_RUBBER_BAND_ON   # the race block's rubber band word: 1 on (stock), 0 off
assert memmap.lo(BAND) < 0x8000


def add(k, r, patch, stock, test, middle, complete):
    """Adds the two routines to `r` and their entries through `patch`, as sites.add does; `test` is the mode test's
    text."""
    TEXT, branch = k.TEXT, k.branch
    refs = TEXT + REFS_SITE
    word = stock(refs, branch(refs, TEXT + REFS_GETTER, True))
    assert k.branch_target(word, refs) == TEXT + REFS_GETTER
    r.add("cpu_refs", test + f"""    beq cpu_refs_stock
    lis 12, {memmap.hi(BAND)}
    lwz 12, {memmap.lo(BAND)}(12)
    cmpwi 12, 0
    bne cpu_refs_stock
    li 12, {BEST_RANK}
    stb 12, {BEST:#x}(29)
    li 12, {WORST_RANK}
    stb 12, {WORST:#x}(29)
    @b {TEXT + REFS_PAST:#x}
cpu_refs_stock:
    @bl {TEXT + REFS_GETTER:#x}
    @b {refs + 4:#x}
""")
    patch(refs, word, 0, "cpu_refs")
    middle.append((refs, ["r12", "cr0", "lr"], "cpu_refs"))

    leader = TEXT + LEADER_SITE
    word = stock(leader, LEADER_KNOWN)
    # the mode test in r3, not r12: r3 is written (lwz r3, 0(r23)) at 0xC53FC, where the way back goes, and r12 is
    # not proven dead there (the accelerator's jump table at 0xC64DC comes first)
    test3 = f"    lis 3, {k.C}\n    lwz 3, {k.MODE}(3)\n    cmpwi 3, 0\n"
    assert test == f"    lis 12, {k.C}\n    lwz 12, {k.MODE}(12)\n    cmpwi 12, 0\n", test
    r.add("cpu_leader", test3 + f"""    beq cpu_leader_stock
    lis 3, {memmap.hi(BAND)}
    lwz 3, {memmap.lo(BAND)}(3)
    cmpwi 3, 0
    bne cpu_leader_stock
    li 4, {STATE}
    @b {TEXT + LEADER_BACK:#x}
cpu_leader_stock:
    .long {word:#010x}
    @b {TEXT + LEADER_BACK:#x}
""")
    patch(leader, word, 0, "cpu_leader")
    middle.append((leader, ["r3", "cr0"], "cpu_leader"))

    if complete:                                 # the race's way back from cpu_refs is not the site's next word
        for resource in ("r12", "cr0", "lr"):
            proof = liveness.dead(stock, TEXT + REFS_PAST, resource)
            assert proof is True, f"cpu_refs: {resource} after {TEXT + REFS_PAST:08X}: {proof}"
