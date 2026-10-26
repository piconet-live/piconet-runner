#!/usr/bin/env python3
"""tools.py: the Windows build tools, found as each one's own documented way finds it; pins.tsv says only which
version is needed (the VERSION_ rows), never where a tool is.

  Visual Studio   vswhere.exe in %ProgramFiles(x86)%\\Microsoft Visual Studio\\Installer, the installer's own query:
                  the newest install in VERSION_VS's range with the C++ x64 tools -> its vcvars64.bat
  LLVM            clang.exe on PATH -> the folder above its bin (it must hold lib/cmake: the recompiler links it)
  Python          the py launcher (py -VERSION_PYTHON), else the first python.exe on PATH of that version
  cmake, ninja    on PATH as the build scripts see it (vcvars, then Python and its Scripts in front): --say only
  git             TOOL_GIT (git.exe: Git for Windows on PATH)

A PC that keeps a tool elsewhere names it in ~/.config/piconet/pins.tsv (outside every checkout, read over pins.tsv
as port/setup/lib.sh reads it): TOOL_VCVARS_WIN (the vcvars64.bat), TOOL_LLVM_WIN (LLVM's folder), TOOL_PYTHON_WIN
(Python's folder). A row there is taken as it is written and its version checked.

  tools.py --shell   NAME='value' lines for the shell (lib.sh find_tools); one line on standard error for each tool
                     missing or of another version, and status 1
  tools.py --say     the same check for a person: found/MISSING lines, with cmake's and ninja's versions
Python imports find(layout()) for a dictionary of the same names (port/build/manifest.py). Standard library only."""
import os
import re
import shlex
import subprocess
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
PINS = os.path.join(ROOT, "port", "setup", "pins.tsv")
LOCAL = os.path.expanduser("~/.config/piconet/pins.tsv")
HOW = "or name it in ~/.config/piconet/pins.tsv"
VC_TOOLS = "Microsoft.VisualStudio.Component.VC.Tools.x86.x64"
YEARS = {"16": "2019", "17": "2022", "18": "2026"}   # Visual Studio's major version and the name it is sold by


def layout(pins=PINS, local=LOCAL):
    """pins.tsv as a dictionary (repository paths, version pins, the hashes of what the disc gives), LOCAL's rows
    (a PC's own tools and disc) over them."""
    out = {}
    for table in (pins, local):
        if not table or not os.path.isfile(table):
            continue
        with open(table, encoding="utf-8") as f:
            for line in f:
                cells = line.rstrip("\n").split("\t")
                if len(cells) >= 2 and cells[0] not in ("name", "") and not cells[0].startswith("#"):
                    out[cells[0]] = cells[1]
    return out


def run(command):
    """COMMAND's standard output without CRs, or None. Windows programs are started from /mnt/c, so that a
    checkout on WSL's own filesystem gives them no \\\\wsl.localhost folder (cmd.exe refuses one)."""
    try:
        done = subprocess.run(command, capture_output=True, timeout=120, cwd="/mnt/c" if os.path.isdir("/mnt/c") else None)
    except (OSError, subprocess.TimeoutExpired):
        return None
    return done.stdout.decode("utf-8", "replace").replace("\r", "").strip() if done.returncode == 0 else None


def unix(path):
    """PATH as WSL spells it (a Windows path through wslpath; one that is WSL's already as it is)."""
    return path if path.startswith("/") else run(["wslpath", "-u", path]) or path


def where(name):
    """Every NAME.exe on PATH as Windows sees it, in order."""
    return (run(["where.exe", name]) or "").splitlines()


def version_of(text, pattern):
    found = re.search(pattern, text or "")
    return found[1] if found else None


def visual_studio(lay):
    want = lay.get("VERSION_VS", "")
    major = want.lstrip("[(").split(".")[0]
    named = f"Visual Studio {YEARS[major]} Build Tools" if major in YEARS else "Visual Studio Build Tools"
    need = (f"{named} (version {want}) with the C++ workload (MSVC x64), visualstudio.microsoft.com/downloads: "
            f"install it, {HOW} as TOOL_VCVARS_WIN (its vcvars64.bat)")
    if lay.get("TOOL_VCVARS_WIN"):
        vcvars = lay["TOOL_VCVARS_WIN"]
        return (vcvars, None) if os.path.isfile(unix(vcvars)) else (None, f"no vcvars64.bat at TOOL_VCVARS_WIN {vcvars}: {need}")
    folder = run(["cmd.exe", "/c", "echo %ProgramFiles(x86)%"])
    vswhere = os.path.join(unix(folder), "Microsoft Visual Studio", "Installer", "vswhere.exe") if folder else ""
    if not os.path.isfile(vswhere):
        return None, f"missing: {need}"
    found = run([vswhere, "-latest", "-products", "*", "-version", want, "-requires", VC_TOOLS, "-property", "installationPath"])
    if not found:
        others = run([vswhere, "-all", "-products", "*", "-requires", VC_TOOLS, "-property", "catalog_productLineVersion"])
        have = f" (here: {', '.join(sorted(set(others.split())))})" if others else ""
        return None, f"missing: {need}{have}"
    vcvars = found.splitlines()[0] + r"\VC\Auxiliary\Build\vcvars64.bat"
    return (vcvars, None) if os.path.isfile(unix(vcvars)) else (None, f"no vcvars64.bat in {found}: {need}")


