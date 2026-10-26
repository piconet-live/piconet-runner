#!/usr/bin/env python3
"""The game's facts, generated: one table (game.tsv) of addresses, offsets, masks and limits, written out as a C++
header, a Python module and a C# class, so that no number is typed twice (README.md).

  generate.py               writes the generated files (OUTPUTS)
  generate.py --check       fails when a generated file on disk is stale
  generate.py --value NAME  prints one value (for shell scripts); NAME_AT is the address of a row that has a base

The table: tab-separated, columns name, group, value, of, type, manifest, source, meaning. `value` is a number, a
quoted text, or `=` and an expression over the names above it and the protocol schemas' (relay_*, replay_*:
schema_names). `of`
names the row an offset counts from. A row that breaks a rule is an error with its line. Standard library only."""
import ast
import csv
import importlib.util
import os
import re
import sys

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.normpath(os.path.join(here, "..", ".."))
TABLE = os.path.join(here, "game.tsv")
STATE_MAP = os.path.join(here, "machine-state.csv")
LAYOUT_TODAY = os.path.join(here, "layout-today.tsv")
# Where each generated file goes. Until the packages that use them land, the header and the module stay here; the C#
# class is the protocol's (Online.Protocol compiles it). PROTOCOL is the protocol's folder from the repository's root,
# in the protocol's checkout (port/setup/pins.tsv's REPO_PROTOCOL, read by port/setup/parts.py: "protocol" today; after
# the split, in external/protocol).
sys.path.insert(0, os.path.join(root, "port", "setup"))
import parts  # noqa: E402
PROTOCOL = os.path.relpath(parts.path_in("REPO_PROTOCOL", "protocol", root), root).replace(os.sep, "/")
SCHEMA = os.path.join(root, PROTOCOL, "relay.toml")   # the relay's packets: the wire rows' values (schema_names)
OUTPUTS = {"cxx": "port/facts/generated/facts.h", "python": "port/facts/generated/facts.py",
           "csharp": f"{PROTOCOL}/generated/GameFacts.g.cs"}

COLUMNS = ["name", "group", "value", "of", "type", "manifest", "source", "meaning"]
# group: the C++ namespace (None: not in the header), the C# class (None: not in C#)
GROUPS = {"limit": ("limits", "Limits"), "window": ("layout", None), "control": ("layout", None),
          "slot": ("layout", None), "race": ("layout", None), "rel": ("game", None), "game": ("game", None),
          "machine": ("game", None), "site": ("game", None), "build": (None, None), "wire": ("wire", "Wire"),
          "file": ("file", "Files")}
NAMESPACES = ["layout", "game", "limits", "wire", "file"]
# type: lowest and highest value, C++ type, C# type. ptr: where a word that holds a guest address is.
NUMBERS = {"u8": (0, 0xFF, "std::uint8_t", "byte"), "u16": (0, 0xFFFF, "std::uint16_t", "ushort"),
           "s16": (-0x8000, 0x7FFF, "std::int16_t", "short"), "u32": (0, 0xFFFFFFFF, "std::uint32_t", "uint"),
           "ptr": (0, 0xFFFFFFFF, "std::uint32_t", "uint"), "int": (-0x80000000, 0x7FFFFFFF, "int", "int")}
TEXTS = {"mark": (2, 4), "str": (1, 64)}   # shortest and longest; mark: a file's or a packet's mark, as bytes
OPERATORS = {ast.Add: lambda a, b: a + b, ast.Sub: lambda a, b: a - b, ast.Mult: lambda a, b: a * b,
             ast.FloorDiv: lambda a, b: a // b, ast.LShift: lambda a, b: a << b, ast.RShift: lambda a, b: a >> b,
             ast.BitAnd: lambda a, b: a & b, ast.BitOr: lambda a, b: a | b}
# The annex's two regions, the table's size of each, and the names of the masks made from them.
MASKS = [("BLOCK_SKIP", "block", "MACHINE_SIZE"), ("PROGRESS_SKIP", None, "PROGRESS_SIZE")]


class FactsError(Exception):
    pass


