#!/usr/bin/env python3
"""manifest.py: what a build was made from, written into the build's own output folder as build-manifest.json.

  manifest.py recompiler WORK                      -> WORK/work/dolrecomp-port/build-manifest.json
  manifest.py runner WORK                          -> WORK/ModernGekko/build/build-manifest.json
  manifest.py module-begin WORK NAME [--switches F --bundle DIR --profile F --extra "ADDR:FILE ..." --tool T]
                                                   -> WORK/work/cg-NAME.inputs.json, before the build starts
  manifest.py module-finish WORK NAME              -> WORK/work/mod-cg-NAME/build-manifest.json
  manifest.py compare A B                          the entries in which two manifests differ (not `made`)
  manifest.py show FILE                            the short form: what, from what, complete or not
  manifest.py check FILE                           status 0 only for a complete manifest (ask before anything ships)

A manifest holds: the repository's commit (and every file that differs from it), each source tree's commit and tree
hash with its uncommitted changes, the SHA-1 of every patch, of the build scripts as the work folder runs them and
of the module template, every switch, the SHA-1 of every input (the game's code from the disc, the bundle, the
movie player's images, the profile), the tools' versions, and the SHA-1 of what was built. `complete` is true only
when every one of these was found, the inputs did not change during the build and nothing differs from a commit;
`missing` says what was not. A build without a complete manifest is a test build, not a build anyone ships: the
build scripts say so in their last line, and `check` answers with its status. Hashes only: no game data."""
import argparse, glob, hashlib, json, os, platform, re, subprocess, sys, time

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, "..", "setup"))
import tools as finder  # noqa: E402  (port/setup/tools.py: where the Windows tools are)
root = os.path.abspath(os.path.join(here, "..", ".."))
MODULE_FILE = "gGFZE01_recomp.dll"


def layout():
    """port/setup/pins.tsv as a dictionary (repository paths, version pins, the hashes of what the disc gives), this
    PC's rows in ~/.config/piconet/pins.tsv over them, as lib.sh reads them."""
    return finder.layout()


_found = {}


def windows_tools(lay):
    """LAY with the Windows tools where port/setup/tools.py finds them (TOOL_VCVARS_WIN, TOOL_LLVM_WIN,
    TOOL_PYTHON_WIN; "" for one not found, which the manifest then lacks). Looked for once a run."""
    if not _found:
        _found.update({name: value or "" for name, value in finder.find(lay)[0].items()})
    return {**lay, **_found}


def sha1(path):
    if not path or not os.path.isfile(path):
        return None
    h = hashlib.sha1()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def run(command, cwd=None):
    try:
        done = subprocess.run(command, cwd=cwd, capture_output=True, timeout=300)
    except (OSError, subprocess.TimeoutExpired):
        return None
    return None if done.returncode else (done.stdout + done.stderr).decode("utf-8", "replace").replace("\r", "").strip("\n")


def win(path):
    return run(["wslpath", "-w", path]) or path


def repository():
    status = run(["git", "status", "--porcelain"], root)
    return {"commit": run(["git", "rev-parse", "HEAD"], root), "branch": run(["git", "branch", "--show-current"], root),
            "differs_from_commit": None if status is None else [line[3:] for line in status.splitlines()]}


def tree(work, folder, git):
    """One source tree in the work folder: its commit, its content (the tree hash) and what is not committed."""
    at = os.path.join(work, folder)
    if not os.path.exists(os.path.join(at, ".git")):
        return {"folder": folder, "commit": None, "tree": None}
    g = lambda *a: run([git, "-C", win(at), *a])
    status = g("status", "--porcelain", "--ignore-submodules=all")
    out = {"folder": folder, "commit": g("rev-parse", "HEAD"), "tree": g("rev-parse", "HEAD^{tree}"),
           "uncommitted": None if status is None else [line[3:] for line in status.splitlines()]}
    if out["uncommitted"]:
        out["uncommitted_diff_sha1"] = hashlib.sha1((g("diff", "HEAD") or "").encode()).hexdigest()
    return out


def files(folder, pattern="*"):
    """name -> SHA-1 for the files of a folder (not below it)."""
    return {os.path.basename(p): sha1(p) for p in sorted(glob.glob(os.path.join(folder, pattern))) if os.path.isfile(p)}


