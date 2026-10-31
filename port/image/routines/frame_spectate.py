"""The frame routine's spectating (netframe.py assembles the parts in order; its docstring, at +248 and +56, says
what a spectator sees and presses). Four routines, by when they run:

`speccontrols` (its text is frame_speccontrols.py's), on entry of every displayed frame, before the first network
hook: reads this game's pad and makes the spectator's choices. The pad block (.bss 0x998) is memory a rollback puts back, so it is read here, where it
still holds this frame's pad, never after the steps (there, on a frame with a rollback, it held the pad of the
rollback's depth in frames before: presses came late, twice or not at all). Newly pressed is told against +64, in
the control block. A choice is written to the control block only (+252 the racer, memmap.SPEC_MODE, SPEC_HEIGHT,
+68 the HUD): nothing a rollback puts back holds one.

`specstep`, inside every step (called by `manager`, before the game's task manager): writes the choices into the
cameras, so a step run again after a rollback's restore makes them again by itself. The TV cameras' racer (live
camera +0x8) and style (+0x0) and the chase camera's racer and view (camera record +0x2, +0xA4) are written in every
step. What sets a shot up anew is done in one step only, the first step of the frame the choice was made on
(SPEC_CHANGE, taken from the step word when SPEC_PENDING is set): a manual shot's type (live camera +0x12), and the
chase camera's set-up behind another racer (fn_1_5370(0, N): its record made anew, so the state the game camera
took for the racer before, as 5, retired, goes). A rollback to before that step runs it again and the set-up with
it; a rollback to after it restores memory that already holds it. It marks the step (memmap.SPEC_STEP) for
`specwatch`.

The cameras' set-up (`ss_cameras`: the game's fn_1_5370) takes the cameras' tasks out and makes them anew, at the
head of their list, with the flags of [.bss 0xDB0], which is 0 (read in a dump of run cam5-f): without bit 1 the
task manager's pass of a step that is not a frame's last leaves a task out (frame_tasks.py `rt_cam` gives the
cameras' tasks that bit once a step, before `specstep`). So `ss_cameras` gives the two cameras' tasks of list 20
(the chase camera's fn_1_90E0, the TV cameras' fn_1_EE44: `rt_cam`'s own two for that list) the bit itself after
the set-up: the new tasks run in the very step that made them, whichever step of a frame it is.

A manual shot's variant is never outside the variants of the type the camera holds. The TV task fn_1_EE44 sets a
shot up when the wanted type or variant (+0x12, +0x14) differs from the one in use (+0x2, +0x4): the type's set-up
reads the variant in use, the variant's the type in use, and with style 4 neither draws another. While it does so
the task may replace the wanted type by 1 or 3, drawn at random, when the watched racer's section word has
0x01000000 or on a stretch of course 0x0B (0xF0C0-0xF184, 0xF1F8-0xF2BC), and a shot's own update may ask for
another type. The types have 6, 20, 9 and 2 variants: a variant kept through such a change indexed the new type's
table past its end. So in every step of a manual shot:
- the step of the choice writes the type chosen and, if the camera holds another, the variant in use and wanted
  as the height but 1 at most, which every type has; the height itself follows a step later;
- a step that finds a type wanted that is not in use and is not ours (the game's own pick, left by the variant's
  set-up or a shot's update) writes the variant in use and wanted as 1 at most before the type's set-up runs;
- a step that finds our type in use writes the height as the wanted variant (inside that type's count: the
  controls keep the height there);
- a step that finds another type in use, nothing wanted, writes nothing: the game's pick stands. `speccontrols`
  then takes it for the mode on the next frame's entry (SPEC_MODE is the mode in use, the height the variant in
  use), from what that frame shows; the C-stick goes on from there, D-pad down from the mode last asked for
  (SPEC_ASKED), so every mode is at most six presses away whatever the game picks.

`spectate`, after a frame's steps: counts the frames the local seat has been out and, at 180, picks the leader. A
local racer who has finished spectates too, from memmap.FINISH_SPECTATE steps after its finish (FINISH_KIND 1 and
FINISH_END, frame_hud.py `finwatch`, the step word's; never once the results are on): the finish camera has shown its
first round of shots by then and starts the same round again (run fs-m0: shots at 0, 139, 186 and 234 steps after the
finish, the pull-back from 234 and the circling to 733, the first shot again at 734), and the FINISH!, the place and
their sounds are over. Until then its own camera and the settled place stay, as they were. It counts as 180 at once,
takes the HUD's finished bit off (+268: no FINISH! or place over the racer watched; `hudstate` gives it back with the
results, frame_hud.py) and watches the best-placed racer still racing (neither finished nor out), else the leader as
a racer out does. From there it is a racer out's spectating in everything: the controls, the cameras, the HUD.

The game's rank icons (1st, 2nd, 3rd above the machines) are not drawn above the racer the cameras follow
(rankicons.py, fn_1_12999C's two port reads): what the stock game does for the local racer, done for the racer
watched, whichever camera or racer a choice here gives.

A racer who is out is not watched (frame_specwatch.py: `ss_watched`, which `specstep` asks in every step, and
`specwatch`, which asks again once the step's race tasks have run). The racer asked for (+252) is the player's:
only a button changes it. The racer shown is the cameras', memory a rollback puts back.

`spechud`, after a frame's steps: the spectator HUD (port 0 no longer marked retired, the HUD parts' mask), off
with A or behind the chase camera (the game's HUD there is the local seat's, not the watched racer's: off is
honest), and off once the results begin (rule flag 0x1000: frame_hud.py `hudstate` gives an out racer the results
pages there, and the HUD's parts would stand over them), and the tests' log (memmap.SPEC_LOG): one entry a frame
while spectating, round when full
(memmap.SPEC_LOGGED: the next entry's place), which a dump of the data window shows beside the rollback log; only
in a game the host has told to keep the logs (memmap.IMAGE_LOGS: a launch file that asks for a dump). An entry is 8 bytes: the step word's low half, the racer chosen (+252), the mode in
the high four bits with the game's camera mode ([.bss 0xF68]+0x48) in the low, the height chosen (its bit 0x80: the
racer the cameras follow is out at this frame's end; 0x40: the racer asked for is); then what the
cameras hold: with the TV cameras the racer they follow, the type and the variant in use (live camera +0x6, +0x2,
+0x4: +0x6 is what the task took from +0x8, which `specstep` writes), with the chase camera its racer, view and
state (camera record +0x2, +0xA4, +0xA8).

The modes (memmap.SPEC_MODE; D-pad down takes the next, and the height starts at 0): 0 automatic shots (TV style 5,
the game's own after fn_1_5370(1, N)), 1 the course's cameras (TV style 1: the course file's shots where the course
has them), 2 to 5 a manual shot (TV style 4, no picker; type 0 to 3 and the height as its variant), 6 the chase
camera (as racing looks, behind the watched racer).

Never touched: a machine's block, a progress record, the slot's seat, the input rows, the executable's generator."""
import frame_speccontrols
import frame_specwatch
import memmap
import outtest