class Row:
    def __init__(self, line, cells):
        self.line = line
        for key, cell in zip(COLUMNS, cells):
            setattr(self, key, cell)
        self.manifest = [k.strip() for k in self.manifest.split(";") if k.strip()]
        self.number = self.type in NUMBERS
        self.at = None        # the guest address, for a row whose base has one
        self.schema = False   # its value is the relay schema's (load)
        self.shown = None     # the value as the generated files write it

    @property
    def word(self):
        """A four-byte mark as the big-endian word the game's memory holds."""
        return int.from_bytes(self.value.encode(), "big") if self.type == "mark" and len(self.value) == 4 else None


def evaluate(text, names):
    """An expression over whole numbers and the names above; an unknown name or anything else is a ValueError."""
    def walk(node):
        if isinstance(node, ast.Constant) and type(node.value) is int:
            return node.value
        if isinstance(node, ast.Name):
            if node.id not in names:
                raise ValueError(f"expression over an unknown name {node.id}")
            return names[node.id]
        if isinstance(node, ast.BinOp) and type(node.op) in OPERATORS:
            return OPERATORS[type(node.op)](walk(node.left), walk(node.right))
        if isinstance(node, ast.UnaryOp) and isinstance(node.op, ast.USub):
            return -walk(node.operand)
        raise ValueError(f"cannot read the expression {text!r}")
    try:
        return walk(ast.parse(text.strip(), mode="eval").body)
    except SyntaxError:
        raise ValueError(f"cannot read the expression {text!r}") from None


def schema_names(path=SCHEMA):
    """The relay schema's numbers by the names a row's expression may use, read by the protocol's own loader
    (protocol/generate.py): relay_version, relay_mark (a text), relay_header_size, relay_record_size, relay_key_size,
    relay_max_frames_per_packet, relay_max_payload, relay_ai_drives_I; per packet relay_type_NAME and relay_len_NAME
    (its bytes after the header and, from the relay, the seat's key; before its records), relay_len_NAME_optional
    (with its optional field); per flag relay_GROUP_VALUE (relay_joined_flags_over). Without the schema: none."""
    if not os.path.exists(path):
        return {}
    folder = os.path.dirname(path)
    spec = importlib.util.spec_from_file_location("protocol_generate", os.path.join(folder, "generate.py"))
    module = importlib.util.module_from_spec(spec)
    sys.path.insert(0, folder)   # its own modules (board, codec_cs, ...)
    try:
        spec.loader.exec_module(module)
        protocol, packets = module.load(path)
        replay = module.replay.load(os.path.join(folder, "replay.toml"), protocol["record_size"])
    except (module.SchemaError, module.replay.SchemaError) as e:
        raise FactsError(f"{os.path.basename(path)}: {e}") from None
    finally:
        sys.path.remove(folder)

    def snake(name):
        return re.sub(r"(?<!^)(?=[A-Z])", "_", name).lower()
    out = {"relay_mark": protocol["mark"]}
    for key in ("version", "header_size", "record_size", "key_size", "max_frames_per_packet", "max_payload"):
        out[f"relay_{key}"] = protocol[key]
    out.update({f"relay_ai_drives_{i}": b for i, b in enumerate(protocol["ai_drives"])})
    for p in packets:
        out[f"relay_type_{snake(p.name)}"] = p.type
        head = p.head - (protocol["key_size"] if p.from_relay else 0)
        out[f"relay_len_{snake(p.name)}"] = head
        if p.optional:
            out[f"relay_len_{snake(p.name)}_optional"] = head + p.optional.size
    for group in protocol["flags"]:
        out.update({f"relay_{snake(group['name'])}_{snake(v['name'])}": v["value"] for v in group["values"]})
    # the replay file's (replay.toml): replay_mark, replay_frames_mark, replay_lineup_mark, replay_results_mark (texts),
    # replay_lineup_size, replay_version_NAME (plain, stats, custom, mines, seats, grid), replay_version_written
    out.update({"replay_mark": replay["mark"], "replay_frames_mark": replay["frames_mark"],
                "replay_lineup_mark": replay["lineup_mark"], "replay_results_mark": replay["results_mark"],
                "replay_lineup_size": replay["lineup_size"], "replay_version_written": replay["written"]})
    out.update({f"replay_version_{snake(v['name'])}": v["number"] for v in replay["versions"]})
    return out


