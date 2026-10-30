"""The local racer's own rumble and player sounds, as a versus player gets them (for the frame routine, netframe.py,
which includes this code and calls `seatport_install` on entry every frame).

In a race of ours no machine has a pad port (+0x474 is -1 for every machine: seats 1 to 29 never had one, and seat
0's goes with the hand-over to a CPU driver in its first step, standin.py), and only seat 0 has a player index
(+0x475 = 0), in every game. The game keys two kinds of feedback by them, both asked for inside the simulation step:
- rumble: every request (fn_1_F7338(port, kind, frames)) passes the machine's port, so none was ever made. The sites
  are the port reads just before the call: the contact pair fn_1_239C4 (0x245E8, 0x24754: kind 4 for 12 frames for a
  hit; 0x24778, 0x2478C kind 2, 0x247B0, 0x247C4 kind 3, 9 frames, by the contact's strength), the wall response
  fn_1_266A8 (0x267A4-0x269A4: kinds 2, 3, 4 for 9 frames, by the wall hit's strength, two groups), the boost
  fn_1_26F34 (0x27238 kind 3, 0x2733C kind 1, 15 frames), and kind 2 for 30 frames from the KO fn_1_25004
  (0x25038), the track query fn_1_2A694 (0x2A7CC), the wall corner fn_1_2A8D0 (0x2AAB0) and fn_1_2BA48 (0x2BB44).
  The helper gives port 0 for the local racer's machine (slot 'FZSI' +4, which the camera and the HUD follow; the
  followed racer in a replay played as the race) and -1 for every other, as follow.py's do. fn_1_F7338 then finds the
  port's machine (0xF7360, `bl fn_1_866B4`) to skip one out of contact (+0x58C bit 0x10): with port 0 that lookup
  finds seat 0 (the hand-over left seat 0 in .bss 0x6D868[0]), so the helper there gives the local seat for port 0
  and asks the game's own lookup for any other. The request lands in pad 0's slot (.bss 0x7F02C), under the game's
  own conditions (rumble on, pad 0's option, at most 180 frames, fn_1_F73A8), and the rumble task (fn_1_F7128)
  drives the motor. Pad 0 is the runner's port 0, the local player's controller (as hits.cpp's hit rumble).
- player sounds: fourteen sounds of a machine are a player's only. Each site reads +0x475 (-1: not a player) and then
  skips a machine with flag 0x04000000 (driven by a CPU: fn_1_8D894's hand-over, which a versus player gets only at
  the finish) or 0x200 (a CPU's rules): the contact pair's 0xA9092100 for each machine of a contact (0x246E4,
  0x24724), the crash bit's 0xA9072100/0xA9072200 (fn_1_248C0: 0x24954, 0x249A8), the breakdown's and KO's
  0xA9091200 (fn_1_24A4C 0x24E20, fn_1_2BA48 0x2BB84), and the restore's eight (fn_1_2BF8C 0x2C030-0x2C298:
  0xA9092200, 2300, 2500, 2600, 2400, 0xA9061C00, 2400, 1200). Seat 0 has +0x475 = 0 but flag 0x04000000, the others
  -1, so none played for anyone. The +0x475 read gives 0 for the local racer and -1 for the others; the mask that
  follows (`addi r0, r3, 0x200`: 0x04000200) becomes 0x200, plus 0x04000000 once the machine has finished (0x10000),
  as the hand-over sets it for a versus player then (every seat's flag 0x200 is cleared each step: standin.py).
Exactness: nothing here writes the simulation's memory. The helpers read the local seat and the machine's flags; what
they change is whether a sound command is queued and whether pad 0's rumble request is written (.bss 0x7F028-
0x7F04C, kept out of rollbacks: untracked.py), and neither is read by any step. A step simulated again plays neither:
mute.py stops the sound queue and fn_1_F7338 at its entry. Counters (the image's own, control block, never rolled
back; tests and the playback's log): +708 rumble requests asked for pad 0 by the local racer, +712 player-sound checks
the local racer reached (counted on steps run for the first time only), +716 finish stops (frame_hud.py `finwatch`).
(A count adds 1 with `nor` and `neg`: `addi r0, r0, 1` would read r0 as 0.)
Control block +704: the main.rel address the sites were patched for (follow.installer)."""
import memmap
from follow import installer, follow, s16

