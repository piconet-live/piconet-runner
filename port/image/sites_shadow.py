"""A rival's shadow follows its drawn body (sites.py calls add() before its entry table). Native form only: the
fork's image does not have it.

The game places a machine's shadow in the shadows' task (CarShadowCalc, fn_1_56D4C: task list 22, one a machine,
its argument the shadow's record, record +0x48 the machine). It builds the record's matrix (+0xC, three rows of
four) from the machine's up vector (+0x1BC, the road's normal under it) and, for the drawn-body kind, the drawn
matrix's turn, and in both of its kinds the matrix's place (+0x18, +0x28, +0x38) is the machine's contact point on
the road (+0x1D4): the point of the road under the machine, also while it is in the air. The shadow's picture
(CarShadowPreRend, fn_1_568F8) is the body drawn from along the up vector with the drawn matrix's turn only
(the executable's 0x8006DC6C copies no place). So the shadow lies where the machine really is, and a rival that
smooth.py draws beside that after a rollback is parted from its shadow by the offset.

The site is fn_1_56D4C's 0x57174, where its two kinds meet with the matrix written (r29 the record, r30 the
machine). For a machine drawn with an offset in this frame (its drawn transform, sites_drawn.py `drawn_machine`;
never this game's own machine, never outside a race of ours) the offset's part along the road is added to the
record's place: the offset less its part along the up vector. A machine on the road has no such part left
(smooth.py), so its shadow moves exactly as its body; a machine in the air keeps its whole offset, and its shadow
stays on the road, under the drawn body. Only the record is written: memory of the drawing, which the shadows' task writes again whenever
it runs. The contact point, the up vector and the drawn matrix are read; no word of a machine's block is written
(the contact point is in the checksum).

What the routine uses beyond the replaced instruction's own f0, and why the game does not miss it (the image
builder proves the integer registers, r0, r5 to r12, CR field 0 and CTR, dead after the site: liveness.py; it
knows no float registers, so this part is by reading fn_1_56D4C from the site to its return): the site is
0x57174 `lfs f0, 0x8c(r31)`, which the routine runs last; then 0x57178 `lwz r0, 0(r29)` writes r0 and 0x5717C
`fmuls f1, f0, f31` writes f1, each before anything reads it; 0x57180 to 0x571C0 read and write only f0, f1, r0
and the three registers the function saved; from 0x571C4 it restores f31, r29 to r31 and returns to the task
manager, which holds nothing in a float register over a task's call. So the routine uses f0 and f1 freely and
keeps f2 to f5 on its own stack; the image's test of the site holds every float register but f0 and f1 to its
value.

Tests' log (only in a game told to keep the logs, memmap.IMAGE_LOGS: a players' game leaves the routine right
after the shadow's place is written): the square of the distance along the road between the drawn body and the shadow, less what the game
itself has between them ((drawn place - record's place) - (race's place - contact point), without its part along
the up vector), the frame's largest in smooth.py's globals +56 and the upper half of that float in the frame's
log entry (+6). Control block +456 bit 4 (tests): the record is left as the game wrote it, and the log then says
how far the shadows were parted before."""
import memmap
import smooth


def add(k, r, patch, stock, test, la, middle):
    """Adds the routine to `r` and its site's entry through `patch`, as sites.add does; `test` is the mode test's
    text, `la` loads an address."""
    site, word = k.TEXT + k.SHADOW_PLACED[0], stock(k.TEXT + k.SHADOW_PLACED[0], k.SHADOW_PLACED[1])
    place = lambda i: 0x18 + 0x10 * i                    # the record's place, a row each
    along = "".join(f"""    lfs 1, {12 + 4 * i}(10)
    fnmsubs 1, 0, {3 + i}, 1
    lfs 2, {place(i)}(29)
    fadds 2, 2, 1
    stfs 2, {place(i)}(29)
""" for i in range(3))
    parted = "".join(f"""    lfs 0, {0x158 + 0x10 * i}(30)
    lfs 1, {place(i)}(29)
    fsubs 0, 0, 1
    lfs 1, {4 * i}(10)
    fsubs 0, 0, 1
    lfs 1, {0x1D4 + 4 * i}(30)
    fadds 0, 0, 1
    stfs 0, {40 + 4 * i}(1)
""" for i in range(3))
    r.add("shadow_follow", test + f"""    beq sf_out
    mflr 12
    mr 9, 30
    bl drawn_machine
    mtlr 12
    cmpwi 10, 0
    beq sf_out
    stwu 1, -64(1)
    stfd 2, 8(1)
    stfd 3, 16(1)
    stfd 4, 24(1)
    stfd 5, 32(1)
    lfs 3, 0x{memmap.MACHINE_UP_X:X}(30)
    lfs 4, 0x{memmap.MACHINE_UP_X + 4:X}(30)
    lfs 5, 0x{memmap.MACHINE_UP_X + 8:X}(30)
    lis 12, {k.C}
    lwz 0, 456(12)
    andi. 0, 0, 4
    bne sf_parted
    lfs 0, 12(10)
    fmuls 0, 0, 3
    lfs 1, 16(10)
    fmadds 0, 1, 4, 0
    lfs 1, 20(10)
    fmadds 0, 1, 5, 0
""" + along + f"""sf_parted:
    lis 12, {k.C}
    lwz 0, {memmap.IMAGE_LOGS - memmap.CONTROL}(12)
    cmpwi 0, 0
    beq sf_done
""" + parted + f"""    lfs 0, 40(1)
    fmuls 2, 0, 0
    fmuls 0, 0, 3
    lfs 1, 44(1)
    fmadds 2, 1, 1, 2
    fmadds 0, 1, 4, 0
    lfs 1, 48(1)
    fmadds 2, 1, 1, 2
    fmadds 0, 1, 5, 0
    fnmsubs 2, 0, 0, 2
""" + la(11, memmap.SMOOTH + 0x600) + f"""    lfs 1, 56(11)
    fcmpu 0, 2, 1
    ble sf_done
    stfs 2, 56(11)
    lwz 9, 8(11)
    andi. 9, 9, {memmap.SMOOTH_LOG_FRAMES - 1}
    slwi 9, 9, 3
""" + la(12, memmap.SMOOTH_LOG) + f"""    add 12, 12, 9
    lhz 0, 56(11)
    sth 0, 6(12)
sf_done:
    lfd 2, 8(1)
    lfd 3, 16(1)
    lfd 4, 24(1)
    lfd 5, 32(1)
    addi 1, 1, 64
sf_out:
    .long {word:#010x}
    @b {site + 4:#x}
""")
    assert smooth.FRAMES == 0x600 + 8                    # the frames' count the log's place is taken from
    patch(site, word, 0, "shadow_follow")
    middle.append((site, ["r0", "r5", "r6", "r7", "r8", "r9", "r10", "r11", "r12", "cr0", "ctr"], "shadow_follow"))
