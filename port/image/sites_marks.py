"""The draw marks: which machine's body or burner the next draws are, told to the host at the game's own draw sites so that
Aurora knows each draw's object by the game itself, not by a rule on its state (port/moderngekko/src/marks.cpp). The
drawing only: no guest register or memory changes, and the game's command stream is not touched (the emulator puts the mark
into its own copy of the stream for Aurora). Native form only. sites.py calls add().

| Site | Where | What |
|---|---|---|
| `mark_body` | CarDisp fn_1_84860's first instruction, 0x84860 (`stwu r1, -0xa0(r1)`; r3 the car object: +0x32C its machine's block) | BODY: what this task draws is that machine's (body, pilot, parts; the drop shadow is only queued here) |
| `mark_body_end` | CarDisp's one `blr`, 0x850C0 (every return goes through the common epilogue) | END |
| `mark_deferred` | the deferred-draw walker fn_1_547F8's `bctrl`, 0x54824 (DrawOrdSys fn_1_545B8's list; r3 the node, the callback in r12 and CTR) | for the burner's draw fn_1_98F28 (node +8 its Burner record, whose +0x48 is its machine's block): BURNER, the call, END. Any other callback: the call alone |

The host calls change no register (window.cpp); the routines keep what the game's code reads. Every start calls them, a plain
start too (offline play draws with Aurora as well): the host does nothing when Aurora does not draw. No mode test: the stock
game's state is the same with them (port/image/tests/test_image.py lists them as untested)."""

BODY, BURNER, END = 1, 2, 3
BODY_SITE, BODY_WORD = 0x84860, 0x9421FF60      # fn_1_84860: stwu r1, -0xa0(r1)
BODY_END, BLR = 0x850C0, 0x4E800020             # fn_1_84860's blr
WALK, BCTRL = 0x54824, 0x4E800421               # fn_1_547F8: bctrl (r3 the node)
BURNER_DRAW = 0x98F28                           # fn_1_98F28: the burner's draw callback


def add(k, r, patch, stock, middle):
    """Adds the three routines to `r` and their entries through `patch`, as sites.add does; `middle`: the sites in the middle
    of a function and what must be dead after them."""
    TEXT, HOST = k.TEXT, "host_mark"
    site = TEXT + BODY_SITE
    word = stock(site, BODY_WORD)
    # at the function's entry: r0, r11, r12 and cr0 carry nothing in (the caller's bctrl); r3 the object, r4 kept
    r.add("mark_body", f"""    mflr 0
    mr 11, 3
    mr 12, 4
    mr 4, 3
    li 3, {BODY}
    @bl {HOST}
    mr 3, 11
    mr 4, 12
    mtlr 0
    .long {word:#010x}
    @b {site + 4:#x}
""")
    patch(site, word, 0, "mark_body")
    middle.append((site, ["r0", "r11", "r12"], "mark_body"))

    site = TEXT + BODY_END
    word = stock(site, BLR)
    # at the return: r11 and r12 carry nothing out; r3 and LR kept; the routine returns in the site's place
    r.add("mark_body_end", f"""    mflr 11
    mr 12, 3
    li 3, {END}
    @bl {HOST}
    mr 3, 12
    mtlr 11
    blr
""")
    patch(site, word, 0, "mark_body_end")   # (not a middle site: a return, after which the EABI keeps nothing in r11, r12, cr0)

    site = TEXT + WALK
    word = stock(site, BCTRL)
    burner = TEXT + BURNER_DRAW
    # the callback is in CTR (r12 too); after the call r3 and r4 are the callback's, as in the stock game
    r.add("mark_deferred", f"""    mfctr 12
    lis 11, {burner >> 16:#x}
    ori 11, 11, {burner & 0xFFFF:#x}
    cmplw 12, 11
    beq md_burner
    bctrl
    @b {site + 4:#x}
md_burner:
    mr 4, 3
    li 3, {BURNER}
    @bl {HOST}
    mr 3, 4
    bctrl
    mr 11, 3
    li 3, {END}
    @bl {HOST}
    mr 3, 11
    @b {site + 4:#x}
""")
    patch(site, word, 0, "mark_deferred")
    middle.append((site, ["r11", "r12", "cr0", "lr"], "mark_deferred"))
