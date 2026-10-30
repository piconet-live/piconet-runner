"""The hand-back monitors' pictures (routines/monitors.py; a rolling start's monitors show the game's movie and do
not use them), read before the race (sites.py's frame routine calls
`monitors_preload` after `results_preload`, on the same black frame before the start gate). Native form only: the
fork's image has no reader, and its monitors stay off (their state word stays 0).

The start's movie (jpeg/compo.arc, 210 JPEG frames of 128 x 128) is freed at the end of the start countdown
(fn_1_106B68), so the five frames the monitors show (monitors.FRAMES) are read from the disc once, here: one block
of the game's main heap (memmap.MONITORS_BLOCK: the 128 x 128 RGBA8 texture, the decoder's 16 KiB of work, five
slots of 4 KiB for the JPEGs), taken at the top of its largest free block as results_preload takes its files (a
filler below it is taken first and given back), and only when that block keeps sites.py's RESULTS_RESERVE beyond
it. The archive is the disc's U8 file: its header (the magic, the node table at 0x20) and each frame's node
(0x20 + 12 x (frame + 3): a file, its offset, its size) are read into the slot the frame then takes. Any failure
leaves the state at 3 and gives the block back; nothing the race reads is touched (the heap's layout below the
block is the one it has without it). State, the block and the sizes: memmap.MONITORS +4, +8, +12."""
import memmap
import monitors

MAGIC = 0x55AA382D


def add(k, r, la, D):
    """Adds `monitors_preload` to `r` (build.Routines); `la` loads an address, `D` gives a main.rel .data address."""
    TEXT, MAIN_HEAP, HEAP_TABLE, RESERVE = k.TEXT, k.MAIN_HEAP, k.HEAP_TABLE, k.RESULTS_RESERVE
    FN = lambda offset: f"{TEXT + offset:#x}"
    ARCHIVE, FILE_NAME = D(0x3F188 + 0x1F4), D(0x1A368)       # "jpeg/compo.arc"; results_preload's allocation tag
    SIZE, S = memmap.MONITORS_BLOCK, monitors.STATE
    assert monitors.JPEG_SLOT == 0x1000 and monitors.JPEGS == 0x14000
    frames = ""
    for i, frame in enumerate(monitors.FRAMES):
        node = 0x20 + 12 * (frame + 3)
        assert node % 4 == 0 and node < 0x8000
        frames += f"""    addi 21, 23, {i * 0x1000:#x}
    addi 3, 1, 8
    mr 4, 21
    li 5, 0x20
    li 6, {node:#x}
    @bl {FN(0x458A0)}
    cmpwi 3, 0
    blt mp_close
    lbz 0, 0(21)
    cmpwi 0, 0
    bne mp_close
    lwz 20, 4(21)
    lwz 19, 8(21)
    cmplwi 19, 0
    beq mp_close
    cmplwi 19, 0xFE0
    bgt mp_close
    stw 19, {12 + 4 * i}(31)
    addi 5, 19, 31
    rlwinm 5, 5, 0, 0, 26
    addi 3, 1, 8
    mr 4, 21
    mr 6, 20
    @bl {FN(0x458A0)}
    cmpwi 3, 0
    blt mp_close
"""
    r.add("monitors_preload", la(12, memmap.MONITORS) + f"""    lwz 0, {S}(12)
    cmpwi 0, 0
    bnelr
""" + la(11, memmap.REL_BSS) + f"""    lha 9, 0x960(11)
    cmpwi 9, 2
    bnelr
    lha 9, 0x3C2A(11)
    cmpwi 9, 0x27
    bnelr
    addis 9, 11, 2
    lwz 9, 0x6C28(9)
    cmpwi 9, 1
    bnelr
    li 0, 1
    stw 0, {S}(12)
    stwu 1, -0xC0(1)
    mflr 0
    stw 0, 0xC4(1)
    stmw 19, 0x8C(1)
""" + la(31, memmap.MONITORS) + la(9, MAIN_HEAP) + "    lwz 27, 0(9)\n" + la(9, HEAP_TABLE) + f"""    lwz 9, 0(9)
    mulli 0, 27, 12
    add 9, 9, 0
    lwz 9, 4(9)
    li 26, 0
mp_walk:
    cmplwi 9, 0
    beq mp_walked
    lwz 0, 8(9)
    cmplw 0, 26
    ble mp_next
    mr 26, 0
mp_next:
    lwz 9, 4(9)
    b mp_walk
mp_walked:
""" + la(0, SIZE + RESERVE + 0x60) + """    cmplw 26, 0
    blt mp_none
""" + la(0, SIZE + 0x60) + """    subf 4, 0, 26
    mr 3, 27
""" + la(5, FILE_NAME) + f"""    li 6, 0x7101
    @bl {FN(0x45D0)}
    mr. 25, 3
    beq mp_none
    mr 3, 27
""" + la(4, SIZE) + la(5, FILE_NAME) + f"""    li 6, 0x7102
    @bl {FN(0x45D0)}
    mr 24, 3
    mr 3, 27
    mr 4, 25
""" + la(5, FILE_NAME) + f"""    li 6, 0x7103
    @bl {FN(0x46B4)}
    cmpwi 24, 0
    beq mp_none
    stw 24, {monitors.BLOCK}(31)
""" + la(3, ARCHIVE) + f"""    addi 4, 1, 8
    @bl {FN(0x45730)}
    cmpwi 3, 0
    beq mp_free
    addis 23, 24, 1
    addi 23, 23, 0x4000
    addi 3, 1, 8
    mr 4, 23
    li 5, 0x20
    li 6, 0
    @bl {FN(0x458A0)}
    cmpwi 3, 0
    blt mp_close
    lwz 0, 0(23)
""" + la(9, MAGIC) + """    cmpw 0, 9
    bne mp_close
    lwz 0, 4(23)
    cmpwi 0, 0x20
    bne mp_close
""" + frames + f"""    addi 3, 1, 8
    @bl {FN(0x45850)}
    li 0, 2
    stw 0, {S}(31)
mp_out:
    lmw 19, 0x8C(1)
    lwz 0, 0xC4(1)
    mtlr 0
    addi 1, 1, 0xC0
    blr
mp_close:
    addi 3, 1, 8
    @bl {FN(0x45850)}
mp_free:
    mr 3, 27
    mr 4, 24
""" + la(5, FILE_NAME) + f"""    li 6, 0x7104
    @bl {FN(0x46B4)}
    li 0, 0
    stw 0, {monitors.BLOCK}(31)
mp_none:
    li 0, 3
    stw 0, {S}(31)
    b mp_out
""")
