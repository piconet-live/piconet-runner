"""The mod's memory: the fork's RAM above the game's 24 MiB (0x81800000 on; dolphin-fzgx maps it for the mod and
never tells the game). One place for every address the builders and the harness use."""
import importlib.util
import os

# The facts table's generated module (port/facts/generated/facts.py, from port/facts/game.tsv). Every name below that
# the table has is the table's value (`_facts.NAME`; the host's rules' words come to it from the protocol's rules.toml);
# a number typed here is the image's own. The routines name a machine's block and flags from it too
# (memmap.MACHINE_SPEED, memmap.MACHINE_FLAG_FINISHED: the table's machine group).
_spec = importlib.util.spec_from_file_location(
    "port_facts", os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "facts", "generated", "facts.py"))
_facts = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_facts)
# a machine's block, its progress record and their flags, by the table's names (its machine group): the routines'
# texts say memmap.MACHINE_PROGRESS where they read a machine's word
globals().update({k: v for k, v in vars(_facts).items() if k.startswith(("MACHINE_", "PROGRESS_"))})
IMAGE = _facts.IMAGE      # mod image (modbuild.py), up to 64 KiB
CONTROL = _facts.CONTROL  # control block (netframe.py): park target, extra steps, flags, gate, timings, counters
CHECKSUMS = _facts.CHECKSUMS  # checksum ring: 1024 x u32, entry race frame & 1023
SMOOTH = _facts.SMOOTH    # rivals' drawn offsets (smooth.py): 30 x 48 bytes, globals at +0x600
EFFECTS = _facts.EFFECTS_AT  # tests: the effects' counters (sites_effects.py), 304 bytes
NAMES = _facts.NAMES      # player names (playernames.py): 30 x 32 bytes, NUL-terminated, seat order; empty: the pilot's
NAME_SIZE = _facts.NAME_SIZE
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
MONITORS = _facts.MONITORS
MONITORS_BLOCK = 0x19000
# Mines that come back (sites_mines.py; the host's rule RACE_MINE_REGEN): +0 the state of the countdowns' block (0 not
# tried, 1 taken, 3 none: the rule is 0 or the heap had no room), +4 its address in the game's main heap (0 none).
# Written once, on the black frame before the start gate, and never in the race: no step writes it, and the block it
# names is the undo log's like the rest of the heap. The block (MINE_BLOCK bytes): +0 the mines the per-step pass met
# with the rule (it changes in every step, so the undo log watches its page from the race's first step), +4 the mines
# that went off, +8 the mines that came back, +16 a countdown for each of the first MINE_TABLE mines of the course's
# mine walk (u16, the steps left until it is armed again).
MINES = _facts.MINES
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
RACE = _facts.RACE
RACE_COURSE, RACE_MACHINE0, RACE_OPPONENTS, RACE_LAPS, RACE_TYPE, RACE_SHUFFLE, RACE_CPUSEED, RACE_MACHINES = (
    _facts.RACE_COURSE, _facts.RACE_MACHINE0, _facts.RACE_OPPONENTS, _facts.RACE_LAPS, _facts.RACE_TYPE,
    _facts.RACE_SHUFFLE, _facts.RACE_CPUSEED, _facts.RACE_MACHINES)
