"""A late frame's drawing skipped (sites.py calls add() before its entry
table). Native form only: the fork's image does not have it. The host decides (port/core/src/present.cpp);
these routines ask it, and draw nothing when it says so.

A frame of MD_GAME draws in two places after its steps: the draw half of fn_1_37AA8, from its `bl fn_1_3CC4` at
0x37C18 to its `bl fn_1_C6F8C` at 0x37E44 (the draw manager once a viewport, the HUD, the REPLAY overlay, the
end-of-frame HUD), and the main loop's tail after the mode machine (_prolog 0x41C to 0x4C4: the ordered draw list,
the screen effects, the overlay pass, the sprites' draw, the debug text), then the main loop's depth read-back
(fn_1_9E1D0, `GXPeekZ`, 0x554). Every call of the two is drawing but these, which stay: the sound (fn_1_A0214), the
sprites' tick (fn_1_4DE44: their updates, not their draw), the face board's two hooks and the conditional ones
(fn_1_D3144, fn_1_D3178, fn_1_A6E68, fn_1_D0D88's debug read-back), the casino screens' hook (fn_1_FB870),
fn_1_B6B28, the copy's clear colour (fn_80007C60), the timers (fn_8006FFDC) and the GPU's draw-done wait
(fn_8006FDEC); and the machine sound update (fn_1_727C), which the draw half's view set-up (fn_1_5948) calls last
and which a skipped frame calls by itself.

`present_draw` (at 0x37C18): in a race or a replay (the mode word), stores 1 into memmap.DRAW_ASK (a call to the
host, which answers in memmap.DRAW_SKIP before the next load), and with DRAW_SKIP set calls fn_1_727C and goes to
fn_1_37AA8's epilogue (0x37E48, which restores everything from the stack); else the stock call, or the high refresh
rate's routine at the same site (sites_hfr.py `hfr`, which makes it): a frame that draws nothing tells that one
nothing, and the host never skips while pictures between are drawn (present.cpp). The tail's calls
(`present_*`): with DRAW_SKIP set, branch past the call or the run of calls; else the stock call. `present_depth`
(0x554): the read-back unless skipping, then DRAW_ASK = 2 (the host times the drawing up to here). `present_overlay`
(0x490, the overlay pass: the game's course map) never calls it in a race, whether the frame draws or not (MAP_PASS).
`present_copy` (the executable's frame begin, 0x8006FDA4 `bl fn_80035110`, GXCopyDisp with clear): with DRAW_SKIP still set (the
host clears it at the next frame tick, after this frame begin) no copy: the frame before drew nothing, so the
picture in the display buffer, the one before it, is shown again, and the embedded frame buffer, cleared by the
copy before, is still clear for this frame's drawing.

Every routine uses r11, r12 and CR field 0 only, each dead after its site (liveness.py proves it at the site; a run
of calls skipped lands on a call or a `lbz`/`cmplwi` that writes them again before reading). No step reads either
word, no rollback puts them back."""
import memmap

# (site in main.rel .text, the call's target, where a skip goes on, name)
TAIL = [(0x41C, 0x545B8, 0x420, "present_ord"),        # DrawOrdSys: the ordered draw list
        (0x43C, 0x76218, 0x44C, "present_screen"),     # the screen effects: fn_1_76218(0), fn_1_728B0, fn_1_76218(1)
        (0x490, 0x79654, 0x494, "present_overlay"),    # the per-viewport overlay pass
        (0x4A4, 0x4DEC0, 0x4A8, "present_sprites"),    # the sprites' draw
        (0x4A8, 0x4FD64, 0x4B4, "present_sorted"),     # the sorted sprite draw, fn_8006CD50, fn_8006CDA8
        (0x4C0, 0xA9250, 0x4C8, "present_text")]       # the debug text fn_1_A9250(0), the sorted sprite draw again
