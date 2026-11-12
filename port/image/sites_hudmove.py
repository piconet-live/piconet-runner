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
| `hm_*` (wraps) | each part's `bl` in fn_1_C72D4 (one viewport), fn_1_C771C (its other layout), fn_1_C6F8C (faces board, timer), fn_1_CDC8C (its timer), fn_1_15C36C (banners, FINISH/RETIRED), and the CHECK sign's own `bl fn_1_4F734` in fn_1_12907C (the Check task's drawing, a task callback: the one packet it queues) | the part's call between the host's PUSH and POP |
| `hs_*` (story blocks) | each mask test's `bl fn_1_5910` in fn_1_CDC8C and fn_1_CEB38, where one story part's block starts | the host's SET: the packets from here on are that part's (a block of the story's own, no mode part: STORY_OTHER) |
| `hm_sprite` | fn_1_4DEC0 0x4DF24, a pool sprite's packet queued (`bl fn_1_4F734`, r30 the sprite) | between SPRITE (the host names the part by the sprite's step callback: the boost lightning, wrong way) and POP |
| `race_time_score` | called by the wraps at 0xC73D8 and 0xC7820 (the race time, `bl fn_1_CBC24`, in both one-viewport layouts) | the game's call, then, in a race with a mode and the HUD on (the HUD part mask not 0): when the window says SCORE is shown for the racer the picture follows (memmap.MODE_HUD_SCORE: the number + 1), the part SCORE between the host's PUSH and POP: its label (the text at MODE_HUD_SCORE_LABEL: a `tag` text or the default SCORE) in the font at MODE_HUD_SCORE_FONT (the game's font number; 5 unless the mode chose one) at TIME's place (586, 67), upper case, white, 0.75 of the font's size, and the number in the race time's digit pictures (0x9440 + d, each over its shadow 0x9430 as the race time draws them) at y 81, right-aligned at the race time's last digit (x 596), 22 units a digit, one to six digits, inside the race time's right Wide Screen group, as the race time is; then, when the window says TEXT is shown (MODE_HUD_TEXT: the text's address), the part TEXT between its own PUSH and POP: the text in the font at MODE_HUD_TEXT_FONT, upper case, white, the font's own size, centred by the drawer (align 9: the middle of the text on the point) at its home (320, 116), inside the centre Wide Screen group (set for it with one viewport and the right group set back after, as the game's own code sets them) |
| `ax_rank`, `ax_faces` | called by the wraps at 0xC7350 (fn_1_C72D4's rank) and 0xC7144 (fn_1_C6F8C's faces board) | under `hud_layout ax` (the window's HUD_AX): the rank by fn_1_CA690 at (24, 412) in the left Wide Screen group, and the board's rows copied from .rodata 0x5A08 over the stack copy of 0x59C0, as the game's AX layout (fn_1_C771C, fn_1_C6F8C's other branch, chosen by the menu's arcade bit .bss 0x8B3A0 +0x94 0x40000000, which is never set: it changes more than the drawing); else the game's own call |
| `hm_flush` | fn_1_4FD64's entry, the queue drawn (`stwu r1, -0x60(r1)`) | the host's FLUSH: every queued packet of a moved part rewritten, the parts' record cleared |

With the window's word MODE_HUD_MOVE 0 (no layout, every start but a test's) each routine is the game's instruction
alone after one load and test: no host call. The host calls change no register; each routine keeps r3 and r4 around
them, so the part's call and the game's code after a site see what they would."""
import memmap

PUSH, POP, SET, SPRITE, FLUSH = 1, 2, 3, 4, 5
FACES_HIDDEN = 2   # MODE_HUD_MOVE's bit: the layout hides the faces board (its 3D heads: sites_modehud.py mode_faces)
# The parts, by number (hud_move.cpp PARTS, the same list: the image's tests hold them equal): NONE, the protocol's `part`
# choice in its order (TEXT the last a mode names), then the two of the HUD no mode names (BOOST, STORY_OTHER)
PARTS = ["NONE", "TIMER", "DISTANCE", "DIGIT", "WARNING", "SUCCESS", "FAILED", "TIME_OVER", "SPEED_GAUGE", "OBJECTS",
         "LEADER_GAP", "GHOST_GAP", "RANK", "LAPS", "SPEED", "ENERGY", "LAP_TIMES", "RACE_TIME", "FACES",
         "SPARE", "SKULLS", "WRONG_WAY", "BANNERS", "FINISH", "CHECK", "SCORE", "TEXT", "BOOST", "STORY_OTHER"]
P = {name: i for i, name in enumerate(PARTS)}

# (site, the called function, part): main.rel .text offsets
WRAPS = [
    (0xC7330, 0xCA8FC, "SPARE"), (0xC7350, 0xCA2A4, "RANK"), (0xC735C, 0xCDC8C, "STORY_OTHER"),
    (0xC7360, 0xCEB38, "STORY_OTHER"), (0xC7384, 0xC8DC0, "ENERGY"), (0xC738C, 0xCADC4, "SPEED"),
    (0xC73A8, 0xCC280, "LAPS"), (0xC73D8, 0xCBC24, "RACE_TIME"), (0xC73EC, 0xCB424, "LAP_TIMES"),
    (0xC73F8, 0xCB028, "LAP_TIMES"), (0xC7408, 0xCAB38, "SKULLS"),
    (0xC7780, 0xCA690, "RANK"), (0xC77A8, 0xCD7BC, "STORY_OTHER"), (0xC77CC, 0xC8DC0, "ENERGY"),
    (0xC77D4, 0xCADC4, "SPEED"), (0xC77F0, 0xCC280, "LAPS"), (0xC7820, 0xCBC24, "RACE_TIME"),
    (0xC7834, 0xCB424, "LAP_TIMES"), (0xC7840, 0xCB028, "LAP_TIMES"), (0xC7850, 0xCAB38, "SKULLS"),
    (0xC7144, 0xC8294, "FACES"), (0xC7184, 0xC8294, "FACES"), (0xC7204, 0xCF5B0, "TIMER"),
    (0xCDCEC, 0xCF5B0, "TIMER"),
    (0x15C458, 0x15EFEC, "BANNERS"), (0x15C47C, 0x15DFD4, "FINISH"),
    # the CHECK sign (image 0x944F at the screen's foot, x the rival's): fn_1_12907C runs as a task callback (fn_1_3522C
    # registers it), so its site is the queueing call itself, between PUSH and POP as hm_sprite's; r3 the packet on its
    # stack, r4 the image group, both kept
    (0x129948, 0x4F734, "CHECK"),
]
# (site, part): a story block's `bl fn_1_5910`, before its test of the part mask (the bit in the comment)
VIEWPORT = 0x5910
STORY_BLOCKS = [
    (0xCDCF0, "OBJECTS"), (0xCE170, "SPEED_GAUGE"), (0xCE818, "DISTANCE"),                 # fn_1_CDC8C: 0x4000 0x2000 0x1000
    (0xCEB8C, "STORY_OTHER"), (0xCED28, "WARNING"), (0xCED90, "DIGIT"), (0xCEDCC, "TIME_OVER"),  # fn_1_CEB38: 0x800 0x400 0x8 0x80
    (0xCEE44, "STORY_OTHER"), (0xCEE88, "STORY_OTHER"), (0xCEECC, "DIGIT"), (0xCF048, "SUCCESS"),  # 0x40 0x20 0x8 0x200
    (0xCF2C0, "FAILED"), (0xCF40C, "STORY_OTHER"), (0xCF4E8, "STORY_OTHER"),                   # 0x100 0x2 0x10
]
# `hud_layout ax` (the window's flag HUD_AX, mode_hud.h): two wraps call a routine of ours in place of the game's
# function, which draws as the AX layout (fn_1_C771C, fn_1_C6F8C's other branch) and else jumps to the function itself
AX_FLAG = 4096
AX_RANK, AX_FACES = 0xC7350, 0xC7144            # fn_1_C72D4's bl fn_1_CA2A4; fn_1_C6F8C's bl fn_1_C8294 (r3 the rows' copy)
AX_RANK_DRAW = 0xCA690                          # fn_1_CA690(mode, x, y): the AX layout's rank, called at (24, 412)
VIEWPORTS, ANCHOR_SET, ANCHOR_CLEAR = 0x58C4, 0x52070, 0x52088
ANCHOR_CENTRE, ANCHOR_RIGHT = 0x140, 0x220      # the Wide Screen groups' anchors (fn_1_C72D4: 96 left, 320 centre, 544 right)
RODATA = 0x15F710                               # main.rel's .rodata, from its base
AX_ROWS, ROWS_SIZE = RODATA + 0x5A08, 0x48       # .rodata 0x5A08: the AX layout's faces rows
# SCORE (mode_hud.h WINDOW_SCORE), drawn in the race time's call (fn_1_CBC24) by race_time_score: the race time's packet
# template (.rodata 0x26F8, 22 words: flags 5, z 0.1, white), its digit pictures (.rodata 0x5E98: 0x9440 to 0x9449), their
# shadow 0x9430 at z + 0.01 (.rodata 0x5DC8), the digits' y 81 (the race time's 67 + 14), the last digit's x 596 (its 444 +
# 152), 22 units a digit; the label on the TIME picture's row (y 67), its right edge at 615 (lined up with ENERGY and TIME as drawn, measured in a race), in the
# letters of the game's own HUD labels (below), queued through fn_1_51E60 as the race time is; a label with a letter those
# have not got is written by fn_1_4A0D8 (the text drawer the rank and the ghost names use) in the window's font (mode_hud.h
# FONT_IDS: 5, the 18 x 18 ASCII of the name tags, unless the mode chose another of the common sheet's), the HUD labels'
# cyan, two thirds of the font's size, its right edge at the same x (the drawer's own alignment 2). TEXT (mode_hud.h
# WINDOW_TEXT) after it, by the same routine: the text at its home (320, 116), centred on the point by the drawer's own
# alignment (fn_1_495C8: 1 the middle across, 8 the middle down), in the centre Wide Screen group
RACE_TIME_SITES, RACE_TIME_DRAW = (0xC73D8, 0xC7820), 0xCBC24
SCORE_TEMPLATE, SCORE_DIGITS, SCORE_SHADOW_DZ, SCORE_SHADOW = RODATA + 0x26F8, RODATA + 0x5E98, RODATA + 0x5DC8, 0x9430
SCORE_LAST_X, SCORE_STEP, SCORE_Y, SCORE_LABEL_RIGHT, SCORE_LABEL_Y = 596.0, 22.0, 81.0, 615.0, 67.0
SCORE_LABEL_SCALE, SCORE_LABEL_COLOUR, SCORE_LABEL_RIGHT_ALIGNED = 2.0 / 3.0, 0x00FFFFFF, 2
# The label's letters: cut from the game's own HUD label pictures (bitmap group 0x94, bmp_rac.tpl, loaded with the race HUD,
# so in every race: 0x9400 ENERGY, 0x9401 km/h, 0x9402 LAP, 0x9403 NAME, 0x9404 ND, 0x9405 POS., 0x9406 RD, 0x9407 RIVAL
# (yellow, not used), 0x9408 ST, 0x9409 TH, 0x940A TIME; 12 rows each, the cyan italic capitals with a navy edge). A letter
# is one packet of its picture with the letter's texel columns x0 to x1 as the packet's u0 and u1 and (x1 - x0) / width as
# its scale_x: the drawer's quad is the picture's width times scale_x (fn_1_4EDAC), flags 5 the left edge on x and the top
# on y, so the letter is drawn at the sheet's own size, and the letters abut as they do in the pictures. Letter: (picture,
# x0, x1), read from the sheet's column profiles (score-look, 2026-11-12). The letters the labels have: A D E G H I L M N O
# P R S T Y and the full stop; a space is 4 units of nothing. Any other character (B C F J K Q U V W X Z, a digit, -) sends
# the whole label to the text drawer.
LABEL_PICTURES = {0: 48, 2: 24, 3: 36, 4: 20, 5: 28, 9: 20, 10: 32}   # picture (0x9400 + n): its width in texels
LABEL_LETTERS = {"E": (0, 0, 7), "N": (0, 7, 15), "R": (0, 22, 30), "G": (0, 30, 38), "Y": (0, 38, 47), "L": (2, 0, 7),
                 "A": (3, 8, 17), "M": (3, 17, 26), "D": (4, 8, 17), "P": (5, 0, 8), "O": (5, 8, 17), "S": (5, 17, 24),
                 ".": (5, 24, 28), "T": (10, 0, 8), "I": (10, 8, 13), "H": (9, 8, 17)}
LABEL_SPACE, LABEL_NO_PICTURE = 4, 15   # a space: LABEL_SPACE units of nothing (its entry's picture LABEL_NO_PICTURE)


def label_table():
    """The letters' table: a word a letter (the character in the top byte, the picture's number in the next four
    bits, x0 and x1 in six bits each, the picture's width in the low byte), then a 0. The routine walks it for each
    character and makes the packet's u0, u1 and scale_x from the three numbers (the image has no room for them as
    floats: 57,344 bytes, the test logs after)."""
    words = [ord(" ") << 24 | LABEL_NO_PICTURE << 20 | LABEL_SPACE << 8 | 1]
    for c, (picture, x0, x1) in sorted(LABEL_LETTERS.items()):
        w = LABEL_PICTURES[picture]
        assert 0 <= x0 < x1 <= w < 256 and x1 < 64 and picture < LABEL_NO_PICTURE
        words.append(ord(c) << 24 | picture << 20 | x0 << 14 | x1 << 8 | w)
    return words + [0]


def label_width(text):
    """How wide the letters draw `text` (units of the 640 x 480 screen), or None when a character is not theirs."""
    total = 0.0
    for c in text:
        if c == " ":
            total += LABEL_SPACE
        elif c in LABEL_LETTERS:
            total += LABEL_LETTERS[c][2] - LABEL_LETTERS[c][1]
        else:
            return None
    return total
TEXT_HOME_X, TEXT_HOME_Y, TEXT_CENTRED = 320.0, 116.0, 9
QUEUE_PICTURE = 0x51E60                          # fn_1_51E60(packet): a 640 x 480 picture into the viewport's queue
TEXT_BEGIN, TEXT_FONT, TEXT_UPPER, TEXT_COLOUR, TEXT_SCALE, TEXT_AT, TEXT_PUTS = 0x49410, 0x494DC, 0x4962C, 0x49514, 0x4955C, 0x496FC, 0x4A0D8
TEXT_ALIGN = 0x495C8                             # fn_1_495C8(align): bits 0-1 across (1 the middle, 2 the right), 8 the middle down, 0x10 the foot
CALLEE = {AX_RANK: "ax_rank", AX_FACES: "ax_faces", **{at: "race_time_score" for at in RACE_TIME_SITES}}
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

    ax = la(12, memmap.MODE_HUD) + f"    lwz 12, 0(12)\n    andi. 12, 12, {AX_FLAG}\n"
    # the AX layout's rank: fn_1_CA690 at (24, 412) in the left Wide Screen group, as fn_1_C771C draws it (r3 the racer)
    r.add("ax_rank", ax + f"""    beq axr_stock
    stwu 1, -0x20(1)
    mflr 0
    stw 0, 0x24(1)
    stw 31, 0x1C(1)
    mr 31, 3
    @bl {TEXT + VIEWPORTS:#x}
    cmpwi 3, 1
    bne axr_draw
    @bl {TEXT + ANCHOR_CLEAR:#x}
    li 3, 0x60
    @bl {TEXT + ANCHOR_SET:#x}
axr_draw:
    mr 3, 31
    li 4, 0x18
    li 5, 0x19C
    @bl {TEXT + AX_RANK_DRAW:#x}
    @bl {TEXT + VIEWPORTS:#x}
    cmpwi 3, 1
    bne axr_back
    @bl {TEXT + ANCHOR_CLEAR:#x}
    li 3, 0x140
    @bl {TEXT + ANCHOR_SET:#x}
axr_back:
    lwz 31, 0x1C(1)
    lwz 0, 0x24(1)
    mtlr 0
    addi 1, 1, 0x20
    blr
axr_stock:
    @b {TEXT + 0xCA2A4:#x}
""")
    # the AX layout's faces rows over the stack copy of the race's that r3 points at, then the board's draw
    r.add("ax_faces", ax + f"""    beq axf_stock
""" + la(11, memmap.REL + AX_ROWS) + f"""    li 12, {ROWS_SIZE // 4}
    mtctr 12
    addi 12, 3, -4
    addi 11, 11, -4
axf_copy:
    lwzu 0, 4(11)
    stwu 0, 4(12)
    bdnz axf_copy
axf_stock:
    @b {TEXT + 0xC8294:#x}
""")

    # SCORE and TEXT after the race time: the stock call first (r3 to r7 its arguments, untouched), then, in a race with
    # a mode and the HUD on (the part mask of viewport 0, which the drawers test themselves), the counter when the window
    # shows one, and the text when the window has one
    import struct
    bits = lambda f: struct.unpack(">I", struct.pack(">f", f))[0]
    word = lambda reg, value: f"    lis {reg}, {value >> 16:#x}\n    ori {reg}, {reg}, {value & 0xFFFF:#x}\n"
    r.add("race_time_score", f"""    stwu 1, -0xC0(1)
    mflr 0
    stw 0, 0xC4(1)
    stmw 26, 0x90(1)
    stfd 30, 0xA8(1)
    stfd 31, 0xB0(1)
    @bl {TEXT + RACE_TIME_DRAW:#x}
""" + la(12, memmap.MODE) + f"""    lwz 12, 0(12)
    cmpwi 12, 0
    beq rts_out
""" + la(12, memmap.MODES) + f"""    lwz 12, 4(12)
    cmplwi 12, 0
    beq rts_out
""" + la(12, memmap.REL + memmap._facts.HUD_PARTS) + f"""    lwz 12, 0(12)
    cmplwi 12, 0
    beq rts_out
""" + la(31, memmap.MODE_HUD) + f"""    lwz 30, {memmap.MODE_HUD_SCORE - memmap.MODE_HUD:#x}(31)
    cmplwi 30, 0
    beq rts_text
    addi 30, 30, -1
    lwz 12, {memmap.MODE_HUD_MOVE - memmap.MODE_HUD:#x}(31)
    cmpwi 12, 0
    beq rts_label
    li 3, {PUSH}
    li 4, {P["SCORE"]}
    @bl host_hud
rts_label:
""" + la(11, memmap.REL + SCORE_TEMPLATE) + f"""    addi 12, 1, 0x20
    li 29, 22
rts_copy:
    lwz 0, 0(11)
    stw 0, 0(12)
    addi 11, 11, 4
    addi 12, 12, 4
    addi 29, 29, -1
    cmpwi 29, 0
    bne rts_copy
    lis 0, 0x4330
    stw 0, 0x78(1)
    stw 0, 0x80(1)
    lis 0, -0x8000
    stw 0, 0x7C(1)
    lfd 30, 0x78(1)
    @la 28, rts_letters
    lwz 29, {memmap.MODE_HUD_SCORE_LABEL - memmap.MODE_HUD:#x}(31)
    mr 26, 29
rts_check:
    lbz 0, 0(29)
    cmpwi 0, 0
    beq rts_checked
    bl rts_find
    cmpwi 27, 0
    beq rts_font
    addi 29, 29, 1
    b rts_check
rts_checked:
""" + word(0, bits(SCORE_LABEL_RIGHT)) + f"""    stw 0, 0x14(1)
    lfs 31, 0x14(1)
""" + word(0, bits(SCORE_LABEL_Y)) + f"""    stw 0, 0x28(1)
rts_letter:
    cmpw 29, 26
    beq rts_digits
    addi 29, 29, -1
    lbz 0, 0(29)
    bl rts_find
    rlwinm 11, 27, 18, 26, 31
    rlwinm 12, 27, 24, 26, 31
    xoris 11, 11, 0x8000
    stw 11, 0x84(1)
    lfd 3, 0x80(1)
    fsub 3, 3, 30
    xoris 12, 12, 0x8000
    stw 12, 0x84(1)
    lfd 4, 0x80(1)
    fsub 4, 4, 30
    fsubs 4, 4, 3
    fsubs 31, 31, 4
    rlwinm 0, 27, 12, 28, 31
    cmpwi 0, {LABEL_NO_PICTURE}
    beq rts_letter
    ori 0, 0, 0x9400
    stw 0, 0x20(1)
    stfs 31, 0x24(1)
    rlwinm 0, 27, 0, 24, 31
    xoris 0, 0, 0x8000
    stw 0, 0x84(1)
    lfd 5, 0x80(1)
    fsub 5, 5, 30
    fdivs 3, 3, 5
    stfs 3, 0x38(1)
    fdivs 4, 4, 5
    stfs 4, 0x30(1)
    fadds 3, 3, 4
    stfs 3, 0x40(1)
    addi 3, 1, 0x20
    @bl {TEXT + QUEUE_PICTURE:#x}
    b rts_letter
rts_find:
    mr 12, 28
rts_find_next:
    lwz 27, 0(12)
    cmpwi 27, 0
    beqlr
    rlwinm 11, 27, 8, 24, 31
    cmpw 11, 0
    beqlr
    addi 12, 12, 4
    b rts_find_next
rts_font:
    @bl {TEXT + TEXT_BEGIN:#x}
    lwz 3, {memmap.MODE_HUD_SCORE_FONT - memmap.MODE_HUD:#x}(31)
    @bl {TEXT + TEXT_FONT:#x}
    @bl {TEXT + TEXT_UPPER:#x}
    li 3, {SCORE_LABEL_RIGHT_ALIGNED}
    @bl {TEXT + TEXT_ALIGN:#x}
""" + word(0, SCORE_LABEL_COLOUR) + f"""    stw 0, 0x10(1)
    addi 3, 1, 0x10
    @bl {TEXT + TEXT_COLOUR:#x}
""" + word(0, bits(SCORE_LABEL_SCALE)) + f"""    stw 0, 0x14(1)
    lfs 1, 0x14(1)
    fmr 2, 1
    @bl {TEXT + TEXT_SCALE:#x}
""" + word(0, bits(SCORE_LABEL_RIGHT)) + f"""    stw 0, 0x14(1)
    lfs 1, 0x14(1)
""" + word(0, bits(SCORE_LABEL_Y)) + f"""    stw 0, 0x14(1)
    lfs 2, 0x14(1)
    @bl {TEXT + TEXT_AT:#x}
    lwz 3, {memmap.MODE_HUD_SCORE_LABEL - memmap.MODE_HUD:#x}(31)
    @bl {TEXT + TEXT_PUTS:#x}
rts_digits:
    li 0, 0
    stw 0, 0x38(1)
    lis 0, 0x3F80
    stw 0, 0x40(1)
    stw 0, 0x30(1)
""" + word(0, bits(SCORE_Y)) + f"""    stw 0, 0x28(1)
""" + word(0, bits(SCORE_LAST_X)) + f"""    stw 0, 0x14(1)
    lfs 31, 0x14(1)
""" + word(0, bits(SCORE_STEP)) + f"""    stw 0, 0x14(1)
""" + la(12, memmap.REL + SCORE_SHADOW_DZ) + f"""    lwz 0, 0(12)
    stw 0, 0x18(1)
""" + la(28, memmap.REL + SCORE_DIGITS) + f"""rts_digit:
    li 12, 10
    divwu 27, 30, 12
    mullw 0, 27, 12
    subf 0, 0, 30
    rlwinm 0, 0, 2, 0, 29
    lwzx 0, 28, 0
    stw 0, 0x20(1)
    stfs 31, 0x24(1)
    addi 3, 1, 0x20
    @bl {TEXT + QUEUE_PICTURE:#x}
    li 0, 0
    ori 0, 0, {SCORE_SHADOW:#x}
    stw 0, 0x20(1)
    lfs 1, 0x2C(1)
    stfs 1, 0x1C(1)
    lfs 0, 0x18(1)
    fadds 1, 1, 0
    stfs 1, 0x2C(1)
    addi 3, 1, 0x20
    @bl {TEXT + QUEUE_PICTURE:#x}
    lfs 1, 0x1C(1)
    stfs 1, 0x2C(1)
    lfs 0, 0x14(1)
    fsubs 31, 31, 0
    mr 30, 27
    cmplwi 30, 0
    bne rts_digit
    lwz 12, {memmap.MODE_HUD_MOVE - memmap.MODE_HUD:#x}(31)
    cmpwi 12, 0
    beq rts_text
    li 3, {POP}
    li 4, {P["SCORE"]}
    @bl host_hud
rts_text:
    lwz 30, {memmap.MODE_HUD_TEXT - memmap.MODE_HUD:#x}(31)
    cmplwi 30, 0
    beq rts_out
    lwz 12, {memmap.MODE_HUD_MOVE - memmap.MODE_HUD:#x}(31)
    cmpwi 12, 0
    beq rts_centre
    li 3, {PUSH}
    li 4, {P["TEXT"]}
    @bl host_hud
rts_centre:
    @bl {TEXT + VIEWPORTS:#x}
    mr 29, 3
    cmpwi 29, 1
    bne rts_draw
    @bl {TEXT + ANCHOR_CLEAR:#x}
    li 3, {ANCHOR_CENTRE:#x}
    @bl {TEXT + ANCHOR_SET:#x}
rts_draw:
    @bl {TEXT + TEXT_BEGIN:#x}
    lwz 3, {memmap.MODE_HUD_TEXT_FONT - memmap.MODE_HUD:#x}(31)
    @bl {TEXT + TEXT_FONT:#x}
    @bl {TEXT + TEXT_UPPER:#x}
    lis 0, -1
    ori 0, 0, 0xFFFF
    stw 0, 0x10(1)
    addi 3, 1, 0x10
    @bl {TEXT + TEXT_COLOUR:#x}
    li 3, {TEXT_CENTRED}
    @bl {TEXT + TEXT_ALIGN:#x}
""" + word(0, bits(TEXT_HOME_X)) + f"""    stw 0, 0x14(1)
    lfs 1, 0x14(1)
""" + word(0, bits(TEXT_HOME_Y)) + f"""    stw 0, 0x14(1)
    lfs 2, 0x14(1)
    @bl {TEXT + TEXT_AT:#x}
    mr 3, 30
    @bl {TEXT + TEXT_PUTS:#x}
    cmpwi 29, 1
    bne rts_placed
    @bl {TEXT + ANCHOR_CLEAR:#x}
    li 3, {ANCHOR_RIGHT:#x}
    @bl {TEXT + ANCHOR_SET:#x}
rts_placed:
    lwz 12, {memmap.MODE_HUD_MOVE - memmap.MODE_HUD:#x}(31)
    cmpwi 12, 0
    beq rts_out
    li 3, {POP}
    li 4, {P["TEXT"]}
    @bl host_hud
rts_out:
    lfd 31, 0xB0(1)
    lfd 30, 0xA8(1)
    lmw 26, 0x90(1)
    lwz 0, 0xC4(1)
    mtlr 0
    addi 1, 1, 0xC0
    blr
rts_letters:
""" + r.words("rts_letters", label_table()))

    for n, (at, target, part) in enumerate(WRAPS):
        site, fn = TEXT + at, TEXT + target
        word = stock(site, k.branch(site, fn, True))
        assert k.branch_target(word, site) == fn
        callee = CALLEE.get(at, f"{fn:#x}")
        name = f"hm_{at:x}"
        r.add(name, on + f"""    beq {name}_stock
""" + call(PUSH, f"li 4, {P[part]}") + f"""    @bl {callee}
""" + call(POP, f"li 4, {P[part]}") + f"""    @b {site + 4:#x}
{name}_stock:
    @bl {callee}
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
