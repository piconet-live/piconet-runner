"""How a name tag (nametags.py) fades: it never pops in or out.

A seat's tag state (memmap.TAGS +0x10 + 16 x seat, +0 the fade) moves by a fixed step a drawn frame: up by
1 / FADE_IN (12 drawn frames, about 200 ms at 60) while the tag is chosen and placed, down by 1 / FADE_OUT (about
150 ms) when it is not chosen, left out by the overlap rule or off screen, and down by 1 / FADE_LEAVE (about 100 ms)
when its seat left the race (finished, out, or driven by the AI). The tag is drawn with alpha ease(fade) x the
distance fade, ease(t) = t (2 - t): an ease-out, fast at first and soft at the end; the first frame of a fade in
is drawn at 0.16. Only a seat that cannot be drawn at all (behind the camera, past FAR, where the distance fade is 0
already, the followed racer, no name) loses its state at once.

The state starts afresh (every seat's fade and flags 0) when the picture is not continuous with the last drawn
frame: the running word (control block +120) changed, the step word (+124) went back or moved more than JUMP steps
(a new race, a replay's seek), or the followed racer changed (a camera switch). So the first frame of a race, or of
a seek, fades in from 0 like any other. The header's +4, +8 and +12 keep the running word, the step word and the
followed racer seen last."""

FADE_IN, FADE_OUT, FADE_LEAVE = 12, 9, 6   # drawn frames
JUMP = 60                                  # steps


def fresh(state, followed):
    """Zero every seat's state when the picture is not continuous (state = memmap.TAGS, r31 = the control block).
    Uses r0, r9 to r12."""
    return f"""    lwz 9, 120(31)
    lwz 10, 124(31)
    lwz 11, 4({state})
    lwz 12, 8({state})
    lwz 0, 12({state})
    stw 9, 4({state})
    stw 10, 8({state})
    stw {followed}, 12({state})
    cmpw 9, 11
    bne fresh
    cmpw 0, {followed}
    bne fresh
    subf 10, 12, 10
    cmplwi 10, {JUMP}
    ble kept
fresh:
    li 0, 0
    mr 9, {state}
    li 10, 0
f_seat:
    stw 0, 16(9)
    stw 0, 24(9)
    addi 9, 9, 16
    addi 10, 10, 1
    cmpwi 10, 30
    blt f_seat
kept:
"""


def down(step, seat, scratch, consts, out, leave):
    """f`step` = the fade-out step: the leaving one when the candidate's seat word has 0x400 (left the race)."""
    return f"""    lfs {step}, {out}({consts})
    andi. {scratch}, {seat}, 0x400
    beq d_step
    lfs {step}, {leave}({consts})
d_step:
"""


def ease(fade, scratch, consts, two):
    """f`fade` = ease(f`fade`) = fade x (2 - fade)."""
    return f"""    lfs {scratch}, {two}({consts})
    fsubs {scratch}, {scratch}, {fade}
    fmuls {fade}, {fade}, {scratch}
"""


def gone(leaving, entry, consts):
    """A seat that is leaving (r`leaving` not 0) and whose tag has faded out is no candidate: to c_not."""
    return f"""    cmpwi {leaving}, 0
    beq c_record
    lfs 0, 0({entry})
    lfs 6, 20({consts})
    fcmpu 0, 0, 6
    ble c_not
c_record:
"""


def leaving(leaving, seat, record, consts):
    """A leaving seat's candidate: seat word | r`leaving` << 9 (0x200 off screen, 0x400 left the race), a score no
    choice takes; to c_score. Others go on at c_chosen."""
    return f"""    cmpwi {leaving}, 0
    beq c_chosen
    slwi 0, {leaving}, 9
    or 0, 0, {seat}
    stw 0, 16({record})
    lfs 6, 76({consts})
    b c_score
c_chosen:
"""