GUARD, RUMBLES, SOUNDS, STOPS = 704, 708, 712, 716
RESIM = 488                        # control block: the step about to run was simulated before (mute.py)
H = memmap.hi(memmap.IMAGE + 0x14)
C = s16(memmap.hi(memmap.CONTROL))
LOOKUP = 0xDC + 0x866B4            # fn_1_866B4, from the module's base

# (text offset, original word, mask, helper)
RUMBLE_SITES = [
    (0x245E8, 0x88780474, 0xFFFFFFFF, "p24_3"), (0x24754, 0x88790474, 0xFFFFFFFF, "p25_3"),
    (0x24778, 0x88780474, 0xFFFFFFFF, "p24_3"), (0x2478C, 0x88790474, 0xFFFFFFFF, "p25_3"),
    (0x247B0, 0x88780474, 0xFFFFFFFF, "p24_3"), (0x247C4, 0x88790474, 0xFFFFFFFF, "p25_3"),
    (0x25038, 0x881E0474, 0xFFFFFFFF, "p30_0"),
    (0x267A4, 0x887E0474, 0xFFFFFFFF, "p30_3"), (0x267C8, 0x887E0474, 0xFFFFFFFF, "p30_3"),
    (0x267E0, 0x887E0474, 0xFFFFFFFF, "p30_3"), (0x26958, 0x887E0474, 0xFFFFFFFF, "p30_3"),
    (0x2697C, 0x887E0474, 0xFFFFFFFF, "p30_3"), (0x26994, 0x887E0474, 0xFFFFFFFF, "p30_3"),
    (0x27238, 0x887E0474, 0xFFFFFFFF, "p30_3"), (0x2733C, 0x887E0474, 0xFFFFFFFF, "p30_3"),
    (0x2A7CC, 0x881A0474, 0xFFFFFFFF, "p26_0"), (0x2AAB0, 0x88180474, 0xFFFFFFFF, "p24_0"),
    (0x2BB44, 0x881E0474, 0xFFFFFFFF, "p30_0"),
]
LOOKUP_SITE = (0xF7360, 0x4BF8F355, 0xFFFFFFFF, "lookup")       # fn_1_F7338: bl fn_1_866B4
SOUND_SITES = [            # (the +0x475 read, its word, the machine's register; the mask's offset)
    (0x246E4, 0x88190475, 25, 0x246FC), (0x24724, 0x88180475, 24, 0x2473C),
    (0x24954, 0x881E0475, 30, 0x24968), (0x249A8, 0x881E0475, 30, 0x249BC),
    (0x24E20, 0x881D0475, 29, 0x24E38), (0x2BB84, 0x881E0475, 30, 0x2BB9C),
    (0x2C030, 0x881E0475, 30, 0x2C048), (0x2C06C, 0x881E0475, 30, 0x2C084),
    (0x2C0A8, 0x881E0475, 30, 0x2C0C0), (0x2C0E4, 0x881E0475, 30, 0x2C0FC),
    (0x2C114, 0x881E0475, 30, 0x2C12C), (0x2C144, 0x881E0475, 30, 0x2C15C),
    (0x2C1EC, 0x881E0475, 30, 0x2C204), (0x2C298, 0x881E0475, 30, 0x2C2B0),
]
MASK = 0x38030200                  # addi r0, r3, 0x200 (r3 = 0x04000000): the flags a player's sound skips
SITES = (RUMBLE_SITES + [LOOKUP_SITE] + [(o, w, 0xFFFFFFFF, f"s{r}") for o, w, r, _ in SOUND_SITES]
         + [(m, MASK, 0xFFFFFFFF, "mask") for _, _, _, m in SOUND_SITES])
assert 0x80000 + 0x6790 == LOOKUP and 0xF7360 + 0x03F8F354 - 0x04000000 == 0x866B4

