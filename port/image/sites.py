"""The patch list of the native form (build.py calls add() once): a routine for every site of the game's code where
the fork had a Gecko code, and the list's entry for it. Every routine tests the mode word first and in a plain start
does what the instruction it replaced did. In today's order: the mode machine's start with the race's boot routine,
the inserts at a function's first instruction (seats, sound queue, rumble), the per-race CPU seed, the sites that
were calls (frame routine, name tags, chase camera, main.rel linked), the results screen's pictures, the hand-back
monitors' pictures (sites_monitors.py),
the cameras' own words (sites_camera.py), the drawn transform's readers (sites_drawn.py), the shadows' place (sites_shadow.py), the effects' draws
(sites_effects.py), the seats' custom machines (sites_custom.py) and their pilots (sites_pilot.py), mines that come back
(sites_mines.py), the leader's gap popup started in the step (sites_leadergap.py), the game mode's block, mode point, speed hold and a seat's own Restore (sites_mode.py), computer drivers at their own pace (sites_cpu.py), the simulation's own generator (sites_generator.py), the dash plate's and the energy boost's length as data (sites_dash.py), the machine setting's curve as data
(sites_curve.py), a
stat pack a seat (sites_seats.py), the start grid (sites_grid.py), true widescreen's view (sites_view.py), the frames
drawn between two steps (sites_hfr.py), the countdown movie's request (sites_countdown.py), and the entry table.
The constants and the two branch helpers are build.py's (`k`), where the facts table's proof reads them until they
come from the facts."""
import memmap
import sites_camera
import sites_countdown
import sites_cpu
import sites_curve
import sites_generator
import sites_custom
import sites_dash
import sites_drawn
import sites_effects
import sites_grid
import sites_leadergap
import sites_hfr
import sites_mines
import sites_mode
import sites_modehud
import sites_monitors
import sites_pilot
import sites_present
import sites_seats
import sites_shadow
import sites_view


