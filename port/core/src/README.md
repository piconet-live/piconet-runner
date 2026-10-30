# The core's sources

One library. Whatever comes from outside the program is read in one place: files in `files_*.cpp`, the relay's
packets in `relay_wire.cpp`. `internal.h` is what the files call of each other; `files.h` what the file readers
give; `net_internal.h` and `undo_log.h` are the relay session's and the undo log's own.

| File | What |
|---|---|
| `core.cpp` | The interface of `include/fzgx/core.h`: `Init`, the host and the options every file asks for |
| `guest.cpp` | The game's memory by guest address, and the image's data window |
| `boot.cpp` | What a race or a playback has written into the game: the image, the race's values, the options' words, a story stage's name (the protocol's `courses.toml`) for the course intro's title card, and in a race the player's own controller options |
| `options_screen.cpp` | The start that opens the game's own controller options (the launch file's `controllerSetup`): asks the game for Options once the title stands, for the controller menu once Options stands, and stops the game when the player leaves it |
| `files.h` | A race as read from a file (`Match`), the bundle (`BundleFiles`), the readers' declarations |
| `files_launch.cpp`, `files_launch_debug.cpp`, `files_launch_part.h` | The launch file (`--config FILE`): every setting of a start, read into `fzgx::Options`; an unknown key, a wrong kind, a value out of range refused by name. Its members are described in `protocol/launch.toml`: the generated `launch_file.g.h` takes each member of a plain kind, these files the objects, the members of a form of their own and every rule between members. The header: one object of the file, its members taken by name |
| `files_match.cpp` | A race's match (the launch file's `match.match`): the set-up and seats, checked; the JSON helpers |
| `files_bundle.cpp` | The bundle: image, manifest, patch list or codes; writes the patch list into the game's code |
| `files_stats.cpp`, `stat_ranges.h` | A race's stat pack (the match start's `setup.stats`): its entries checked against the table of ranges, its fingerprint, every refusal; the table (the protocol's `generated/stat_ranges.g.h`, from `protocol/stats.toml`; `handbook/stat-packs.md`) |
| `stat_pack.cpp` | The pack in the game: laid over the loaded car data every frame until the race runs, compared after, and checked at the go against the table and the racers' weights |
| `files_save.cpp` | The player's saved game, read for its controller options: the game's own checks, and the ranges its Options screens write |
| `files_replay.cpp` | A replay file's two readers: the race of a playback, and the steps it runs and compares |
| `files_disc.cpp` | The disc's files: a machine's stock texture files, for the colours |
| `files_os.cpp` | `File`: the one place a file of this computer is opened |
| `replay.cpp`, `replay.h` | Writing a replay (its header, then lineup, steps and results) |
| `board.cpp`, `board_net.h`, `board_race.cpp`, `board_map.cpp` | The launcher's status board (`include/fzgx/board.h`; the layout is generated from `protocol/board.toml` into `protocol/generated/status_board.g.h`): taken at Init, the heartbeat, the phase, the race's snapshot, events, the mailbox; the race and the session as the board shows them; the file's mapping |
| `paint.cpp` | Any colour for a machine: its loaded textures found in memory and repainted |
| `session.cpp` | The relay session from start to end: joining, the start gate, the hook before every step |
| `relay_wire.cpp` | The relay's packets: the one place they are received and read, and what this game sends |
| `step.cpp` | A step of the race: its inputs (official or predicted), the checksum of the step before, a step run again, a frame's line of the rollback log |
| `frame.cpp` | A displayed frame of the race before its own step (`NormalStep`): the list of its steps, and the steps: lockstep, or prediction and rollback |
| `frame_report.cpp` | A race frame's accounts, moved out of `frame.cpp`: the frame before as a line of the rollback log and a part of the timing sums, the averages and counters to the mod, the pictures the host has shown |
| `schedule.cpp` | The time sync of a race's frames: how far ahead of its schedule the game is, what the host's pacing owes, a step more to catch up or a frame held |
| `summary.cpp`, `summary.h`, `files_summary.cpp` | A race's one line in the player's file (`race-summary.txt`): what is counted while it runs, the line, and the file kept to its last 300 lines |
| `freeze.cpp` (`fzgx/freeze.h`) | A game that stands still in a race: what the game's thread publishes, when a watching thread of the host's reports it, the summary line's three words; the test setting that makes a game stand |
| `panel.cpp`, `panel_art.cpp` (`fzgx/panel.h`) | The panel over the picture before a race: what it shows from a start to the go (LOADING, WAITING FOR PLAYERS) and its lines, which the game window writes in the launcher's display face (Saira Condensed ExtraBold, `port/moderngekko/src/panel.cpp`); the game's energy meter scaled for the screen as the load's bar, the load's progress from the game's preload queue |
| `art.cpp` (`fzgx/art.h`) | Pictures from the game's own files: a packed file (.lz) unpacked, a texture of a texture file (.tpl, RGB5A3) as pixels |
| `loglines.cpp`, `files_logs.cpp` (`fzgx/loglines.h`) | The game's log as a file a player sends: which of the emulator's lines go into it and how many, a line without the player's folders; the file a start writes and the logs kept beside it |
| `playback.cpp` | Playback of a replay: its session, its check against the recording, its end; the summaries |
| `instruments.cpp` | The tests' instruments: lag and jitter, trace, dumps, altered state, forced rollbacks, audit; each off unless the launch file's `debug` part names it |
| `spectate.cpp` | A game whose racer is out or has finished: it falls behind live by playing slower, then watches in lockstep on official frames only, a slack of frames behind the newest; its inputs still sent on a racer's schedule |
| `out_test.h` | Whether a machine is out of the race: the game's own verdict (as the image's `outtest.py`), for what the scrubber, the replay's racer watched and the status board's snapshot show |
| `net_internal.h` | What those nine share: the mod's words, the session's state, what they call of each other |
| `practice.cpp` (`fzgx/practice.h`) | Practice with rewind, the decision alone: what each displayed frame does (race on, stand, the countdown, a branch), the minute's reach and its keys, the one save and its expiry, the stick's scrub speed, the run kept for the replay |
| `practice_session.cpp` | A practice's session (the launch file's `match.practice`): one seat, no relay, each step's row the pad's; each displayed frame moved by `Rewind` with the one undo log (a standing picture by its step before run again, silently), every step run again checked against its first run, the bar told, the kept run to the replay |
| `rollback.cpp`, `rollback.h`, `checksum.cpp` | Rollback as its user sees it: save a step, restore to a step, re-simulate, checksum; the state checksum and the words it leaves out |
| `undo_log.cpp`, `undo_log.h` | The undo log that puts the simulation's memory back: tracking, the scan pool, restore |
| `strip.cpp` | The game window's title strip as numbers (`fzgx/strip.h`): its layout and what a point of the window is; no window and no drawing |
| `udp.cpp`, `udp.h` | A UDP socket, not blocking |
| `util.cpp`, `util.h` | Strings, SHA-1, the log, and the declaration of `File` |
| `internal.h` | What the files above call of each other: `boot`, `net`, `paint` |
| `game_facts.h` | The facts table's generated header for these files: the game's and the mod's addresses by name (`table::`, `game::`); no game address is typed here as a number |
