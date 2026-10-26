"""The cameras' own words of the patch list (sites.py calls add() before its entry table): the index of the routines
that read a camera record for a machine's viewport slot. Native form only: the fork's image does not have it.

The TV cameras' task (fn_1_EE44) is the game's own: it draws from the executable's generator (0x801A63C0) and the
effects' (.data 0x1D628), as each game's drawing does. A routine that gave the task words of its own in a race was
not kept: two racing games' words differ step by step
already, so it made no game's words another's, and no step of a race reads what the words give."""


def add(k, r, patch, stock, test, middle):
    """Adds the routines to `r` and an entry for each site through `patch`, as sites.add does; `test` is the mode
    test's text."""
    TEXT, CAMERA_INDEXES, CAMERA_INDEX_WORD = k.TEXT, k.CAMERA_INDEXES, k.CAMERA_INDEX_WORD
    # --- the cameras' records are indexed inside them. The game keeps four camera records (0x1FC bytes each, at
    # [.bss 0xF6C]) and finds a machine's by its viewport slot (block +0x475), which is -1 (0xFF) for a machine no
    # screen follows: every machine but seat 0's in a race of ours, as every CPU machine in the stock game. The two
    # routines that write a record by that slot (fn_1_713C, fn_1_715C) are called from the chase camera only, with
    # the slot sites.py's two words give (`camera_aa84`, `camera_ac08`: 0). Three routines that read one are called by the effects' and the cars'
    # drawing with a machine's own slot, some without asking whether it has one: fn_1_725C (the view, +0xA4),
    # fn_1_6EC0 (the state, +0xA8), fn_1_681C (the position, +0x10): for slot 0xFF they read 0x1FA04 bytes past the
    # records' start, whatever the heap holds there, and what is drawn follows from it. In a race or a replay the
    # index is 0 whenever it is not one of the four: the one screen's camera, which is the local racer's.
    for offset in CAMERA_INDEXES:
        site, name = TEXT + offset, f"camera_index_{offset:x}"
        word = stock(site, CAMERA_INDEX_WORD)
        r.add(name, test + f"""    .long {word:#010x}
    beq {name}_back
    cmplwi 0, 4
    blt {name}_back
    li 0, 0
{name}_back:
    @b {site + 4:#x}
""")
        patch(site, word, 0, name)
        middle.append((site, ["r12", "cr0"], name))
