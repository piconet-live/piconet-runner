"""package_names.py: whose names no copy may hold, gathered from this PC when a copy is made or searched
(port/build/package_identities.py searches for them). Nothing here is written anywhere: the list exists while the
script runs.

Each source is asked, and each is in the result as gathered or not:

  login      the login's name, its home folder's name, its full name as the system has it
  host       the host name, whole and up to its first dot
  windows    the Windows user's name and display name, the user folder's name and its short form (the 8.3 name
             an old program writes: FIRSTN~1), the computer's name; asked of Windows itself (powershell.exe)
  git:NAME   git's user, author and committer name and address as the repository in the folder NAME sees them
             (its own settings, the user's, the system's). A tree on a Windows drive is asked through Git for
             Windows, whose settings are another user's file
  more       the caller's file of names the PC does not know (a nickname, an older address): a line is a name, or
             a kind, a tab and a name; `#` begins a comment. A line that cannot be read is an error, not a skip

**A source that cannot be asked is a failure**: the caller refuses the copy (a copy is not searched for names that
were never gathered). The one way past it is to name the source as knowingly left out (`without`): a PC that is no
WSL has no Windows side, a checkout may have no sibling repository. A source with nothing set (no display name, no
git user) is gathered, with no name."""
import getpass, json, os, platform, re, shutil, socket, subprocess

WINDOWS = r"""
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$ErrorActionPreference = 'Stop'
$account = Get-CimInstance Win32_UserAccount -Filter "Name='$($env:USERNAME)' and Domain='$($env:USERDOMAIN)'"
$short = (New-Object -ComObject Scripting.FileSystemObject).GetFolder($env:USERPROFILE).ShortName
@{ user = $env:USERNAME; folder = $env:USERPROFILE; computer = $env:COMPUTERNAME; display = [string]$account.FullName; short = [string]$short } | ConvertTo-Json
"""
WINDOWS_KINDS = (("user", "the Windows user's name"), ("display", "the Windows user's display name"), ("folder", "the Windows user folder's name"),
                 ("short", "the Windows user folder's short name"), ("computer", "the Windows computer's name"))


class NotGathered(Exception):
    """A source could not be asked: why."""


def of_login():
    found = [("this PC's login name", getpass.getuser()), ("the name of the login's home folder", os.path.basename(os.path.expanduser("~").rstrip("/")))]
    try:
        import pwd
        found.append(("the login's full name", pwd.getpwuid(os.getuid()).pw_gecos.split(",")[0]))
    except (ImportError, KeyError):
        pass
    return found


def of_host():
    return [("this PC's host name", part) for host in sorted({socket.gethostname(), platform.node()}) for part in (host, host.split(".")[0])]


def of_windows():
    shell = shutil.which("powershell.exe")
    if not shell:
        raise NotGathered("no Windows side was found (powershell.exe)")
    try:
        done = subprocess.run([shell, "-NoProfile", "-NonInteractive", "-Command", WINDOWS], capture_output=True, stdin=subprocess.DEVNULL, timeout=90,
                              cwd=os.path.dirname(shell))
        said = json.loads(done.stdout.decode("utf-8-sig", "replace")) if done.returncode == 0 else None
    except (OSError, subprocess.TimeoutExpired, ValueError):
        said = None
    if not isinstance(said, dict) or not all(said.get(key) for key in ("user", "folder", "computer")):
        raise NotGathered("Windows did not answer (powershell.exe: its user, display name, user folder and computer)")
    said["folder"] = re.split(r"[\\/]", said["folder"].rstrip("\\/"))[-1]
    return [(kind, said.get(key) or "") for key, kind in WINDOWS_KINDS]


def of_git(repository):
    name = os.path.basename(os.path.abspath(repository))
    if not os.path.isdir(repository):
        raise NotGathered("no such folder")
    command = ["git", "-C", repository]
    if re.match(r"/mnt/[a-z]/", os.path.abspath(repository) + "/"):
        if not (shutil.which("git.exe") and shutil.which("wslpath")):
            raise NotGathered("a tree on a Windows drive, and no Git for Windows to ask (git.exe)")
        command = ["git.exe", "-C", subprocess.run(["wslpath", "-w", repository], capture_output=True, text=True).stdout.strip() or repository]
    try:
        inside = subprocess.run(command + ["rev-parse", "--git-dir"], capture_output=True, stdin=subprocess.DEVNULL, timeout=60)
        done = subprocess.run(command + ["config", "--get-regexp", r"^(user|author|committer)\.(name|email)$"], capture_output=True,
                              stdin=subprocess.DEVNULL, timeout=60)
    except (OSError, subprocess.TimeoutExpired):
        raise NotGathered("git could not be run")
    if inside.returncode or done.returncode not in (0, 1) or (done.returncode == 1 and done.stdout.strip()):
        raise NotGathered("no git repository there, or git did not answer")
    return [(f"git's {line.split(' ', 1)[0]} ({name})", line.split(" ", 1)[1]) for line in done.stdout.decode("utf-8", "replace").splitlines() if " " in line]


def of_more(path):
    found = []
    try:
        with open(path, encoding="utf-8") as f:
            lines = f.read().splitlines()
    except (OSError, UnicodeDecodeError) as why:
        raise NotGathered(f"the file of more names cannot be read ({type(why).__name__})")
    for number, line in enumerate(lines, 1):
        kind, tab, text = line.partition("\t")
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        if tab and not text.strip():
            raise NotGathered(f"line {number} of the file of more names has a kind and no name")
        found.append((kind.strip() or "a name the caller's file adds", text) if tab else ("a name the caller's file adds", line))
    return found


def gather(repositories, more=None, without=()):
    """-> ([(kind, text)], [(source, "gathered" | "left out" | "NOT GATHERED", in words)]). The texts are this PC's
    people's names: they are searched for and never printed. The second list has every source that was asked."""
    asked = [("login", of_login), ("host", of_host), ("windows", of_windows)]
    asked += [("git:" + os.path.basename(os.path.abspath(r)), lambda r=r: of_git(r)) for r in dict.fromkeys(repositories)]
    asked += [("more", lambda: of_more(more))] if more else []
    found, sources = [], []
    for source, ask in asked:
        if source in without:
            sources.append((source, "left out", "by the caller's word"))
            continue
        try:
            names = [(kind, text.strip()) for kind, text in ask() if text and text.strip()]
        except NotGathered as why:
            sources.append((source, "NOT GATHERED", str(why)))
            continue
        except (KeyError, OSError) as why:
            sources.append((source, "NOT GATHERED", type(why).__name__))
            continue
        found += names
        sources.append((source, "gathered", f"{len(names)} name{'s' if len(names) != 1 else ''}" if names else "none is set"))
    seen, out = set(), []
    for kind, text in found:
        if (kind, text.lower()) not in seen:
            seen.add((kind, text.lower()))
            out.append((kind, text))
    return out, sources


def not_gathered(sources):
    """What a caller refuses for: the sources that could not be asked, in words; "" when every one was."""
    failed = [f"{source} ({why})" for source, state, why in sources if state == "NOT GATHERED"]
    return "; ".join(failed)
