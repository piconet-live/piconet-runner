"""The frame routine's HUD pass (netframe.py assembles the parts in order; its docstring, at +264 and at
memmap.FINISH_KIND, says what they write).

`hudstate`, run after a frame's steps in a session's race and results: port 0's retired bits and the HUD part mask
from the local machine, and the results board's slot 0 kept for the local racer. Retired, and the HUD's parts off,
only when the game's own verdict says the machine is out (outtest.py), as the game takes them off only then
(fn_1_32200); all clear while it races: a breakdown that coasts on, or a wreck the Restore rule puts back, keeps its
HUD and its energy meter. The game's own steps, as for its port-0 machine:
- out (fn_1_32A40 0x32F20 asks fn_1_32600, then fn_1_32200 every step): one cause bit, the first of the flags
  0x10000000 (0x26B18), 0x80 (0x26B19), 0x800 (0x26B1A) in that order (0x32238-0x32288), so a wreck (fn_1_25004
  sets 0x800 and 0x80) is a breakdown's; the part mask .data 0x3D544[port] 0 (0x32574); flag 0x08000000 (the
  uniform pass gives it here, standin_routine.py), with which the finish (fn_1_850C4) never comes;
- finished (0x10000; fn_1_32A40 tests it first, 0x32CE4, and never asks the out test of it): no cause bit, the
  mask 0 (0x32E6C), whatever else its flags hold: a breakdown that coasts over the line, energy run out or a
  wreck on the cool-down lap is a finish;
- the banner (fn_1_15DFD4, drawn by fn_1_15C36C) shows one a frame: 0x26B18 kind 4, else 0x26B1A kind 2, else
  0x26B19 kind 1 (the three RETIRED, fn_1_15DD7C; the kind goes to the banner sound, fn_1_A4C9C), else 0x26B1E
  FINISH! (fn_1_15C6C0). With one bit kept the wreck's kind is 1, as the game's; with all three it was 2.
The game ORs the bit in every step it is out; here it is the bit of this frame's flags, which differs only for a
machine out with 0x800 alone that gains 0x80 later (the game keeps both: kind 2; here kind 1).

`finwatch`, the local racer's own finish, run at every step boundary of a frame: after the network hook of every
step (a rollback puts the memory back inside that hook) and after the frame's last step. The game counts one
countdown, for the machine with a pad port (.bss 0x50EC[port]; fn_1_32A40 sets 480 steps at the finish, fn_1_32200
300 when the machine is out, 0xFFFF before either; 180 at a finish in race type 5, which a race of ours never is),
and that machine is seat 0 in every game: "FINISH!" and the place came up at seat 0's finish everywhere. The
simulation keeps that countdown; the picture reads memmap.FINISH_COUNT (follow.py's six sites).
- The kind (memmap.FINISH_KIND) is what the local machine's flags say at this boundary: 1 finished (0x10000), else
  2 out by the game's own verdict (outtest.py: never under the Restore rule, a breakdown only at rest), else 0. It is compared at every boundary
  and never latched: a rollback that takes a predicted finish back gives 0 at the boundary after its hook, the
  countdown and the kept results go with it, and the step that finishes the machine again, run with the official
  inputs, starts both anew. Two boundaries are one step apart, so the step is known exactly.
- When the kind changes to 1 or 2, memmap.FINISH_END is the step word (+124, the session's, never rolled back by
  the undo log) plus 480 or 300: the countdown is END minus the step word, as the game's own is after the same
  steps. Nothing in rolled-back memory counts it.
- When it changes to 1 with the results board's slot still empty (seat 0's own finish fills it in the game that
  follows seat 0), the machine's progress record, max speed and place are kept (+0x400, +0x5FC, +0x600; +276):
  taken at the boundary after the finish step itself, and taken again whenever that step is run again, so what is
  kept is the official finish's. `hudstate` writes them into the slot every frame.
- memmap.FINISH_COUNT: with kind 0 (also outside a session, before the race phase, a watcher) the game's own
  countdown of port 0, so those pictures are the stock game's; else 0 once rule flag 0x1000 is set (the results
  phase swaps the HUD's pictures and draws no banner: the stock game sets the flag only with its countdown at 0),
  else the steps left. While spectating (+56) the kind stands and the countdown runs out.
- Run out, with the results not on yet: 0, for a finished machine as for one that is out, until the results.
  The banner (fn_1_15C6C0) draws "FINISH!" from 480 down to its threshold and the place below it; follow.py gives
  the threshold versus's 300 (the stock game has 0 with rule flag 0x04000000, ours: the place only at 0, at the
  first frame of its zoom-in, which a countdown standing at 0 then kept on the screen, so this read 0xFFFF,
  "nothing", and the place never showed). With 300 the frame at 0 is the place settled: it stays up while the
  results wait for the other players, as versus's stays for the frames before its results. Out, at 0 the retired
  banners stay up, as in the stock game.

A local machine still racing when the results begin (the host's rule ends a race so many steps after the first
finish: raceend.py; without the rule the results never begin with a machine racing). The game's results are made
for a racer with a finish; three things give this one its own:
- its row of the results: at the first boundary that finds the race-over test's "over" said (memmap.END_OVER; only
  read here) with the machine still racing, its progress record, max speed and place are kept as they stand, as a
  finish keeps them. That boundary is the one before the step the results begin at, and nothing is simulated again
  from there, so they are the official standing at the rule's end: the place by position, no finish time;
- memmap.FINISH_COUNT is 0 once rule flag 0x1000 is set, for a racing machine as for the others. It read the
  game's own countdown, seat 0's, which is 0xFFFF when seat 0 has not finished: the results pages take no button
  while it is not 0 (0x14A094), so such a player could not have left the results;
- `hudstate` shows it as a finished racer's from rule flag 0x1000 on (control block +268, and the HUD's parts off):
  the results pages draw only for a racer with that bit (the first: fn_1_1420E0, 0x14211C), and the race's HUD
  would stay over the list. The machine itself is not touched: it races on behind the results, under its player;
- the first results page is a finisher's own: its lap times, read as "the last lap, the one before" back from a
  finish, and their sum as TOTAL (fn_1_143780, 0x14393C on). For a racer short of the line the laps would stand
  under the wrong numbers and the total would be a time they never set. So while that page stands (the results
  pages' state, .bss 0x8E51D, is 0) `hudstate` asks for the frame routine's A press (control block +0xA34: the
  menu's button words on the next frame's entry, no control record), which the page takes as the player's: the
  game's own step to the list of every racer, begun at this racer's place (fn_1_14A328). It stops with the page.

A local machine out by the game's own verdict when the results begin (a KO, spectating or not yet: our
exception, as stock versus shows an out human no results pages) gets the same three, drawing only:
- its row: at the first boundary that finds "over" said with the kind 2 standing (also while spectating, +56,
  where the kind is no longer compared), its progress record, max speed and place (+0x482, at most 30) are kept
  as they stand, as the game's own fill at the results' start (fn_1_33A20 0x343B4-0x34460: a port's slot still
  empty gets its machine's record, +0xD8 and +0x482) would keep them for an out port-0 machine; its list row is
  the game's own DISQUALIFIED (fn_1_2E928 writes 0xFE), never touched here;
- from rule flag 0x1000 on, `hudstate` shows it as a finished racer's (+268, so fn_1_1420E0 0x14211C draws the
  pages for its viewport), the race's HUD parts off and the RETIRED cause bits clear, the spectator's as well
  (frame_spectate.py `spechud` leaves the HUD parts off once the flag is set). memmap.FINISH_COUNT is already 0
  there (the kind stands at 2: `fw_local`), so the pages take buttons (0x14A094);
- the first page's lap times would be a finisher's: it is passed by the same A press (fn_1_143780, fn_1_14A328).
A local machine that finished and spectates (frame_spectate.py `spectate`, FINISH_SPECTATE steps after its finish)
gets, from rule flag 0x1000 on, what a finisher who does not spectate gets: its finished bit (+268, which `spectate`
took off), its kept row and the HUD's parts off. While spectating without the results, and for a spectator whose
machine is neither out nor finished, `hudstate` writes nothing, as before. The machine and every word the simulation
reads are untouched."""
import memmap
import outtest
import seatport


