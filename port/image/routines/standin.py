"""Gecko C0 code: a race of 30 CPUs where the player stands in for any one of them.  standin.py [SEAT]
Each frame the C0 body installs `routine` as the per-machine hook (main.rel .bss 0x6D850) wherever the game has its
own rule hook fn_1_331EC there (as puppet.py). The routine, for every machine, after its controls are filled and
before its physics step:
- racer 0 (the human of a Practice race), once: hands it to a fresh CPU driver with the game's own fn_1_8D894 (what
  the game does when a human finishes), so every seat is a CPU;
- at racer 0 (the last machine task of a frame): a seat outside the race becomes 0; D-pad right/left on pad 1 (raw PADStatus, .bss 0x998) moves the
  stand-in seat up/down, and viewport 0's camera record ([.bss 0xF6C] +0x2) follows it, with the HUD (the record
  is left alone while this game spectates behind the chase camera: control block +248 at 180 and memmap.SPEC_MODE
  the chase camera's, when the record is the watched racer's, frame_spectate.py);
- for the stand-in seat: replaces the AI's controls with pad 1's, the way fn_1_83858 builds a human's in control
  mode 1 (floats .bss 0xC70, record .bss 0xA88, newly set bits .bss 0xCC0; side attack |steer| > 0.8 with strength
  0.4 * steer, spin |steer| > 0.2 after 60 frames, boost, the accelerator edge, 0x1000), dropping the AI's requests;
  a spin under way (+0x190, the rotation left, non-zero) keeps its flag 0x8: the flag is the spin itself, which the
  machine's spin state (fn_1_287E8) clears when it ends and aborts if the flag goes before (the routine cleared it
  every frame, so a spin stopped the moment the spin button was let go);
- then calls fn_1_331EC as usual (countdown and finish rules apply).
It also replaces the race-over test (.bss 0x3C30+0xCC, normally fn_1_33890, which ends the race when the humans
finish) with `over`: the race ends when the last machine has finished (0x10000) or is out (0x80), and with the
race block's flow word when every player seat has finished or is out by the game's verdict (raceend.py); it keeps the
original's guards (.bss 0x25BA0 set, rule flag 0x800) and its time limit (rule flag 0x04000000 clear and the race
clock .bss 0x50A4 past 0x57030: rule flag 0x80000, result -1). In a running session it says "over" only when every
step so far ran with the official inputs, and asks the host to check them otherwise (standin_race.py; the time
limit's answer is not held so). With the host's rule in the race block (memmap.RACE_FINISH_LIMIT) the race also ends
for everyone so many steps after the first finish, and the results wait for the last player's "FINISH!" (raceend.py).
Slot 'FZSI': +4 seat, +8 enabled, +12 previous buttons, +16/+20/+24 the constants 0.8, 0.2, 0.4, +28 handovers,
+32 the race frame (.bss 0x50A4) from which the stand-in stops driving and every seat is the AI's (0: never),
+40 (uniform only) the player seats: bit i = seat i; 0 = the
stand-in seat while the stand-in drives, else every seat (a race of bots only). After the ranking pass, through a wrapper of the
per-racer post-contact hook (.bss 0x6D970, normally fn_1_32A40), the best and worst rank among these seats and
their machines are written to .bss 0x6DA7A..0x6DA7D, the rubber band's references (otherwise port 0's machine). Only
the fork's image (the JIT reference) still paces its CPUs against them: in the native form the rubber band is off
in a race (sites_cpu.py: the CPU pre-pass calls none of the getters that read them), so there the
mask means the player seats only for the machine setting and the input buffer below;
+36 uniform: when set, every machine's flag 0x200 (CPU damage, contact and immunity rules) is cleared each frame,
so all 30 seats race under the human rules while the AI machinery (0x04000000) keeps running on every seat (and every
machine that is destroyed (+0x58C bit 0x10, fn_1_25004: removed from contact) or has come to rest after breaking down
(+0x58C bit 0x80, set by fn_1_24A4C), and has not finished, gets the retired flag 0x08000000, as a CPU's breakdown
gives it in fn_1_24A4C: the HUD's racer count, "/30", counts the machines without it, and on the
human rules it never came, so the count stayed; set in a pass over all machines from each machine's call, since a
destroyed machine's own control task no longer runs. Not at the breakdown (flag 0x80): a human machine out of
energy coasts on, bouncing off walls, and may still cross the line; with the retired flag from the breakdown on, it
stopped dead. With the race block's flow word, memmap.RACE_FLOW, the flag goes by the game's own verdict instead,
outtest.py: never under the Restore rule, and at rest after a breakdown on the human rules too); the
player seats (as +40) get the machine setting at full top speed (100 in the per-seat table .bss 0x25C48, read by the
rule hook fn_1_331EC) and the other seats 50, which leaves a machine's stock stats unchanged (what CPUs race with);
in a race the Dolphin fork booted (its race block, memmap.RACE 'FZRC', is there) every seat gets the match's setting
instead (memmap.RACE_SETTINGS: the host's, or the player's choice). During the rule hook call each seat's player record pointer (.bss 0x53F8) and its
flag 0x04000000 are hidden, so that the hook treats every seat as it treats a human: the setting comes from the
table and the machine's stats are rebuilt from it each frame (fn_1_82EDC, fn_1_22B10, fn_1_22E8C).
In a network session (control block +116) the D-pad no longer moves the stand-in seat: the local seat is the
player's, and the D-pad is the spectator's (netframe.py), except while control block +0xA38 is set: a replay played
as the race (the Dolphin fork, Core.FzgxPlayMode "race") lets D-pad right/left move the stand-in seat, so the game's
own camera and the HUD follow any racer; every seat's controls there come from the recording, so only the view
changes. Test knockout (control block, set alike in every game):
+240 seat + 1 (0: none; 31: every seat but seat 0, held at 1.0 unless broken down), +244 race frame from which
that machine's energy (+0x184) is held at 0, so the next hit
destroys it, the same way everywhere (it is simulation state; ko.py did this from outside, for one game only).
Input buffer (the netcode's): player seats (REFMASK's seats, or the stand-in seat) take their controls for race frame
f (.bss 0x50A4) from a ring at memmap.INPUTS (0x81820000): 1024 frames x 30 seats x 8 bytes, entry (f & 1023) * 30 + seat,
each the game's own 6-byte control record (as the pad layer's .bss 0xA88 and replays: byte 0 bits, steer, pitch,
strafe, accelerator, brake). The record is turned into the five control floats exactly as the pad layer does
(int -> double with the 0x43300000 bias, fsubs, fdivs by 100; signed axes, unsigned pedals), then mapped as
fn_1_83858 maps a human in control mode 1; "newly pressed" bits come from frame f-1's record. The local seat (the
stand-in seat) first copies port 0's record (.bss 0xA88) to frame f + DELAY; other seats' records are written by the
network side. No capture while the netcode re-simulates (netcode/netframe.py's flag, control block +12). +44 delay, +48 100.0, +56/+64 the int->double biases.
In a network session (control block +116, set by the fork's network layer) the index is the step counted from the
relay's "go" (+124) once running (+120), and before that entry 1023 (all zero then: neutral controls on every player
seat until the race starts the same way everywhere). In a session the routine captures nothing: the fork reads the
local pad (.bss 0xA88) itself once per displayed frame, so a rollback's re-simulated steps cannot capture it.
A record whose steering byte is 0x80 (-128, outside a stick's -100..100) means "the AI drives": the seat keeps the
AI's own controls for that step (the player rules still apply). So does a machine that has finished (0x10000), as the
game hands a human's machine to a CPU driver at the finish (fn_1_8D894): from the step it crosses the line, the same
in every game, the AI drives it on, whatever its player's controller does. Tests drive every seat this way and give random
seats bursts of real inputs, so the races are real and the predictions still miss.
standin.py [SEAT] [OFF] [UNIFORM] [REFMASK] [DELAY]"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import memmap
from keystone import Ks, KS_ARCH_PPC, KS_MODE_PPC32, KS_MODE_BIG_ENDIAN
args = [a for a in sys.argv[1:] if not a.startswith("--")]
sys.argv = [sys.argv[0]] + args + [a for a in sys.argv[1:] if a.startswith("--")]
seat = int(sys.argv[1]) if len(args) > 0 else 0
off = int(sys.argv[2]) if len(args) > 1 else 0   # race frame from which the stand-in stops driving (0: never)
uniform = int(sys.argv[3]) if len(args) > 2 else 0   # 1: every seat gets the human damage and contact rules
refmask = int(sys.argv[4], 0) if len(args) > 3 else 0  # rubber-band reference seats (bit i = seat i; 0 = players)
delay = int(sys.argv[5]) if len(args) > 4 else 0       # input delay (frames) for the local seat's pad
parts = "--parts" in sys.argv   # for the mod image (port/image/routines/modbuild.py): the routines and the installer's offsets
import standin_routine, standin_inputs, standin_race
asm = standin_routine.asm(seat, off, uniform, refmask, delay) + standin_inputs.asm() + standin_race.asm()
ks = Ks(KS_ARCH_PPC, KS_MODE_PPC32 + KS_MODE_BIG_ENDIAN)
fill = lambda a, o, r: a.replace("OVER_OFF", str(o)).replace("REFS_OFF", str(r)).replace("REFS_HERE", str(r + 24))
probe = bytes(ks.asm(fill(asm, 0, 0))[0])
over_off = probe.index(bytes.fromhex("3D808000818C30C83D6C001C396BA04038600000")) - 8   # `over` relative to data
refs_off = probe.index(bytes.fromhex("9421FFF07C0802A6900100149" + "3E1000C7C7F1B78")) - 8  # `refs` relative to data
code = bytes(ks.asm(fill(asm, over_off, refs_off))[0])
assert code.index(bytes.fromhex("3D808000818C30C83D6C001C396BA04038600000")) - 8 == over_off
assert code[8:12] == b"FZSI", code[:16].hex()            # data at +8 (8-aligned for the doubles), routine at +72
assert code[8 + 108:8 + 112] == bytes.fromhex("7FC802A6"), code[8 + 100:8 + 116].hex()  # mflr 30 at r_here = data + 108
assert code[8 + 56:8 + 72] == bytes.fromhex("43300000800000004330000000000000")
assert code[8 + refs_off + 24:8 + refs_off + 28] == bytes.fromhex("7D8802A6")          # mflr 12 at refs_here
assert 0x32A40 + 0xDC == 0x30000 + 0x2B1C and 0x6D970 == 0x70000 - 0x2690 and 0x6DA7A == 0x70000 - 0x2586
assert 0x8D894 + 0xDC == 0x90000 - 0x2690                # fn_1_8D894 = header + .text (0xDC) + 0x8D894
assert 0x33890 + 0xDC == 0x30000 + 0x396C                # fn_1_33890, the original race-over test
assert 0x3C30 + 0xCC == 0x3CFC and 0x3C30 + 0x1474 == 0x50A4
if parts:
    import json
    inst = code.index(bytes.fromhex("7D2802A67C0803A6"))     # after_data: mflr 9; mtlr 0
    print(json.dumps({"image": code[8:inst].hex(), "installer": code[inst + 8:].hex(), "routine": 72,
                      "over": over_off, "refs": refs_off}))
    sys.exit(0)
if len(code) % 8: code += bytes.fromhex("60000000")
words = [code[i:i + 4].hex().upper() for i in range(0, len(code), 4)]
print("\n".join([f"C0000000 {len(words) // 2:08X}"] + [f"{words[i]} {words[i + 1]}" for i in range(0, len(words), 2)]))
