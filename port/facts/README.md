# The game's facts

One table of the game's and the mod's addresses, offsets, masks and limits: `game.tsv`. The C++ core, the Python
image generators and the C# server and launcher get the same numbers from it, generated. A number typed twice can
drift, and a wrong offset desyncs a race without a word.

**The adapter (`port/moderngekko/src`) takes its addresses from `facts.h`, and so does the core for every game address (`port/core/src/game_facts.h`: `table::NAME`, `game::NAME`); so do the image's data window (`port/image/routines/memmap.py` takes every name the table has from `generated/facts.py`) and a machine's block, its progress record and their flag bits (the `machine` group: `game::MACHINE_SPEED`, `memmap.MACHINE_SPEED`), wherever the core and the image's routines read them; the core's other offsets, the image generators' other numbers and the server still type theirs.** The `machine` group's rows are each proven against their row of `machine-state.csv` (`tests/places.tsv`), so a field's name and the map's meaning stay one. `tests/` proves that every value equals the constant it will replace.

## The files

`game.tsv` is the table; its columns, how to add a row and what is never typed by hand again:
[TABLE.md](TABLE.md).

`machine-state.csv` is the map of a machine's state: from it come the words the state checksum leaves out
(`BLOCK_SKIP`, `PROGRESS_SKIP`). `layout-today.tsv` keeps the order and comments the core's `layout.h` had, so the
header's `fzgx::layout` still opens with that file's 40 lines, byte for byte, now that the core includes the header in
its place (`port/core/include/fzgx/guest.h`); without it those names are written in the table's order, each in its group's namespace.

`rollback-state.tsv` is the list of rollback's state: a row for every region of main memory, the locked cache and the
data window, and for what lies outside them (the chips, the emulator, the session): whether a step reads it, whether
a rollback puts it back, whether the checksum covers it, and what it is paired with that a rollback does not put
back. Its header gives the columns and the four rules. Nothing is generated from it: the core's test
`TestRollbacksStateList` (`port/core/tests/test_rollback_state.cpp`) runs the undo log over every byte and fails where
the list and the code disagree, and `port/image/tests/test_rollback_state.py` holds the image's list of memory never
rolled back to the same rows. Both run in the full check (`scripts/check.sh`), at every landing. A change to what a rollback restores changes the row.

## Generated

`generate.py` (standard library) writes them; `--check` fails when one is stale; `--value NAME` prints one value.
- `generated/facts.h`: `fzgx::layout` (the names `layout.h` had first), `fzgx::game`, `fzgx::limits`, `fzgx::wire`, `fzgx::file`.
- `generated/facts.py`: every name.
- `protocol/generated/GameFacts.g.cs`: the groups `limit`, `wire`, `file` and `rel` (Online.Protocol compiles it).
