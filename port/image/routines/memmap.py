"""The mod's memory: the fork's RAM above the game's 24 MiB (0x81800000 on; dolphin-fzgx maps it for the mod and
never tells the game). One place for every address the builders and the harness use."""
IMAGE = 0x81800000        # mod image (modbuild.py), up to 64 KiB
CONTROL = 0x81810000      # control block (netframe.py): park target, extra steps, flags, gate, timings, counters
CHECKSUMS = 0x81811000    # checksum ring: 1024 x u32, entry race frame & 1023
SMOOTH = 0x81812000       # rivals' drawn offsets (smooth.py): 30 x 48 bytes, globals at +0x600
EFFECTS = SMOOTH + 0x640  # tests: the effects' counters (sites_effects.py), 304 bytes
NAMES = 0x81813000        # player names (playernames.py): 30 x 32 bytes, NUL-terminated, seat order; empty: the pilot's
NAME_SIZE = 32
TAGS = NAMES + 0x400       # the name tags' state (nametags.py): a frame count, then 30 x 16 bytes; the picture's only
# The rejoin hand-back's 3, 2, 1 on the start's floating monitors (monitors.py; port/image/sites_monitors.py reads
# the pictures). The picture's only: no step reads it, no rollback puts it back, the checksum does not cover it.
# The host's counter is HANDBACK (steps left, below). +0 the step at which control comes back (control block +124
# plus HANDBACK, worked out while HANDBACK runs and kept, so that GO stays up after it reaches 0). +4 the pictures
# (sites_monitors.py, the gate's black frame): 0 not tried, 1 trying, 2 read, 3 not read (no room, no file). +8
# their block in the game's main heap (MONITORS_BLOCK bytes: the texture, the decoder's work, five JPEGs). +12 the
# five JPEGs' sizes. +32 the hand-back the words below are for (+0 when they were set). +36 the picture in the
# texture, plus 1 (0 none). +40 the countdown at the last drawn frame (the sounds' edge). +44 the texture's words as
# the last decode left them (a rollback can put the texture back). +48 the followed racer + 1 and +52 the step word
# the eased frame is for (monitors_ease.py). +0x40 the monitors' record (0x88 bytes, the game's own layout: the point,
# the eased turn, the tilt, the yaw). +0xC8 the eased frame: position, back, up (36 bytes, floats; to +0xEC).
MONITORS = TAGS + 0x200
MONITORS_BLOCK = 0x19000
# Mines that come back (sites_mines.py; the host's rule RACE_MINE_REGEN): +0 the state of the countdowns' block (0 not
# tried, 1 taken, 3 none: the rule is 0 or the heap had no room), +4 its address in the game's main heap (0 none).
# Written once, on the black frame before the start gate, and never in the race: no step writes it, and the block it
# names is the undo log's like the rest of the heap. The block (MINE_BLOCK bytes): +0 the mines the per-step pass met
# with the rule (it changes in every step, so the undo log watches its page from the race's first step), +4 the mines
# that went off, +8 the mines that came back, +16 a countdown for each of the first MINE_TABLE mines of the course's
# mine walk (u16, the steps left until it is armed again).
MINES = MONITORS + 0x100
MINE_TABLE = 64
MINE_COUNTS = 16
MINE_BLOCK = MINE_COUNTS + 2 * MINE_TABLE
# The race's setup (written by the Dolphin fork from the match file before the game reaches the race; the built-in
# codes read it, boot-to-race/racecodes.py): 'FZRC', then words: +4 course, +8 port 0's machine, +12 CPU opponents
# (racers - 1), +16 laps, +20 race type (6 laps, 3 free run), +24 the DOL generator's value up to the race (SHUFFLE),
# +28 the per-race CPU seed; +32 the 30 seats' machines (bytes, seat 0 first); +64 the start grid's word (1: the race
# names its grid, sites_grid.py; 0: the game's own sort, every recording made before it), +68 the grid (30 bytes, the
# seat of each slot, pole first); +98 and +99 unused, +100 the rubber band (RACE_RUBBER_BAND_ON, below), +104 to +256 unused; the race's rules: +256 the CPU level (0-3), +260 boost from the start (the fork
# sets rule flag 0x10 through control block +520), +264 restore (words), +268 every seat's machine setting (30 bytes,
# 0 acceleration to 100 top speed; standin.py), +300 the steps after the first finish at which the race ends for
# everyone (a word; 0: no such end; raceend.py); +304 the race's flow (a word: 1, a machine is out by the game's own
# verdict, outtest.py, and the race waits for the player seats alone, raceend.py; 0, every recording made before
# them: the masks of before); +308 the steps after which a mine that went off comes back (a word, 0 to 300;
# 0: the game's own way, a mine stays gone, and every recording made before the rule;
# sites_mines.py); +320 the seats' custom machines (RACE_CUSTOM, below); to +1280.
RACE = 0x81814000
RACE_COURSE, RACE_MACHINE0, RACE_OPPONENTS, RACE_LAPS, RACE_TYPE, RACE_SHUFFLE, RACE_CPUSEED, RACE_MACHINES = (
    4, 8, 12, 16, 20, 24, 28, 32)