def show(text, value):
    """A number as the generated files write it: hexadecimal where the table wrote it so, or for an address."""
    typed = re.fullmatch(r"0[xX]([0-9A-Fa-f]+)", text)
    if typed:
        return f"0x{value:0{len(typed.group(1))}X}"
    return f"0x{value:08X}" if text.startswith("=") and value >= 0x10000 else str(value)


def load(path=TABLE):
    """The table's rows, checked. Every error names the file and the row's line."""
    label = os.path.basename(path)
    rows, by_name, taken = [], {}, {}
    schema = None   # the relay schema's names, read at the first row that uses one
    with open(path, encoding="utf-8", newline="") as f:
        lines = f.read().split("\n")
    if lines[0].split("\t") != COLUMNS:
        raise FactsError(f"{label}:1: the first line must be the columns {', '.join(COLUMNS)}")
    for number, text in enumerate(lines[1:], 2):
        if not text.strip() or text.startswith("#"):
            continue

        def fail(message):
            raise FactsError(f"{label}:{number}: {message}")
        cells = text.split("\t")
        if len(cells) != len(COLUMNS):
            fail(f"{len(cells)} columns, not {len(COLUMNS)}")
        row = Row(number, [c.strip() for c in cells])
        if not re.fullmatch(r"[A-Z][A-Z0-9_]*", row.name):
            fail(f"the name {row.name!r} is not capitals, digits and underscores")
        made = [row.name] + ([row.name + "_AT"] if row.of else []) + ([row.name + "_WORD"] if row.type == "mark" else [])
        for name in made:
            if name in taken:
                fail(f"duplicate name {name} (line {taken[name]})")
            if name in [m[0] for m in MASKS]:
                fail(f"the name {name} is the generator's (a checksum mask)")
        if row.group not in GROUPS:
            fail(f"unknown group {row.group!r} (the groups: {', '.join(GROUPS)})")
        if row.type not in NUMBERS and row.type not in TEXTS:
            fail(f"unknown type {row.type!r} (the types: {', '.join([*NUMBERS, *TEXTS])})")
        if not re.fullmatch(r"[^\s:]+:\d+", row.source):
            fail(f"the source {row.source!r} is not one path and line (path:line)")
        if not row.meaning:
            fail("no meaning")
        # its value is a protocol schema's (which one: "relay", "replay"; else None): no source types it
        row.schema = "replay" if "replay_" in row.value else "relay" if "relay_" in row.value else None
        if row.schema and schema is None:
            schema = schema_names()
        if row.number:
            try:
                row.value = evaluate(row.value[1:] if row.value.startswith("=") else row.value,
                                     {**{n: v for n, v in (schema or {}).items() if type(v) is int},
                                      **{n: r.value for n, r in by_name.items() if r.number}})
            except ValueError as e:
                fail(str(e))
            low, high = NUMBERS[row.type][:2]
            if not low <= row.value <= high:
                fail(f"the value {row.value} (0x{row.value:X}) does not fit the type {row.type} ({low} to {high})")
            row.shown = show(cells[2].strip(), row.value)
        elif row.schema:
            name = row.value.removeprefix("=").strip()
            if not isinstance(schema.get(name), str):
                fail(f"{row.value} is no text of the relay schema")
            row.value = schema[name]
        else:
            quoted = re.fullmatch(r"'([\x20-\x7E]*)'", row.value)
            low, high = TEXTS[row.type]
            if not quoted or not low <= len(quoted.group(1)) <= high or '"' in row.value or "\\" in row.value:
                fail(f"the value {row.value} is not a text of {low} to {high} plain characters in single quotes")
            row.value = quoted.group(1)
        if row.of:
            base = by_name.get(row.of)
            if base is None or not base.number or not row.number:
                fail(f"the base {row.of} is not a number in a row above")
            address = base.value if not base.of else base.at
            if base.type != "ptr" and address is None:
                fail(f"the base {row.of} has no address and is not a ptr")
            if base.type != "ptr":
                row.at = address + row.value
                if not 0 <= row.at <= 0xFFFFFFFF:
                    fail(f"the address {row.of} + {row.name} (0x{row.at:X}) does not fit 32 bits")
        by_name[row.name] = row
        taken.update(dict.fromkeys(made, number))
        rows.append(row)
    return rows