def asm():
    return _TEXT.replace("FINISH_KIND", str(memmap.FINISH_KIND - memmap.CONTROL)).replace(
        "FINISH_END", str(memmap.FINISH_END - memmap.CONTROL)).replace(
        "FINISH_COUNT", str(memmap.FINISH_COUNT - memmap.CONTROL)).replace(
        "END_OVER", str(memmap.END_OVER - memmap.CONTROL)).replace(
        "OUT_HUD\n", outtest.clause(10, 30, 0, 7, "hs_out", "hs_racing")).replace(
        "OUT_FINISH\n", outtest.clause(10, 30, 0, 8, "fw_kind", "fw_on")).replace("FINISH_STOP\n", seatport.finish_stop())


_TEXT = """hudstate:
    lwz 9, 116(31)
    cmpwi 9, 0
    beqlr
    lha 9, 0x3C2A(30)
    cmpwi 9, 0x29
    bltlr
    lwz 5, 56(31)
    cmpwi 5, 0
    beq hs_local
    lwz 0, 0x3C30(30)
    andi. 0, 0, 0x1000
    beqlr
hs_local:
    lis 9, 0x8180
    lwz 9, 0x14(9)
    addis 10, 30, 7
    lwz 10, -0x27C8(10)
    mulli 9, 9, 0x620
    add 10, 10, 9
    lwz 8, 0(10)
    li 6, 0
    andis. 0, 8, 1
    beq hs_test
    b hs_flags
hs_test:
OUT_HUD
hs_out:
    lwz 0, 0x3C30(30)
    andi. 0, 0, 0x1000
    bne hs_results
    li 6, 1
    b hs_flags
hs_racing:
    cmpwi 5, 0
    bnelr
    lwz 0, 0x3C30(30)
    andi. 0, 0, 0x1000
    beq hs_flags
hs_results:
    oris 8, 8, 1
    addis 9, 30, 9
    lbz 0, -0x1AE3(9)
    cmpwi 0, 0
    bne hs_flags
    li 0, 1
    stw 0, 0xA34(31)
hs_flags:
    rlwinm 0, 8, 16, 31, 31
    stb 0, 268(31)
    lwz 0, 276(31)
    cmpwi 0, 0
    beq hs_bits
    addi 11, 31, 0x400
    addis 12, 30, 2
    addi 12, 12, 0x6314
    li 0, 127
    mtctr 0
hs_put:
    lwz 0, 0(11)
    stw 0, 0(12)
    addi 11, 11, 4
    addi 12, 12, 4
    bdnz hs_put
    addis 12, 30, 2
    lwz 0, 0x5FC(31)
    stw 0, 0x6304(12)
    lbz 0, 0x600(31)
    stb 0, 0x6B04(12)
hs_bits:
    cmpwi 6, 0
    beq hs_none
    andis. 0, 8, 0x1000
    bne hs_low
    andi. 0, 8, 0x80
    beq hs_causes
    rlwinm 8, 8, 0, 21, 19
    b hs_causes
hs_none:
    rlwinm 8, 8, 0, 4, 2
hs_low:
    rlwinm 8, 8, 0, 21, 19
    rlwinm 8, 8, 0, 25, 23
hs_causes:
    addis 9, 30, 2
    lbz 0, 0x6B19(9)
    rlwinm 0, 0, 0, 0, 30
    rlwinm 7, 8, 25, 31, 31
    or 0, 0, 7
    stb 0, 0x6B19(9)
    lbz 0, 0x6B1A(9)
    rlwinm 0, 0, 0, 0, 30
    rlwinm 7, 8, 21, 31, 31
    or 0, 0, 7
    stb 0, 0x6B1A(9)
    lbz 0, 0x6B18(9)
    rlwinm 0, 0, 0, 0, 30
    rlwinm 7, 8, 4, 31, 31
    or 0, 0, 7
    stb 0, 0x6B18(9)
    addis 10, 30, -1
    andis. 0, 8, 0x0001
    bne hs_off
    cmpwi 6, 0
    bne hs_off
    lwz 0, 60(31)
    cmpwi 0, 0
    beqlr
    stw 0, 0xB04(10)
    blr
hs_off:
    li 0, 0
    stw 0, 0xB04(10)
    blr
finwatch:
    lwz 9, 116(31)
    cmpwi 9, 0
    beq fw_racing
    lha 9, 0x3C2A(30)
    cmpwi 9, 0x29
    blt fw_racing
    lis 9, 0x8180
    lwz 9, 0x14(9)
    addis 10, 30, 7
    lwz 10, -0x27C8(10)
    mulli 9, 9, 0x620
    add 10, 10, 9
    lwz 0, 56(31)
    cmpwi 0, 0
    bne fw_out_row
    lwz 8, 0(10)
    li 7, 1
    li 6, 480
    andis. 0, 8, 0x0001
    bne fw_kind
    li 7, 2
    li 6, 300
OUT_FINISH
fw_on:
    li 7, 0
    lwz 0, END_OVER(31)
    cmpwi 0, 0
    beq fw_kind
    lwz 0, FINISH_KIND(31)
    cmpwi 0, 0
    bne fw_kind
fw_row:
    lwz 0, 276(31)
    cmpwi 0, 0
    bne fw_count
    addis 9, 30, 2
    lwz 0, 0x6304(9)
    cmpwi 0, 0
    bne fw_count
    b fw_keep
fw_out_row:
    lwz 0, FINISH_KIND(31)
    cmpwi 0, 2
    bne fw_count
    lwz 0, END_OVER(31)
    cmpwi 0, 0
    bne fw_row
    b fw_count
fw_racing:
    li 7, 0
fw_kind:
    lwz 0, FINISH_KIND(31)
    cmpw 0, 7
    beq fw_out_row
    stw 7, FINISH_KIND(31)
    li 0, 0
    stw 0, 276(31)
    cmpwi 7, 0
    beq fw_count
    lwz 0, 124(31)
    add 0, 0, 6
    stw 0, FINISH_END(31)
    cmpwi 7, 1
    bne fw_count
FINISH_STOP
    addis 9, 30, 2
    lwz 0, 0x6304(9)
    cmpwi 0, 0
    bne fw_count
fw_keep:
    lwz 11, 0x49C(10)
    addi 12, 31, 0x400
    li 0, 127
    mtctr 0
fw_copy:
    lwz 0, 0(11)
    stw 0, 0(12)
    addi 11, 11, 4
    addi 12, 12, 4
    bdnz fw_copy
    lwz 0, 0xD8(10)
    stw 0, 0x5FC(31)
    lbz 0, 0x482(10)
    cmplwi 0, 30
    ble fw_place
    li 0, 30
fw_place:
    stb 0, 0x600(31)
    li 0, 1
    stw 0, 276(31)
fw_count:
    lwz 0, FINISH_KIND(31)
    cmpwi 0, 0
    bne fw_local
    lhz 0, 0x50EC(30)
    lwz 9, 0x3C30(30)
    andi. 9, 9, 0x1000
    beq fw_put
    li 0, 0
    b fw_put
fw_local:
    li 0, 0
    lwz 9, 0x3C30(30)
    andi. 9, 9, 0x1000
    bne fw_put
    lwz 9, FINISH_END(31)
    lwz 10, 124(31)
    subf. 9, 10, 9
    mr 0, 9
    bgt fw_put
    li 0, 0
fw_put:
    stw 0, FINISH_COUNT(31)
    blr
"""
