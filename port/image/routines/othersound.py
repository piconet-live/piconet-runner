"""Other racers' player sounds turned down (for the frame routine, netframe.py, which includes this code and calls
`othersound_install` on entry every frame).

The machine sound update (fn_1_739C, once per displayed frame) gives the camera's machine and the nearest others a
voice each (table .bss 0x6EA98 + 0x74C, 0x14 bytes a voice: +1 the machine, +2 its volume, 127 at the camera down to
0 at 300 units, fn_1_A3D5C) and sends, before each sound it starts for a machine, the volume command 0xB027 with that
voice's volume (fn_80067344(1, 0x10, 0xB0270000, volume); a start takes the volume last sent). Some of a machine's
sounds are a player's only: the game skips them for CPU machines (flag 0x200, or 0x280): in fn_1_A3654 everything
after its CPU check (0xA37E4: the lean sounds 0x1B/0x1C/0x16 and the destroyed alarm 0x2E/0x12), in fn_1_A31E4 the
boost's start 0x15 (and the boost loop's count), in fn_1_A3C78 the sound 0x0F. Online every seat races under the
human rules (standin.py clears 0x200), so every machine near the camera made them at the volume of a player's own
machine (much louder than the CPUs' in a normal race, where they make none).
Here, for every machine but the one the camera follows (fn_1_6A80: the local seat, or the racer watched), the volume
sent before those sounds is SCALE / 256 of the voice's, and the voice's own volume is sent again after them, so the
engine and the sounds CPUs make too keep the game's volume. SCALE: the game's split-screen factor (1, 0.9, 0.85, 0.8
for 1 to 4 screens: 1 - 0.1 log2 n) taken to 30 racers, 0.51 (chosen by ear). Presentation only (the sound
system's memory is not rolled back, untracked.py; these functions run in a frame's last step).
Control block +524: the main.rel address the sites were patched for (follow.installer); +528: the voice volume + 1 to
send again after the sounds (0: none); +548: the scale + 1 (the launcher's "other racers' sounds": 257 full, 132
quieter, 1 off; 0: SCALE), which the Dolphin fork writes from the match file."""
import memmap
from follow import installer, s16

SCALE = 131   # / 256 = 0.51
GUARD = 524
RESTORE = 528
SETTING = 548
C = s16(memmap.hi(memmap.CONTROL))

# fn_1_A3654 at its CPU check (`rlwinm. r0, r30, 0, 22, 22`) and its end (`psq_l f31, 0x38(r1)`); fn_1_A31E4's and
# fn_1_A3C78's volume command (`bl fn_80067344`, relocated at load: any bl) and their loops' next voice (`addi`)
SITES = [(0xA37E4, 0x57C005AD, 0xFFFFFFFF, "lean"),
         (0xA39FC, 0xE3E10038, 0xFFFFFFFF, "lean_end"),
         (0xA32E4, 0x48000001, 0xFC000003, "boost"),
         (0xA330C, 0x3BBD0014, 0xFFFFFFFF, "boost_next"),
         (0xA3CFC, 0x48000001, 0xFC000003, "slide"),
         (0xA3D24, 0x3BDE0014, 0xFFFFFFFF, "slide_next")]

# fn_1_6A80 (main.rel text, at 0xDC in the module: [0x800030C8] is the module's base)
CALL_FOLLOWED = """    lis 12, 0x8000
    lwz 12, 0x30C8(12)
    addi 12, 12, 0xDC + 0x6A80
    mtctr 12
    bctrl
"""
CALL_VOLUME = """    li 3, 1
    li 4, 0x10
    lis 5, -0x4FD9
    lis 12, 0x8006
    ori 12, 12, 0x7344
    mtctr 12
    bctrl
"""
ENTER = """    mflr 0
    stw 0, 4(1)
    stwu 1, -32(1)
"""
LEAVE = """    addi 1, 1, 32
    lwz 0, 4(1)
    mtlr 0
"""


def scaled(label, machine):
    """r6 = the voice's volume; machine: the register holding the sound's machine. Sends the volume, scaled (and
    marked for sending again) unless the machine is the camera's."""
    return ENTER + f"""    stw 6, 8(1)
""" + CALL_FOLLOWED + f"""    lwz 6, 8(1)
    cmpw 3, {machine}
    beq {label}
    cmpwi 6, 0
    beq {label}
    addi 0, 6, 1
    lis 12, {C}
    stw 0, {RESTORE}(12)
    lwz 11, {SETTING}(12)
    cmpwi 11, 0
    bne {label}_have
    li 11, {SCALE + 1}
{label}_have:
    addi 11, 11, -1
    mullw 6, 6, 11
    srwi 6, 6, 8
{label}:
""" + CALL_VOLUME + LEAVE


def again(label, tail):
    """Sends the voice's own volume again if it was scaled, then runs the site's instruction(s) `tail`."""
    return f"""    lis 12, {C}
    lwz 6, {RESTORE}(12)
    cmpwi 6, 0
    beq {label}
    li 0, 0
    stw 0, {RESTORE}(12)
    addi 6, 6, -1
""" + ENTER + CALL_VOLUME + LEAVE + f"""{label}:
{tail}    blr
"""


HELPERS = {
    # fn_1_A3654: r28 the machine, r27 its voice, r29 .bss 0x6EA98; the check's own instruction last (it sets cr0)
    "lean": """    mulli 12, 27, 0x14
    add 12, 12, 29
    lbz 6, 0x74E(12)
""" + ENTER + """    stw 6, 8(1)
""" + CALL_FOLLOWED + f"""    lwz 6, 8(1)
    cmpw 3, 28
    beq osl_done
    cmpwi 6, 0
    beq osl_done
    addi 0, 6, 1
    lis 12, {C}
    stw 0, {RESTORE}(12)
    lwz 11, {SETTING}(12)
    cmpwi 11, 0
    bne osl_have
    li 11, {SCALE + 1}
osl_have:
    addi 11, 11, -1
    mullw 6, 6, 11
    srwi 6, 6, 8
""" + CALL_VOLUME + "osl_done:\n" + LEAVE + """    rlwinm. 0, 30, 0, 22, 22
    blr
""",
    "lean_end": again("osle_done", "    .long 0xE3E10038\n"),
    # fn_1_A31E4: r23 the machine, r6 its voice's volume
    "boost": scaled("osb_send", 23) + "    blr\n",
    "boost_next": again("osbn_done", "    addi 29, 29, 0x14\n"),
    # fn_1_A3C78: r28 the machine, r6 its voice's volume
    "slide": scaled("oss_send", 28) + "    blr\n",
    "slide_next": again("ossn_done", "    addi 30, 30, 0x14\n"),
}


def asm():
    """The helpers and `othersound_install` (r31 = the control block; called with bl; uses r0, r3-r12 and 32(r1))."""
    return installer("othersound_install", "os", SITES, HELPERS, GUARD)
