#!/usr/bin/env python3
"""Where today's sources type the numbers the facts table holds, and how each is read back (places.tsv).

  places.py             reads every place and prints what differs from the table, then the totals
  places.py --sources   rewrites game.tsv's `source` lines to where each row's value stands today (after lines
                        moved or a file was split); a place that is found no more is still an error.
A row's source must name a file that holds one of its places; a line that an edit shifted is not an error. A row
whose value is a protocol schema's (`=relay_...`, `=replay_...`, generate.schema_names) needs no place: no source
types it, and its source is the schema's line (protocol/relay.toml, protocol/replay.toml); a place it has is checked
all the same.

places.tsv, tab-separated: fact (a row's name, NAME_AT, or an expression over the generated names), file, how, what,
count. A file without a folder is in the image generators' folder. `how`:
  cxx   the C++ constant `what` (constexpr NAME = expression), its expression worked out
  enum  the member `what` (Enum.Member) of a C++ or C# enum
  py    the Python module-level name `what` (NAME or NAME[i]), its expression worked out
  cs    the C# constant `what` (const TYPE NAME = number)
  re    a bare number: every match of the pattern `what` (its one group a number); `count` matches are expected
  hex   as re, the group being hexadecimal digits without 0x
  text  as re, the groups joined being the text of a mark
  use   no number is typed there any more: the C++ file takes the value from the generated header and names it
        `what` (game::IDLE_GPU_SYNC), `count` times; the file must include facts.h, itself or through its headers
In C++ files a pattern does not see // comments. Standard library only."""
import ast
import os
import re
import sys

here = os.path.dirname(os.path.abspath(__file__))
facts = os.path.dirname(here)
root = os.path.normpath(os.path.join(facts, "..", ".."))
sys.path.insert(0, facts)
sys.path.insert(0, os.path.join(root, "port", "setup"))
import generate  # noqa: E402
import parts  # noqa: E402  the other parts' checkouts (port/setup/pins.tsv)

PLACES = os.path.join(here, "places.tsv")
GENERATORS = "port/image/routines"   # the image generators' folder: a place named without a folder is there
INCLUDE_ROOTS = ["port/core/include", "port/facts/generated"]
FACTS_H = "port/facts/generated/facts.h"
CXX = (".cpp", ".h")
# a place in another part's file is read from that part's checkout (port/setup/pins.tsv): its path as the part has it
OTHER_PARTS = (("net/src/Launcher.", "REPO_LAUNCHER"), ("net/src/Online.Protocol/", "REPO_PROTOCOL"),
               ("protocol/", "REPO_PROTOCOL"))


# the runner's optional parts (the gates): a checkout without one proves the
# rows it can, and a place in it is left out
OPTIONAL = ("port/gates/",)


def left_out(path):
    return any(path.startswith(p) and not os.path.isdir(os.path.join(root, p)) for p in OPTIONAL)


class Missing(Exception):
    pass


def full(path):
    if "/" not in path:
        return os.path.join(root, GENERATORS, path)
    part = next((name for prefix, name in OTHER_PARTS if path.startswith(prefix)), None)
    return parts.path_in(part, path) if part else os.path.join(root, path)


_texts = {}


def text_of(path):
    if path not in _texts:
        if not os.path.exists(full(path)):
            raise Missing(f"{path} does not exist")
        with open(full(path), encoding="utf-8") as f:
            _texts[path] = f.read()
    return _texts[path]


def line_of(text, position):
    return text.count("\n", 0, position) + 1


def number(text):
    text = re.sub(r"(?<=[0-9A-Fa-f])(?:[uU][lL]{0,2}|[lL]{1,2})$", "", text.strip())
    return int(text, 16) if text[:2] in ("0x", "0X") else int(text)


def without_comments(text):
    """C++ text with every // comment blanked (positions and lines kept)."""
    return re.sub(r"//[^\n]*", lambda m: " " * len(m.group()), text)


