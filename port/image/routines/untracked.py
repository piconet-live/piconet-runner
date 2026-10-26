"""Memory the fork never rolls back (for the frame routine, netframe.py, which includes this code and calls
`untracked_write` on entry every frame, before the network hook).

Control block +0x700: the count of ranges, then start, end pairs (at most 32); the fork reads them when rollback
starts. Pages wholly inside a range are left out of its undo log (never compared, logged or put back); the parts of
other pages are kept as they are through a restore. Both are presentation that must run on through a rollback, outside
the checksums; the cost is that what they did in a mispredicted moment is not taken back.
- The sound system's memory: put back by a rollback, the race music jumped back and then skipped. The game gives it
  its own OS heap ([.bss 0xDC4], made current around every sound allocation, fn_1_4438 and the sound library's hooks
  fn_1_9FE74/fn_1_9FEE8): the sound-effects work buffer (0x28000), the ADX music block (0x974A0: the two CRI ADX
  players, their stream and decode buffers) and the reverb's buffers, allocated at boot and at a course's load and
  unchanged in a race. Kept: that heap (its bounds from its own free and allocated cells: the heap's descriptor keeps
  no start or end), its descriptor, and the game's sound manager's variables (MANAGER: the sounds playing, the
  players' handles, the mute byte, and the per-machine loops of the machine sound update, fn_1_727C/fn_1_739C, run
  once per displayed frame from the frame function), so the manager does not lose a sound that is playing (with only
  sound.c's first 0x748 bytes kept, the pit's recharge loop once played on). A list of pages measured while paused on
  one course (the first version) missed most of the ADX block, whose buffers are written a part at a time, and was
  wrong on other courses.
- The face board's memory (the 3D pilot portraits of the top six, main.rel face.c): it loads and frees portrait
  models as racers enter and leave the top six, inside its own heap (made current around every face board call,
  fn_1_D2F50/fn_1_D2F84; the current-heap variable is main.dol's, never rolled back). Put back by a rollback, its
  bookkeeping went out of step with what the hardware had loaded (crashes in OSFreeToHeap, GFX FIFO "Unknown opcode"
  in garbage display lists). Kept: its heap ([.bss 0x7ACF0], size [.bss 0x7ACEC]), that heap's descriptor in the OS
  heap table ([0x801A6744] + 12 x [.data 0x3D924]), its variables (.bss 0x7ACD8-0x7AD48) and the file loader's state
  it reads portraits with (.bss 0x384D0, 8 bytes; nothing else loads files during a race). It picks its six from the
  race order every frame, so after a rollback it follows the corrected order.
- The HUD's presentation words (HUD below): written only by the HUD drawing.
- The 2D sprite pool (SPRITES below): the boost lightning on the energy meter and the HUD's other short animations.
  Put back by a rollback to before the drawing had moved it, the lightning stood at one picture while every frame
  had a rollback.
- The start countdown's movie request (MOVIE below): the ARQ request record the countdown task posts once a step
  (sites_countdown.py). The ARQ library's queue is the executable's, below the undo log: put back by a rollback, the
  record's link disagreed with the queue and the queue ended in itself.
- The rumble (RUMBLE below): put back by a rollback, a rumble the race asked for in a step later run again (where
  mute.py drops the request) was taken back, and the hits a rollback revealed had none (hits.cpp asks for it)."""
import memmap
import native