HELPERS = {
    "p24_3": follow(24, 3), "p25_3": follow(25, 3), "p30_3": follow(30, 3), "p30_0": follow(30, 0),
    "p26_0": follow(26, 0), "p24_0": follow(24, 0),
    "s25": follow(25, 0), "s24": follow(24, 0), "s30": follow(30, 0), "s29": follow(29, 0),
    # fn_1_F7338's lookup of the port's machine (r3 = the port, sign-extended): port 0 is the local seat (counted);
    # any other port the game's own fn_1_866B4 answers (called from here, as othersound.py calls fn_1_6A80)
    "lookup": f"""    cmpwi 3, 0
    bne sp_lookup_game
    lis 12, {C}
    lwz 0, {RUMBLES}(12)
    nor 0, 0, 0
    neg 0, 0
    stw 0, {RUMBLES}(12)
    lis 3, {H}
    lwz 3, 0x14(3)
    blr
sp_lookup_game:
    mflr 0
    stw 0, 4(1)
    stwu 1, -16(1)
    lis 12, 0x8000
    lwz 12, 0x30C8(12)
    addis 12, 12, {LOOKUP >> 16}
    addi 12, 12, {LOOKUP & 0xFFFF}
    mtctr 12
    bctrl
    addi 1, 1, 16
    lwz 0, 4(1)
    mtlr 0
    blr
""",
    # r0 = the flags a player's sound skips: 0x200, and 0x04000000 once the machine (flags in r4) has finished;
    # counted on a step run for the first time. Reached for the local racer only (the +0x475 helper gives the others
    # -1). Uses r0 and r12; CR0 is set anew by the site's next instruction (`and.`)
    "mask": f"""    lis 12, {C}
    lwz 0, {RESIM}(12)
    cmpwi 0, 0
    bne sp_mask
    lwz 0, {SOUNDS}(12)
    nor 0, 0, 0
    neg 0, 0
    stw 0, {SOUNDS}(12)
sp_mask:
    rlwinm 0, 4, 10, 5, 5
    ori 0, 0, 0x200
    blr
""",
}


def finish_stop():
    """For frame_hud.py `finwatch`, where the local racer's kind has just become "finished" (r10 = its machine, r30 =
    main.rel's .bss, r31 = the control block): pad 0's rumble stopped hard, as fn_1_32A40 asks for a versus player's
    port at the finish (0x32EC0: fn_1_F7338(port, 5, 0)), under fn_1_F7338's conditions (rumble on, .bss 0x7F028 bit
    0; pad 0's option, .data 0x3E8F0 bit 0; the machine in contact, +0x58C bit 0x10 clear), written as fn_1_F73A8
    writes kind 5 (duration 0) into pad 0's request (.bss 0x7F02C). Written, not asked through fn_1_F7338: a finish a
    rollback revealed is seen at a step boundary of a step run again, where mute.py stops fn_1_F7338. Uses r0, r9,
    r11 and CR0."""
    return f"""    addis 9, 30, 8
    lbz 0, -0xFD8(9)
    andi. 0, 0, 1
    beq fw_no_stop
    addis 11, 30, -1
    lbz 0, 0x1EB0(11)
    andi. 0, 0, 1
    beq fw_no_stop
    lwz 0, 0x{memmap.MACHINE_FLAGS2:X}(10)
    andi. 0, 0, 0x10
    bne fw_no_stop
    li 0, 5
    stb 0, -0xFD4(9)
    li 0, 0
    sth 0, -0xFD2(9)
    lwz 0, {STOPS}(31)
    nor 0, 0, 0
    neg 0, 0
    stw 0, {STOPS}(31)
fw_no_stop:
"""


BSS, DATA = 0x1BA040, 0x16D600     # main.rel's .bss and .data from the module's base
assert 0x80000 - 0xFD8 == 0x7F028 and 0x80000 - 0xFD4 == 0x7F02C and DATA + 0x3E8F0 == BSS - 0x10000 + 0x1EB0


def asm():
    """The helpers and `seatport_install` (r31 = the control block; called with bl; uses r0, r3-r12 and 32(r1))."""
    return installer("seatport_install", "sp", SITES, HELPERS, GUARD)