def scripts(work, lay):
    """The build's own files as this build ran them: the superbuild (CMakeLists.txt, CMakePresets.json and
    port/build/cmake/*.cmake), which the build reads from this checkout, so each is the tracked file unless it differs
    from the commit (repository.differs_from_commit); and fastlookup.py, the recompiler's tree's (WORK/DolRecomp/tools,
    in the fork's commit, which `trees` checks), as the module's build runs it."""
    changed = set(repository()["differs_from_commit"] or [])
    names = ["CMakeLists.txt", "CMakePresets.json"] + sorted(
        os.path.relpath(p, root).replace(os.sep, "/") for p in glob.glob(os.path.join(root, lay["REPO_BUILD"], "cmake", "*.cmake")))
    out = {name: {"sha1": sha1(os.path.join(root, name)), "is_tracked_file": name not in changed} for name in names}
    fast = sha1(os.path.join(work, "DolRecomp", "tools", "fastlookup.py"))
    out["fastlookup.py"] = {"sha1": fast, "is_tracked_file": fast is not None}
    return out


def tools(work, lay, recompiler=None):
    lay = windows_tools(lay)
    llvm = run(["wslpath", "-u", lay["TOOL_LLVM_WIN"]]) or ""
    first = lambda text: next((line.strip() for line in (text or "").splitlines() if "version" in line.lower()), None)
    out = {"clang": first(run([os.path.join(llvm, "bin", "clang.exe"), "--version"])),
           "llvm_profdata": first(run([os.path.join(llvm, "bin", "llvm-profdata.exe"), "--version"])),
           "msvc": None, "windows_sdk": None, "cmake": None, "ninja": None, "python_windows": None,
           "python_wsl": platform.python_version(), "git_windows": run([lay["TOOL_GIT"], "--version"])}
    # the Windows tools as the build scripts find them: tools.bat prints name=value lines
    bat = win(os.path.join(root, lay["REPO_BUILD"], "win", "tools.bat"))
    for line in (run(["cmd.exe", "/c", bat, lay["TOOL_VCVARS_WIN"], lay["TOOL_PYTHON_WIN"]], work) or "").splitlines():
        name, _, value = line.partition("=")
        if name in out and value.strip():
            out[name] = value.strip().rstrip("\\")
    if recompiler:
        out["recompiler"] = {"file": os.path.relpath(recompiler, work), "sha1": sha1(recompiler),
                             "manifest_sha1": sha1(os.path.join(os.path.dirname(recompiler), "build-manifest.json"))}
    return out


def switches(path):
    """The switches file (`env: set A=B&& set C=D&& `) as a dictionary, with the file's hash."""
    if not os.path.isfile(path):
        return {"file": path, "sha1": None, "values": None}
    with open(path) as f:
        text = f.read()
    values = dict(part.strip()[4:].split("=", 1) for part in re.sub(r"^env: ", "", text).split("&&") if part.strip().startswith("set "))
    return {"file": os.path.relpath(path, root) if path.startswith(root) else path, "sha1": sha1(path), "values": values}


def common(work, lay, kind, name):
    git = lay["TOOL_GIT"]
    return {"schema": 1, "kind": kind, "name": name, "complete": False, "missing": [],
            "repository": repository(), "build_scripts": scripts(work, lay),
            "trees": {"dolrecomp": tree(work, "DolRecomp", git), "moderngekko": tree(work, "ModernGekko", git),
                      "recompcore": tree(work, "ModernGekko/vendor/dolphin", git),
                      "recompcore_dolrecomp": tree(work, "ModernGekko/vendor/dolphin/DolRecomp", git),
                      "module_template": tree(work, "work/module-template", git)},
            "made": {"work": work, "host": platform.node(), "at": time.strftime("%Y-%m-%dT%H:%M:%S%z")}}


def module_inputs(work, a):
    game = os.path.join(work, "extracted", "GFZE01")
    extra = [{"address": e.split(":", 1)[0], "file": os.path.basename(e.split(":", 1)[1]), "sha1": sha1(e.split(":", 1)[1])}
             for e in (a.extra or "").split()]
    return {"main.dol.orig": sha1(os.path.join(game, "sys", "main.dol.orig")),
            "main.rel": sha1(os.path.join(work, "derived", "main.rel")),
            "bundle": {n: sha1(os.path.join(a.bundle, n)) for n in ("mod.bin", "patches.json", "symbols.json", "manifest.json")},
            "extra": extra, "profile": {"file": a.profile, "sha1": sha1(a.profile)} if a.profile else "none",
            "module_template": files(os.path.join(work, "work", "module-template"))}