RACE_CPULEVEL, RACE_BOOST, RACE_RESTORE, RACE_SETTINGS = 256, 260, 264, 268
RACE_FINISH_LIMIT = 300
RACE_FLOW = 304
RACE_MINE_REGEN = 308
RACE_GRID_ON, RACE_GRID = 64, 68
# +100 the rubber band (a word: 1, the CPU drivers pace against the player seats, the game's own way, every recording
# made before 0.4.0; 0, every driver at its own pace, every race and practice from 0.4.0; sites_cpu.py)
RACE_RUBBER_BAND_ON = 100
# The seats' custom machines (garage-built: three parts and a pilot; sites_custom.py): 30 entries of 32 bytes, seat
# order. +0 a word, not 0 when the seat races its own parts (0xC0000000: a garage record's "used, parts" bits); +4,
# +12, +20 the body's, cockpit's and booster's part id (bytes: 0-24, 25-49, 50-74) and +8, +16, +24 each part's
# colour word (red, green, blue, the material byte), laid out as a garage record's three part entries (.bss 0x5480,
# record +0x81A4); +28 the pilot (a byte: the stock machine 0-40 whose pilot drives it, as a record's +0x81A0). The
# game reads an entry through a record's address made up for it (CUSTOM_RECORD: entry + 4 - 0x81A4).
RACE_CUSTOM = 320
CUSTOM_ENTRY = 32
CUSTOM_ID = 0x32          # the machine id a custom seat races with (the game's own for garage record 0)
CUSTOM_RECORD = 0x81A4 - 4
RACE_SIZE = RACE_CUSTOM + 30 * CUSTOM_ENTRY
assert RACE_SETTINGS + 30 <= RACE_FINISH_LIMIT and RACE_FINISH_LIMIT + 4 <= RACE_FLOW and RACE_FLOW + 4 <= RACE_MINE_REGEN
assert RACE_MINE_REGEN + 4 <= RACE_CUSTOM
assert RACE_MACHINES + 30 <= RACE_GRID_ON and RACE_GRID_ON + 4 <= RACE_GRID and RACE_GRID + 30 <= RACE_CPULEVEL
assert RACE_GRID + 30 <= RACE_RUBBER_BAND_ON and RACE_RUBBER_BAND_ON + 4 <= RACE_CPULEVEL
# The race's end by that rule (raceend.py, which says how the two were measured): the steps the results wait after
# the last player seat's finish, the game's own wait after a finish (fn_1_32A40 sets its countdown to 480); and what
# the race clock (.bss 0x50A4) is ahead of a machine's finish time (its progress record's +0x1D8, frames) at the
# step boundary after the step that finished it.
RESULTS_WAIT = 480
CLOCK_PAST_FINISH = 2
INPUTS = 0x81820000       # input buffer: 1024 frames x 30 seats x 8 bytes (0x3C000)
INPUTS_END = INPUTS + 1024 * 30 * 8
assert IMAGE + 0x10000 <= CONTROL and CONTROL + 0x1000 <= CHECKSUMS and CHECKSUMS + 0x1000 <= SMOOTH
assert SMOOTH + 0x1000 <= NAMES and NAMES + 30 * NAME_SIZE <= TAGS and TAGS + 0x200 <= MONITORS and MONITORS + 0x100 <= RACE and RACE + RACE_SIZE <= INPUTS
assert MONITORS + 0x100 <= MINES and MINES + 8 <= RACE
# Tests: the rank icons' exclusion (follow.py `icon`), counted only in a game told to keep the logs (IMAGE_LOGS):
# 30 words a racer, the drawn frames in which its icon was held back (the racer the HUD follows, or the local seat),
# then 30 words, the frames in which it was let through to the icon's own tests. The picture's only: no step reads
# them, no rollback puts them back, the checksum does not cover them.
ICONS = MINES + 0x10
assert MINES + 8 <= ICONS and ICONS + 2 * 30 * 4 <= RACE
# Each seat's own stats and constants (a stat pack a seat: sites_seats.py, sites_dash.py, sites_curve.py; the core writes
# the block for every race and replay, handbook/stat-pack-constants.md). +0 a byte a seat: not 0, the seat's stock
# machine takes its stats from its row below in place of the car data's block (racer creation and every step);
# +SEAT_SETTING a word, the constants record of the seat whose machine setting runs now (sites_curve.py), else 0;
# +SEAT_ROWS a row a seat of the car data's block size (0xB4), as the loaded table holds a block; +SEAT_CONSTANTS a
# record a seat of SEAT_CONSTANT bytes: +0 the dash plate's A, +4 its B, +8 a boost frame's energy use, +12 the boost
# length's base (floats), +16 the speed cap (a double), +24 the boost length's scale, +28 the machine setting's curve
# (a float a slot, slot order), +0xF0 its one double.
SEAT_BLOCK = INPUTS_END
SEAT_SETTING = 0x20
SEAT_ROWS = 0x300
SEAT_ROW = 0xB4
SEAT_CONSTANTS = 0x2000
SEAT_CONSTANT = 0x100
SEAT_BLOCK_SIZE = 0x4000
SEAT_DASH_BASE = 0
SEAT_DASH_SCALE = 4
SEAT_ENERGY_USE = 8
SEAT_BOOST_BASE = 12
SEAT_SPEED_CAP = 16
SEAT_BOOST_SCALE = 24
SEAT_CURVE = 28
SEAT_CURVE_DOUBLE = 0xF0
assert SEAT_SETTING >= 30 and SEAT_SETTING + 4 <= SEAT_ROWS and SEAT_ROWS + 30 * SEAT_ROW <= SEAT_CONSTANTS
assert SEAT_CONSTANTS + 30 * SEAT_CONSTANT <= SEAT_BLOCK_SIZE and SEAT_CURVE_DOUBLE % 8 == 0 and SEAT_CURVE_DOUBLE + 8 <= SEAT_CONSTANT
assert SEAT_BLOCK + SEAT_BLOCK_SIZE <= 0x82000000   # inside the 8 MiB above 24 MiB with MEM1Size 32 MiB
# The native form (the recompiled game on its retail 24 MiB). Nothing is behind these
# addresses: the image is native code, the data from IMAGE to SEAT_BLOCK + SEAT_BLOCK_SIZE a buffer of the host program.
MODE = CONTROL + 0xF00    # what the game was started for: 0 a plain start (every patched site does what the stock game
                          # does), 1 a race (a match file, or a replay played as the race). Written by the host before
                          # the game boots.