# the host's rules the games apply (the protocol's rules.toml, through the facts table): their words
RACE_CPULEVEL, RACE_BOOST, RACE_RESTORE = _facts.RACE_CPULEVEL, _facts.RACE_BOOST, _facts.RACE_RESTORE
RACE_FINISH_LIMIT, RACE_MINE_REGEN = _facts.RACE_FINISH_LIMIT, _facts.RACE_MINE_REGEN
RACE_SETTINGS = _facts.RACE_SETTINGS
RACE_FLOW = _facts.RACE_FLOW
RACE_GRID_ON, RACE_GRID = _facts.RACE_GRID_ON, _facts.RACE_GRID
# +100 the rubber band (a word: 1, the CPU drivers pace against the player seats, the game's own way, every recording
# made before 0.4.0; 0, every driver at its own pace, every race and practice from 0.4.0; sites_cpu.py)
RACE_RUBBER_BAND_ON = _facts.RACE_RUBBER_BAND_ON
# The seats' custom machines (garage-built: three parts and a pilot; sites_custom.py): 30 entries of 32 bytes, seat
# order. +0 a word, not 0 when the seat races its own parts (0xC0000000: a garage record's "used, parts" bits); +4,
# +12, +20 the body's, cockpit's and booster's part id (bytes: 0-24, 25-49, 50-74) and +8, +16, +24 each part's
# colour word (red, green, blue, the material byte), laid out as a garage record's three part entries (.bss 0x5480,
# record +0x81A4); +28 the pilot (a byte: the stock machine 0-40 whose pilot drives it, as a record's +0x81A0). The
# game reads an entry through a record's address made up for it (CUSTOM_RECORD: entry + 4 - 0x81A4).
RACE_CUSTOM = _facts.RACE_CUSTOM
CUSTOM_ENTRY = 32
CUSTOM_ID = 0x32          # the machine id a custom seat races with (the game's own for garage record 0)
CUSTOM_RECORD = 0x81A4 - 4
RACE_SIZE = _facts.RACE_SIZE
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
INPUTS = _facts.INPUTS    # input buffer: 1024 frames x 30 seats x 8 bytes (0x3C000)
INPUTS_END = _facts.INPUTS_END
assert IMAGE + 0x10000 <= CONTROL and CONTROL + 0x1000 <= CHECKSUMS and CHECKSUMS + 0x1000 <= SMOOTH
assert SMOOTH + 0x1000 <= NAMES and NAMES + 30 * NAME_SIZE <= TAGS and TAGS + 0x200 <= MONITORS and MONITORS + 0x100 <= RACE and RACE + RACE_SIZE <= INPUTS
assert MONITORS + 0x100 <= MINES and MINES + 8 <= RACE
# Tests: the rank icons' exclusion (follow.py `icon`), counted only in a game told to keep the logs (IMAGE_LOGS):
# 30 words a racer, the drawn frames in which its icon was held back (the racer the HUD follows, or the local seat),
# then 30 words, the frames in which it was let through to the icon's own tests. The picture's only: no step reads
# them, no rollback puts them back, the checksum does not cover them.
ICONS = _facts.ICONS
assert MINES + 8 <= ICONS and ICONS + 2 * 30 * 4 <= RACE
# Each seat's own stats and constants (a stat pack a seat: sites_seats.py, sites_dash.py, sites_curve.py; the core writes
# the block for every race and replay, handbook/stat-pack-constants.md). +0 a byte a seat: not 0, the seat's stock
# machine takes its stats from its row below in place of the car data's block (racer creation and every step);
# +SEAT_SETTING a word, the constants record of the seat whose machine setting runs now (sites_curve.py), else 0;
# +SEAT_ROWS a row a seat of the car data's block size (0xB4), as the loaded table holds a block; +SEAT_CONSTANTS a
# record a seat of SEAT_CONSTANT bytes: +0 the dash plate's A, +4 its B, +8 a boost frame's energy use, +12 the boost
# length's base (floats), +16 the speed cap (a double), +24 the boost length's scale, +28 the machine setting's curve
# (a float a slot, slot order), +0xF0 its one double.
SEAT_BLOCK = _facts.SEAT_BLOCK
SEAT_SETTING = _facts.SEAT_SETTING
SEAT_ROWS = _facts.SEAT_ROWS
SEAT_ROW = _facts.CAR_BLOCK
SEAT_CONSTANTS = _facts.SEAT_CONSTANTS
SEAT_CONSTANT = _facts.SEAT_CONSTANT
SEAT_BLOCK_SIZE = _facts.SEAT_BLOCK_SIZE
SEAT_DASH_BASE = _facts.SEAT_DASH_BASE
SEAT_DASH_SCALE = _facts.SEAT_DASH_SCALE
SEAT_ENERGY_USE = _facts.SEAT_ENERGY_USE
SEAT_BOOST_BASE = _facts.SEAT_BOOST_BASE
SEAT_SPEED_CAP = _facts.SEAT_SPEED_CAP
SEAT_BOOST_SCALE = _facts.SEAT_BOOST_SCALE
SEAT_CURVE = _facts.SEAT_CURVE
SEAT_CURVE_DOUBLE = _facts.SEAT_CURVE_DOUBLE
assert SEAT_SETTING >= 30 and SEAT_SETTING + 4 <= SEAT_ROWS and SEAT_ROWS + 30 * SEAT_ROW <= SEAT_CONSTANTS
assert SEAT_CONSTANTS + 30 * SEAT_CONSTANT <= SEAT_BLOCK_SIZE and SEAT_CURVE_DOUBLE % 8 == 0 and SEAT_CURVE_DOUBLE + 8 <= SEAT_CONSTANT
assert SEAT_BLOCK + SEAT_BLOCK_SIZE <= 0x82000000   # inside the 8 MiB above 24 MiB with MEM1Size 32 MiB
# The native form (the recompiled game on its retail 24 MiB). Nothing is behind these
# addresses: the image is native code, the data from IMAGE to SEAT_BLOCK + SEAT_BLOCK_SIZE a buffer of the host program.
MODE = _facts.MODE        # what the game was started for: 0 a plain start (every patched site does what the stock game
                          # does), 1 a race (a match file, or a replay played as the race). Written by the host before
                          # the game boots.