def unset(value, path=""):
    """The paths of every entry that is None: what the manifest could not establish."""
    if value is None:
        return [path]
    if isinstance(value, dict):
        return [p for k, v in value.items() for p in unset(v, f"{path}.{k}" if path else k)]
    if isinstance(value, list):
        return [p for i, v in enumerate(value) for p in unset(v, f"{path}[{i}]")]
    return []


def finish(m, to):
    """Decides `complete`, writes the manifest, and returns the exit status."""
    m["missing"] += unset({k: v for k, v in m.items() if k != "missing"})
    if m["repository"].get("differs_from_commit"):
        m["missing"].append("repository: files differ from the commit (" + ", ".join(m["repository"]["differs_from_commit"][:5]) + ")")
    for name, t in m["trees"].items():
        if t.get("uncommitted"):
            m["missing"].append(f"trees.{name}: uncommitted changes")
    m["complete"] = not m["missing"]
    with open(to, "w") as f:
        json.dump(m, f, indent=1, sort_keys=True)
        f.write("\n")
    print(f"manifest: {to}: " + ("complete" if m["complete"] else f"NOT COMPLETE ({len(m['missing'])}): " + "; ".join(m["missing"][:6])))
    return 0 if m["complete"] else 1


def chunks(folder):
    """Count and one hash over the recompiler's objects (name and SHA-1 of every chunk_*.o, in name order)."""
    names = sorted(glob.glob(os.path.join(folder, "chunk_*.o")))
    h = hashlib.sha1()
    for p in names:
        h.update(f"{os.path.basename(p)} {sha1(p)}\n".encode())
    return {"count": len(names), "sha1": h.hexdigest() if names else None}


