"""The start countdown's movie request:
one word of the patch list. Native form only: the fork's image does not have it. sites.py calls add() last but the
entry table.

The game's countdown task `Starter` (fn_1_1049E0, a simulation task) posts, every step while the start's movie
streams, an ARAM-to-memory copy of a movie frame (`bl ARQPostRequest` at 0x1050B4, the executable's 0x800205A0) with
one request record of main.rel's .bss (0x85814, 0x20 bytes, untracked.py MOVIE). The ARQ library's queue (its heads,
tails and pending requests, the executable's .sbss 0x801A6A00 to 0x801A6A28) lies below the undo log and the copy in
flight is the emulator's, so a rollback put the record back and not the queue: a step run again posted the record
while the queue's tail still was that record, the record's `next` became the record itself, and every completion
started the same copy again (173,107 copies in crash-202550 forced 1x6 against 420), into a heap 1 buffer the
countdown's end frees: the copies then wrote over heap 1's new cells (heap 1 broke at step 431, a kind 25 effect
got no buffer and drew from a null pointer).

| Site | Where | What the game did | Now, in a race or replay of ours |
|---|---|---|---|
| `starter_arq` | fn_1_1049E0 0x1050B4 | `bl ARQPostRequest` | the same call, but not in a step simulated before (control block +488, as mute.py's sounds and rumble) |

So the record is posted once a step, the first time the step runs, and it is never put back by a restore: the queue
and the record always agree. A step run again shows the movie frame the first run asked for (presentation only: no
step reads the movie). Putting the queue back with the record instead was not taken: the copy in flight and its
completion interrupt are the emulator's state, which a restore cannot carry."""
import memmap

SITE = 0x1050B4                 # fn_1_1049E0's `bl ARQPostRequest`
ARQ_POST = 0x800205A0           # ARQPostRequest (the executable)
SIMULATED_BEFORE = 488          # control block +488: the step about to run was simulated before (netframe.py)


def add(k, r, patch, stock, test, middle, complete):
    """Adds `starter_arq` to `r` (build.Routines) and its entry through `patch`; `stock` reads the game's word."""
    site = k.TEXT + SITE
    word = stock(site, k.branch(site, ARQ_POST, True))
    if complete:
        assert k.branch_target(word, site) == ARQ_POST
    r.add("starter_arq", test + f"""    beq starter_arq_post
    lis 12, {memmap.hi(memmap.CONTROL)}
    lwz 12, {SIMULATED_BEFORE}(12)
    cmpwi 12, 0
    bne starter_arq_back
starter_arq_post:
    @bl {ARQ_POST:#x}
starter_arq_back:
    @b {site + 4:#x}
""")
    patch(site, word, 0, "starter_arq")
    middle.append((site, ["r12", "cr0"], "starter_arq"))