# ---- C++ constants

_constants = {}

def cxx_constants(path):
    """{name: (expression, line)} of every `constexpr TYPE NAME = EXPRESSION[, NAME = EXPRESSION];` in the file."""
    if path in _constants:
        return _constants[path]
    text = without_comments(text_of(path))
    out = _constants[path] = {}
    for m in re.finditer(r"\bconstexpr\s+(?:const\s+)?[\w:]+[\s*&]+(\w+\s*=[^;{]*);", text):
        position = m.start(1)
        for part in re.split(r",(?=\s*\w+\s*=)", m.group(1)):
            name, _, expression = part.partition("=")
            out[name.strip()] = (expression.strip(), line_of(text, position + part.find(name.strip())))
            position += len(part) + 1
    return out


def cxx_includes(path):
    """The repository's headers a C++ file includes, nearest first, through the headers they include."""
    seen, queue = [], [path]
    while queue:
        current = queue.pop(0)
        for name in re.findall(r'#include "([^"]+)"', text_of(current)):
            for folder in [os.path.dirname(current), *INCLUDE_ROOTS]:
                candidate = os.path.normpath(os.path.join(folder, name)).replace(os.sep, "/")
                if os.path.exists(os.path.join(root, candidate)) and candidate not in seen and candidate != path:
                    seen.append(candidate)
                    queue.append(candidate)
                    break
    return seen


def cxx_value(path, name, depth=0):
    """The value of a C++ constant: its expression over numbers, the file's and its headers' constants, layout::X."""
    files = [path, *cxx_includes(path)]
    for file in files:
        constants = cxx_constants(file)
        if name in constants:
            expression, line = constants[name]
            break
    else:
        raise Missing(f"{path}: no constant {name}")
    if depth > 20:
        raise Missing(f"{path}: {name} is defined through itself")
    quoted = re.fullmatch(r'"([^"\\]*)"', expression)
    if quoted:
        return quoted.group(1), line, file
    names = {}
    plain = re.sub(r"\b(?:fzgx::)?layout::(\w+)", lambda m: names.setdefault("layout__" + m.group(1), m.group(1)) and "layout__" + m.group(1), expression)
    plain = re.sub(r"\b(0[xX][0-9A-Fa-f]+|\d+)[uUlL]+\b", r"\1", plain)
    known = {}
    for word in set(re.findall(r"(?<![\w.])[A-Za-z_]\w*", plain)):
        known[word] = cxx_value(FACTS_H, names[word], depth + 1)[0] if word in names else cxx_value(file, word, depth + 1)[0]
    try:
        return generate.evaluate(plain, known), line, file
    except ValueError as e:
        raise Missing(f"{file}:{line}: {name}: {e}") from None


def enum_value(path, what):
    """A member of `enum [class] NAME [: TYPE] { A = 1, B, C }` (C++ or C#): its value and line."""
    enum, _, member = what.partition(".")
    text = without_comments(text_of(path))
    m = re.search(rf"\benum\s+(?:class\s+)?{enum}\b[^{{;]*\{{([^}}]*)\}}", text)
    if not m:
        raise Missing(f"{path}: no enum {enum}")
    value, position = -1, m.start(1)
    for part in m.group(1).split(","):
        name, _, given = part.partition("=")
        if name.strip():
            value = number(given) if given.strip() else value + 1
            if name.strip() == member:
                return value, line_of(text, position + part.find(member))
        position += len(part) + 1
    raise Missing(f"{path}: enum {enum} has no {member}")


# ---- Python constants