C = memmap.CONTROL
MODE, HEIGHT, CHANGE, PENDING, LOGGED = (memmap.SPEC_MODE - C, memmap.SPEC_HEIGHT - C, memmap.SPEC_CHANGE - C,
                                         memmap.SPEC_PENDING - C, memmap.SPEC_LOGGED - C)
# each mode's highest height, five bits a mode (mode 6's two bits are the word's last); lis takes a signed half
TOPS = sum((n - 1) << (5 * mode) for mode, n in enumerate(memmap.SPEC_HEIGHTS))
assert TOPS < 1 << 32 and all(n - 1 < 32 for n in memmap.SPEC_HEIGHTS)
assert memmap.SPEC_LOG_ENTRIES < 0x8000 and memmap.SPEC_CHASE == memmap.SPEC_MODES - 1
STICK = 40          # the C-stick's Y (PADStatus +5, signed) from which it counts as pushed up or down
# a finished racer spectates from memmap.FINISH_SPECTATE steps after its finish: FINISH_END is the finish's boundary
# plus the countdown's 480 (frame_hud.py `finwatch`)
FIN_AFTER = memmap.FINISH_SPECTATE - 480
assert 0 < FIN_AFTER < 0x8000


def asm():
    text = _TEXT
    # a machine is out by the game's own verdict, outtest.py: the local seat's (r10), the leader (r3), the racer
    # the cameras follow and the one asked for (r12, the log's)
    for name, m, t1, out, racing in (("OUT_SPECTATE", 10, 7, "sp_out", "sp_racing"),
                                     ("OUT_LEADER", 3, 9, "sp_find_next", "sp_found"),
                                     ("OUT_SHOWN", 12, 10, "sh_log_shown", "sh_log_asked"),
                                     ("OUT_ASKED", 12, 10, "sh_log_out", "sh_log_racing"),
                                     ("OUT_FINISHED", 3, 10, "spf_skip", "spf_racing")):
        text = text.replace(name + "\n", outtest.clause(m, 30, 0, t1, out, racing))
    for name, value in (("S_MODES", memmap.SPEC_MODES), ("S_CHASE", memmap.SPEC_CHASE), ("S_MANUAL", memmap.SPEC_MANUAL),
                        ("S_COURSE", memmap.SPEC_COURSE), ("TOPS_HI", ((TOPS >> 16) ^ 0x8000) - 0x8000), ("TOPS_LO", TOPS & 0xFFFF),
                        ("LOG_HI", memmap.hi(memmap.SPEC_LOG)), ("LOG_LO", memmap.lo(memmap.SPEC_LOG)),
                        ("S_MODE", MODE), ("S_HEIGHT", HEIGHT), ("S_CHANGE", CHANGE), ("S_PENDING", PENDING),
                        ("S_LOGGED", LOGGED), ("LOG_ENTRIES", memmap.SPEC_LOG_ENTRIES), ("STICK_UP", STICK),
                        ("S_STEP", memmap.SPEC_STEP - C), ("S_ASKED", memmap.SPEC_ASKED - C),
                        ("S_LOGS", memmap.IMAGE_LOGS - C), ("FIN_KIND", memmap.FINISH_KIND - C),
                        ("FIN_END", memmap.FINISH_END - C), ("FIN_AFTER", FIN_AFTER),
                        ("STICK_DOWN", -STICK)):
        text = text.replace(name, str(value))
    return text