def add(k, r, patch, stock, complete, codes, hooks, mute):
    """Adds the routines to `r` (build.Routines) in order and an entry for each site through `patch`; `stock` reads
    the game's word at a site. Returns the sites in the middle of a function: (address, what must be dead after the
    site, the routine's name)."""
    TEXT, C, MODE, DOL_LINKED, DOL_SOUND, SHUFFLE_WORD = k.TEXT, k.C, k.MODE, k.DOL_LINKED, k.DOL_SOUND, k.SHUFFLE_WORD
    RESULTS_SITES, RESULTS_RESERVE = k.RESULTS_SITES, k.RESULTS_RESERVE
    SAVEGPR_21, GX_SYNC, DC_FLUSH = k.SAVEGPR_21, k.GX_SYNC, k.DC_FLUSH
    MAIN_HEAP, HEAP_TABLE, LANGUAGE = k.MAIN_HEAP, k.HEAP_TABLE, k.LANGUAGE
    branch, branch_target = k.branch, k.branch_target
    # the game's functions the routines call or return into
    ECC, FRAME_CALL, TAG_CALL = TEXT + 0xECC, TEXT + 0x37B38, TEXT + 0x37C74
    FN_407C, FN_C7224 = TEXT + 0x407C, TEXT + 0xC7224
    SEED, CAMERA = TEXT + 0xF47AC, (TEXT + 0xAA84, TEXT + 0xAC08)
    test = f"    lis 12, {C}\n    lwz 12, {MODE}(12)\n    cmpwi 12, 0\n"

    # --- the mode machine's start (fn_1_ECC, every game frame on the main thread). A plain start: nothing. A race (a
    # replay plays as the race): the host's frame tick, the two codes Gecko ran every frame (C0: the seat routines
    # installed as the game's hooks; the shuffle word held until the race), then the fork's boot routine of a race.
    inserts = {name: {a: w for kind, a, w in rows if kind == "insert"} for name, rows in codes.items()}
    first = stock(ECC, 0x9421FFF0)
    assert inserts["race"][ECC][-1] == first, "the boot routine ends with the instruction it replaced"
    installer = next(w for kind, a, w in hooks if kind == "routine")
    assert installer[-1] in (0x4E800020, 0x60000000) and 0x4E800020 in installer[-2:]
    r.add("mode_machine", test + f"""    beq mm_stock
    stwu 1, -0x90(1)
    mflr 0
    stw 0, 0x94(1)
    stmw 3, 0x14(1)
    stw 12, 0x10(1)
    @bl host_tick
    bl mm_seats
    bl mm_shuffle
    lwz 12, 0x10(1)
    lmw 3, 0x14(1)
    lwz 0, 0x94(1)
    mtlr 0
    addi 1, 1, 0x90
    b boot_race
mm_stock:
    .long {first:#010x}
    @b {ECC + 4:#x}
mm_seats:
""" + r.words("mm_seats", installer) + f"""mm_shuffle:
    lis 12, 0x8000
    lwz 12, 0x30C8(12)
    addis 11, 12, 0x1C
    addi 11, 11, -0x5FC0
    lha 0, 0x960(11)
    cmpwi 0, 2
    beqlr
    lis 12, {memmap.hi(memmap.RACE + memmap.RACE_SHUFFLE) - 0x10000}
    lwz 11, {memmap.lo(memmap.RACE + memmap.RACE_SHUFFLE)}(12)
    lis 12, {memmap.hi(SHUFFLE_WORD) - 0x10000}
    stw 11, {memmap.lo(SHUFFLE_WORD)}(12)
    blr
""")
    assert 0x1BA040 == 0x1C0000 - 0x5FC0 and 0x14 + 29 * 4 <= 0x90
    r.add("boot_race", r.words("boot_race", inserts["race"][ECC]) + f"    @b {ECC + 4:#x}\n")
    patch(ECC, first, 0, "mode_machine")           # a new word of 0: the branch to the routine, filled in below

    # --- inserts at a function's first instruction: in a plain start the instruction itself, then on
    entries = [("seats", "seats", TEXT + 0x3181C, 0x9421FFB0, "fn_1_3181C: the race block's machines over the game's list"),
               ("sound_queue", "mute", DOL_SOUND[0], DOL_SOUND[1], "fn_80067898 (the executable): no sound from a step simulated before"),
               ("rumble", "mute", TEXT + 0xF7338, 0x9421FFE0, "fn_1_F7338: no rumble from a step simulated before")]
    mute_inserts = {a: w for kind, a, w in mute if kind == "insert"}
    for name, source, site, known, what in entries:
        words = (mute_inserts if source == "mute" else inserts[source])[site]
        word = stock(site, known)
        assert words[-1] == word, f"{name}: the insert ends with {words[-1]:08X}, the game has {word:08X}"
        r.add(name, test + f"    bne {name}_on\n    .long {word:#010x}\n    @b {site + 4:#x}\n{name}_on:\n" +
              r.words(name, words) + f"    @b {site + 4:#x}\n")
        patch(site, word, 0, name)

    middle = []                                              # sites in the middle of a function: what must be dead after

    # --- the per-race CPU seed (fn_1_F47AC, normally the time base): the race block's in a race (a replay's too)
    seed_at = memmap.RACE + memmap.RACE_CPUSEED
    seed_word = stock(SEED, 0x9421FFF0)
    r.add("cpu_seed", test + f"""    bne cpu_seed_on
    .long {seed_word:#010x}
    @b {SEED + 4:#x}
cpu_seed_on:
    lis 3, {memmap.hi(seed_at) - 0x10000}
    lwz 3, {memmap.lo(seed_at)}(3)
    blr
""")
    patch(SEED, seed_word, 0, "cpu_seed")

    # --- sites that were calls. The routine is entered by a plain branch and goes back by one, like every other: it
    # needs no stack frame then, and the stack is laid out as in the stock game (mode 0) and as on the fork.
    # the frame routine in place of the simulation manager (fn_1_37AA8's `bl fn_1_407C`)
    r.add("frame", test + f"""    bne frame_on
    @bl {FN_407C:#x}
    @b {FRAME_CALL + 4:#x}
frame_on:
    @bl mines_preload
    @bl mode_preload
    @bl mode_story
    @bl results_preload
    @bl monitors_preload
    @bl frame_routine
    @b {FRAME_CALL + 4:#x}
""")
    assert branch_target(stock(FRAME_CALL, branch(FRAME_CALL, FN_407C, True)), FRAME_CALL) == FN_407C
    patch(FRAME_CALL, stock(FRAME_CALL, branch(FRAME_CALL, FN_407C, True)), 0, "frame")
    # the name tags before the HUD (the per-viewport `bl fn_1_C7224`), with the fork's own 16 bytes of stack
    r.add("tags", test + f"""    beq tags_hud
    stwu 1, -16(1)
    stw 3, 8(1)
    @bl name_tags
    lwz 3, 8(1)
    addi 1, 1, 16
tags_hud:
    @bl {FN_C7224:#x}
    @b {TAG_CALL + 4:#x}
""")
    patch(TAG_CALL, stock(TAG_CALL, branch(TAG_CALL, FN_C7224, True)), 0, "tags")
    # the chase camera's record: the followed machine's pad port (fn_1_86634) in a plain start, else record 0
    for site in CAMERA:
        name = f"camera_{site - TEXT:x}"
        word = stock(site)
        port = branch_target(word, site) if complete else site      # without the game the call's target is not known
        r.add(name, test + f"""    bne {name}_on
    @bl {port:#x}
    @b {site + 4:#x}
{name}_on:
    li 3, 0
    @b {site + 4:#x}
""")
        patch(site, word, 0, name)
    # main.rel is linked (the executable's loader, after OSLink and before the module's first code): the host
    # writes main.rel's words of this list. LR is free there: the loader calls the module's prolog next.
    r.add("linked", f"""    @bl host_linked
    .long {DOL_LINKED[1]:#010x}
    @b {DOL_LINKED[0] + 4:#x}
""")
    patch(DOL_LINKED[0], stock(DOL_LINKED[0], DOL_LINKED[1]), 0, "linked")
    middle.append((DOL_LINKED[0], ["lr"], "linked"))

    # --- the results screen's pictures. At a player's finish the game frees the race's pictures and reads the
    # results screen's three bitmap groups from the disc, 32 KiB a field, on its main thread: nothing is drawn and
    # no step runs for 17 fields (fn_1_3B11C, the results phase's first frame). The two sets do not fit in memory
    # together (the bitmap heap is 1,150,000 bytes; the race's set unpacks to 1,122,848, the results' to
    # 1,135,872), so the swap stays, but its disc reads go: the three packed files (at most 0.56 MiB) are read
    # into the game's main heap on the black frame before the start gate (the same frame and test as the gate's),
    # and at the finish each group is unpacked from there by the game's own unpacker into the game's own heap.
    # The files go to the top of the heap's largest free block (a filler below them is taken first and given back),
    # so everything the game allocates afterwards lies where it lies without them; they are read only when the
    # block keeps RESULTS_RESERVE bytes beyond them, else nothing is read and the game loads from the disc as shipped.
    # The files stay in memory to the game's end: the results phase can begin twice (the session's last rollback
    # goes back before it), and a start for a race never returns to a menu.
    # The third group is a pilot's portrait: at the finish the game takes the pilot of the machine with pad port 0
    # (fn_1_86900(0)) or, when no machine has it, of the first car: seat 0's either way (port 0 is seat 0's; a
    # finished machine loses its port). At the gate's frame the car objects are not made yet (their list is empty
    # until the step into the countdown), so the pilot is read where the host put it before the boot: the race
    # block's machine of seat 0 (a custom machine's: the entry's pilot, sites_custom.py). Another group at the finish has no file in memory and comes from the disc.
    la = lambda reg, address: (f"    lis {reg}, {memmap.hi(address) - (0x10000 if address & 0x80000000 else 0)}\n"
                               f"    ori {reg}, {reg}, {memmap.lo(address)}\n")
    D = lambda offset: memmap.REL + 0x16D600 + offset            # main.rel .data
    P = memmap.PRELOAD - memmap.CONTROL
    FN = lambda offset: f"{TEXT + offset:#x}"
    GROUPS, HEAPS, PILOTS, FILE_NAME, HEADER = D(0xFCD4), D(0x6CA0), D(0x1A2B4), D(0x1A368), memmap.REL_BSS + 0x3E000
    name_of = lambda: (f"""    rlwinm 9, 29, 1, 0, 30
    addi 9, 9, {P + 4}
    lwzx 3, 31, 9
    mulli 3, 3, 0x28
""" + la(9, GROUPS) + "    add 3, 3, 9\n" + la(9, LANGUAGE) + """    lwz 9, 0(9)
    rlwinm 9, 9, 2, 0, 29
    add 3, 3, 9
    lwz 3, 4(3)
""")
    r.add("results_preload", f"""    lis 12, {C}
    lwz 0, {P}(12)
    cmpwi 0, 0
    bnelr
""" + la(11, memmap.REL_BSS) + f"""    lha 9, 0x960(11)
    cmpwi 9, 2
    bnelr
    lha 9, 0x3C2A(11)
    cmpwi 9, 0x27
    bnelr
    addis 9, 11, 2
    lwz 9, 0x6C28(9)
    cmpwi 9, 1
    bnelr
    li 0, 1
    stw 0, {P}(12)
    stwu 1, -0xC0(1)
    mflr 0
    stw 0, 0xC4(1)
    stmw 20, 0x90(1)
    lis 31, {C}
    li 0, 0x97
    stw 0, {P + 4}(31)
    li 0, 0x99
    stw 0, {P + 12}(31)
""" + la(9, memmap.RACE + memmap.RACE_MACHINES) + f"""    lbz 3, 0(9)
    cmpwi 3, {memmap.CUSTOM_ID}
    bne rp_pilot
""" + la(9, memmap.RACE + memmap.RACE_CUSTOM) + f"""    lbz 3, 28(9)
rp_pilot:
    extsb 3, 3
    @bl {FN(0x12C930)}
    extsh 0, 3
    rlwinm 0, 0, 2, 0, 29
""" + la(9, PILOTS) + f"""    lwzx 0, 9, 0
    stw 0, {P + 20}(31)
    li 29, 0
    li 28, 0
rp_size:
""" + name_of() + f"""    addi 4, 1, 8
    @bl {FN(0x45730)}
    cmpwi 3, 0
    beq rp_none
    addi 3, 1, 8
""" + la(4, HEADER) + f"""    li 5, 0x20
    li 6, 0
    @bl {FN(0x458A0)}
    mr 24, 3
    addi 3, 1, 8
    @bl {FN(0x45850)}
    cmpwi 24, 0
    blt rp_none
""" + la(4, HEADER) + f"""    lwbrx 3, 0, 4
    addi 3, 3, 0x27
    rlwinm 3, 3, 0, 0, 26
    addi 9, 1, 0x78
    stwx 3, 9, 29
    add 28, 28, 3
    addi 28, 28, 0x20
    addi 29, 29, 4
    cmpwi 29, 12
    blt rp_size
""" + la(9, MAIN_HEAP) + "    lwz 27, 0(9)\n" + la(9, HEAP_TABLE) + f"""    lwz 9, 0(9)
    mulli 0, 27, 12
    add 9, 9, 0
    lwz 9, 4(9)
    li 26, 0
rp_walk:
    cmplwi 9, 0
    beq rp_walked
    lwz 0, 8(9)
    cmplw 0, 26
    ble rp_next
    mr 26, 0
rp_next:
    lwz 9, 4(9)
    b rp_walk
rp_walked:
    stw 26, {P + 32}(31)
    addis 0, 28, {RESULTS_RESERVE >> 16}
    cmplw 26, 0
    blt rp_none
    subf 4, 28, 26
    addi 4, 4, -0x60
    mr 3, 27
""" + la(5, FILE_NAME) + f"""    li 6, 0x7001
    @bl {FN(0x45D0)}
    mr. 25, 3
    beq rp_none
    li 29, 0
rp_take:
    mr 3, 27
    addi 9, 1, 0x78
    lwzx 4, 9, 29
""" + la(5, FILE_NAME) + f"""    li 6, 0x7002
    @bl {FN(0x45D0)}
    addi 9, 1, 0x84
    stwx 3, 9, 29
    addi 29, 29, 4
    cmpwi 29, 12
    blt rp_take
    mr 3, 27
    mr 4, 25
""" + la(5, FILE_NAME) + f"""    li 6, 0x7003
    @bl {FN(0x46B4)}
    li 29, 0
rp_read:
    addi 9, 1, 0x84
    lwzx 0, 9, 29
    cmplwi 0, 0
    beq rp_skip
""" + name_of() + f"""    addi 4, 1, 8
    @bl {FN(0x45730)}
    cmpwi 3, 0
    beq rp_skip
    addi 3, 1, 8
    addi 9, 1, 0x84
    lwzx 4, 9, 29
    addi 9, 1, 0x78
    lwzx 5, 9, 29
    li 6, 0
    @bl {FN(0x458A0)}
    mr 24, 3
    addi 3, 1, 8
    @bl {FN(0x45850)}
    cmpwi 24, 0
    blt rp_skip
    rlwinm 9, 29, 1, 0, 30
    addi 9, 9, {P + 8}
    addi 10, 1, 0x84
    lwzx 0, 10, 29
    stwx 0, 31, 9
rp_skip:
    addi 29, 29, 4
    cmpwi 29, 12
    blt rp_read
    li 0, 2
    stw 0, {P}(31)
rp_out:
    lmw 20, 0x90(1)
    lwz 0, 0xC4(1)
    mtlr 0
    addi 1, 1, 0xC0
    blr
rp_none:
    li 0, 3
    stw 0, {P}(31)
    b rp_out
""")
    assert RESULTS_RESERVE & 0xFFFF == 0 and RESULTS_RESERVE >> 16 < 0x8000
    # A group whose packed file is in memory, loaded as fn_1_48004 and fn_1_47AD4 load it (r3 the group, r4 the
    # bitmap heap's number): nothing when it is loaded already; else that heap made if it is not there, the
    # unpacked size's block from it, the game's unpacker, the cache flush, and fn_1_47AD4's own second half (from
    # 0x47D58: the texture objects, the group's entry), entered with the frame its first half builds. Any other
    # group: fn_1_48004 itself.
    r.add("results_load", f"""    lis 12, {C}
    lwz 11, {P + 4}(12)
    cmpw 11, 3
    lwz 11, {P + 8}(12)
    beq rl_found
    lwz 11, {P + 12}(12)
    cmpw 11, 3
    lwz 11, {P + 16}(12)
    beq rl_found
    lwz 11, {P + 20}(12)
    cmpw 11, 3
    lwz 11, {P + 24}(12)
    bne rl_disc
rl_found:
    cmplwi 11, 0
    beq rl_disc
    mulli 10, 3, 0x28
""" + la(9, GROUPS) + f"""    add 10, 10, 9
    lwz 0, 0(10)
    cmpwi 0, 0
    bnelr
    stwu 1, -0xA0(1)
    mflr 0
    stw 0, 0xA4(1)
    stw 11, 8(1)
    stw 4, 12(1)
    stw 10, 16(1)
    addi 11, 1, 0xA0
    @bl {SAVEGPR_21:#x}
    lwz 24, 8(1)
    lwz 29, 12(1)
    lwz 28, 16(1)
    mr 3, 29
    @bl {FN(0x48418)}
    @bl {GX_SYNC:#x}
    mulli 0, 29, 12
""" + la(9, HEAPS) + f"""    lwzx 31, 9, 0
    li 0, 4
    lwbrx 3, 24, 0
    addi 0, 3, 0x1F
    rlwinm 22, 0, 0, 0, 26
    mr 3, 31
    addi 4, 22, 0x10
""" + la(5, FILE_NAME) + f"""    li 6, 0x12F
    @bl {FN(0x45D0)}
    mr 27, 3
    addi 0, 27, 0x2F
    rlwinm 26, 0, 0, 0, 26
    mr 3, 24
    mr 4, 26
    @bl {FN(0x12860)}
    mr 3, 26
    mr 4, 22
    @bl {DC_FLUSH:#x}
    lis 12, {C}
    lwz 9, {P + 28}(12)
    addi 9, 9, 1
    stw 9, {P + 28}(12)
    li 30, 0
    @b {FN(0x47D58)}
rl_disc:
    @b {FN(0x48004)}
""")
    for offset in RESULTS_SITES:
        site, name = TEXT + offset, f"results_{offset:x}"
        word = stock(site, branch(site, TEXT + 0x48004, True))
        if complete:
            assert branch_target(word, site) == TEXT + 0x48004
        r.add(name, test + f"""    bne {name}_on
    @bl {FN(0x48004)}
    @b {site + 4:#x}
{name}_on:
    @bl results_load
    @b {site + 4:#x}
""")
        patch(site, word, 0, name)
        middle.append((site, ["r12", "cr0"], name))

    sites_monitors.add(k, r, la, D)
    sites_camera.add(k, r, patch, stock, test, middle)
    sites_drawn.add(r)
    sites_shadow.add(k, r, patch, stock, test, la, middle)
    sites_effects.add(k, r, patch, stock, test, la, middle)
    sites_custom.add(k, r, patch, stock, test, middle, complete)
    sites_pilot.add(k, r, patch, stock, test, middle, complete)
    sites_seats.add(k, r, patch, stock, test, middle)
    sites_grid.add(k, r, patch, stock, test, middle, complete)
    sites_mines.add(k, r, patch, stock, test, la, D, middle)
    sites_mode.add(k, r, patch, stock, test, la, D, middle)
    sites_modehud.add(k, r, patch, stock, test, la, middle)
    sites_cpu.add(k, r, patch, stock, test, middle, complete)
    sites_generator.add(k, r, patch, stock, test, middle)
    sites_leadergap.add(k, r, patch, stock, test, middle)
    sites_dash.add(k, r, patch, stock, test, la, middle)
    sites_curve.add(k, r, patch, stock, test, middle)
    sites_present.add(k, r, patch, stock, middle, draw_next="hfr")   # one site with the hfr routine: 0x37C18
    sites_view.add(k, r, patch, stock, test, middle, complete)
    sites_hfr.add(k, r, patch, stock, test, middle, entry=False)
    sites_countdown.add(k, r, patch, stock, test, middle, complete)

    # Never run: one call to every routine that is entered through a pointer or a plain branch, so that the
    # recompiler, which starts a function at every call target, starts one at each of them.
    r.add("entry_table", "".join(f"    @bl {name}\n" for name in
                                 ["seat_routine", "race_over", "rank_refs", "frame_routine", "name_tags"] + r.names) + "    blr\n")
    return middle