PRELOAD = _facts.PRELOAD   # the results screen's pictures, read before the race (port/image/build.py, results_preload):
                          # +0 state (0 not tried, 1 trying, 2 read, 3 not read: no room or no file), then three pairs
                          # (bitmap group, the packed file's address in the game's heap or 0), +28 loads served from
                          # memory, +32 the main heap's largest free block when it was tried (bytes)
# The race's end in a session (standin_race.py `over`, frame_replay.py `endtest`; the host: port/core step.cpp). The
# game's race-over test runs once a displayed frame, outside the steps, and what it starts (the results phase) cannot
# be simulated again: it may only say "over" when every step it rests on ran with the official inputs.
END_OFFICIAL = _facts.END_OFFICIAL  # the host's: every step below this one ran, or runs in this frame, with the official
                                 # inputs (0: a host that does not say; the race then ends as it did before these words)
END_ASKED = _facts.END_ASKED     # the image's: 1, every machine has finished or is out and a step below the one about
                                 # to run is not official yet; the host's next frame waits for those steps' official
                                 # inputs, runs no new step, and clears it
END_OVER = _facts.END_OVER       # the image's: 1, the race-over test said "over" (on official steps only): the host
                                 # predicts no step from here on
HANDBACK = _facts.HANDBACK       # the host's: steps left until a returning seat's player drives it again (the hand-back
                                 # after a rejoin), counting down to 0 at that step; 0: none.
                                 # For the picture only (the countdown monitors): no step reads it
# The local racer's finish (frame_hud.py `finwatch`; follow.py's countdown sites read the third). The image's own, for
# the picture only: no step reads them, no rollback puts them back, the checksum does not cover them.
FINISH_KIND = _facts.FINISH_KIND  # what the local machine is, at the last step boundary looked at: 0 racing, 1 finished,
                                 # 2 out. A rollback that takes the finish back makes it 0 again
FINISH_END = _facts.FINISH_END   # the step word (+124) at which the local countdown reaches 0: the boundary after the
                                 # step that finished the machine + 480, or that put it out + 300
FINISH_COUNT = _facts.FINISH_COUNT  # what the picture reads in place of the game's countdown (.bss 0x50EC): the local
                                 # countdown, 0 once the results are on; the game's own while FINISH_KIND is 0;
                                 # run out before the results: 0 when out, 0xFFFF ("not started") when finished
FINISH_SPECTATE = 734            # steps after the local racer's finish from which it spectates (frame_spectate.py
                                 # `spectate`): the finish camera's first round of shots has ended there (it then
                                 # starts it again: run fs-m0) and the place and its sounds are long over
# The racer the results' cameras follow (follow.py, the results camera's site; for the picture only, no step reads
# either word, no rollback puts them back, the checksum does not cover them).
RESULTS_KEEP = _facts.RESULTS_KEEP  # the host's: not 0, the results' cameras follow the local seat whatever it is (a
                                 # spectator's game: its seat is a racer it watches, not its own)
