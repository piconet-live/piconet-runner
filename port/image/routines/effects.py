"""The effect update's race-state write, made independent of the camera (for the frame routine, netframe.py, which
includes this code and calls `effects_install` on entry every frame).

The effect update of a machine's car effect (type 1, fn_1_68284) clears bits 0x00200000 and 0x01000000 of the
machine's car object word +0x390 ([.bss 0x6D83C] + i * 0x440) while the machine is not broken down (0x685F8-0x68614);
the contact task reads 0x00200000 to destroy a machine of a colliding pair (0x8518C), and fn_1_24A4C sets it when a
machine falls out. The clear runs only past a camera check (fn_1_66B8 at 0x6855C: in view, or the local port's
machine), so each game cleared it for other machines than the others did. Since the effect update now runs in every
step (netframe.py), the helper at 0x68554 (in place of `lfs f1, 0x10(r29)`, which it then runs) does the clear for
every machine under the update's own condition (r25 == 0 and flag 0x80 clear in r31), before the camera check; the
clear after the check then finds the bits already clear. Control block +512: the main.rel address the site was
patched for (follow.installer)."""
from follow import installer

# (text offset, original word, mask, helper)
SITES = [(0x68554, 0xC03D0010, 0xFFFFFFFF, "clear")]

HELPERS = {
    "clear": """    cmpwi 25, 0
    bne ec_skip
    andi. 0, 31, 0x80
    bne ec_skip
    lwz 12, 0x390(30)
    rlwinm 12, 12, 0, 11, 9
    rlwinm 12, 12, 0, 8, 6
    stw 12, 0x390(30)
ec_skip:
    lfs 1, 0x10(29)
    blr
""",
}


def asm():
    """The helper and `effects_install` (r31 = the control block; called with bl; uses r0, r3-r12 and 32(r1))."""
    return installer("effects_install", "e", SITES, HELPERS, 512)
