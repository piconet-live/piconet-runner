"""The machine setting's curve as data (handbook/stat-pack-constants.md): the words of the patch list that make the
machine setting, fn_1_22B10 (block, s), read every
number of its curve from the seat's constants (memmap.SEAT_BLOCK, a record a seat, +SEAT_CURVE on) instead of
main.rel's pool. The core writes every seat's record for every race and replay: its stat pack's constants, or the
game's own (port/core/src/const_ranges.h, slots 7 to 61). Native form only: the fork's image does not have it, and
its runs are the stock game's curve. sites.py calls add().

The game's function stays, every instruction of it; only words of it are patched, each to read one number:

- **Which seat.** fn_1_22B10 gets the block (a copy on the caller's stack) and the setting, never the machine. Its
  two calls in a machine's step (fn_1_331EC 0x336B8 and 0x336F4, the racer in r27, its index at +4) become
  `setting_call_*`: in a race of ours with a racer index under 30, the seat's record's address goes into the word
  SEAT_SETTING before the call and 0 after it; otherwise the call is the game's own and the word stays 0. Its other
  callers (fn_1_2FDBC, the Grand Prix's stat check, which no race of ours installs; fn_1_139440, the menu's preview)
  run with the word 0.
- **Each number** (the table SITES, in the function's order): a site at the instruction that brings the number into
  its register. With SEAT_SETTING 0 it is the replaced word itself: the game's own load from the pool (r4,
  .rodata 0x840), byte for byte. Otherwise the number comes from the record: `load` and `double` replace the pool
  load (`lfs`/`lfd`) by one from the record into the same register; `fmr` replaces a copy of a pool number made
  earlier (0.0 or 1.5) by a load of its own; `after` runs the replaced word, then loads (the compare at 0x22B3C reads
  1.0 as the drift's floor; f0 then becomes the max speed's reference); `before` loads, then runs the replaced word
  (a number the game's register held for an earlier role: 2.0 at 0x22B80, 1.0 at 0x22E7C). The six loads of 0.0
  that only test the side of the setting (d against 0) are not sites.
- **Registers.** A site tests the mode word (a plain start runs the replaced word) and SEAT_SETTING with r12 and
  cr7, which the function never uses (it returns with them without meaning: the
  liveness proof covers both); r0 (the boost's flag from 0x22B14) and cr0 (live at 0x22E48) are never touched. A
  site writes the register the replaced word writes and, for `before`, the register it then reads, which the
  function writes before it reads it again (f4 at 0x22BD0, f3 nowhere after 0x22E7C).

Why the game's own numbers give the game's bits: each record number is the bits the replaced word put in its register
(the pool word, or the copy's source), and every other instruction is the game's; the record's double is the pool's
double (lfd at 0x22D14). The replay set shows it as well. Why stock paths are the game's: the word SEAT_SETTING is 0
outside the two calls of a race of ours, and every site then runs the replaced word with the registers as the game
left them."""
import memmap
from sites_seats import seat_constants

SETTING = memmap.SEAT_BLOCK + memmap.SEAT_SETTING
CURVE_FUNCTION = 0x22B10
CALLS = ((0x336B8, 0x4BFEF459), (0x336F4, 0x4BFEF41D))   # bl fn_1_22B10 in fn_1_331EC, the racer r27