PRELOAD = CONTROL + 0xF10  # the results screen's pictures, read before the race (port/image/build.py, results_preload):
                          # +0 state (0 not tried, 1 trying, 2 read, 3 not read: no room or no file), then three pairs
                          # (bitmap group, the packed file's address in the game's heap or 0), +28 loads served from
                          # memory, +32 the main heap's largest free block when it was tried (bytes)
# The race's end in a session (standin_race.py `over`, frame_replay.py `endtest`; the host: port/core step.cpp). The
# game's race-over test runs once a displayed frame, outside the steps, and what it starts (the results phase) cannot
# be simulated again: it may only say "over" when every step it rests on ran with the official inputs.
END_OFFICIAL = CONTROL + 0xF40   # the host's: every step below this one ran, or runs in this frame, with the official
                                 # inputs (0: a host that does not say; the race then ends as it did before these words)
END_ASKED = CONTROL + 0xF44      # the image's: 1, every machine has finished or is out and a step below the one about
                                 # to run is not official yet; the host's next frame waits for those steps' official
                                 # inputs, runs no new step, and clears it
END_OVER = CONTROL + 0xF48       # the image's: 1, the race-over test said "over" (on official steps only): the host
                                 # predicts no step from here on
HANDBACK = CONTROL + 0xF4C       # the host's: steps left until a returning seat's player drives it again (the hand-back
                                 # after a rejoin), counting down to 0 at that step; 0: none.
                                 # For the picture only (the countdown monitors): no step reads it
