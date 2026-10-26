"""Gecko code fixing the race setup's randomness, so that a race depends only on the setup and the inputs, not on
each game's boot history:  setup.py [SHUFFLE] [CPU_SEED]   (defaults 0x00001234 0x12345678)
- SHUFFLE: the DOL's generator (0x801A63C0), which shuffles the CPUs' machines at setup, is held at this value
  while main.rel's mode (.bss 0x960) is not MD_GAME (2), i.e. up to the race;
- CPU_SEED: fn_1_F47AC (the per-race CPU seed, normally OSGetTick) returns this value.
These are the values the game's own replay header saves. Online, the server supplies them.
--params: both come from the race block (memmap.RACE, written by the Dolphin fork from the match file): the seed
function loads its word, and a C0 code copies the shuffle word, so the code is the same for every race."""
import os, sys
PARAMS = "--params" in sys.argv
sys.argv = [a for a in sys.argv if a != "--params"]
shuffle = int(sys.argv[1], 0) if len(sys.argv) > 1 else 0x00001234
seed = int(sys.argv[2], 0) if len(sys.argv) > 2 else 0x12345678
f47ac = 0xDC + 0xF47AC        # main.rel's .text starts 0xDC after its header
mode = 0x1BA040 + 0x960       # .bss 0x960, the current mode
if PARAMS:
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import memmap
    seed_at, shuffle_at = memmap.RACE + memmap.RACE_CPUSEED, memmap.RACE + memmap.RACE_SHUFFLE
    lo = lambda a: a & 0xFFFF
    print("\n".join([
        "48000000 800030C8", "DE000000 80008180",
        f"14{f47ac:06X} {0x3C600000 | memmap.hi(seed_at):08X}",            # lis r3, RACE@h
        f"14{f47ac + 4:06X} {0x80630000 | lo(seed_at):08X}",               # lwz r3, CPUSEED(r3)
        f"14{f47ac + 8:06X} 4E800020",                                     # blr
        f"3A{mode:06X} 00000002",
        "C0000000 00000003",                                               # 0x801A63C0 = the race block's SHUFFLE
        f"{0x3D800000 | memmap.hi(shuffle_at):08X} {0x816C0000 | lo(shuffle_at):08X}",   # lis r12; lwz r11, (r12)
        "3D80801A 916C63C0",                                               # lis r12, 0x801A; stw r11, 0x63C0(r12)
        "4E800020 00000000",
        "E2000002 00000000", "E0000000 80008000"]))
    sys.exit(0)
print("\n".join([
    "48000000 800030C8", "DE000000 80008180",                # po = main.rel's header, if loaded
    f"14{f47ac:06X} {0x3C600000 | (seed >> 16):08X}",        # lis r3, SEED@h
    f"14{f47ac + 4:06X} {0x60630000 | (seed & 0xFFFF):08X}", # ori r3, r3, SEED@l
    f"14{f47ac + 8:06X} 4E800020",                           # blr
    f"3A{mode:06X} 00000002",                                # if mode != MD_GAME
    f"041A63C0 {shuffle:08X}",                               #   0x801A63C0 = SHUFFLE
    "E2000002 00000000", "E0000000 80008000"]))