_TEXT = frame_speccontrols.TEXT + f"""spectate:
    lwz 9, 116(31)
    cmpwi 9, 0
    beqlr
    lha 9, 0x3C2A(30)
    cmpwi 9, 0x29
    bnelr
    lwz 8, 248(31)
    cmpwi 8, 180
    bgelr
    lis 9, 0x8180
    lwz 9, 0x14(9)
    addis 10, 30, 7
    lwz 10, -0x27C8(10)
    mulli 9, 9, 0x{memmap.MACHINE_SIZE:X}
    add 10, 10, 9
    lwz 0, FIN_KIND(31)
    cmpwi 0, 1
    bne sp_test
    lwz 0, 0x3C30(30)
    andi. 0, 0, 0x1000
    bne sp_test
    lwz 0, FIN_END(31)
    lwz 7, 124(31)
    subf 0, 0, 7
    cmpwi 0, FIN_AFTER
    bge spf_on
sp_test:
OUT_SPECTATE
sp_racing:
    li 0, 0
    b sp_count
sp_out:
    addi 0, 8, 1
sp_count:
    stw 0, 248(31)
    cmpwi 0, 180
    bltlr
    li 0, 1
    stw 0, 56(31)
sp_lead:
    addis 5, 30, 7
    lwz 6, -0x27C8(5)
    lbz 5, -0x27B6(5)
    li 4, 0
    mr 3, 6
sp_find:
    lwz 9, 0x{memmap.MACHINE_PROGRESS:X}(3)
    cmpwi 9, 0
    beq sp_find_next
    lbz 9, 0x{memmap.PROGRESS_RANK:X}(9)
    cmpwi 9, 0
    bne sp_find_next
OUT_LEADER
sp_find_next:
    addi 3, 3, 0x{memmap.MACHINE_SIZE:X}
    addi 4, 4, 1
    cmpw 4, 5
    blt sp_find
    li 4, 0
sp_found:
    stw 4, 252(31)
    blr
spf_on:
    li 0, 180
    stw 0, 248(31)
    li 0, 1
    stw 0, 56(31)
    li 0, 0
    stb 0, 268(31)
    addis 5, 30, 7
    lwz 3, -0x27C8(5)
    lbz 5, -0x27B6(5)
    li 4, 0
    li 11, -1
    li 12, 255
spf_loop:
    cmpw 4, 5
    bge spf_end
    lwz 9, 0x{memmap.MACHINE_PROGRESS:X}(3)
    cmpwi 9, 0
    beq spf_skip
    lwz 0, 0(3)
    andis. 0, 0, 1
    bne spf_skip
OUT_FINISHED
spf_racing:
    lbz 0, 0x{memmap.PROGRESS_RANK:X}(9)
    cmplw 0, 12
    bge spf_skip
    mr 12, 0
    mr 11, 4
spf_skip:
    addi 3, 3, 0x{memmap.MACHINE_SIZE:X}
    addi 4, 4, 1
    b spf_loop
spf_end:
    cmpwi 11, 0
    blt sp_lead
    stw 11, 252(31)
    blr
specstep:
    lwz 9, 248(31)
    cmpwi 9, 180
    bltlr
    lha 9, 0x3C2A(30)
    cmpwi 9, 0x29
    bnelr
    lwz 9, 0xF68(30)
    cmpwi 9, 0
    beqlr
    mflr 0
    stw 0, 20(1)
    li 0, 1
    stw 0, S_STEP(31)
    lbz 11, 0x48(9)
    lwz 0, S_PENDING(31)
    cmpwi 0, 0
    beq ss_mode
    li 0, 0
    stw 0, S_PENDING(31)
    lwz 9, 124(31)
    addi 9, 9, 1
    stw 9, S_CHANGE(31)
ss_mode:
    lwz 10, S_MODE(31)
    cmpwi 10, S_CHASE
    beq ss_chase
ss_tv:
    cmpwi 11, 9
    beq ss_tv_on
    lwz 4, 252(31)
    li 3, 1
    bl ss_cameras
ss_tv_on:
    lwz 7, 0xF70(30)
    cmpwi 7, 0
    beq ss_ret
    addi 6, 7, 8
    bl ss_watched
    sth 4, 8(7)
    lwz 10, S_MODE(31)
    li 0, 5
    cmpwi 10, S_COURSE
    blt ss_style
    li 0, 1
    beq ss_style
    li 0, 4
ss_style:
    stb 0, 0(7)
    cmpwi 10, S_MANUAL
    blt ss_ret
    addi 10, 10, -S_MANUAL
    lha 8, 2(7)
    lha 11, 0x12(7)
    lwz 12, S_HEIGHT(31)
    lwz 9, 124(31)
    addi 9, 9, 1
    lwz 0, S_CHANGE(31)
    cmpw 0, 9
    bne ss_hold
    sth 10, 0x12(7)
    cmpw 8, 10
    beq ss_variant
    b ss_low
ss_hold:
    cmpw 8, 11
    bne ss_pending
    cmpw 8, 10
    bne ss_ret
ss_variant:
    sth 12, 0x14(7)
    b ss_ret
ss_pending:
    lha 12, 4(7)
ss_low:
    cmplwi 12, 1
    ble ss_low_set
    li 12, 1
ss_low_set:
    sth 12, 4(7)
    sth 12, 0x14(7)
    b ss_ret
ss_chase:
    lwz 9, 124(31)
    addi 9, 9, 1
    lwz 0, S_CHANGE(31)
    cmpw 0, 9
    beq ss_chase_set
    cmpwi 11, 9
    bne ss_chase_on
ss_chase_set:
    lwz 4, 252(31)
    li 3, 0
    bl ss_cameras
ss_chase_on:
    lwz 9, 0xF6C(30)
    cmpwi 9, 0
    beq ss_ret
    addi 6, 9, 2
    bl ss_watched
    cmpwi 3, 0
    beq ss_chase_racer
    li 3, 0
    bl ss_cameras
    lwz 9, 0xF6C(30)
    cmpwi 9, 0
    beq ss_ret
    addi 6, 9, 2
    bl ss_watched
ss_chase_racer:
    sth 4, 2(9)
    lwz 10, S_HEIGHT(31)
    addi 10, 10, 1
    clrlwi 10, 10, 30      # height 3: view 0, the first person view
    sth 10, 0xA4(9)
ss_ret:
    lwz 0, 20(1)
    mtlr 0
    blr
ss_cameras:
    stwu 1, -16(1)
    mflr 0
    stw 0, 20(1)
    lis 12, 0x8000
    lwz 12, 0x30C8(12)
    addi 12, 12, 0x544C
    mtctr 12
    bctrl
    lis 7, 0x8000
    lwz 7, 0x30C8(7)
    addis 11, 7, 1
    addi 11, 11, -0x6E44
    addis 12, 7, 1
    addi 12, 12, -0x10E0
    lwz 9, 0xDAC(30)
    lwz 10, 0x68(9)
    cmpwi 10, 0
    beq ssc_done
    lwz 10, 8(10)
ssc_next:
    cmpwi 10, 0
    beq ssc_done
    lwz 8, 0(10)
    cmpw 8, 11
    beq ssc_flag
    cmpw 8, 12
    bne ssc_skip
ssc_flag:
    lwz 8, 0x14(10)
    ori 8, 8, 1
    stw 8, 0x14(10)
ssc_skip:
    lwz 10, 8(10)
    b ssc_next
ssc_done:
    lwz 0, 20(1)
    addi 1, 1, 16
    mtlr 0
    blr
""" + frame_specwatch.TEXT + f"""spechud:
    addis 10, 30, -1
    lwz 0, 56(31)
    cmpwi 0, 0
    bne sh_on
    lwz 0, 0xB04(10)
    cmpwi 0, 0
    beqlr
    stw 0, 60(31)
    blr
sh_on:
    addis 9, 30, 2
    lbz 0, 0x6B18(9)
    andi. 0, 0, 0xFE
    stb 0, 0x6B18(9)
    lbz 0, 0x6B19(9)
    andi. 0, 0, 0xFE
    stb 0, 0x6B19(9)
    lbz 0, 0x6B1A(9)
    andi. 0, 0, 0xFE
    stb 0, 0x6B1A(9)
    addis 9, 30, 9
    lbz 0, -0x258(9)
    andi. 0, 0, 0xFB
    stb 0, -0x258(9)
    lwz 8, S_MODE(31)
    lwz 0, 68(31)
    cmpwi 0, 0
    li 0, 0
    bne sh_mask
    lwz 9, 0x3C30(30)
    andi. 9, 9, 0x1000
    bne sh_mask
    cmpwi 8, S_CHASE
    beq sh_mask
    lwz 0, 60(31)
sh_mask:
    stw 0, 0xB04(10)
    lwz 0, S_LOGS(31)
    cmpwi 0, 0
    beqlr
    lwz 9, S_LOGGED(31)
    cmplwi 9, LOG_ENTRIES
    blt sh_log_at
    li 9, 0
sh_log_at:
    addi 0, 9, 1
    stw 0, S_LOGGED(31)
    slwi 9, 9, 3
    lis 11, LOG_HI
    ori 11, 11, LOG_LO
    add 11, 11, 9
    lwz 0, 124(31)
    sth 0, 0(11)
    lwz 0, 252(31)
    stb 0, 2(11)
    slwi 0, 8, 4
    lwz 9, 0xF68(30)
    cmpwi 9, 0
    beq sh_log_mode
    lbz 9, 0x48(9)
    andi. 9, 9, 15
    or 0, 0, 9
sh_log_mode:
    stb 0, 3(11)
    lwz 0, S_HEIGHT(31)
    stb 0, 4(11)
    cmpwi 8, S_CHASE
    beq sh_log_chase
    lwz 9, 0xF70(30)
    cmpwi 9, 0
    beqlr
    lha 0, 2(9)
    stb 0, 6(11)
    lha 0, 4(9)
    stb 0, 7(11)
    lha 12, 6(9)
    b sh_log_racer
sh_log_chase:
    lwz 9, 0xF6C(30)
    cmpwi 9, 0
    beqlr
    lha 0, 0xA4(9)
    stb 0, 6(11)
    lha 0, 0xA8(9)
    stb 0, 7(11)
    lha 12, 2(9)
sh_log_racer:
    stb 12, 5(11)
    addis 9, 30, 7
    lwz 8, -0x27C8(9)
    lbz 9, -0x27B6(9)
    cmpwi 8, 0
    beqlr
    cmplw 12, 9
    bge sh_log_asked
    mulli 12, 12, 0x{memmap.MACHINE_SIZE:X}
    add 12, 8, 12
OUT_SHOWN
sh_log_shown:
    lbz 0, 4(11)
    ori 0, 0, 0x80
    stb 0, 4(11)
sh_log_asked:
    lwz 12, 252(31)
    cmplw 12, 9
    bgelr
    mulli 12, 12, 0x{memmap.MACHINE_SIZE:X}
    add 12, 8, 12
OUT_ASKED
sh_log_racing:
    blr
sh_log_out:
    lbz 0, 4(11)
    ori 0, 0, 0x40
    stb 0, 4(11)
    blr
"""
