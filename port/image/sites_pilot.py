"""The seats' pilots in the race's pictures: the words of the patch list that make the race's pilot readers take a
custom seat's own pilot (the race block's entry, sites_custom.py) where the game took garage record 0's. Native form
only. sites.py calls add() after sites_custom.add().

A custom seat races id 0x32 (memmap.CUSTOM_ID), and the game's three pilot lookups by machine id read, for an id
0x32 to 0x35, the pilot byte (+0x81A0) of garage record id - 0x32: record 0, this game's own save, whatever seat
the machine is. They are `fn_1_12C930` (machine id to the pilot's picture index: portraits, the pilot's model
files), `fn_1_12CCB0` (the same for the pilot's other models, by a number 1 to 3) and `fn_1_12C7B8` (machine id to
the stock machine whose pilot drives it: the face board). For a stock id below 0x29 each answers from the id alone,
so a site gives the lookup the seat's pilot (a stock machine id 0 to 40, the entry's byte 28) in place of 0x32, and
the game's own table does the rest. In a race or replay of ours each site finds the seat, then `seat_pilot` answers
the pilot: the seat's entry's for a seat with an entry and the id 0x32, else the id as it was. In a plain start a
site does what the word it replaced did.

| Sites | Where | The seat from |
|---|---|---|
| `pilot_car_*` (12) | the car's id load `lbz r3, 0x328(r3)` before `fn_1_12C930`: fn_1_33A20 (the results' slots, 4), race_result_init fn_1_3B11C (the results' portrait group), race_result_main fn_1_3B7D4 (4), fail_result_main fn_1_3CE94 (3) | the car (r3): its racer (+0x32C), the racer's index (+4) |
| `pilot_name` | fn_1_14267C 0x142784, `mr r3, r28` before `fn_1_12C930` (the first results page) | the car r29 |
| `pilot_model_*` (2) | fn_1_A7294's calls of `fn_1_12C930` and `fn_1_12CCB0` (the pilot's models, loaded per car) | the car r28 |
| `pilot_models_*` (2) | `extsb r27, r27` of fn_1_A744C and fn_1_A75DC (the same models, by the same lookups) | the car r23, r24 |
| `pilot_face_*` (5) | the calls of `fn_1_12C7B8` in fn_1_D123C, fn_1_D19B8 (the face files), fn_1_D1C94 (2) and face_calc fn_1_D2404 | the racer's index in r27, r30, r29, r28 |

Left as they are: the results rows (fn_1_147508, fn_1_147A04) read the per-racer record's pilot (+0x80), which
`custom_result` already sets from the entry; the machine select (fn_1_13A848, fn_1_13A9B8) and fn_1_12C710 are
menus; grid_fill_cpus (fn_1_34AC0) chooses computer racers' machines, which the host's seats set in a race of ours.
Pictures only: no step reads what these sites answer (the face board, the portraits, the pilot's models)."""
import memmap
import sites_custom

CAR_ID_LOAD = 0x88630328                       # lbz r3, 0x328(r3): a car's machine id
CARS = (0x33F9C, 0x340C0, 0x341B0, 0x342A0, 0x3B750, 0x3C0AC, 0x3C1D0, 0x3C2C0, 0x3C430, 0x3D4F8, 0x3D5E8, 0x3D758)
FACES = ((0xD1274, 27), (0xD1A14, 30), (0xD1E30, 29), (0xD1EFC, 29), (0xD26D8, 28))   # (site, the seat's register)


def of_car(reg):
    """r11 = the seat of the car in register `reg`."""
    return f"    lwz 11, 0x32C({reg})\n    lha 11, 4(11)\n"


def add(k, r, patch, stock, test, middle, complete):
    """Adds the routines to `r` and an entry for each site through `patch`, as sites.add does; `test` is the mode
    test's text."""
    TEXT, branch, branch_target = k.TEXT, k.branch, k.branch_target
    PICTURE, MODEL, PILOT_OF = TEXT + 0x12C930, TEXT + 0x12CCB0, TEXT + 0x12C7B8

    # r3 the machine id, r11 the seat (a site reaches it in a race or replay of ours only): r3 = the seat's pilot
    # for a custom seat, else unchanged. Uses r11, r12, cr0.
    r.add("seat_pilot", f"""    cmpwi 3, {memmap.CUSTOM_ID}
    bnelr
""" + sites_custom.lookup(11, "sp_none") + f"""    lbz 3, {sites_custom.PILOT}(11)
sp_none:
    blr
""")

    def site(name, offset, known, on, off, dead):
        """`on`, the site's text in a race or replay of ours; `off`, the stock path's (WORD the replaced word, NEXT
        the way back to the site)."""
        at = TEXT + offset
        word = stock(at, known)
        text = test + f"    beq {name}_off\n" + on + "NEXT" + f"{name}_off:\n" + off + "NEXT"
        r.add(name, text.replace("WORD", f"    .long {word:#010x}\n").replace("NEXT", f"    @b {at + 4:#x}\n"))
        patch(at, word, 0, name)
        middle.append((at, dead, name))

    def call(name, offset, to, seat):
        """A site that is the lookup's call: the seat, its pilot, the call."""
        at = TEXT + offset
        word = stock(at, branch(at, to, True))
        if complete:
            assert branch_target(word, at) == to, f"{at:08X}"
        site(name, offset, word, seat + f"    bl seat_pilot\n    @bl {to:#x}\n", f"    @bl {to:#x}\n",
             ["r11", "r12", "cr0"])

    # the car's id load: the seat read before the load takes r3
    for offset in CARS:
        site(f"pilot_car_{offset:x}", offset, CAR_ID_LOAD, of_car(3) + "WORD    bl seat_pilot\n", "WORD",
             ["r11", "r12", "cr0", "lr"])
    site("pilot_name", 0x142784, 0x7F83E378, of_car(29) + "WORD    bl seat_pilot\n", "WORD",
         ["r11", "r12", "cr0", "lr"])

    # the pilot's models: fn_1_A7294's two calls (the id in r30 is tested against 0x22 and 0x27 before: kept)
    call("pilot_model_a7320", 0xA7320, PICTURE, of_car(28))
    call("pilot_model_a7338", 0xA7338, MODEL, of_car(28))
    # fn_1_A744C and fn_1_A75DC: the id register both lookups take (r24 does not keep the car in the loop)
    for offset, car in ((0xA74B4, 23), (0xA7614, 24)):
        site(f"pilot_models_{offset:x}", offset, 0x7F7B0774, "WORD" + of_car(car) + """    mr 3, 27
    bl seat_pilot
    mr 27, 3
""", "WORD", ["r3", "r11", "r12", "cr0", "lr"])

    # the face board and its files: the racer's index is the seat
    for offset, seat in FACES:
        call(f"pilot_face_{offset:x}", offset, PILOT_OF, f"    mr 11, {seat}\n")