def flat(value, path=""):
    if isinstance(value, dict):
        return {p: v for k, x in value.items() for p, v in flat(x, f"{path}.{k}" if path else k).items()}
    return {path: value}


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("what", choices=["recompiler", "runner", "module-begin", "module-finish", "compare", "show", "check"])
    p.add_argument("work"); p.add_argument("name", nargs="?")
    p.add_argument("--switches"); p.add_argument("--bundle"); p.add_argument("--profile"); p.add_argument("--extra")
    p.add_argument("--tool", default="dolrecomp-port")
    p.add_argument("--host-timers", choices=["ON", "OFF"], default=None,
                   help="runner: the core's scoped timers built in (port/moderngekko/build.sh HOST_TIMERS=1)")
    a = p.parse_args()
    if a.what in ("show", "check"):
        if not os.path.isfile(a.work):
            print(f"manifest: none at {a.work}: not a build anyone ships")
            return 1
        with open(a.work) as f:
            m = json.load(f)
        out = m.get("outputs", {})
        print(f"{m['kind']} {m.get('name') or ''}: " + ("complete" if m["complete"] else "NOT COMPLETE: " + "; ".join(m["missing"])))
        print(f"  repository {str(m['repository']['commit'])[:8]}; trees " + ", ".join(f"{k} {str(t['commit'])[:8]}" for k, t in m["trees"].items()))
        print("  built: " + ", ".join(f"{k} {str(v.get('sha1') if isinstance(v, dict) else v)[:8]}" for k, v in out.items()))
        return 0 if m["complete"] or a.what == "show" else 1
    if a.what == "compare":
        with open(a.work) as f, open(a.name) as g:
            one, two = flat(json.load(f)), flat(json.load(g))
        differ = sorted(k for k in set(one) | set(two) if one.get(k) != two.get(k) and not k.startswith("made."))
        for k in differ:
            print(f"{k}: {one.get(k)} | {two.get(k)}")
        print(f"manifest: {len(differ)} entries differ" if differ else "manifest: equal but for where and when")
        return 1 if differ else 0
    lay, work = layout(), os.path.abspath(a.work)
    if a.what == "recompiler":
        out = os.path.join(work, "work", "dolrecomp-port")
        m = common(work, lay, "recompiler", "dolrecomp-port")
        m["tools"] = tools(work, lay)
        log = os.path.join(work, "work", "dolrecomp-port.log")
        digest = re.search(r"Codegen source digest: ([0-9a-f]+)", open(log, errors="replace").read()) if os.path.isfile(log) else None
        m["codegen_source_digest"] = digest.group(1) if digest else None      # the recompiler's own hash of its sources: part of every cache key
        m["outputs"] = {"dolrecomp.exe": {"sha1": sha1(os.path.join(out, "dolrecomp.exe"))}}
        return finish(m, os.path.join(out, "build-manifest.json"))
    if a.what == "runner":
        out = os.path.join(work, "ModernGekko", "build")
        m = common(work, lay, "runner", "moderngekko-run")
        m["tools"] = tools(work, lay)
        source = os.path.join(work, "port", "src")                           # the core and the adapter as the compiler read them
        m["inputs"] = {"port_sources": {os.path.relpath(q, source): sha1(q) for q in sorted(glob.glob(os.path.join(source, "**", "*"), recursive=True)) if os.path.isfile(q)} or None}
        m["outputs"] = {"moderngekko-run.exe": {"sha1": sha1(os.path.join(out, "moderngekko-run.exe"))}}
        # a profiling runner (the core's scoped timers) is never a build anyone ships; not said: not known
        m["host_timers"] = {"ON": True, "OFF": False}.get(a.host_timers)
        if m["host_timers"]:
            m["missing"].append("a profiling runner (HOST_TIMERS=1, the core's scoped timers): never shipped")
        return finish(m, os.path.join(out, "build-manifest.json"))
    record = os.path.join(work, "work", f"cg-{a.name}.inputs.json")
    if a.what == "module-begin":
        m = common(work, lay, "module", a.name)
        m["switches"] = switches(a.switches)
        m["switches"]["fixed"] = {"DOLRECOMP_MEM2_BASE": "0xA0000000", "recompiler_arguments": "-j16 --backend=llvm --native-abi off --cpu gekko --gamecube", "module_compiler": "clang"}
        m["arguments"] = {"switches": a.switches, "bundle": a.bundle, "profile": a.profile, "extra": a.extra, "tool": a.tool}
        m["inputs"] = module_inputs(work, a)
        m["tools"] = tools(work, lay, os.path.join(work, "work", a.tool, "dolrecomp.exe"))
        with open(record, "w") as f:
            json.dump(m, f, indent=1, sort_keys=True)
        print(f"manifest: inputs recorded in {record}")
        return 0
    if not os.path.isfile(record):
        sys.exit(f"manifest: no record of the inputs ({record}): the build was not started by build-module.sh")
    with open(record) as f:
        m = json.load(f)
    again = module_inputs(work, argparse.Namespace(**m["arguments"]))
    if again != m["inputs"]:
        m["missing"].append("inputs changed while the build ran: " + ", ".join(sorted(k for k in again if again[k] != m["inputs"][k])))
    if tree(work, "DolRecomp", lay["TOOL_GIT"]) != m["trees"]["dolrecomp"] or tree(work, "ModernGekko/vendor/dolphin", lay["TOOL_GIT"]) != m["trees"]["recompcore"]:
        m["missing"].append("a source tree changed while the build ran")
    built, made = os.path.join(work, "work", f"cg-{a.name}"), os.path.join(work, "work", f"mod-cg-{a.name}")
    m["inputs"]["recompiler_input"] = sha1(os.path.join(built, "main-combo.dol"))
    m["outputs"] = {MODULE_FILE: {"sha1": sha1(os.path.join(made, MODULE_FILE)), "bytes": os.path.getsize(os.path.join(made, MODULE_FILE)) if os.path.isfile(os.path.join(made, MODULE_FILE)) else None},
                    "chunks": chunks(os.path.join(built, "generated", "chunks")),
                    "native_tables.c": {"sha1": sha1(os.path.join(built, "generated", "native_tables.c"))},
                    "generated.h": {"sha1": sha1(os.path.join(built, "generated", "generated.h"))},
                    "module_tables.inc": {"sha1": sha1(os.path.join(made, "module_tables.inc"))}}
    log = os.path.join(work, "work", f"cg-{a.name}-mod.log")       # which Python CMake took for the module's tables
    found = re.search(r"Found Python3: (\S+) \(found version \"([^\"]+)\"", open(log, errors="replace").read()) if os.path.isfile(log) else None
    m["tools"]["module_python"] = f"{found.group(2)} ({found.group(1)})" if found else None
    m["made"]["finished"] = time.strftime("%Y-%m-%dT%H:%M:%S%z")
    return finish(m, os.path.join(made, "build-manifest.json"))


if __name__ == "__main__":
    sys.exit(main())
