"""The simulation's own generator (race-map gap G5). The executable's generator word 0x801A63C0 is one LCG
(x * 0x676A4B6B + 0x33CB, inlined at every draw) that the whole game draws from: the TV cameras' shots, the
effects' spawners, the course scenery, the course cameras, the HUD, the starter's drones, the results, and two
draws whose value the race itself reads. Effects and cameras are each game's own (a game's own viewport, the
racer its cameras follow, a spectator's choices, the effects pool's state), so from the first draw one game made
and another did not, the word is each game's own too (measured 2026-10-30: two games of one race parted at step
2361 on course 16, at the effects' fn_1_6D7BC). A draw the race reads from it then differs between games.

The draws the race reads (every reference to the word in main.rel, 234 in 64 routines, read for what the value
is written to; no other module that runs in a race, nor the executable, draws from it):

| Site | Where | What the value decides |
|---|---|---|
| `simgen_cpu` | fn_1_12F4A8 0x13013C (the computer drivers' scripted driver, `fn_1_C4ABC` 0xC4CEC; its modes 17 and 18) | whether the driver sets flag 0x8 (spin) in its CPU block, which its machine's controls take |
| `simgen_hazard` | fn_1_DE4B0 0xDE5A0 (the hazard objects' pass fn_1_E3C38, PhysObjCalc fn_1_E49D4: courses 37, 38, 41) | a hazard object's choice, which pushes and damages machines |

Each site is the draw's first instruction, `lis r4, 0x801A` (the word's high half; its low half 0x63C0 is the
draw's next instructions' offset). The routine gives r4 the high half plus the shift word
(memmap.SIM_GENERATOR_SHIFT): 0, the stock word, in a plain start and in every race whose race block says 0 (a
replay recorded before the fix); in a race with memmap.RACE_SIM_GENERATOR 1 (every race and practice from this
build, and their replays: the core reads the replay header's "simGenerator") the frame routine's step wrapper
(frame_replay.py `simgen`) makes it SIM_GENERATOR - 0x801A63C0, so the draw reads and steps SIM_GENERATOR, a word
of the data window no other code draws from. The wrapper seeds it before every step from the step and the race's
CPU seed (race block +28), so a step run again after a rollback, in any game and in a playback, starts from the
same value, and only the race's own draws step it within the step: no rollback has to put it back.

Every other draw stays on 0x801A63C0, which keeps looking random to the effects and cameras and is put back by a
rollback as before. Registers: each routine writes r4 only, as the replaced `lis r4` did. Native form only: the
fork's image does not have it. sites.py calls add()."""
import memmap

SITES = (
    ("simgen_cpu", 0x13013C),     # fn_1_12F4A8: lis r4, 0x801A (the scripted driver's random flag 0x8)
    ("simgen_hazard", 0xDE5A0),   # fn_1_DE4B0: lis r4, 0x801A (the hazard objects' pass)
)
KNOWN = 0x3C80801A                # lis r4, 0x801A: the high half of 0x801A63C0 (its low half 0x63C0 is positive)
SHIFT = memmap.SIM_GENERATOR_SHIFT
assert memmap.EXECUTABLE_GENERATOR == 0x801A63C0 and memmap.lo(SHIFT) < 0x8000


def add(k, r, patch, stock, test, middle):
    """Adds the routines to `r` and an entry for each site through `patch`, as sites.add does."""
    for name, offset in SITES:
        site = k.TEXT + offset
        word = stock(site, KNOWN)
        r.add(name, f"""    lis 4, {memmap.hi(SHIFT)}
    lwz 4, {memmap.lo(SHIFT)}(4)
    addis 4, 4, -0x7FE6
    @b {site + 4:#x}
""")
        patch(site, word, 0, name)
