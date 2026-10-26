"""True widescreen (sites.py calls add() before its entry table): the race view at the game's Wide Screen without
the game's cut of the vertical view. Native form only: the fork's image does not have it. For the picture only.

What the game does in Wide Screen (the screen-option word, main.rel .bss 0x6F5F0, is 1 or 2; fn_1_A5DC4 asks it):
its aspect getter fn_1_A6FE8 gives 16/9 (rodata 0x49F8) where Normal gives 4/3, and the race's viewport set-up
fn_1_5948, with one screen (.bss 0xF75 = 1, every race of ours), multiplies the camera's fovy by 0.8660254 (rodata
0x1D8, at 0x61F4) before it sets the projection (fn_1_A7024 at 0x621C). So its 16:9 keeps the horizontal view a
little wider than Normal's (76.2 against 69.5 degrees at the base fovy of 55) and cuts the vertical (47.6 against
55). True widescreen leaves the multiply out: the whole vertical view of Normal, the sides widened (85.6 degrees).
The other race views already project so: the early branch of fn_1_5948 (0x5A00-0x5A40) and the fixed cameras'
FixCameraExec (fn_1_857C, fn_1_DCE60, fn_1_DCED0) take the aspect getter's 16/9 with the camera's own fovy; the TV
cameras (fn_1_5370) set their record's aspect from the getter and are projected by fn_1_5948, through this site.

The culling is the same in both: the frustum the game culls with (the planes at camera record +0x15C) is built by
the cameras' tasks (GameCameraExec fn_1_90E0 at 0xA840, LiveCameraExec fn_1_EE44 at 0xF3A4, both fn_1_549F4)
from the record's own fovy and aspect, never multiplied; fn_1_5948 copies it in (fn_1_54E00, 0x614C) before the
multiply. What changes is the projection alone: the GX matrix (the executable's 0x8019E250) and the projection
words at .bss 0x6F608 (fovy, aspect, near, far, tan(fovy/2), its inverse), which only drawing reads (the HUD's
text, the rank markers, the name tags, the machines' level of detail, the course's and the objects' draws).

The control word TRUE_ASPECT (memmap.py; the host's, from the launch file) is 0 for the game's own Wide Screen, or a
float's bits: true widescreen at that aspect. Two sites:
- fn_1_5948 0x61F4 `fmuls f25, f25, f0` (the 0.8660254): left out while the word is not 0. The routine uses r12
  and CR field 0, dead after the site (0x61F8 goes to 0x6208, which loads r3 and calls fn_1_A7024).
- fn_1_A6FE8 0xA7008 `b 0xA7014` (the Wide Screen way, after f1 = 16/9): f1 is the word's aspect while it is not 0,
  so the aspect is a parameter of the race view and not the game's 16:9 constant. The launcher writes 16/9, the
  getter's own value to the bit, so at 16:9 nothing that reads the aspect sees another number. The routine uses
  r0, r12 and CR field 0, dead after the site (0xA7014 loads r0 and returns).
In a plain start (mode word 0) both do what the game does, whatever the word holds."""
import memmap

VIEW_SITE = (0x61F4, 0xEF390032)       # fn_1_5948: `fmuls f25, f25, f0`, fovy times 0.8660254
ASPECT_SITE = (0xA7008, 0x4800000C)    # fn_1_A6FE8: `b 0xA7014`, after the Wide Screen aspect's load
ASPECT_RETURN = 0xA7014
TRUE_ASPECT = memmap.TRUE_ASPECT - memmap.CONTROL


def add(k, r, patch, stock, test, middle, complete):
    """Adds the routines to `r` and an entry for each site through `patch`, as sites.add does; `test` is the mode
    test's text."""
    TEXT, C = k.TEXT, k.C
    site = TEXT + VIEW_SITE[0]
    word = stock(site, VIEW_SITE[1])
    r.add("true_view", test + f"""    beq true_view_stock
    lis 12, {C}
    lwz 12, {TRUE_ASPECT}(12)
    cmpwi 12, 0
    bne true_view_back
true_view_stock:
    .long {word:#010x}
true_view_back:
    @b {site + 4:#x}
""")
    patch(site, word, 0, "true_view")
    middle.append((site, ["r12", "cr0"], "true_view"))

    site = TEXT + ASPECT_SITE[0]
    word = stock(site, ASPECT_SITE[1])
    if complete:
        assert k.branch_target(word, site) == TEXT + ASPECT_RETURN
    r.add("true_aspect", test + f"""    beq true_aspect_back
    lis 12, {C}
    lwz 0, {TRUE_ASPECT}(12)
    cmpwi 0, 0
    beq true_aspect_back
    lfs 1, {TRUE_ASPECT}(12)
true_aspect_back:
    @b {TEXT + ASPECT_RETURN:#x}
""")
    patch(site, word, 0, "true_aspect")
    middle.append((site, ["r0", "r12", "cr0"], "true_aspect"))
