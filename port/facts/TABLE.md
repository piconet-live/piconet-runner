# The facts table: its columns, and how to add a row

`game.tsv`, the one table of this folder ([README.md](README.md)).

## The table

Tab-separated, one row a fact: `name`, `group`, `value`, `of`, `type`, `manifest`, `source`, `meaning`.
- `value`: a number, a text in single quotes, or `=` and an expression over the names above it. The wire rows take
  theirs from the relay's schema, `protocol/relay.toml`, by its names (`=relay_version`, `=relay_len_pong`: every
  name in `generate.py`'s `schema_names`); such a row needs no place, and its `source` is the schema's line. The
  host's rules' rows (their race block words, `RACE_CPULEVEL` and the like, and their limits) take theirs from
  `protocol/rules.toml` the same way (`=rule_at_mine_regen`, `=rule_most_finish_limit`), as the replay file's
  versions take theirs from `protocol/replay.toml`.
- `of`: the row this offset counts from. A base with an address gives `NAME_AT`, the offset's own address. A base of
  type `ptr` is a pointer: the offset counts from where it points, so it has no address.
- `type`: `u8 u16 s16 u32 int`, `ptr`, `mark` (2 to 4 bytes of a file or packet; 4 also give `NAME_WORD`), `str`.
- `manifest`: keys of `recomp/manifest.toml` that must hold the same value, `;` between them.
- `source`: the path and line the value was taken from (no folder: the image generators'). The file must hold one of the row's places; the line is only a pointer.
- `meaning`: one line in plain words. It becomes the comment in every generated file.

## To add a row

1. Add it under its group, below every name it uses. A wrong row is an error with its line.
2. Add to `tests/places.tsv` every place that types the value today (`tests/places.py` says how each is read).
3. Run `generate.py`, then `check.sh` (every commit runs it). After a file was split or a constant moved, `tests/places.py`
   reports the places it no longer finds, to be pointed at the new file by hand; `--sources` then rewrites the `source` lines.

**Never typed by hand again:** a guest address; an offset into the control block, the slot, the race block, a
machine's block or its progress record; a section offset of main.rel; a packet type or length; a file's mark or
version; the 30 seats, the 6-byte record, the input delay; a checksum mask. Name the row, or add it. A relay
packet's numbers are the schema's (`protocol/relay.toml`): a new packet or field goes there, and its row names it.