LIST = 0x700
# The sound manager's variables kept (main.rel .bss): sound.c's own, the race's sound set-up's table (0x6F1E4, filled
# by fn_1_34F38, which creates the ADX players, and used by the machine sound update) and the tables of the machine
# sound update (fn_1_727C/fn_1_739C: the per-machine loops it started; with 0x6F1E4 rolled back, a boost loop played
# on). Left tracked between them: variables the race's own code writes (fn_1_2DF98, fn_1_391F0, fn_1_3A330 at race
# over, fn_1_6AF70, fn_1_EBE4, fn_1_1049E0); kept whole to 0x6F600, the race desynced (812 / 900). From 0x6F5C0:
# relocation.c's. 0x6F32C-0x6F334: the lean sound's on/off state per machine (fn_1_A3654 starts loop 0xA9091B00 when
# it is 0 and stops it only when it is not; rolled back to 0, the start was sent again and the stop never: it looped).
# 0x6F240-0x6F242: the counts of local machines boosting and (+1) of the other condition that fn_1_A31E4 compares
# each frame with the last: up, it starts the boost loop 0xA9090C00; down to 0, it stops it (0x0D00). Rolled back,
# the loop was started again and again and never stopped (a live race: 59 starts, 0 stops; the official race: 3, 3).
MANAGER = [(0x6EA98, 0x6F239), (0x6F240, 0x6F242), (0x6F244, 0x6F316), (0x6F32C, 0x6F334), (0x6F41C, 0x6F5C0)]
# Presentation-only words of the race HUD's per-port entries (.bss 0x8FDA8, 0x34 each): +0x30, the lap-time list's
# slide (written by the HUD drawing, fn_1_15EFEC, after the steps; read by the drawing and the skull counter, fn_1_CAB38,
# through fn_1_15E1D0). Put back by a rollback to its value at an earlier step, it was stale on frames with a rollback
# and current on the others: after a lap the skull counter jumped up and down every frame.
HUD = [(0x8FDA8 + port * 0x34 + 0x30, 0x8FDA8 + port * 0x34 + 0x34) for port in range(4)]
# The 2D sprite pool (main.rel .bss 0x4C688, 64 entries of 0x80: the whole object, the next, 0x4E688, is another):
# fn_1_4DF60 hands out a free entry, its update callback +0x78 runs once a displayed frame from the main loop's tail
# (fn_1_4DE44 at _prolog 0x494) and fn_1_4DEC0 (0x4A4) draws the live entries. Every reference to the object in the
# game's modules (their relocations; intra-module branches by their encoding): font.c's routines 0x4DCD0-0x4E0A4
# (the clears fn_1_4DDC0, fn_1_4DE04, fn_1_4E060; the tick, the draw, the allocator; 15 field-address helpers
# 0x4DCD0-0x4DDBC that nothing calls). Their callers: the allocator only from the HUD (fn_1_C8DC0 0xC95BC: the boost
# lightning, flag 0x40, callback fn_1_CA218; fn_1_C96A4 0xCA110, the same for several viewports; fn_1_CD51C 0xCD578,
# callback fn_1_CD6C0), all under hud_draw fn_1_C7224, which fn_1_37AA8 calls at 0x37C74, after the simulation
# manager's call at 0x37C14 that the frame routine replaces with the steps; the callbacks write their entry only and
# read the machines through getters; the clears from mode and phase changes and set-up (_prolog, fn_1_33A20 from
# fn_1_36B10, fn_1_37E98, fn_1_3B7D4, fn_1_3CE94, fn_1_47A60, fn_1_481E8, story.rel), none of them a simulation
# manager task. So no step reads it, and the checksum (machine blocks and progress records) does not cover it.
SPRITES = [(0x4C688, 0x4C688 + 64 * 0x80)]
# The rumble (main.rel .bss 0x7F028-0x7F04C): +0 the on byte, +4 four pads' requests (kind, duration), +0x14 four
# pads' motors as they run. Written by the request fn_1_F73A8 (through fn_1_F7338, which mute.py stops in a step run
# again), the clear fn_1_F70D0 and the rumble task fn_1_F7128 (list 31, flag 0: a frame's last step only), which
# drives the motors (PADControlMotor); read by nothing else. 0x7F04C, the task's handle, stays tracked.
RUMBLE = [(0x7F028, 0x7F04C)]
# The countdown movie's ARAM request record (main.rel .bss 0x85814, one ARQRequest of 0x20 bytes: link, owner, type,
# priority, source, destination, length, callback), in the stream record at .bss 0x85280 (+0x594); written by
# ARQPostRequest from Starter (fn_1_1049E0) and by the ARQ library's service of the queue, read by nothing else.
MOVIE = [(0x85814, 0x85834)]   # the native form only (sites_countdown.py); the fork's image is as before
MAX = 32
C = memmap.hi(memmap.CONTROL)


def s16(v):
    return v - 0x10000 if v >= 0x8000 else v