def llvm(lay):
    want = lay.get("VERSION_LLVM", "")
    need = (f"LLVM {want} for Windows with lib/cmake (clang+llvm-{want}-x86_64-pc-windows-msvc, "
            f"github.com/llvm/llvm-project releases): put its bin on PATH, {HOW} as TOOL_LLVM_WIN (its folder)")
    folder = lay.get("TOOL_LLVM_WIN")
    if not folder:
        clang = where("clang")
        if not clang:
            return None, f"missing: {need}"
        folder = clang[0].rsplit("\\", 2)[0]
    at = unix(folder)
    have = version_of(run([os.path.join(at, "bin", "clang.exe"), "--version"]), r"clang version (\S+)")
    if have is None:
        return None, f"no clang.exe in {folder}\\bin: {need}"
    if have != want:
        return None, f"LLVM {have} at {folder}, but {want} is needed: {need}"
    if not os.path.isdir(os.path.join(at, "lib", "cmake", "llvm")):
        return None, f"LLVM {want} at {folder} has no lib/cmake/llvm (the installer leaves it out; the release archive has it): {need}"
    return folder, None


def python(lay):
    want = lay.get("VERSION_PYTHON", "")
    need = f"Python {want} for Windows (python.org): install it, {HOW} as TOOL_PYTHON_WIN (its folder)"
    ask = "import sys; print('%d.%d' % sys.version_info[:2], sys.base_prefix)"
    if lay.get("TOOL_PYTHON_WIN"):
        folder = lay["TOOL_PYTHON_WIN"]
        shown = run(["cmd.exe", "/c", f"echo {folder}"]) or folder        # cmd expands a %VARIABLE% as the build scripts do
        said = run([os.path.join(unix(shown), "python.exe"), "-c", ask])
        have = said.split(" ", 1)[0] if said else None
        if have != want:
            return None, (f"no python.exe at TOOL_PYTHON_WIN {folder}: {need}" if have is None else
                          f"Python {have} at TOOL_PYTHON_WIN {folder}, but {want} is needed: {need}")
        return folder, None
    launcher = where("py")
    candidates = [[unix(launcher[0]), f"-{want}"]] if launcher else []
    candidates += [[unix(p)] for p in where("python") if "\\WindowsApps\\" not in p]
    for command in candidates:
        said = run(command + ["-c", ask])
        if said and said.split(" ", 1)[0] == want:
            return said.split(" ", 1)[1], None
    return None, f"missing: {need}"


def find(lay):
    """TOOL_VCVARS_WIN, TOOL_LLVM_WIN, TOOL_PYTHON_WIN and TOOL_GIT as found (None for one not found), and the
    problems, one line each."""
    out, problems = {"TOOL_GIT": lay.get("TOOL_GIT") or "git.exe"}, []
    for name, look in (("TOOL_VCVARS_WIN", visual_studio), ("TOOL_LLVM_WIN", llvm), ("TOOL_PYTHON_WIN", python)):
        out[name], problem = look(lay)
        if problem:
            problems.append(problem)
    return out, problems


def build_tools(found, lay):
    """cmake's and ninja's versions as the build scripts see them (port/build/win/tools.bat), with lines for the
    ones missing or of another version than the one module n6 was built with (a note: not refused)."""
    bat = run(["wslpath", "-w", os.path.join(ROOT, "port", "build", "win", "tools.bat")])
    said = run(["cmd.exe", "/c", bat, found["TOOL_VCVARS_WIN"], found["TOOL_PYTHON_WIN"]]) or ""
    have = dict(line.partition("=")[::2] for line in said.splitlines())
    lines, missing = [], []
    for name, pin in (("cmake", "VERSION_CMAKE"), ("ninja", "VERSION_NINJA")):
        value, want = (have.get(name) or "").strip(), lay.get(pin, "")
        if not value or value.startswith("ECHO"):
            missing.append(f"missing: {name} ({want} built module n6): put it on PATH (cmake.org, ninja-build.org, or pip install {name} in Python's Scripts)")
        else:
            lines.append(f"found {name} {value}" + ("" if value.startswith(want) else f" (note: module n6 was built with {want})"))
    return lines, missing


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else ""
    if mode not in ("--shell", "--say"):
        sys.exit(__doc__)
    sys.stdout.reconfigure(line_buffering=True)    # found lines and MISSING lines (standard error) in order
    lay = layout()
    found, problems = find(lay)
    if mode == "--shell":
        for name, value in found.items():
            if value is not None:
                print(f"{name}={shlex.quote(value)}")
        for problem in problems:
            print(f"tools: {problem}", file=sys.stderr)
        sys.exit(1 if problems else 0)
    for name, value in found.items():
        if value is not None:
            print(f"tools: found {name} {value}")
    if found["TOOL_VCVARS_WIN"] and found["TOOL_PYTHON_WIN"]:
        lines, missing = build_tools(found, lay)
        print("\n".join(f"tools: {line}" for line in lines))
        problems += missing
    for problem in problems:
        print(f"tools: MISSING {problem.removeprefix('missing: ')}", file=sys.stderr)
    sys.exit(1 if problems else 0)


if __name__ == "__main__":
    main()