# (main.rel .text offset, the game's word, kind, the float register, the slot's name, its offset in a seat's record).
# The names and offsets are protocol/stats.toml's (test_curve_sites.py holds them together); what each number does:
# port/core/src/const_ranges.h.
SITES = (
    (0x22B1C, 0xC004001C, "load", 0, "SettingMiddle", 0x1C),
    (0x22B30, 0xC0040014, "load", 0, "DriftFastFloor", 0x20),
    (0x22B34, 0xFC201890, "fmr", 1, "AccelFastTarget", 0x24),
    (0x22B3C, 0xFC050040, "after", 0, "SpeedFastRef", 0x28),
    (0x22B48, 0xC084007C, "load", 4, "DriftFastLoss", 0x2C),
    (0x22B60, 0xFC001890, "fmr", 0, "SpeedSlowRef", 0x30),
    (0x22B64, 0xC0640014, "load", 3, "DriftSlowEdge1", 0x34),
    (0x22B68, 0xC0240080, "load", 1, "AccelSlowTarget", 0x38),
    (0x22B74, 0xC0840078, "load", 4, "DriftSlowTarget1", 0x3C),
    (0x22B80, 0xEC6400F2, "before", 4, "DriftSlowGain1", 0x40),
    (0x22B90, 0xC0640084, "load", 3, "DriftSlowEdge2", 0x44),
    (0x22BA0, 0xC0840088, "load", 4, "DriftSlowGain2", 0x48),
    (0x22BB8, 0xC0A40088, "load", 5, "DriftSlowGain3", 0x4C),
    (0x22BD4, 0xC064008C, "load", 3, "DriftCap", 0x50),
    (0x22BF4, 0xC064001C, "load", 3, "BoostKeepAccel", 0x54),
    (0x22C08, 0xC0640068, "load", 3, "BoostKeepSpeed", 0x58),
    (0x22C40, 0xC0640090, "load", 3, "SpeedFastAccelEdge1", 0x5C),
    (0x22C44, 0xC0E40014, "load", 7, "SpeedFastGainMid", 0x60),
    (0x22C50, 0xC0E40094, "load", 7, "SpeedFastGainLow", 0x64),
    (0x22C58, 0xC064001C, "load", 3, "SpeedFastAccelEdge2", 0x68),
    (0x22C68, 0xC0640098, "load", 3, "SpeedFastSpeedEdge", 0x6C),
    (0x22C74, 0xFCE02090, "fmr", 7, "SpeedFastGainHighSlow", 0x70),
    (0x22C7C, 0xC0E4009C, "load", 7, "SpeedFastGainHigh", 0x74),
    (0x22C80, 0xC06400A0, "load", 3, "SpeedFastScale", 0x78),
    (0x22C90, 0xC06400A4, "load", 3, "SpeedSlowOffset", 0x7C),
    (0x22C94, 0xC08400A8, "load", 4, "SpeedSlowSpan", 0x80),
    (0x22C9C, 0xC0640014, "load", 3, "SpeedSlowClamp", 0x84),
    (0x22CB0, 0xC0640068, "load", 3, "SpeedSlowSlope", 0x88),
    (0x22CB4, 0xC0840090, "load", 4, "SpeedSlowBase", 0x8C),
    (0x22CBC, 0xC0A400AC, "load", 5, "SpeedSlowScale", 0x90),
    (0x22CD0, 0xC0640038, "load", 3, "AccelSlowHighEdge", 0x94),
    (0x22CE8, 0xC02400B0, "load", 1, "AccelSlowHighTarget", 0x98),
    (0x22CEC, 0xC0640078, "load", 3, "AccelSlowHighGain", 0x9C),
    (0x22D14, 0xC88400B8, "double", 4, "AccelGain", memmap.SEAT_CURVE_DOUBLE),
    (0x22D3C, 0xC0240090, "load", 1, "TurnDecelAccelEdge", 0xA0),
    (0x22D48, 0xC02400C0, "load", 1, "TurnDecelLowAccelEdge", 0xA4),
    (0x22D4C, 0xC0C40014, "load", 6, "TurnDecelBase", 0xA8),
    (0x22D58, 0xC0240084, "load", 1, "SpeedLowAccelFactor", 0xAC),
    (0x22D5C, 0xFCC00890, "fmr", 6, "TurnDecelLowAccelFactor", 0xB0),
    (0x22D68, 0xC02400C4, "load", 1, "TurnDecelHighEdge", 0xB4),
    (0x22D74, 0xC0240084, "load", 1, "TurnDecelHighFactor", 0xB8),
    (0x22D88, 0xC0240078, "load", 1, "TurnDecelSlowFactor", 0xBC),
    (0x22D94, 0xC08400B0, "load", 4, "TurnDecelGain", 0xC0),
    (0x22D9C, 0xC02400C8, "load", 1, "TurnDecelFloor", 0xC4),
    (0x22DCC, 0xC02400CC, "load", 1, "LightWeight", 0xC8),
    (0x22DDC, 0xC02400B0, "load", 1, "LightAccelCap", 0xCC),
    (0x22E04, 0xC0040038, "load", 0, "TurnMoveFastLoss", 0xD0),
    (0x22E08, 0xC0240014, "load", 1, "TurnMoveFastBase", 0xD4),
    (0x22E24, 0xC0040068, "load", 0, "TurnMoveSlowGain", 0xD8),
    (0x22E28, 0xC0240014, "load", 1, "TurnMoveSlowBase", 0xDC),
    (0x22E40, 0xC0040060, "load", 0, "GripGain", 0xE0),
    (0x22E48, 0xC0640014, "load", 3, "GripBase", 0xE4),
    (0x22E70, 0xC0040018, "load", 0, "BoostGain", 0xE8),
    (0x22E7C, 0xEC03002A, "before", 3, "BoostBase", 0xEC),
)
assert memmap.SEAT_CURVE == 0x1C and len({s[5] for s in SITES}) == len(SITES)
assert all(s[5] + (8 if s[2] == "double" else 4) <= memmap.SEAT_CONSTANT for s in SITES)


