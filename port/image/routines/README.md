# The image's generators

Each file is a script (or a part of one) that assembles PowerPC text with keystone and prints the result. They find
each other by name in this folder, so the names stay. With `FZGX_NATIVE=1` (set by `../build.py`) they build the
native form: every routine at a patched site tests the mode word, and the installers write nothing (`native.py`).
`memmap.py` is the mod's memory as the generators name it; the facts table (`port/facts/game.tsv`) holds the same values, proved equal in every commit's check.

## The image, in the order it is assembled

`modbuild.py OUT SEAT OFF UNIFORM REFMASK DELAY` writes the image and prints its hooks as Gecko code lines:

1. `FZMD`, the version, the size, the network hook's word.
2. The seat routines, `standin.py --parts`, one text: `standin_routine.py` (the slot `FZSI`, the per-machine routine,
   the uniform rules, the test knock-out, the player seats), `standin_inputs.py` (the input buffer),
   `standin_race.py` (`refs`, the rubber band's references, which only the fork's image still reads: the native form's
   band is off, `../sites_cpu.py`; `over`, and the installer, which becomes the hooks' C0 code). `raceend.py` is the
   text of "the race is over" (every machine finished or out; the host's rule that ends a race so long after the
   first finish), assembled into `over` and into `frame_replay.py`'s `endtest`: one text, so the two agree.
3. `FZFR` and the frame routine, `netframe.py`, one text: `frame_entry.py`, `frame_tasks.py`, `frame_hud.py`,
   `frame_spectate.py` (with `frame_speccontrols.py`, `frame_specwatch.py`), `frame_replay.py`, `frame_checksum.py`, then `follow.py` (with `rankicons.py`), `banners.py`, `effects.py`,
   `raceview.py`, `othersound.py`, `playernames.py`, `smooth.py`, `untracked.py`. Each gives its text with `asm()`.
4. `FZNT` and the name tags, `nametags.py`, entered through a call to `monitors.py` (the rejoin hand-back's 3, 2, 1
   on the start's floating monitors, `memmap.HANDBACK`; their pictures are read by `../sites_monitors.py`).
5. In the native form only: `FZPT` and the patched sites' routines, added by `../build.py` from `../sites.py`.

`netframe.py`'s docstring lists every word of the control block; `standin.py`'s the slot and the input buffer.

## The camera

Two generators hold what a player does with the camera in a race of ours; each one's docstring is its account.

| File | What | Where its state is |
|---|---|---|
| `raceview.py` | A racer changes the view (1 to 3) as on a console: four words of the game's fn_1_ABAC, fed from the followed seat's input rows | The game's own camera record, which a rollback puts back; the rows make the change again |
| `frame_spectate.py` | A racer who is out watches the others: `speccontrols` reads the pad on a frame's entry, `specstep` writes the choices into the cameras inside every step, `spectate` and `spechud` run after the steps | The control block (`memmap.SPEC_MODE` and its neighbours, +252), which no rollback puts back |
| `rankicons.py` | No rank icon (1st, 2nd, 3rd) above the racer the HUD follows: a racer's own, a spectator's watched one | Nothing of its own: the game's cameras say whom the HUD follows |

A racer who is out is never watched (`frame_specwatch.py`): the cameras leave one in the step it goes out in,
for the next in race order that still races, and no button lands on one. The racer asked for stays the player's
choice (only a button changes it), so a rollback that takes the going-out back puts the cameras back on it. The spectator's buttons: D-pad left/right the racer
before/after in race order, up the leader, down the next mode
(automatic shots, the course's cameras, four manual shots, the chase camera behind the racer), the C-stick up/down
the camera height (a manual shot's variant, the chase camera's view), A the HUD. `memmap.SPEC_LOG` is the tests'
log of it: an entry a displayed frame, read from a dump of the data window (`debug.dump`).

Three more things of the camera are words of the patch list alone (`../sites.py`, native form): the chase camera's
record is record 0 for every seat (`camera_aa84`, `camera_ac08`); a routine that reads a camera record for a
machine's viewport slot reads record 0 when the machine has none (`camera_index_*`: the game read far past the four
records). The race's two generator words are logged for tests after every step (`memmap.GENERATOR_LOG`): they
differ between two games of one race, each game's drawing drawing from them, and no step reads what they give.

## Rivals as they are drawn

`smooth.py` draws a rival where it was expected after a rollback and fades the difference out; a machine on the
road keeps its true height above it (only the part along the road is smoothed). Its seat's offset is the machine's
drawn transform, which `../sites_drawn.py` (native form) reads for every picture placed from where the race has the
machine: `../sites_shadow.py` moves a rival's shadow with its drawn body, along the road, and `../sites_effects.py`
draws its effects (sparks, boost, energy, attacks, wrecks, road dust) with the offset. `memmap.SMOOTH_LOG` is the
tests' log of the first two, `memmap.EFFECTS` the effects' counters.

The three logs for tests (`SPEC_LOG`, `GENERATOR_LOG`, `SMOOTH_LOG`) are kept only where the host says so
(`memmap.IMAGE_LOGS`, which the core sets in a game whose launch file asks for a dump): a players' game writes
none of them (`tests/test_logs.py` and the tests of each log).

## The codes at the game's sites

| File | What it prints |
|---|---|
| `direct.py` | The boot straight into a race (`--params`: the race is read from the race block) |
| `seats.py` | Every seat's machine, from a list (`--params`: from the race block) |
| `setup.py` | The race setup's randomness held: for the fork's bundle and the manifest's boot scene; in the native form `../sites.py` does it |
| `mute.py` | No sound and no rumble from a step simulated before |

`../fork_bundle.py` writes them as code lines (`codes.txt`). `../build.py` reads the same lines and turns each insert
into a routine of the image and a patched word.

## Rules

- The order of the texts is the order of the bytes: a part is cut or joined at a label line, never reordered.
- A change here is checked by `../check.sh` and by the oracle in [`../README.md`](../README.md).
