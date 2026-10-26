"""Builds the mod bundle the Dolphin fork carries (dolphin-fzgx Data/Sys/fzgx/): the same files for every race, so
nothing but the fork runs at race time (the race's own values come from the match file, which the fork writes into
mod memory: memmap.RACE, memmap.NAMES and the seat routines' slot 'FZSI').
  fork_bundle.py OUTDIR   -> OUTDIR/mod.bin (the image, memmap.IMAGE), OUTDIR/codes.txt (Gecko code lines: boot to the
                        race (a replay's playback too), race setup, seats, the mod's hooks), OUTDIR/manifest.json
The image is built with neutral slot values (seat 0, uniform rules, delay 2); the fork writes the race's seat, player
mask and delay into the slot (+4, +40, +44) before the game reaches the race."""
import hashlib, json, os, subprocess, sys
here = os.path.dirname(os.path.abspath(__file__)); root = os.path.join(here, "..", ".."); py = sys.executable
routines = os.path.join(here, "routines")
sys.path.insert(0, routines)
import memmap

out = sys.argv[1]
os.makedirs(out, exist_ok=True)
image = os.path.join(out, "mod.bin")
run = lambda *a: subprocess.run([py, *a], capture_output=True, text=True, check=True).stdout.strip()
hooks = run(os.path.join(routines, "modbuild.py"), image, "0", "0", "1", "0", "2")
# "Boot to race": a race from a match file, and a replay's playback, which plays as the race
sections = [("Boot to race", run(os.path.join(routines, "direct.py"), "--params")),
            ("Race setup", run(os.path.join(routines, "setup.py"), "--params")),
            ("Seats", run(os.path.join(routines, "seats.py"), "same:0", "--params")),
            ("Mod hooks", hooks)]
with open(os.path.join(out, "codes.txt"), "w", newline="\n") as f:
    for name, lines in sections:
        f.write(f"${name}\n{lines}\n")
data = open(image, "rb").read()
slot = data.find(b"FZSI")
assert slot >= 0 and memmap.IMAGE + slot == 0x81800010, hex(slot)
commit = subprocess.run(["git", "-C", root, "describe", "--always", "--dirty"], capture_output=True, text=True).stdout.strip()
manifest = {"version": 1, "commit": commit, "image": memmap.IMAGE, "image_sha1": hashlib.sha1(data).hexdigest(),
            "slot": memmap.IMAGE + slot, "race": memmap.RACE, "names": memmap.NAMES, "name_size": memmap.NAME_SIZE}
json.dump(manifest, open(os.path.join(out, "manifest.json"), "w"), indent=1, sort_keys=True)
print(f"bundle: {len(data)} bytes of image, {sum(len(l.splitlines()) for _, l in sections)} code lines, {commit}")
