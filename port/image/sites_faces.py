"""The mark on the top six portraits: the glowing orange square marks the racer the HUD follows, where the game marks
racer 0. Native form only. sites.py calls add(). For the picture only.

What the game does. The board's draw, fn_1_C8294 (from the HUD's fn_1_C6F8C 0xC7144, 0xC7184 with r5 = 1, and the
results page fn_1_144254 0x14460C with r5 = 0), takes each row's entry (fn_1_D2D58, by +0x10, the row) and asks at
0xC86AC: with the board's flag .bss 0x7ACE8 clear, the row is marked when the entry's +0x18 is not -1 (0xC86CC, 0xC86D0).
A marked row gets a sprite pulsing on a 30-frame cycle (0xC876C to 0xC8878) and the orange square fn_1_C7CBC (0xC8BD8,
colour rodata 0x5C84, F08C14FF). Entry +0x18 is fn_1_3F440(the entry's rank, +0x14), set only when a row settles
(fn_1_D2404 0xD25EC, 0xD2700; fn_1_D1C94 0xD1EE4): race block +0xE[rank], the machine at that rank (the ranking writes
it, 0x8BD48), when that is below the number of human players (.bss 0x3C38), else -1. So the mark means "a human's
machine, by player number": on a console, the players. A race of ours has one human by the game's count, so it marks
racer 0, whoever this game's seat is (the board is drawn only below two humans: 0xC70EC). The flag's only writer stores
0 (0xD1890), so the other test (+0x18 == 0, 0xC86B8) is never asked and stays as it is. The RIVAL tag on the same rows
(0xC8BE4, fn_1_40B20) answers -1 for a race type other than 1 and 5, which a race of ours never is: left.

| Site | Where | Now, in a race or replay of ours |
|---|---|---|
| `face_mark` | fn_1_C8294 0xC86CC, `lwz r0, 0x18(r31)` | r0 = 0 for the row of the racer to mark, -1 for the others |

The racer to mark: on the race's board (r15 not 0) the racer the HUD follows, as rankicons.py reads it, so a player's
own racer while racing and the racer watched by a spectator, a finished player or a replay's viewer, and it moves with
the camera at once where +0x18 waits for a row to settle; on the results page (r15 = 0) the local seat, as the results
list's lit row (follow.py), since the results' cameras may watch the winner. The routine reads and sets r0, r12, CR0;
all of it runs inside the draw, writes no memory, calls nothing and uses no generator. No step reads what it answers:
r0 goes only to the test at 0xC86D0 and from there to this draw's sprites and return value. In a plain start the site
does what the word did."""
import rankicons

SITE, KNOWN = 0xC86CC, 0x801F0018        # fn_1_C8294: lwz r0, 0x18(r31), the entry's mark


def add(k, r, patch, stock, test, middle):
    """Adds the routine to `r` and its entry through `patch`, as sites.add does; `test` is the mode test's text."""
    at = k.TEXT + SITE
    word = stock(at, KNOWN)
    r.add("face_mark", test + f"""    beq fm_stock
    cmpwi 15, 0
    beq fm_own
""" + rankicons.followed("fm_") + f"""    lwz 0, 4(31)
    cmpw 0, 12
    li 0, -1
    bne fm_back
    li 0, 0
fm_back:
    @b {at + 4:#x}
fm_stock:
    .long {word:#010x}
    @b {at + 4:#x}
""")
    patch(at, word, 0, "face_mark")
    middle.append((at, ["r12", "cr0"], "face_mark"))