# The local racer's finish (frame_hud.py `finwatch`; follow.py's countdown sites read the third). The image's own, for
# the picture only: no step reads them, no rollback puts them back, the checksum does not cover them.
FINISH_KIND = CONTROL + 0xF50    # what the local machine is, at the last step boundary looked at: 0 racing, 1 finished,
                                 # 2 out. A rollback that takes the finish back makes it 0 again
FINISH_END = CONTROL + 0xF54     # the step word (+124) at which the local countdown reaches 0: the boundary after the
                                 # step that finished the machine + 480, or that put it out + 300
FINISH_COUNT = CONTROL + 0xF58   # what the picture reads in place of the game's countdown (.bss 0x50EC): the local
                                 # countdown, 0 once the results are on; the game's own while FINISH_KIND is 0;
                                 # run out before the results: 0 when out, 0xFFFF ("not started") when finished
# The racer the results' cameras follow (follow.py, the results camera's site; for the picture only, no step reads
# either word, no rollback puts them back, the checksum does not cover them).
RESULTS_KEEP = CONTROL + 0xF5C   # the host's: not 0, the results' cameras follow the local seat whatever it is (a
                                 # spectator's game: its seat is a racer it watches, not its own)
RESULTS_CAMERA = CONTROL + 0xF60  # the image's, written at the results' start and when results_watch moves the
                                 # cameras: 0x80000000 | why << 8 | the racer (follow.py RC_*); the host logs it, writes 0
# A late frame's drawing skipped (sites_present.py; the host: port/core present.cpp). For the picture only: no step
# reads either word, no rollback puts them back, the checksum does not cover them.
DRAW_ASK = CONTROL + 0xF64       # the image's: 1 when a race frame's drawing is about to begin (fn_1_37AA8's draw half),
                                 # 2 when it has ended (the main loop's depth read-back); each store is a call to the host
DRAW_SKIP = CONTROL + 0xF68      # the host's, written when the image asks: not 0, this frame draws nothing and the next
                                 # frame's begin copies no picture (the last one is shown again); cleared at the next
                                 # frame tick
TRUE_ASPECT = CONTROL + 0xF6C    # the host's, for the picture only (sites_view.py): 0, the game's own Wide Screen; a
                                 # float's bits, true widescreen at that aspect (the launcher's: 16/9): the race view
                                 # keeps Normal's whole vertical view, and the aspect getter gives this aspect
# The spectator's choices (frame_spectate.py) and the racing view's scratch (raceview.py). The image's own, for the
# picture only: no rollback puts them back, the checksum does not cover them, and the race's tasks read none.
# `speccontrols` writes the choices once a displayed frame, on entry, from this game's pad; `specstep` writes them
# into the cameras inside every step, so a rollback's restore is repaired by the first step run again.
SPEC_MODE = CONTROL + 0x3A0      # what the spectator follows with: 0 automatic shots, 1 the course's cameras, 2 to 5 a
                                 # manual shot (types 0 to 3), 6 the chase camera behind the watched racer
SPEC_HEIGHT = CONTROL + 0x3A4    # the camera height: the variant within a manual shot, the chase camera's view less 1
SPEC_CHANGE = CONTROL + 0x3A8    # the step word (+124) plus 1 of the step the last choice was made in: that step, run
                                 # for the first time or again, sets the shot up (a manual shot's type and variant,
                                 # the chase camera's racer)
SPEC_PENDING = CONTROL + 0x3AC   # 1: a choice was made on this frame's entry; its first step takes it (SPEC_CHANGE)
SPEC_LOGGED = CONTROL + 0x3B0    # tests: where the log at SPEC_LOG takes its next entry (entries written since it last
                                 # went round)
VIEW_SCRATCH = CONTROL + 0x3B4   # two words: r3 and r4 kept over the racing view's helper (raceview.py `bits`)
SPEC_ASKED = CONTROL + 0x3C0     # the mode D-pad down last asked for: the next press takes the one after it, whatever
                                 # mode is in use since (SPEC_MODE follows the shot type the game itself went to)
