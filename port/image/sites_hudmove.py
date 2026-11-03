"""The classic HUD's moves (port/core/src/hud_move.cpp): each part of the game's own race HUD moved, turned and
mirrored as one piece, from a layout the host reads. The drawing only. Native form only. sites.py calls add().

Every HUD picture and text glyph is a 0x58-byte packet that fn_1_4F734 copies into the frame's queue (.data 0x1C504,
the count .bss 0x646D2); fn_1_4FD64 sorts the queue and draws it later in the frame. The sites tell the host which
part is drawing while packets are queued, and the host, as the queue is drawn, rewrites the queued packets of each
part it has a move for: their position about the part's pivot, the packet's own turn (+0x28) and, for a mirror, its
anchor side (+0x30's low bits) and texture corners (+0x18/+0x20, +0x1C/+0x24). Nothing a step reads: the queue is
the drawing's, written by the drawing in the same frame (classic-hud-draw notes).

| Site | Where | What |
|---|---|---|
| `hm_*` (wraps) | each part's `bl` in fn_1_C72D4 (one viewport), fn_1_C771C (its other layout), fn_1_C6F8C (faces board, timer), fn_1_CDC8C (its timer), fn_1_15C36C (banners, FINISH/RETIRED) | the part's call between the host's PUSH and POP |
| `hs_*` (story blocks) | each mask test's `bl fn_1_5910` in fn_1_CDC8C and fn_1_CEB38, where one story part's block starts | the host's SET: the packets from here on are that part's (a block of the story's own, no mode part: STORY_OTHER) |
| `hm_sprite` | fn_1_4DEC0 0x4DF24, a pool sprite's packet queued (`bl fn_1_4F734`, r30 the sprite) | between SPRITE (the host names the part by the sprite's step callback: the boost lightning, wrong way) and POP |
| `hm_flush` | fn_1_4FD64's entry, the queue drawn (`stwu r1, -0x60(r1)`) | the host's FLUSH: every queued packet of a moved part rewritten, the parts' record cleared |

With the window's word MODE_HUD_MOVE 0 (no layout, every start but a test's) each routine is the game's instruction
alone after one load and test: no host call. The host calls change no register; each routine keeps r3 and r4 around
them, so the part's call and the game's code after a site see what they would."""
import memmap

PUSH, POP, SET, SPRITE, FLUSH = 1, 2, 3, 4, 5
# The parts, by number (hud_move.cpp PARTS): the protocol's `part` choices first, in its order, then the rest of the HUD
PARTS = ["NONE", "TIMER", "DISTANCE", "DIGIT", "WARNING", "SUCCESS", "FAILED", "TIME_OVER", "SPEED_GAUGE", "OBJECTS",
         "LEADER_GAP", "GHOST_GAP", "RANK", "LAPS", "SPEED", "ENERGY", "LAP_TIMES", "RACE_TIME", "FACES",
         "SPARE", "SKULLS", "BOOST", "WRONG_WAY", "BANNERS", "FINISH", "STORY_OTHER"]
P = {name: i for i, name in enumerate(PARTS)}

# (site, the called function, part): main.rel .text offsets
WRAPS = [
    (0xC7330, 0xCA8FC, "SPARE"), (0xC7350, 0xCA2A4, "RANK"), (0xC735C, 0xCDC8C, "STORY_OTHER"),
    (0xC7360, 0xCEB38, "STORY_OTHER"), (0xC7384, 0xC8DC0, "ENERGY"), (0xC738C, 0xCADC4, "SPEED"),
    (0xC73A8, 0xCC280, "LAPS"), (0xC73D8, 0xCBC24, "RACE_TIME"), (0xC73EC, 0xCB424, "LAP_TIMES"),
    (0xC73F8, 0xCB028, "LAP_TIMES"), (0xC7408, 0xCAB38, "SKULLS"),
    (0xC7780, 0xCA690, "SPARE"), (0xC77A8, 0xCD7BC, "STORY_OTHER"), (0xC77CC, 0xC8DC0, "ENERGY"),
    (0xC77D4, 0xCADC4, "SPEED"), (0xC77F0, 0xCC280, "LAPS"), (0xC7820, 0xCBC24, "RACE_TIME"),
    (0xC7834, 0xCB424, "LAP_TIMES"), (0xC7840, 0xCB028, "LAP_TIMES"), (0xC7850, 0xCAB38, "SKULLS"),
    (0xC7144, 0xC8294, "FACES"), (0xC7184, 0xC8294, "FACES"), (0xC7204, 0xCF5B0, "TIMER"),
    (0xCDCEC, 0xCF5B0, "TIMER"),
    (0x15C458, 0x15EFEC, "BANNERS"), (0x15C47C, 0x15DFD4, "FINISH"),
]
# (site, part): a story block's `bl fn_1_5910`, before its test of the part mask (the bit in the comment)
VIEWPORT = 0x5910
STORY_BLOCKS = [
    (0xCDCF0, "OBJECTS"), (0xCE170, "SPEED_GAUGE"), (0xCE818, "DISTANCE"),                 # fn_1_CDC8C: 0x4000 0x2000 0x1000
    (0xCEB8C, "STORY_OTHER"), (0xCED28, "WARNING"), (0xCED90, "DIGIT"), (0xCEDCC, "TIME_OVER"),  # fn_1_CEB38: 0x800 0x400 0x8 0x80
    (0xCEE44, "STORY_OTHER"), (0xCEE88, "STORY_OTHER"), (0xCEECC, "DIGIT"), (0xCF048, "SUCCESS"),  # 0x40 0x20 0x8 0x200
    (0xCF2C0, "FAILED"), (0xCF40C, "STORY_OTHER"), (0xCF4E8, "STORY_OTHER"),                   # 0x100 0x2 0x10
]
SPRITE_SITE, QUEUE = 0x4DF24, 0x4F734           # fn_1_4DEC0: bl fn_1_4F734 (r30 the sprite)
FLUSH_SITE, FLUSH_KNOWN = 0x4FD64, 0x9421FFA0   # fn_1_4FD64: stwu r1, -0x60(r1)


