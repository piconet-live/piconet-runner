"""The netcode's frame routine, for the mod image (modbuild.py): called in place of the `bl fn_1_407C` (simulation
manager) in fn_1_37AA8 (main.rel offset 0x37C14) by the image's trampoline. Control block at memmap.CONTROL (0x81810000):
  +0 park target race frame (test harness: if the race clock .bss 0x50A4 equals it, park until it changes; 0 none)
  +4 frame last parked at
  +8 K: extra simulation steps to run in this displayed frame (re-simulation after a rollback); counts down to 0
  +12 re-simulating (1 while the extra steps run: the seat code then does not record local input)
  +16 extra steps run so far
  +20 the last race frame checksummed
  +40 go, +44 ready, +48 race-start gate on: with the gate on, on the course intro's last frame (MD_GAME phase 0x27,
      intro countdown .bss 0x26C28 == 1: fade done, race music not started) the game sets ready and holds until go
      (the server's start). Nothing is simulated while holding.
  +52 spectator camera request: N + 1 makes the next frame, before the simulation step, call fn_1_5370(1, N) (main.rel
      0x5370: the camera set-up the game's own result screen and replays use: viewport 0 becomes the TV cameras
      (fn_1_EE44, live camera [.bss 0xF70]) following racer N, and the HUD follows that racer); cleared when done
  In a session, unless spectating (+248 < 180), every frame on entry points viewport 0's camera record ([.bss 0xF6C]
      +0x2, the machine it follows) at the local seat (slot 'FZSI' +4): the game sets it to port 0's machine (seat 0)
      for the intro and countdown, and the stand-in routine (standin.py), which also keeps it on the local seat, runs
      only once the machines' tasks do, so every game but seat 0's showed seat 0's machine first, then cut to its own.
  +0xA30 the player is done (the fork then stops the emulation: the launcher starts the game with -b, so it exits): in
      a session, set once the results screens' state (the byte at .bss 0x8E51D, in fn_1_14A028's block at 0x8E518)
      reaches 8, the step after the every-racer page, where the game would save records (and ask for a memory card);
      0 the race's own results, after the local racer's finish, 7 every racer's results (A from the first page; A
      again leaves).
  +0xA34 1 presses A on port 0 for one frame (.bss 0x9F8 held and 0xA00 newly pressed, on entry, before the
      steps whose presentation tasks read them), then goes back to 0. Tests set it; and the HUD pass does for a
      racer still racing when the results begin, to leave the first results page (frame_hud.py).
  +260 tests: 1 lets Start pause in a session. Otherwise every frame of a session sets rule flag 0x08000000 (.bss
      0x3C30), which the race's pause check (fn_1_36B10, 0x37364) reads as "no pause menu": a local pause would stop
      this game's race while the others go on. The game itself never sets the flag.
  main.dol's frame counters (0x801A66A0, 0x801A66A4, 0x801A66A8, 0x801A66C0, 0x801A66C8, counted up once per
      main-loop pass by main.rel's _prolog) are set before every step of a running session to 0x100000 + the step:
      the machines' bob and sway (fn_1_29A0C: sine of 0x801A66A0 x 0x109 and x 0x1A3) went by displayed frames, which
      differ between games (rollbacks, catch-ups) and are not rolled back; the sway is in the drawn matrix +0x14C,
      from which a respawn (fn_1_2BF8C) builds the machine's pose: the games differed while a machine respawned. The
      base keeps the counters going forward at go.
  +520 tests: 1 sets rule flag 0x10 (boost allowed on lap 1, fn_1_331EC) from the race's first step on.
  +512: effects.py (the effect update's race-state write made independent of the camera): the main.rel patched for.
  +524, +528: othersound.py (other racers' player sounds turned down): the main.rel patched for; the volume to restore.
  +532: playernames.py (player names on the results screens): the main.rel patched for.
  +704 to +716: seatport.py (the local racer's rumble and player sounds): the main.rel patched for; counters of
      rumble requests, player-sound checks and finish stops (frame_hud.py `finwatch`).
  +500: banners.py (the HUD banners start in the step that raises them): the main.rel the sites were patched for.
  +264, +268: follow.py (port-0 presentation made the local racer's): the main.rel the sites were patched for, and
      the local racer's "finished" bit for the HUD. After the steps of a session's race and results, unless
      spectating before the results (a spectator's out machine gets the results pages too, from rule flag 0x1000:
      frame_hud.py), the HUD pass (`hudstate`) also sets bit 0 of .bss 0x26B18 / 0x26B19 / 0x26B1A (port 0's retired
      bits, which fn_1_32200 sets for seat 0 by port) from the local machine's flags (0x10000000, 0x80 out,
      0x800 KO), and the HUD part mask .data 0x3D544[0] to 0 when the local racer is out or finished, else to the
      race's own (+60): seat 0's knockout or finish no longer strips every game's HUD. The results board's slot 0
      (.bss 0x26314 progress record, 0x26304 max speed, 0x26B04 place), which the game fills by pad port and the
      race-over loops never fill for a seat without one: at the step boundary after the local racer's finish
      step (`finwatch`, below), with the slot still empty (seat 0's own finish fills it in the game that follows
      seat 0), its progress record, max speed (+0xD8) and place (+0x482, at most 30) are kept at
      +0x400/+0x5FC/+0x600 (+276 kept; +272 is no longer used) and written into the slot every frame from then on
      (so a rollback cannot take them back out). A rollback that takes the finish back drops them, and the finish
      step run again keeps them anew: kept once, on the first frame that showed the finish, they were a
      prediction's whenever that step was corrected later, and the place and lap times shown could differ from
      the recording's.
  memmap.DRAW_ASK, DRAW_SKIP (+0xF64, +0xF68): a late frame's drawing skipped (../sites_present.py; the host's
      port/core/src/present.cpp): the image stores 1 into DRAW_ASK when a frame's drawing begins and 2 when it ends;
      the host answers in DRAW_SKIP, which a skipped frame's drawing sites and the next frame's begin read.
  memmap.FINISH_KIND, FINISH_END, FINISH_COUNT (+0xF50, +0xF54, +0xF58): the local racer's own finish, for the
      picture (frame_hud.py `finwatch`, called on every step boundary: frame_entry.py). The game counts one
      countdown after a finish (.bss 0x50EC[port]: "FINISH!" from 480 to 300, the place from 300 to 0, then the
      results list once the results are on), in the simulation, for the machine with a pad port: seat 0 in every
      game. The simulation keeps it; the six picture readers (follow.py) read FINISH_COUNT: the local countdown,
      started at the boundary after the step that finished the local machine (480) or put it out (300) and
      counted by the step word (+124); 0 once rule flag 0x1000 is set; the game's own while the local machine
      races; a finished machine's that has run out stands at 0 until the results (the place stays up: follow.py).
      Nothing is latched: the kind is compared with the machine's flags on every boundary, so a predicted finish
      that a rollback takes back takes the countdown back with it.
  +248 automatic spectating (in a network session's race phase, after the frame's steps, before drawing): the
      displayed frames the local seat (slot 'FZSI' +4) has been out (retired, 0x08000000: destroyed or at rest after a
      breakdown, standin.py; or 0x800) and not finished (0x10000; a machine broken down, 0x80, coasts on and may
      still finish, and the camera left it 180 frames after the breakdown); at 180 the TV cameras
      (fn_1_5370(1, N)) follow +252, at first the leader (the first-placed machine still in), and the spectator HUD
      (+56) comes on. +252 is the racer watched: the truth, in the control block, which no rollback puts back; the
      cameras' own words (live camera +0x8, the chase camera's record +0x2) are written from it inside every step
      (`specstep`, in the step wrapper before the task manager), and so is the switch to the cameras the mode
      wants, whenever the camera mode ([.bss 0xF68]+0x48) is another: made after the steps, a rollback put the
      camera back to before it every frame and each switch reset the TV camera to its first cut, so the view
      flickered between the racer and the track (seen watching P2); and a racer chosen after the steps was
      booked to the frame's last step and lost to any rollback that ran that step again (seen by testers).
  +56 spectator HUD: when set, after the frame's simulation steps (before drawing) port 0 is no longer marked
      retired for the HUD: its bit in .bss 0x26B18/0x26B19/0x26B1A and bit 0x04 of .bss 0x8FDA8 are cleared and its
      HUD-part mask .data 0x3D544[0] gets back the race's own value, kept at +60 every frame while it is non-zero and
      the switch is off (the game's default, 0xFF9FFFFF, also turns on Time Attack parts) (fn_1_32200 marks it again every frame while the
      machine is out; fn_1_15C36C and the HUD parts read them), so the HUD shows the racer the camera follows,
      until the results begin (rule flag 0x1000): then `hudstate` gives the spectator's out machine the results
      pages as a finisher's, and spechud leaves the HUD parts off (frame_hud.py)
      Spectator controls (frame_spectate.py `speccontrols`), read on entry of every displayed frame, before the
      first network hook, from pad 1's raw buttons and C-stick (.bss 0x998, 0x99D: after the steps the pad block
      is what a rollback put back) OR'ed with +72 (injected by tests, cleared each frame; 0x10000 and 0x20000
      stand for the C-stick up and down), newly pressed against +64: D-pad right/left the next/previous racer in
      race order and up the leader (+252; never a machine that is out, and the cameras leave a watched machine
      in the first step that finds it out: frame_spectate.py), down the next mode (memmap.SPEC_MODE:
      automatic shots, the course's cameras, the four manual shots, the chase camera behind the racer, and round),
      the C-stick up/down the camera height (memmap.SPEC_HEIGHT: a manual shot's variant, the chase camera's
      view 1 to 3; round at either end), A the HUD on/off (+68 = hidden; behind the chase camera it is off:
      the game's HUD there is the local seat's). All of it is this game's own: none enters the control record.
      A racer is skipped while it has no progress record (machine +0x49C, 0 before the race's machines are set up),
      and a watched racer outside the race leaves the racer buttons unread. The stand-in's own D-pad seat switch
      (standin.py) still reads the same buttons while a replay is watched as the race.
  memmap.SPEC_MODE, SPEC_HEIGHT, SPEC_CHANGE, SPEC_PENDING (+0x3A0 to +0x3AC): the spectator's choices and the step
      that sets a shot up anew (frame_spectate.py); SPEC_LOGGED (+0x3B0) and memmap.SPEC_LOG: the tests' log of
      them, an entry a displayed frame, round when full.
  memmap.SPEC_ASKED (+0x3C0): the mode D-pad down last asked for; the next press takes the one after it.
  memmap.SPEC_STEP (+0x3C8): the mark of a spectating step, set by `specstep` and taken by smooth.py's hook where
      the task manager's pass of that step reaches the cameras' list: `specwatch` (frame_specwatch.py) moves the
      cameras off a racer who went out in the step's race tasks.
  memmap.GENERATOR_LOG: the tests' log of the race's two generator words after every step (frame_checksum.py
      `genlog`). memmap.VIEW_SCRATCH (+0x3B4, +0x3B8) and +0x3BC: raceview.py (a racer
      changes the camera view from the input rows): two registers kept over its helper; the main.rel patched for.
  Online (the Dolphin fork's network layer, dolphin-fzgx Source/Core/Core/FzgxNet.cpp): before every
  simulation step, and in every pass of the race-start gate's wait, the routine calls the image's network hook at
  memmap.IMAGE + 0x0C (a `blr` unless the fork, in a network session, runs its handler there in its place). The
  handler sets +116 (network session) and +48 (gate on), releases the gate (+40) on the relay's "go" and then sets
  +120 (running) with +124 = 0, and before step s writes the official inputs of step s into the input buffer.
  +116 network session, +120 running, +124 the step about to run (counted from the relay's "go"): while running,
  the checksum ring and the input buffer are indexed by the step instead of the race clock, and +124 counts up
  after every step
  +28 time base ticks spent in the last re-simulation's extra steps (40.5 MHz on GameCube)
  +488 the step about to run was simulated before (a rollback's replay, the rewound step included), +492 the highest
      step simulated + 1 (0 when not running): mute.py drops sound commands while +488 is set, so a step's sounds
      play once, the first time it is simulated (with only the extra steps muted, the rewound step, the frame's first,
      played its sounds again: "FINISH" was heard twice), and a new step's sounds play even among the extra steps.
  +312 tests, with +24 set: lists (bit n = list n) the steps before a frame's last also skip (to find which list's
      tasks change race state when they run in some steps and not others).
  +24 1: re-simulate without the task filter (tests); 0 (default): as the game's own replay fast-forward does, every
      step of a frame but its last runs with the simulation manager's task filter at 1 ([.bss 0xDAC]+0x14, read by
      fn_1_407C: only tasks whose flags have bit 1 run, so presentation tasks such as effects and sounds are skipped)
      and with .data 0x3599C cleared (fn_1_A96EC/fn_1_A96DC); both are restored before the last step, which runs
      everything. The step with presentation is thus the newest: a frame with K extra steps (a rollback's re-simulation,
      from the rewound step, or catching up) first steps the rewound or oldest step, so presentation run there was K
      steps behind the machines when drawn (engine lights and effects flickered). The re-simulating flag (+12, which the
      fork reads to tell the extra steps from the frame's own) is unchanged: 0 for the first step, 1 for the K after it.
      The values restored are those the frame began with (saved on entry with the manager's list mask), and they are
      also written back right after the network hook of the first step: a rollback puts the memory back to a moment
      inside an earlier frame's extra steps, filter 1 included, and keeping that as the normal value left the simulation
      manager's flag-0 tasks (the chase camera, list 20 fn_1_90E0, among them) off for the rest of the race: the camera
      stayed on the grid and machines went undrawn.
  In a network session too, the steps before a frame's last run the simulation manager's flag-1 tasks only (the race);
      its presentation (flag-0) tasks run in the last, and a rollback puts their memory back with the race's, except the
      memory listed for the fork by `untracked_write` (untracked.py: the sound system's and the face board's, which run
      on through a rollback). Re-running presentation in the extra steps was tried: the race music skipped and stalled
      within 10-20 s. In the race phase (0x29 on) these presentation tasks are given flag bit 1 on entry, so they run in
      every step, re-simulated through a rollback with the race: the cameras (list 20: fn_1_90E0 the chase camera,
      fn_1_EE44 the TV cameras; put back and not re-run, the camera was a rollback's depth behind its machine for a
      frame, which looked like the machine jolting forward) and the HUD's event countdown (list 13: fn_1_15E1E8, which
      runs the lap banner's timers), and fn_1_9A5CC (list 23), race state though flagged as presentation: it
      changes the machines' energy (+0x184) and surface flags (+0x218), so run only in a frame's last step, how often
      it ran depended on where the frames fell; a replay came out different from the first run and the games
      desynced (found by replaying forced rollbacks with the same inputs and masking the lists one by one); and the
      effect update (list 11: fn_1_58694) and the burner update (list 10: fn_1_98230, the engine and boost flames):
      run in a frame's last step only, a rollback set their timers back every frame, effects never expired, the
      190-slot pool filled and every new spark and boost burst failed (with a rollback nearly every frame they all
      vanished); re-simulated with the race they advance once per step (effects.py makes the one race-state write
      of the effect update independent of the camera). The flags are set again inside every step of a session (the
      step wrapper `manager`, before the task manager runs): tasks registered during the race (the lap-time display's
      fn_1_15E1E8 timers at a lap, the banners') were only flagged on the next frame's entry, outside the steps, and a
      rollback to before that entry undid the flag; the replayed steps skipped them, their timers never got past
      their first two frames and the lap-time display (and the skull counter placed by it, fn_1_CAB38) jumped every
      frame (a flickering skull after a lap).
  +452, +456, +460, +464: smooth.py (rivals drawn smoothly through rollbacks): `smooth_install` and `smooth_restore`
      run on entry; +460 is set before the frame's last step, in which the task manager's hook (main.rel 0x40AC) adds
      the rivals' drawn offsets between the race's task lists and the presentation ones.
  memmap.END_OFFICIAL, END_ASKED, END_OVER (+0xF40, +0xF44, +0xF48): the race's end, held until it is official. The
      game's race-over test (standin_race.py, `over`) runs once a displayed frame, outside the steps, and the results
      phase it starts is not simulated again by a rollback: said on predicted steps, a rollback took the phase's
      first frame back and the race ended twice. The host writes END_OFFICIAL (every step below it ran, or runs in
      this frame, with the official inputs); `over` says "over" only when the step about to run is not past it
      (END_OVER: the host predicts nothing from there), and otherwise sets END_ASKED: the host's next frame waits
      for the official inputs of the steps before its own, goes back to the first that was predicted wrong, and
      runs no new step. So that every game says "over" before the same step, a frame runs no further step once a
      step has made the race over (`endtest` before and after every step; frame_entry.py, `extra_cut`): a game that
      had run past the finish on a wrong prediction stands, when it is corrected, where the others stand.
      "Over" itself is raceend.py's text in both routines: every machine finished or out (with the race block's
      flow word, every player seat finished or out by the game's own verdict, outtest.py); and, with the host's rule
      (memmap.RACE_FINISH_LIMIT, steps; 0 without), the race's end for everyone that many steps after the first
      finish, with the results held 480 steps after the last player seat's finish. It reads the machines' flags and
      finish times and the game's race clock (.bss 0x50A4), and latches nothing.
After every simulation step (normal or re-simulated) it stores a checksum of the simulation state for that race
frame in a ring at memmap.CHECKSUMS (in a network session the Dolphin fork computes the same checksum natively
instead, off the emulated CPU, before the next step; control block +216 = 1 has both compute it, and the fork counts
any difference at +220). (1024 x u32, entry race frame & 1023): FNV-1a over 32-bit words of every machine's
block (0x620 bytes) and progress record ([+0x49C], 0x1FC bytes), without the words the skip masks name: pointers
(+0x484..+0x49C, progress +0x90/+0x94) and presentation-only fields (drawn transform, sway, presentation
orientation...; the machine-state map's VISUAL class). The rest is identical across clients in step whatever their
memory layout, code list or camera.
Each extra step first advances the race clock, as the frame function does after its own call (it advances the
clock once per displayed frame, after the simulation manager, in the race phase 0x29 and later only; so do the extra
steps, so a re-simulation during the countdown would not run ahead), then calls the network hook (in a session the
fork writes that step's inputs there and keeps its undo log), then steps."""
import csv, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import memmap, native
# netframe.py [--native [CLEAN_REL]]: the native form, and main.rel as the stock game has it (native.py); modbuild.py
# passes on what it read. Said before any generator below asks.
native.use("--native" in sys.argv[1:2], (sys.argv[2:3] or [None])[0])
from keystone import Ks, KS_ARCH_PPC, KS_MODE_PPC32, KS_MODE_BIG_ENDIAN
# Words left out of the checksum, from the machine-state map: pointers (they differ with the memory layout) and
# presentation-only fields (smoothed once per displayed frame, so a re-simulation leaves them briefly different).
rows = list(csv.DictReader(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "facts", "machine-state.csv"))))
def skipmask(region, size):
    rs = [r for r in rows if r["region"] == region]; offs = [int(r["offset"], 16) for r in rs]; skip = set()
    for i, r in enumerate(rs):
        if r["net"] == "VISUAL" or r["category"] == "POINTER" or "ptr" in r["type"]:
            end = offs[i + 1] if i + 1 < len(offs) else size
            skip.update(range(offs[i] // 4, (end + 3) // 4))
    bits = [1 if k in skip else 0 for k in range(size // 4)]; bits += [0] * (-len(bits) % 32)
    return [int("".join(map(str, bits[i:i + 32])), 2) for i in range(0, len(bits), 32)], len(skip)
BLOCK, nb = skipmask("block", 0x620)
PROG, np_ = skipmask("+0x49C", 0x1FC)
assert len(BLOCK) == 13 and len(PROG) == 4
import frame_entry, frame_tasks, frame_hud, frame_spectate, frame_replay, frame_checksum
asm = frame_entry.asm() + frame_tasks.asm() + frame_hud.asm() + frame_spectate.asm() + frame_replay.asm() + frame_checksum.asm()
import banners, effects, follow, othersound, playernames, raceview, seatport, smooth, untracked
asm += (follow.asm() + banners.asm() + effects.asm() + raceview.asm() + othersound.asm() + playernames.asm() + smooth.asm()
        + untracked.asm() + seatport.asm())
asm = asm.replace("IMAGE_HI", str(memmap.hi(memmap.IMAGE))).replace("CONTROL_HI", str(memmap.hi(memmap.CONTROL))).replace("CHECKSUMS_HI", str(memmap.hi(memmap.CHECKSUMS))).replace("CHECKSUMS_LO", str(memmap.lo(memmap.CHECKSUMS)))
assert memmap.lo(memmap.CONTROL) == 0
asm = asm.replace("TABLES", "".join(f"    .long {w}\n" for w in BLOCK + PROG))
assert -0x1BA040 + 0x16D600 + 0x3599C == -0x10000 - 0x70A4      # .data 0x3599C from .bss
# The network hook: the image's own word at +0x0C (the fork runs its handler there), or, in the native form, a host
# call's address, which has nothing behind it and lies outside every piece of compiled code.
import native
hook = memmap.HOST_NET if native.ON else memmap.IMAGE + 0x0C
asm = asm.replace("NETHOOK_HI", str(memmap.hi(hook))).replace("NETHOOK_LO", str(memmap.lo(hook)))
code = bytes(Ks(KS_ARCH_PPC, KS_MODE_PPC32 + KS_MODE_BIG_ENDIAN).asm(asm)[0])
import json
out = {"image": code.hex()}
if native.ON:
    offsets = native.label_offsets(asm)
    assert 4 * len(native.instructions(asm)) == len(code), "the line count does not give the assembled size"
    out["sites"] = [dict(site, at=offsets[site["label"]], helper_at=offsets[site["helper"]]) for site in native.SITES]
print(json.dumps(out))