def values(rows):
    """Every generated name and its value: NAME, NAME_AT for a row with an address, NAME_WORD for a four-byte mark."""
    out = {}
    for r in rows:
        out[r.name] = r.value
        if r.at is not None:
            out[r.name + "_AT"] = r.at
        if r.word is not None:
            out[r.name + "_WORD"] = r.word
    return out


def skip_masks(rows, path=STATE_MAP):
    """The words the state checksum leaves out, from the machine-state map: pointers (they differ with the memory
    layout) and drawing-only fields. One list of 32-bit masks per region; bit 31 of mask 0 is the region's first word."""
    known = values(rows)
    label = os.path.basename(path)
    with open(path, encoding="utf-8", newline="") as f:
        table = list(csv.DictReader(f))
    for column in ("region", "offset", "type", "category", "net"):
        if not table or column not in table[0]:
            raise FactsError(f"{label}:1: no column {column}")
    out = {}
    for name, region, size_name in MASKS:
        region = region or f"+0x{known['MACHINE_PROGRESS']:03X}"   # the record the block's pointer leads to
        size, skip = known[size_name], set()
        part = [(n, r) for n, r in enumerate(table, 2) if r["region"] == region]
        if not part:
            raise FactsError(f"{label}: no rows of region {region}")
        try:
            offsets = [int(r["offset"], 16) for _, r in part]
        except ValueError:
            raise FactsError(f"{label}: an offset of region {region} is not hexadecimal") from None
        for i, (number, r) in enumerate(part):
            end = offsets[i + 1] if i + 1 < len(part) else size
            if not offsets[i] < end <= size:
                raise FactsError(f"{label}:{number}: offset 0x{offsets[i]:X} is out of order or beyond 0x{size:X}")
            if r["net"] == "VISUAL" or r["category"] == "POINTER" or "ptr" in r["type"]:
                skip.update(range(offsets[i] // 4, (end + 3) // 4))
        words = size // 4
        out[name] = [sum(1 << (31 - k % 32) for k in range(m * 32, min(m * 32 + 32, words)) if k in skip)
                     for m in range((words + 31) // 32)]
    return out


def layout_today(rows, path=LAYOUT_TODAY):
    """The lines the core's layout.h had, in its order and with its comments (layout-today.tsv: name, comment), so that
    the header's layout namespace begins byte for byte as that file read. Without the file: none."""
    if not os.path.exists(path):
        return [], set()
    by_name = {r.name: r for r in rows}
    lines, names = [], set()
    with open(path, encoding="utf-8", newline="") as f:
        for number, text in enumerate(f.read().split("\n")[1:], 2):
            if not text.strip():
                continue
            name, _, comment = text.partition("\t")
            if name not in by_name or not by_name[name].number:
                raise FactsError(f"{os.path.basename(path)}:{number}: {name} is not a number of the table")
            value = by_name[name].value
            line = f"inline constexpr std::uint32_t {name} = " + (f"0x{value:08X};" if value >= 0x10000 else f"{value};")
            lines.append(line + (f"  // {comment}" if comment else ""))
            names.add(name)
    return lines, names


def cxx(rows, masks):
    out = ["// Generated by port/facts/generate.py from port/facts/game.tsv. Do not edit.",
           "// The game's and the mod's addresses, offsets, masks and limits. Guest addresses; every word big-endian.",
           "// NAME_AT: the guest address of an offset whose base has one. NAME_WORD: a four-byte mark as a word.",
           "// SPDX-License-Identifier: GPL-3.0-or-later", "", "#pragma once", "", "#include <array>",
           "#include <cstdint>"]
    today, today_names = layout_today(rows)
    for space in NAMESPACES:
        out += ["", f"namespace fzgx::{space}", "{"]
        if space == "layout":
            out += today
        for r in rows:
            if GROUPS[r.group][0] != space:
                continue
            if space == "layout" and r.name in today_names:
                pass   # its line is above, as layout.h had it
            elif r.number:
                out.append(f"inline constexpr {NUMBERS[r.type][2]} {r.name} = {r.shown};  // {r.meaning}")
            else:
                out.append(f'inline constexpr char {r.name}[] = "{r.value}";  // {r.meaning}')
            if r.at is not None:
                out.append(f"inline constexpr std::uint32_t {r.name}_AT = 0x{r.at:08X};  // {r.of} + {r.name}")
            if r.word is not None:
                out.append(f"inline constexpr std::uint32_t {r.name}_WORD = 0x{r.word:08X};  // '{r.value}' as a word")
        if space == "game":
            for name, words in masks.items():
                out.append("// the state checksum leaves out the words whose bit is set (bit 31 of mask 0: the first word)")
                out.append(f"inline constexpr std::array<std::uint32_t, {len(words)}> {name} = {{"
                           + ", ".join(f"0x{w:08X}" for w in words) + "};")
        out.append(f"}}  // namespace fzgx::{space}")
    return "\n".join(out) + "\n"


def python(rows, masks):
    out = ['"""Generated by port/facts/generate.py from port/facts/game.tsv. Do not edit.',
           "The game's and the mod's addresses, offsets, masks and limits. NAME_AT: the guest address of an offset whose",
           'base has one."""']
    group = None
    for r in rows:
        if r.group != group:
            group = r.group
            out += ["", f"# ---- {group}"]
        out.append(f"{r.name} = " + (r.shown if r.number else ('b' if r.type == 'mark' else '') + f'"{r.value}"')
                   + f"  # {r.meaning}")
        if r.at is not None:
            out.append(f"{r.name}_AT = 0x{r.at:08X}")
    out += ["", "# ---- the state checksum leaves out the words whose bit is set (bit 31 of mask 0: the first word)"]
    for name, words in masks.items():
        out.append(f"{name} = [" + ", ".join(f"0x{w:08X}" for w in words) + "]")
    return "\n".join(out) + "\n"


def pascal(name):
    return "".join(part.capitalize() for part in name.split("_"))


def csharp(rows):
    out = ["// <auto-generated>Generated by port/facts/generate.py from port/facts/game.tsv. Do not edit.</auto-generated>",
           "namespace Online.Protocol;", "",
           "/// <summary>The game's limits, the relay's packets and the files' marks: the same numbers the game's side",
           "/// is built with (port/facts/README.md).</summary>", "public static class GameFacts", "{"]
    for group, (_, name) in GROUPS.items():
        members = [r for r in rows if r.group == group]
        if not name or not members:
            continue
        out += [f"    public static class {name}", "    {"]
        for r in members:
            kind, value = (NUMBERS[r.type][3], r.shown) if r.number else ("string", f'"{r.value}"')
            meaning = r.meaning.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
            out += [f"        /// <summary>{meaning}</summary>", f"        public const {kind} {pascal(r.name)} = {value};", ""]
        out[-1:] = ["    }", ""]
    out[-1:] = ["}"]
    return "\n".join(out) + "\n"


def generated():
    rows = load()
    masks = skip_masks(rows)
    return {OUTPUTS["cxx"]: cxx(rows, masks), OUTPUTS["python"]: python(rows, masks), OUTPUTS["csharp"]: csharp(rows)}


def main(args):
    try:
        if args[:1] == ["--value"] and len(args) == 2:
            known = values(load())
            if args[1] not in known:
                raise FactsError(f"game.tsv has no {args[1]}")
            value = known[args[1]]
            print(f"0x{value:08X}" if isinstance(value, int) and value >= 0x10000 else value)
            return 0
        if args not in ([], ["--check"]):
            print(__doc__, file=sys.stderr)
            return 2
        stale = []
        for path, text in generated().items():
            full = os.path.join(root, path)
            if args:
                if not os.path.exists(full):
                    stale.append(path)
                    continue
                with open(full, encoding="utf-8", newline="") as f:
                    if f.read() != text:
                        stale.append(path)
            else:
                os.makedirs(os.path.dirname(full), exist_ok=True)
                with open(full, "w", encoding="utf-8", newline="\n") as f:
                    f.write(text)
        if stale:
            print("stale (run port/facts/generate.py): " + ", ".join(stale), file=sys.stderr)
            return 1
        return 0
    except FactsError as e:
        print(f"generate.py: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