def add(k, r, patch, stock, la, middle):
    """Adds the routines to `r` and the sites' entries through `patch`, as sites.add does."""
    TEXT = k.TEXT
    on = la(12, memmap.MODE_HUD_MOVE) + "    lwz 12, 0(12)\n    cmpwi 12, 0\n"

    def call(op, arg):
        """A host call that changes no register the game sees (r3 and r4 kept on a frame of its own below r1)."""
        return (f"    stwu 1, -0x20(1)\n    stw 3, 0x8(1)\n    stw 4, 0xC(1)\n    li 3, {op}\n    {arg}\n"
                f"    @bl host_hud\n    lwz 3, 0x8(1)\n    lwz 4, 0xC(1)\n    addi 1, 1, 0x20\n")

    for n, (at, target, part) in enumerate(WRAPS):
        site, fn = TEXT + at, TEXT + target
        word = stock(site, k.branch(site, fn, True))
        assert k.branch_target(word, site) == fn
        name = f"hm_{at:x}"
        r.add(name, on + f"""    beq {name}_stock
""" + call(PUSH, f"li 4, {P[part]}") + f"""    @bl {fn:#x}
""" + call(POP, f"li 4, {P[part]}") + f"""    @b {site + 4:#x}
{name}_stock:
    @bl {fn:#x}
    @b {site + 4:#x}
""")
        patch(site, word, 0, name)
        middle.append((site, ["r12", "cr0", "lr"], name))

    for at, part in STORY_BLOCKS:
        site, fn = TEXT + at, TEXT + VIEWPORT
        word = stock(site, k.branch(site, fn, True))
        name = f"hs_{at:x}"
        r.add(name, on + f"""    beq {name}_stock
""" + call(SET, f"li 4, {P[part]}") + f"""{name}_stock:
    @bl {fn:#x}
    @b {site + 4:#x}
""")
        patch(site, word, 0, name)
        middle.append((site, ["r12", "cr0", "lr"], name))

    site, fn = TEXT + SPRITE_SITE, TEXT + QUEUE
    word = stock(site, k.branch(site, fn, True))
    r.add("hm_sprite", on + """    beq hm_sprite_stock
""" + call(SPRITE, "mr 4, 30") + f"""    @bl {fn:#x}
""" + call(POP, "li 4, 0") + f"""    @b {site + 4:#x}
hm_sprite_stock:
    @bl {fn:#x}
    @b {site + 4:#x}
""")
    patch(site, word, 0, "hm_sprite")
    middle.append((site, ["r12", "cr0", "lr"], "hm_sprite"))

    # the queue's drawing: at the function's entry the link register is the caller's, so it is kept too
    site = TEXT + FLUSH_SITE
    word = stock(site, FLUSH_KNOWN)
    r.add("hm_flush", on + """    beq hm_flush_stock
    stwu 1, -0x20(1)
    stw 3, 0x8(1)
    stw 4, 0xC(1)
    mflr 3
    stw 3, 0x10(1)
""" + f"""    li 3, {FLUSH}
    li 4, 0
    @bl host_hud
    lwz 3, 0x10(1)
    mtlr 3
    lwz 3, 0x8(1)
    lwz 4, 0xC(1)
    addi 1, 1, 0x20
hm_flush_stock:
    stwu 1, -0x60(1)
    @b {site + 4:#x}
""")
    patch(site, word, 0, "hm_flush")
    middle.append((site, ["r12", "cr0"], "hm_flush"))


def ghost_gap(la, which):
    """The host's PUSH (`which` 1) or POP (2) of GHOST_GAP for sites_ghostdraw.py's text, where r3, r4 and r12 are
    free: after the window's test, as the sites above."""
    label = f"gg_mv{which}"
    return la(12, memmap.MODE_HUD_MOVE) + f"""    lwz 12, 0(12)
    cmpwi 12, 0
    beq {label}
    li 3, {PUSH if which == 1 else POP}
    li 4, {P["GHOST_GAP"]}
    @bl host_hud
{label}:
"""