def py_names(path, depth=0):
    """{name: (value, line)} of a Python file's module-level assignments that are plain numbers, tuples of them, or
    expressions over earlier names and memmap.NAME. What cannot be worked out is left out."""
    out = {}
    memmap = None

    def walk(node):
        nonlocal memmap
        if isinstance(node, ast.Constant) and type(node.value) in (int, str, bytes):
            return node.value
        if isinstance(node, ast.Name):
            return out[node.id][0]
        if isinstance(node, ast.Tuple):
            return tuple(walk(e) for e in node.elts)
        if isinstance(node, ast.BinOp) and type(node.op) in generate.OPERATORS:
            return generate.OPERATORS[type(node.op)](walk(node.left), walk(node.right))
        if isinstance(node, ast.Attribute) and isinstance(node.value, ast.Name) and node.value.id == "memmap" and depth == 0:
            memmap = memmap or py_names("memmap.py", 1)
            return memmap[node.attr][0]
        raise KeyError
    for statement in ast.parse(text_of(path)).body:
        if isinstance(statement, ast.Assign) and len(statement.targets) == 1:
            try:
                target, value = statement.targets[0], walk(statement.value)
            except (KeyError, TypeError):
                continue
            if isinstance(target, ast.Name):
                out[target.id] = (value, statement.lineno)
            elif isinstance(target, ast.Tuple) and isinstance(value, tuple) and len(value) == len(target.elts):
                out.update({t.id: (v, statement.lineno) for t, v in zip(target.elts, value) if isinstance(t, ast.Name)})
    return out


def py_value(path, what):
    name, index = re.fullmatch(r"(\w+)(?:\[(\d+)\])?", what).groups()
    names = py_names(path)
    if name not in names:
        raise Missing(f"{path}: no module-level {name} that is a number")
    value, line = names[name]
    return (value[int(index)] if index else value), line


def cs_constants(path):
    """{name: (value, line)} of every C# `const TYPE NAME = number or "text";` in the file."""
    text = text_of(path)
    return {m.group(1): (m.group(3) if m.group(3) is not None else number(m.group(2)), line_of(text, m.start(1)))
            for m in re.finditer(r'\bconst\s+\w+\s+(\w+)\s*=\s*(?:(0[xX][0-9A-Fa-f]+|-?\d+)|"([^"]*)")\s*;', text)}


# ---- one place

def read(file, how, what):
    """[(value, line)]: what the source says at this place. Missing when it is not there."""
    if how == "cxx":
        value, line, where = cxx_value(file, what)
        if where != file:
            raise Missing(f"{file}: no constant {what} (it is in {where})")
        return [(value, line)]
    if how == "enum":
        return [enum_value(file, what)]
    if how == "py":
        return [py_value(file, what)]
    if how == "cs":
        constants = cs_constants(file)
        if what not in constants:
            raise Missing(f"{file}: no constant {what}")
        return [constants[what]]
    if how in ("re", "hex", "text"):
        text = text_of(file)
        text = without_comments(text) if file.endswith(CXX) else text
        convert = {"re": number, "hex": lambda t: int(t, 16), "text": str}[how]
        return [(convert("".join(m.groups())), line_of(text, m.start(1))) for m in re.finditer(what, text)]
    raise Missing(f"places.tsv: unknown way of reading {how!r}")


def uses(file, what, value):
    """[(value, line)] for every time a C++ file names the generated constant `what`: the value is the generated
    one by construction, so what is checked is that the file does take it from facts.h and still names it."""
    if FACTS_H not in cxx_includes(file):
        raise Missing(f"{file} does not include facts.h, so {what} is not the generated value there")
    text = without_comments(text_of(file))
    return [(value, line_of(text, m.start())) for m in re.finditer(r"(?<![\w:])" + re.escape(what) + r"\b", text)]


def places():
    """[(line, fact, file, how, what, count)] of places.tsv."""
    out = []
    with open(PLACES, encoding="utf-8") as f:
        lines = f.read().split("\n")
    for n, text in enumerate(lines[1:], 2):
        if text.strip():
            fact, file, how, what, count = text.split("\t")
            out.append((n, fact, file, how, what, int(count) if count else None))
    return out


