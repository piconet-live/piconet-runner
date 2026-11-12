"""The lap-time list's scroll counts down in the step (port/facts/rollback-state.tsv `lap-list-scroll-*`).

Once the list holds more than four laps, fn_1_CB028 (the earlier laps' rows, from the one-viewport layouts fn_1_C72D4
and fn_1_C771C) scrolls it: each port's entry at .bss 0x7ACA0 (12 bytes a port) has at +8 a countdown that the lap
event fn_1_327F8 sets to 15 (fn_1_CB404, a step), and the drawing draws the top row squeezed to +8/15 and every row
under it moved up by 22 x +8/15, then counts +8 down (0xCB1DC), once a displayed frame. That entry is the race's memory,
put back by every rollback: the drawing's count was taken back with it, so under rollbacks it stood still or went back
by the rollback's depth, and the rows were drawn at another height on every frame (the lap list shook from the fifth
lap on, as long as frames kept rolling back).

`lap_scroll` (fn_1_CB028 0xCB1DC, the countdown's `stb r0, 8(r30)`): in a race of ours the drawing leaves the count
alone; the step wrapper counts it down instead, once a step, before the task manager (frame_replay.py `lapscroll`), so
a lap's 15 is drawn first as in the stock game and the count is a function of the race's steps: a step run again counts
from the state before. In a plain start (the mode word 0) the game's own way stays.

The count is presentation: only fn_1_CB028 reads it; no step's decision reads it, no state checksum covers it. The site
replaces a store whose value the routine skips, so r12 and cr0 are free there (proved dead after the site)."""

from frame_replay import LAP_ENTRIES as ENTRIES, LAP_ENTRY as ENTRY, LAP_COUNT as COUNT, LAP_PORTS as PORTS

SITE, SITE_KNOWN = 0xCB1DC, 0x981E0008        # fn_1_CB028: stb r0, 0x8(r30), the count's store
SET, START = 0xCB404, 15                      # fn_1_CB404: the lap event's start of the count, and what it sets


def add(k, r, patch, stock, test, middle):
    """Adds the routine to `r` and the site's entry through `patch`, as sites.add does."""
    site = k.TEXT + SITE
    word = stock(site, SITE_KNOWN)
    r.add("lap_scroll", test + f"""    bne lsc_ours
    .long {word:#010x}
lsc_ours:
    @b {site + 4:#x}
""")
    patch(site, word, 0, "lap_scroll")
    middle.append((site, ["r12", "cr0"], "lap_scroll"))
