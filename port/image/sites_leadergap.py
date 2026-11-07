"""The leader's gap popup starts in the step (race-map rollback G1; port/facts/rollback-state.tsv `leader-gap-pending`).

When second place crosses a lap line, the lap event fn_1_327F8 only sets a pending flag (fn_1_15E434, .bss 0x8FE7C).
The game takes the flag at the next frame's entry (fn_1_15E44C from fn_1_3A330): it clears the flag, marks the leader's
HUD entry (+0 bit 0x10), sets the entry's timer +9 to 60 and adds the timer's countdown task. That entry runs once a
displayed frame, before the frame's restore, so a rollback in that frame took all of it back: the flag on again, the
mark and the timer gone. The next frame started the popup from its beginning, for as long as frames kept rolling back,
and it was drawn only on frames without one.

`leader_gap` (fn_1_327F8 0x32A00, `bl fn_1_15E434`): in a race of ours, the start right after the flag is set, in the
step, as the same event does for the racers behind it (fn_1_15E330, 0x32A24). The popup is then a function of the race's
steps: a step run again starts it again from the state before, and its countdown task is one of the step's (rt_cam).
The flag is never left set, so the frame's entry finds nothing to take and writes nothing. In a start with no race of
ours (the mode word 0) the game's own way stays.

What the start writes is presentation: the HUD entry, the flag and a task of list 13, the writes fn_1_15E330 already
makes in the step; no step's decision reads them, no state checksum covers them. It draws no random number and plays
no sound. The routine replaces a call, so the volatile registers are free there (as sites_modehud.py's `mode_spare`); the
others its mode test and calls use are proved dead after the site."""

SITE, SITE_KNOWN = 0x32A00, 0x4812BA35        # fn_1_327F8: bl fn_1_15E434
PENDING, START = 0x15E434, 0x15E44C           # fn_1_15E434(heap): sets the flag; fn_1_15E44C: takes it, starts the popup


def add(k, r, patch, stock, test, middle):
    """Adds the routine to `r` and the site's entry through `patch`, as sites.add does."""
    site = k.TEXT + SITE
    word = stock(site, SITE_KNOWN)
    r.add("leader_gap", test + f"""    beq lgp_stock
    @bl {k.TEXT + PENDING:#x}
    @bl {k.TEXT + START:#x}
    @b {site + 4:#x}
lgp_stock:
    @bl {k.TEXT + PENDING:#x}
    @b {site + 4:#x}
""")
    patch(site, word, 0, "leader_gap")
    middle.append((site, ["r11", "r12", "cr0", "lr"], "leader_gap"))