def generated_values():
    """What each generated file says, by the table's names: {"cxx": {...}, "python": {...}, "csharp": {...}}."""
    header, module, sharp = (generate.OUTPUTS[k] for k in ("cxx", "python", "csharp"))
    cxx = {}
    for name in cxx_constants(header):
        cxx[name] = cxx_value(header, name)[0]
    for m in re.finditer(r'inline constexpr char (\w+)\[\] = "([^"]*)";', text_of(header)):
        cxx[m.group(1)] = m.group(2)
    for m in re.finditer(r"std::array<std::uint32_t, \d+> (\w+) = \{([^}]*)\};", text_of(header)):
        cxx[m.group(1)] = [number(w) for w in m.group(2).split(",")]
    python = {}
    exec(compile(text_of(module), module, "exec"), python)
    python = {k: (v.decode() if isinstance(v, bytes) else v) for k, v in python.items() if k.isupper()}
    return {"cxx": cxx, "python": python, "csharp": {k: v for k, (v, _) in cs_constants(sharp).items()}}


def expected(fact, file, rows, made):
    """The generated value a place must equal: read back from the generated file of the place's language, where that
    file has the fact's names; otherwise from the table."""
    table = generate.values(rows)
    if file.endswith(CXX):
        known = made["cxx"]
    elif file.endswith(".py"):
        known = made["python"]
    elif file.endswith((".cs", ".axaml")):
        known = {name: made["csharp"][generate.pascal(name)] for name in table if generate.pascal(name) in made["csharp"]}
    else:
        known = table
    names = set(re.findall(r"[A-Z][A-Z0-9_]*", fact))
    if not names <= known.keys():
        known = table
    if fact in known:
        return known[fact]
    return generate.evaluate(fact, {k: v for k, v in known.items() if isinstance(v, int)})


def manifest_value(data, key):
    """What a manifest key holds. A key is a path of names; name[key=value] picks the table of a list that has that
    value, name[2] the third item; a trailing #1 takes the second of a comma-separated list of hexadecimal numbers."""
    key, _, item = key.partition("#")
    for name, pick in re.findall(r"([\w-]+)(?:\[([^\]]*)\])?(?:\.|$)", key):
        if not isinstance(data, dict) or name not in data:
            raise Missing(f"no {name} in the manifest ({key})")
        data = data[name]
        if pick.isdigit():
            data = data[int(pick)]
        elif pick:
            field, _, wanted = pick.partition("=")
            chosen = [t for t in data if str(t.get(field)) == wanted]
            if len(chosen) != 1:
                raise Missing(f"{len(chosen)} tables of {name} have {pick} in the manifest")
            data = chosen[0]
    return int(data.split(",")[int(item)], 16) if item else data