def site_name(offset):
    return f"curve_{offset:05x}"


def add(k, r, patch, stock, test, middle):
    """Adds the routines to `r` and an entry for each site through `patch`, as sites.add does; `test` is the mode
    test's text."""
    TEXT = k.TEXT
    function = TEXT + CURVE_FUNCTION
    assert test.count("cmpwi 12, 0") == 1     # the mode test, here into cr7 (cr0 is live in the function)

    # the two calls: the seat's record in SEAT_SETTING for the call's length (r11, r12 and cr0 hold nothing over a call)
    for offset, known in CALLS:
        at = TEXT + offset
        stock(at, known)
        name = f"setting_call_{offset:05x}"
        r.add(name, test + f"""    beq {name}_stock
""" + seat_constants(27, f"{name}_stock") + f"""    lis 11, {memmap.hi(SETTING) - 0x10000}
    ori 11, 11, {memmap.lo(SETTING)}
    stw 12, 0(11)
    @bl {function:#x}
    li 12, 0
    lis 11, {memmap.hi(SETTING) - 0x10000}
    ori 11, 11, {memmap.lo(SETTING)}
    stw 12, 0(11)
    @b {at + 4:#x}
{name}_stock:
    @bl {function:#x}
    @b {at + 4:#x}
""")
        patch(at, known, 0, name)
        middle.append((at, ["r11", "r12", "cr0"], name))

    # each number: the record's, or the game's own word with SEAT_SETTING 0
    for offset, known, kind, fpr, slot, record in SITES:
        at = TEXT + offset
        word = stock(at, known)
        name = site_name(offset)
        load = f"lfd {fpr}, {record}(12)" if kind == "double" else f"lfs {fpr}, {record}(12)"
        own = {"load": f"    {load}\n", "double": f"    {load}\n", "fmr": f"    {load}\n",
               "after": f"    .long {word:#010x}\n    {load}\n", "before": f"    {load}\n    .long {word:#010x}\n"}[kind]
        r.add(name, test.replace("cmpwi 12, 0", "cmpwi 7, 12, 0") + f"""    beq 7, {name}_stock
    lis 12, {memmap.hi(SETTING) - 0x10000}
    ori 12, 12, {memmap.lo(SETTING)}
    lwz 12, 0(12)
    cmpwi 7, 12, 0
    beq 7, {name}_stock
{own}    @b {at + 4:#x}
{name}_stock:
    .long {word:#010x}
    @b {at + 4:#x}
""")
        patch(at, word, 0, name)
        middle.append((at, ["r12", "cr7"], name))