RESULTS_CAMERA = _facts.RESULTS_CAMERA  # the image's, written at the results' start and when results_watch moves the
                                 # cameras: 0x80000000 | why << 8 | the racer (follow.py RC_*); the host logs it, writes 0
# A late frame's drawing skipped (sites_present.py; the host: port/core present.cpp). For the picture only: no step
# reads either word, no rollback puts them back, the checksum does not cover them.
DRAW_ASK = _facts.DRAW_ASK       # the image's: 1 when a race frame's drawing is about to begin (fn_1_37AA8's draw half),
                                 # 2 when it has ended (the main loop's depth read-back); each store is a call to the host
DRAW_SKIP = _facts.DRAW_SKIP     # the host's, written when the image asks: not 0, this frame draws nothing and the next
                                 # frame's begin copies no picture (the last one is shown again); cleared at the next
                                 # frame tick
TRUE_ASPECT = _facts.TRUE_ASPECT  # the host's, for the picture only (sites_view.py): 0, the game's own Wide Screen; a
                                 # float's bits, true widescreen at that aspect (the launcher's: 16/9): the race view
                                 # keeps Normal's whole vertical view, and the aspect getter gives this aspect
# The spectator's choices (frame_spectate.py) and the racing view's scratch (raceview.py). The image's own, for the
# picture only: no rollback puts them back, the checksum does not cover them, and the race's tasks read none.
# `speccontrols` writes the choices once a displayed frame, on entry, from this game's pad; `specstep` writes them
# into the cameras inside every step, so a rollback's restore is repaired by the first step run again.
SPEC_MODE = _facts.SPEC_MODE     # what the spectator follows with: 0 automatic shots, 1 the course's cameras, 2 to 5 a
                                 # manual shot (types 0 to 3), 6 the chase camera behind the watched racer
SPEC_HEIGHT = _facts.SPEC_HEIGHT  # the camera height: the variant within a manual shot, the chase camera's view less 1
SPEC_CHANGE = _facts.SPEC_CHANGE  # the step word (+124) plus 1 of the step the last choice was made in: that step, run
                                 # for the first time or again, sets the shot up (a manual shot's type and variant,
                                 # the chase camera's racer)
SPEC_PENDING = _facts.SPEC_PENDING  # 1: a choice was made on this frame's entry; its first step takes it (SPEC_CHANGE)
SPEC_LOGGED = _facts.SPEC_LOGGED    # tests: where the log at SPEC_LOG takes its next entry (entries written since it last
                                 # went round)
VIEW_SCRATCH = _facts.VIEW_SCRATCH  # two words: r3 and r4 kept over the racing view's helper (raceview.py `bits`)
SPEC_ASKED = _facts.SPEC_ASKED      # the mode D-pad down last asked for: the next press takes the one after it, whatever
                                 # mode is in use since (SPEC_MODE follows the shot type the game itself went to)
IMAGE_LOGS = _facts.IMAGE_LOGS   # tests: not 0, the image keeps its three logs (SPEC_LOG, GENERATOR_LOG, SMOOTH_LOG). The
                                 # host sets it in a game whose launch file asks for a dump (`debug.dump`); a
                                 # players' game finds 0 and writes none of them
SPEC_STEP = _facts.SPEC_STEP     # 1 from `specstep` until the task manager's pass of the same step reaches the
                                 # cameras' list: there `specwatch` looks again at who is out (smooth.py's hook)
SPEC_MODES = 7
SPEC_AUTOMATIC, SPEC_COURSE, SPEC_MANUAL, SPEC_CHASE = 0, 1, 2, 6
SPEC_HEIGHTS = (1, 1, 6, 20, 9, 2, 3)   # heights of each mode: a manual shot's variants (fn_1_1138C, fn_1_11748,
                                        # fn_1_11ABC, fn_1_11E98 draw theirs modulo 6, 20, 9, 2), the chase views 1-3