# The overlay pass draws one thing: the game's course map (fn_1_7926C, its only caller; fn_1_79654 does nothing
# else when it does not draw it), which our own course map replaces (port/core/src/minimap_frame.cpp). In a race it is
# never called, in any phase (the course intro, the countdown, the race, the results), in a replay, a practice, a
# spectator's game, and in every frame, also one a rollback or the scrubber ran again: the map is not held off by
# the HUD part mask's bit (.data 0x3D544[viewport] 0x00800000) alone any more: that is a word of tracked memory, which
# a restore puts back and the image's HUD passes rewrite after the steps, and the core clears it once a frame, before
# the steps, for viewport 0 only.
MAP_PASS = 0x490
DRAW = (0x37C18, 0x3CC4, 0x37C1C, 0x37E48)               # the draw half: its first call, the epilogue
SOUND = 0x727C                                           # the machine sound update
DEPTH = (0x554, 0x9E1D0)                                 # the main loop's depth read-back
COPY = (0x8006FDA4, 0x80035110)                          # frame begin's copy to the display buffer (with clear)


def add(k, r, patch, stock, middle, draw_next=None):
    """Adds the routines to `r` and an entry for each site through `patch`, as sites.add does. `draw_next`: a
    routine of another site at 0x37C18 that a frame that draws goes on to in place of the stock call (sites_hfr.py's
    `hfr`, which makes the call itself and comes back after the site)."""
    TEXT, C, branch = k.TEXT, k.C, k.branch
    MODE, ASK, SKIP = (memmap.MODE - memmap.CONTROL, memmap.DRAW_ASK - memmap.CONTROL,
                       memmap.DRAW_SKIP - memmap.CONTROL)

    def head(name, ask=None):
        """r12 the control block; to `name`_stock in a plain start; DRAW_ASK stored; cr0 eq when not skipping."""
        store = f"    li 11, {ask}\n    stw 11, {ASK}(12)\n" if ask else ""
        return f"""    lis 12, {C}
    lwz 11, {MODE}(12)
    cmpwi 11, 0
    beq {name}_stock
{store}    lwz 11, {SKIP}(12)
    cmpwi 11, 0
    beq {name}_stock
"""

    def site(at, target, name, text):
        word = stock(at, branch(at, target, True))
        r.add(name, text)
        patch(at, word, 0, name)
        middle.append((at, ["r11", "r12", "cr0"], name))

    at, target, back, epilogue = (TEXT + x for x in DRAW)
    drawn = f"    @b {draw_next}\n" if draw_next else f"    @bl {target:#x}\n    @b {back:#x}\n"
    site(at, target, "present_draw", head("present_draw", 1) + f"""    @bl {TEXT + SOUND:#x}
    @b {epilogue:#x}
present_draw_stock:
""" + drawn)
    for offset, call, resume, name in TAIL:
        at = TEXT + offset
        # the overlay pass: in a race never, drawing or skipping (MAP_PASS)
        skip = f"    lis 12, {C}\n    lwz 11, {MODE}(12)\n    cmpwi 11, 0\n    beq {name}_stock\n" \
            if offset == MAP_PASS else head(name)
        site(at, TEXT + call, name, skip + f"""    @b {TEXT + resume:#x}
{name}_stock:
    @bl {TEXT + call:#x}
    @b {at + 4:#x}
""")
    at, target = TEXT + DEPTH[0], TEXT + DEPTH[1]
    site(at, target, "present_depth", f"""    lis 12, {C}
    lwz 11, {MODE}(12)
    cmpwi 11, 0
    beq present_depth_stock
    lwz 11, {SKIP}(12)
    cmpwi 11, 0
    bne present_depth_asked
    @bl {target:#x}
    lis 12, {C}
present_depth_asked:
    li 11, 2
    stw 11, {ASK}(12)
    @b {at + 4:#x}
present_depth_stock:
    @bl {target:#x}
    @b {at + 4:#x}
""")
    at, target = COPY
    site(at, target, "present_copy", head("present_copy") + f"""    @b {at + 4:#x}
present_copy_stock:
    @bl {target:#x}
    @b {at + 4:#x}
""")