def check(rows=None):
    """Every place read and compared. Returns (problems, found): found[row name] = [(file, line)] of the places whose
    value was equal; the row a place belongs to is the first name of its fact (without _AT or _WORD)."""
    rows = rows or generate.load()
    made = generated_values()
    names = {r.name for r in rows}
    problems, found, absent = [], {r.name: [] for r in rows}, set()
    for n, fact, file, how, what, count in places():
        owner = re.match(r"[A-Z][A-Z0-9_]*", fact).group()
        owner = next((o for o in (owner, owner.removesuffix("_AT"), owner.removesuffix("_WORD")) if o in names), None)
        if owner is None:
            problems.append(f"places.tsv:{n}: {fact} names no row of the table")
            continue
        if left_out(file):
            absent.add(owner)
            continue
        try:
            want = expected(fact, file, rows, made)
            got = uses(file, what, want) if how == "use" else read(file, how, what)
        except (Missing, ValueError, re.error) as e:
            problems.append(f"places.tsv:{n}: {fact}: {e}")
            continue
        if count is not None and len(got) != count:
            lines = ", ".join(str(line) for _, line in got) or "none"
            problems.append(f"places.tsv:{n}: {fact}: {file} has {len(got)} matches of {what!r} (lines {lines}), not {count}")
        for value, line in got:
            if value != want:
                shown = (f"0x{value:X}", f"0x{want:X}") if isinstance(value, int) and isinstance(want, int) else (value, want)
                problems.append(f"places.tsv:{n}: {file}:{line} says {shown[0]}, the table's {fact} is {shown[1]}")
            else:
                found[owner].append((file, line))
    # the schema as the table's source lines name it: its path in the protocol's checkout, the same wherever that is
    schema = os.path.relpath(generate.SCHEMA, parts.checkout("REPO_PROTOCOL")).replace(os.sep, "/")
    replay_schema = os.path.join(os.path.dirname(schema), "replay.toml")
    for r in rows:
        if r.schema:
            want = replay_schema if r.schema == "replay" else schema
            if r.source.rpartition(":")[0] != want:
                problems.append(f"game.tsv:{r.line}: {r.name}: its value is the {r.schema} schema's, so its source is "
                                f"{want}, not {r.source}")
            continue
        if not found[r.name] and r.name in absent:
            continue                                         # its places are all in a part this checkout has not
        if not found[r.name]:
            problems.append(f"game.tsv:{r.line}: {r.name}: no place in today's sources proves this value")
            continue
        # The source names the file a place of the row stands in. Its line is a pointer for a reader and is not part
        # of the proof: an edit above it shifts it, and a check that failed for that would fail every commit that
        # touches a file with a place (--sources brings the lines up to date).
        file, _, line = r.source.rpartition(":")
        if not left_out(file) and not any(f == file for f, _ in found[r.name]):
            problems.append(f"game.tsv:{r.line}: {r.name}: its source {r.source} is not where the value stands (no place "
                            f"of it is in {file}; places.py --sources rewrites it)")
    return problems, found


def stale_lines(rows=None):
    """The rows whose source line is no longer a line of one of their places (the file is right): [(row, lines)]."""
    rows = rows or generate.load()
    _, found = check(rows)
    out = []
    for r in rows:
        file, _, line = r.source.rpartition(":")
        near = sorted(l for f, l in found[r.name] if f == file)
        if near and int(line) not in near:
            out.append((r, near))
    return out


def rewrite_sources():
    """game.tsv's source lines moved to where the places are found now: the place of the same file nearest the old
    line; when the file holds none any more (it was split), the row's first place."""
    rows = generate.load()
    _, found = check(rows)
    with open(generate.TABLE, encoding="utf-8") as f:
        lines = f.read().split("\n")
    changed = 0
    for r in rows:
        if r.schema:
            continue   # its source is the schema's line (check)
        file, _, line = r.source.rpartition(":")
        near = sorted((abs(l - int(line)), l) for f, l in found[r.name] if f == file)
        if not near and found[r.name]:
            file, near = found[r.name][0][0], [(1, found[r.name][0][1])]
        if near and near[0][0] != 0:
            cells = lines[r.line - 1].split("\t")
            cells[6] = f"{file}:{near[0][1]}"
            lines[r.line - 1] = "\t".join(cells)
            changed += 1
    with open(generate.TABLE, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))
    return changed


if __name__ == "__main__":
    if sys.argv[1:] == ["--sources"]:
        print(f"places: {rewrite_sources()} source lines rewritten")
    problems, found = check()
    print("\n".join(problems + [f"note: game.tsv:{r.line}: {r.name}: line {r.source.rpartition(':')[2]} of its source moved "
                                 f"(now {', '.join(map(str, near))}); --sources rewrites it" for r, near in stale_lines()]))
    print(f"places: {sum(map(len, found.values()))} values in today's sources equal the table's, for "
          f"{sum(1 for v in found.values() if v)} of {len(found)} rows; {len(problems)} problems")
    sys.exit(1 if problems else 0)