SPEC_LOG = _facts.SPEC_LOG                  # tests: the spectator's log, one entry a displayed frame while spectating (frame_spectate.py
SPEC_LOG_ENTRIES = _facts.SPEC_LOG_ENTRIES  # `spechud`): 3584 x 8 bytes (60 s); round when full
GENERATOR_LOG = _facts.GENERATOR_LOG              # tests: the race's two generator words (the executable's 0x801A63C0, the effects' .data
GENERATOR_LOG_STEPS = _facts.GENERATOR_LOG_STEPS  # 0x1D628) after every step, 8 bytes at step & 2047 (frame_checksum.py `genlog`)
EXECUTABLE_GENERATOR = _facts.DOL_RNG
EFFECTS_GENERATOR_DATA = 0x1D628      # main.rel .data
assert len(SPEC_HEIGHTS) == SPEC_MODES and VIEW_SCRATCH + 12 == SPEC_ASKED   # raceview.py's guard word is between them
assert SPEC_ASKED + 4 == IMAGE_LOGS and IMAGE_LOGS + 4 == SPEC_STEP
HFR_DRAWING = _facts.HFR_DRAWING  # reserved, 0: nothing writes or reads it (the emulator draws the pictures between
                                 # two steps, not the game)
assert SPEC_STEP + 4 == HFR_DRAWING and HFR_DRAWING + 4 <= CONTROL + 0x400   # the kept results begin at +0x400
assert RACE + RACE_SIZE <= SPEC_LOG and SPEC_LOG + SPEC_LOG_ENTRIES * 8 == GENERATOR_LOG
assert GENERATOR_LOG + GENERATOR_LOG_STEPS * 8 == INPUTS
SMOOTH_LOG = _facts.SMOOTH_LOG                # tests: the rivals' smoothing, an entry a displayed race frame (smooth.py `sd_log`): 2048 x 8
SMOOTH_LOG_FRAMES = _facts.SMOOTH_LOG_FRAMES  # bytes at frame & 2047, the last quarter of the image's own 64 KiB (the image's
                          # routines take 48 KiB)
assert SMOOTH_LOG + SMOOTH_LOG_FRAMES * 8 == CONTROL
HOSTCALLS = _facts.HOSTCALLS      # host services: a call to one of these addresses leaves native code for the host program.
HOST_NET = _facts.HOST_NET        # the network hook (the frame routine: before every step, in every pass of the start gate)
HOST_TICK = _facts.HOST_TICK      # every game frame of a race or replay start, at the mode machine's entry (fn_1_ECC)
HOST_LINKED = _facts.HOST_LINKED  # main.rel is linked and none of its code has run: the host writes its patched words
HOST_HFR = _facts.HOST_HFR        # the drawn frames between two steps (sites_hfr.py): r3 the operation, r4 its frame
REL = _facts.MAIN_REL       # main.rel's module header in every boot (the pin: main.dol 0x80005940, fze.sample.rel)
REL_TEXT = _facts.REL_TEXT_AT
REL_BSS = _facts.REL_BSS_AT
assert MODE + 4 <= PRELOAD and PRELOAD + 36 <= END_OFFICIAL and END_OVER + 4 == HANDBACK and HANDBACK + 4 <= FINISH_KIND
assert FINISH_COUNT + 4 == RESULTS_KEEP and RESULTS_CAMERA + 4 == DRAW_ASK and DRAW_SKIP + 4 == TRUE_ASPECT
assert TRUE_ASPECT + 4 <= CHECKSUMS and HOSTCALLS >= SEAT_BLOCK + SEAT_BLOCK_SIZE
hi = lambda a: (a >> 16) & 0xFFFF
lo = lambda a: a & 0xFFFF
# A story stage's name (the core writes it, port/core/src/boot.cpp; the protocol's courses.toml): the course intro's
# title card and every screen that names the course read it through the game's own course name table, whose English
# entry for the stage the core points here (the game's own says "---"). NUL-terminated, NAME_SIZE bytes. Not read by
# a step: the picture's only, never rolled back, not in the checksum.
COURSE_NAME = ICONS + 0x100
assert ICONS + 2 * 30 * 4 <= COURSE_NAME and COURSE_NAME + NAME_SIZE <= RACE
