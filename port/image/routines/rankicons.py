"""The rank icons (1st, 2nd, 3rd above the machines in those places), for follow.py's installer: the helpers of
fn_1_12999C's (rank_icon_draw) two reads of a machine's port, native form only.

The stock test: the machine's port (+0x475, r30 the machine) is read at 0x129AA8 (-1: drawn, nothing more asked) and
again at 0x129AC0, compared with fn_1_5910's answer (the viewport's own port, 0 in a race of ours): equal, no icon.
follow.py's `f30_0` made both reads 0 for the local seat ('FZSI' +4) and -1 for the others, so the local racer saw no
icon above itself. Here both give 0 for the racer the HUD follows (fn_1_6B48(0)), and for the local seat when the HUD
follows none: a spectator, a finished or out player watching another racer, and a replay's viewer see no icon above
the racer followed, and it moves with the camera's racer (the same test that hides the local racer's own icon while
racing). While racing, the HUD follows the local seat (netframe.py points the chase
camera at it every frame), so nothing changes there. The fork's image keeps `f30_0`.

Tests: in a game told to keep the logs (memmap.IMAGE_LOGS) the first read counts each racer's drawn frames at
memmap.ICONS: held back (+0, a word a racer) or let through to the icon's own tests (+120). Picture only: the icons
are drawn, no step reads the counters."""
import memmap

H = memmap.hi(memmap.IMAGE + 0x14)          # the slot 'FZSI' +4: the local seat
C = memmap.hi(memmap.CONTROL)


SITES = (0x129AA8, 0x129AC0)   # fn_1_12999C's two reads of the machine's port (r30 the machine)


def followed(p):
    """r12 = the racer the HUD follows, fn_1_6B48(0) read here as it reads it: the camera block [.bss 0xF68], none or
    its +0 bit 0 set: none; its mode +0x48: 11 none, 9 and 10 the TV cameras' racer in use ([.bss 0xF70] +0x6), any
    other the chase camera's (viewport 0's record, [.bss 0xF6C] +0x2); or the local seat when it follows none. Uses
    r0, r12 and CR0 only; a caller may branch to the label `own` for the local seat. Labels start with `p`."""
    bss = lambda off: f"""    lis 12, 0x8000
    lwz 12, 0x30C8(12)
    addis 12, 12, 0x1C
    lwz 12, {0xF68 + off - 0x5FC0}(12)
"""
    assert 0x1C0000 - 0x5FC0 == 0x1BA040 == memmap.REL_BSS - memmap.REL
    return (bss(0) + f"""    cmplwi 12, 0
    beq @own
    lwz 0, 0(12)
    cmpwi 0, 0
    blt @own
    lbz 0, 0x48(12)
    extsb 0, 0
    cmpwi 0, 11
    beq @own
    bgt @chase
    cmpwi 0, 9
    blt @chase
""" + bss(8) + """    cmplwi 12, 0
    beq @own
    lha 12, 6(12)
    b @cmp
@chase:
""" + bss(4) + f"""    cmplwi 12, 0
    beq @own
    lha 12, 2(12)
    b @cmp
@own:
    lis 12, {H}
    lwz 12, 0x14(12)
@cmp:
""").replace("@", p)


def icon(p, count):
    """r0 = 0 when the machine at r30 is the racer the HUD follows (fn_1_6B48(0), read here as it reads it: the
    camera block [.bss 0xF68], none or its +0 bit 0 set: none; its mode +0x48: 11 none, 9 and 10 the TV cameras'
    racer in use ([.bss 0xF70] +0x6), any other the chase camera's (viewport 0's record, [.bss 0xF6C] +0x2)), or the
    local seat when it follows none; -1 for any other. With `count` (the first read, 0x129AA8, asked once a machine and
    drawn frame), and a game told to keep the logs: the racer's counter at memmap.ICONS, held back (+0) or let through
    (+120). Uses r0, r12 and CR0 only (r3 holds fn_1_5910's answer at 0x129AC0; `addi 0, 0, 1` would read rA as the
    number 0, so the count goes up as -(~n)). Labels start with `p`."""
    text = followed("@") + f"""    lha 0, 4(30)
    cmpw 0, 12
    beq @hide
"""
    tally = lambda at: f"""    lis 12, {C}
    lwz 12, {memmap.IMAGE_LOGS - memmap.CONTROL}(12)
    cmpwi 12, 0
    beq @{at}_say
    lha 0, 4(30)
    cmplwi 0, 30
    bge @{at}_say
    slwi 0, 0, 2
    lis 12, {memmap.hi(memmap.ICONS + (120 if at == "show" else 0))}
    ori 12, 12, {memmap.lo(memmap.ICONS + (120 if at == "show" else 0))}
    add 12, 12, 0
    lwz 0, 0(12)
    nor 0, 0, 0
    neg 0, 0
    stw 0, 0(12)
@{at}_say:
""" if count else ""
    text += tally("show") + "    li 0, -1\n    blr\n@hide:\n" + tally("hide") + "    li 0, 0\n    blr\n"
    return text.replace("@", p)