def asm():
    """`untracked_write`: r31 = the control block, r30 = main.rel .bss; uses r0, r3-r12 and cr0."""
    assert 0 <= -(0x1BA040 - 0x16D600) + 0x3D924 + 0x10000 < 0x8000   # .data 0x3D924 from .bss
    movie = MOVIE if native.ON else []
    n = 4 + 2 + len(MANAGER) + len(HUD) + len(SPRITES) + len(RUMBLE) + len(movie)
    assert n <= MAX
    out = "untracked_write:\n    addi 12, 31, %d\n" % LIST
    put = lambda k, reg_lo, reg_hi: f"    stw {reg_lo}, {4 + 8 * k}(12)\n    stw {reg_hi}, {8 + 8 * k}(12)\n"
    # the sound heap: its cells' lowest start and highest end, and its descriptor (no sound heap: empty ranges)
    out += f"""    lwz 9, 0xDC4(30)
    li 3, 0
    li 4, 0
    li 11, 0
    cmplwi 9, 7
    bgt uw_snd_put
    lis 11, {s16(0x801A)}
    lwz 11, 0x6744(11)
    mulli 9, 9, 12
    add 11, 11, 9
    lis 3, -1
    addi 5, 11, 4
    li 6, 2
uw_snd_list:
    lwz 7, 0(5)
    li 8, 512
uw_snd_cell:
    cmpwi 7, 0
    beq uw_snd_next
    addi 8, 8, -1
    cmpwi 8, 0
    beq uw_snd_next
    cmplw 7, 3
    bge uw_snd_lo
    mr 3, 7
uw_snd_lo:
    lwz 0, 8(7)
    add 0, 7, 0
    cmplw 0, 4
    ble uw_snd_hi
    mr 4, 0
uw_snd_hi:
    lwz 7, 4(7)
    b uw_snd_cell
uw_snd_next:
    addi 5, 5, 4
    addi 6, 6, -1
    cmpwi 6, 0
    bne uw_snd_list
    cmplw 3, 4
    blt uw_snd_put
    li 3, 0
    li 4, 0
    li 11, 0
uw_snd_put:
    li 9, 0
    li 10, 0
    cmpwi 11, 0
    beq uw_snd_desc
    mr 9, 11
    addi 10, 11, 12
uw_snd_desc:
""" + put(0, 3, 4) + put(1, 9, 10)
    # the sound manager's variables
    for j, (lo, hi) in enumerate(MANAGER):
        out += "    addis 9, 30, 7\n    addi 9, 9, %d\n    addi 10, 9, %d\n" % (lo - 0x70000, hi - lo)
        out += put(2 + j, 9, 10)
    k = 2 + len(MANAGER)
    # the face board's variables, and the file loader's state
    out += "    addis 9, 30, 8\n    addi 9, 9, %d\n    addi 10, 9, 0x70\n" % (0x7ACD8 - 0x80000) + put(k, 9, 10)
    out += "    addis 9, 30, 4\n    addi 9, 9, %d\n    addi 10, 9, 8\n" % (0x384D0 - 0x40000) + put(k + 1, 9, 10)
    # its heap (none yet: an empty range)
    out += f"""    addis 11, 30, 8
    lwz 9, {0x7ACF0 - 0x80000}(11)
    lwz 10, {0x7ACEC - 0x80000}(11)
    add 10, 9, 10
    cmpwi 9, 0
    bne uw_heap
    li 10, 0
uw_heap:
""" + put(k + 2, 9, 10)
    # its heap's descriptor (heap id -1: none)
    out += f"""    addis 11, 30, -1
    lwz 9, {-(0x1BA040 - 0x16D600) + 0x3D924 + 0x10000}(11)
    li 10, 0
    cmpwi 9, -1
    bne uw_desc
    li 9, 0
    b uw_desc_put
uw_desc:
    lis 10, {s16(0x801A)}
    lwz 10, 0x6744(10)
    mulli 9, 9, 12
    add 9, 9, 10
    addi 10, 9, 12
uw_desc_put:
""" + put(k + 3, 9, 10)
    # the HUD's presentation words, the sprite pool, the rumble and the movie's request
    for j, (lo, hi) in enumerate(HUD + SPRITES + RUMBLE + movie):
        base = (lo + 0x8000) >> 16
        out += "    addis 9, 30, %d\n    addi 9, 9, %d\n    addi 10, 9, %d\n" % (base, lo - (base << 16), hi - lo)
        out += put(k + 4 + j, 9, 10)
    out += f"    li 0, {n}\n    stw 0, 0(12)\n    blr\n"
    return out