IMAGE_LOGS = CONTROL + 0x3C4     # tests: not 0, the image keeps its three logs (SPEC_LOG, GENERATOR_LOG, SMOOTH_LOG). The
                                 # host sets it in a game whose launch file asks for a dump (`debug.dump`); a
                                 # players' game finds 0 and writes none of them
SPEC_STEP = CONTROL + 0x3C8      # 1 from `specstep` until the task manager's pass of the same step reaches the
                                 # cameras' list: there `specwatch` looks again at who is out (smooth.py's hook)
SPEC_MODES = 7
SPEC_AUTOMATIC, SPEC_COURSE, SPEC_MANUAL, SPEC_CHASE = 0, 1, 2, 6
SPEC_HEIGHTS = (1, 1, 6, 20, 9, 2, 3)   # heights of each mode: a manual shot's variants (fn_1_1138C, fn_1_11748,
                                        # fn_1_11ABC, fn_1_11E98 draw theirs modulo 6, 20, 9, 2), the chase views 1-3
SPEC_LOG = 0x81815000     # tests: the spectator's log, one entry a displayed frame while spectating (frame_spectate.py
SPEC_LOG_ENTRIES = 3584   # `spechud`): 3584 x 8 bytes (60 s); round when full
GENERATOR_LOG = 0x8181C000    # tests: the race's two generator words (the executable's 0x801A63C0, the effects' .data
GENERATOR_LOG_STEPS = 2048    # 0x1D628) after every step, 8 bytes at step & 2047 (frame_checksum.py `genlog`)
EXECUTABLE_GENERATOR = 0x801A63C0
EFFECTS_GENERATOR_DATA = 0x1D628      # main.rel .data
assert len(SPEC_HEIGHTS) == SPEC_MODES and VIEW_SCRATCH + 12 == SPEC_ASKED   # raceview.py's guard word is between them
assert SPEC_ASKED + 4 == IMAGE_LOGS and IMAGE_LOGS + 4 == SPEC_STEP
HFR_DRAWING = CONTROL + 0x3CC    # reserved, 0: nothing writes or reads it (the emulator draws the pictures between
                                 # two steps, not the game)
assert SPEC_STEP + 4 == HFR_DRAWING and HFR_DRAWING + 4 <= CONTROL + 0x400   # the kept results begin at +0x400
assert RACE + RACE_SIZE <= SPEC_LOG and SPEC_LOG + SPEC_LOG_ENTRIES * 8 == GENERATOR_LOG
assert GENERATOR_LOG + GENERATOR_LOG_STEPS * 8 == INPUTS
SMOOTH_LOG = 0x8180C000   # tests: the rivals' smoothing, an entry a displayed race frame (smooth.py `sd_log`): 2048 x 8
SMOOTH_LOG_FRAMES = 2048  # bytes at frame & 2047, the last quarter of the image's own 64 KiB (the image's
                          # routines take 48 KiB)
assert SMOOTH_LOG + SMOOTH_LOG_FRAMES * 8 == CONTROL
HOSTCALLS = 0x81860000    # host services: a call to one of these addresses leaves native code for the host program.
HOST_NET = HOSTCALLS      # the network hook (the frame routine: before every step, in every pass of the start gate)
HOST_TICK = HOSTCALLS + 4     # every game frame of a race or replay start, at the mode machine's entry (fn_1_ECC)
HOST_LINKED = HOSTCALLS + 8   # main.rel is linked and none of its code has run: the host writes its patched words
HOST_HFR = HOSTCALLS + 12     # the drawn frames between two steps (sites_hfr.py): r3 the operation, r4 its frame
REL = 0x801BA660          # main.rel's module header in every boot (the pin: main.dol 0x80005940, fze.sample.rel)
REL_TEXT = REL + 0xDC
REL_BSS = REL + 0x1BA040
assert MODE + 4 <= PRELOAD and PRELOAD + 36 <= END_OFFICIAL and END_OVER + 4 == HANDBACK and HANDBACK + 4 <= FINISH_KIND
assert FINISH_COUNT + 4 == RESULTS_KEEP and RESULTS_CAMERA + 4 == DRAW_ASK and DRAW_SKIP + 4 == TRUE_ASPECT
assert TRUE_ASPECT + 4 <= CHECKSUMS and HOSTCALLS >= SEAT_BLOCK + SEAT_BLOCK_SIZE
hi = lambda a: (a >> 16) & 0xFFFF
lo = lambda a: a & 0xFFFF
