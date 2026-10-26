"""Is a register dead after a patched site? A routine put at a site in the middle of a function uses registers the
replaced instruction did not (r12 and a CR field for the mode test, LR when the site becomes a `bl`). That is only
right when the game's code after the site writes each of them before reading it, on every path. `dead()` proves it
from the code itself, or says where the proof stops; the image builder refuses a site without a proof.

Resources are "r0".."r31", "cr0".."cr7", "lr", "ctr". The decoder knows the instructions the game's compiler emits;
a word it does not know counts as reading everything, so an unknown never proves anything.

Calls and returns follow the PowerPC EABI as the game's compiler uses it: a call or a return leaves r11, r12, CR
fields 0, 1, 5, 6, 7 and CTR without meaning (a called function may clobber them and takes no input in them); a
called function reads r3-r10 and r1, r2, r13; r0 loses its meaning at an indirect call and at a return, but counts as
read by a direct call (the compiler's register save helpers are called with values in r0 and r11)."""
import struct

ALL = frozenset([f"r{i}" for i in range(32)] + [f"cr{i}" for i in range(8)] + ["lr", "ctr"])
CALL_KILLS = frozenset(["r11", "r12", "cr0", "cr1", "cr5", "cr6", "cr7", "ctr", "lr"])
CALL_READS = frozenset([f"r{i}" for i in range(1, 11)] + ["r13"])
RETURN_READS = frozenset(["lr", "r1", "r2", "r3", "r4", "r13", "cr2", "cr3", "cr4"] + [f"r{i}" for i in range(14, 32)])


def decode(word, address):
    """(reads, writes, successors, kind): kind is "" for an ordinary instruction, "call", "return", "jump" (an
    indirect jump: successors unknown) or "unknown"."""
    op = word >> 26
    d, a, b = (word >> 21) & 31, (word >> 16) & 31, (word >> 11) & 31
    rc = word & 1
    R = lambda *n: frozenset(f"r{i}" for i in n)
    base = R(a) if a else frozenset()          # rA = 0 means the value 0 in a load, store or addi
    nxt = [address + 4]
    crf = f"cr{d >> 2}"
    rec = frozenset(["cr0"]) if rc else frozenset()
    if op in (7, 8, 12, 13):                   # mulli, subfic, addic, addic.
        return R(a), R(d) | (frozenset(["cr0"]) if op == 13 else frozenset()), nxt, ""
    if op in (14, 15):                         # addi, addis
        return base, R(d), nxt, ""
    if op in (10, 11):                         # cmpli, cmpi
        return R(a), frozenset([crf]), nxt, ""
    if op == 16:                               # bc
        bo, bi = d, a
        reads = set()
        if not bo & 0x10: reads.add(f"cr{bi >> 2}")
        if not bo & 0x04: reads.add("ctr")
        disp = word & 0xFFFC
        disp -= 0x10000 if disp & 0x8000 else 0
        target = disp if word & 2 else address + disp
        always = bo & 0x14 == 0x14
        if word & 1:                           # a conditional call
            return frozenset(reads) | CALL_READS | R(0), CALL_KILLS, nxt, "call"
        writes = frozenset() if bo & 0x04 else frozenset(["ctr"])
        return frozenset(reads), writes, ([target] if always else [target, address + 4]), ""
    if op == 18:                               # b, bl
        disp = word & 0x03FFFFFC
        disp -= 0x04000000 if disp & 0x02000000 else 0
        target = disp if word & 2 else address + disp
        if word & 1:
            return CALL_READS | R(0), CALL_KILLS, nxt, "call"
        return frozenset(), frozenset(), [target & 0xFFFFFFFF], ""
    if op == 19:
        xo = (word >> 1) & 0x3FF
        if xo in (16, 528):                    # bclr, bcctr
            bo, bi = d, a
            reads = {"lr" if xo == 16 else "ctr"}
            if not bo & 0x10: reads.add(f"cr{bi >> 2}")
            if not bo & 0x04 and xo == 16: reads.add("ctr")
            always = bo & 0x14 == 0x14
            if word & 1:                       # blrl, bctrl: an indirect call
                return frozenset(reads) | CALL_READS, CALL_KILLS | R(0), nxt, "call"
            if xo == 16:
                reads |= RETURN_READS
                return frozenset(reads), frozenset(), ([] if always else nxt), "return"
            return frozenset(reads), frozenset(), [], "jump"
        if xo == 0:                            # mcrf
            return frozenset([f"cr{a >> 2}"]), frozenset([crf]), nxt, ""
        if xo in (33, 129, 193, 225, 257, 289, 417, 449):   # CR logic: reads two bits, writes one (the rest of its field stays)
            return frozenset([f"cr{a >> 2}", f"cr{b >> 2}", f"cr{d >> 2}"]), frozenset(), nxt, ""
        if xo == 150:                          # isync
            return frozenset(), frozenset(), nxt, ""
        return ALL, frozenset(), nxt, "unknown"
    if op in (20, 21):                         # rlwimi (reads rA too), rlwinm
        return R(d) | (R(a) if op == 20 else frozenset()), R(a) | rec, nxt, ""
    if op == 23:                               # rlwnm
        return R(d, b), R(a) | rec, nxt, ""
    if op in (24, 25, 26, 27):                 # ori, oris, xori, xoris
        return R(d), R(a), nxt, ""
    if op in (28, 29):                         # andi., andis.
        return R(d), R(a) | frozenset(["cr0"]), nxt, ""
    if op == 31:
        xo = (word >> 1) & 0x3FF
        if xo in (0, 32):                      # cmp, cmpl
            return R(a, b), frozenset([crf]), nxt, ""
        loads = {23: 0, 87: 0, 279: 0, 343: 0, 534: 0, 790: 0, 20: 0, 55: 1, 119: 1, 311: 1, 375: 1}
        if xo in loads:
            return base | R(b), R(d) | (R(a) if loads[xo] else frozenset()), nxt, ""
        fploads = {535: 0, 599: 0, 567: 1, 631: 1}
        if xo in fploads:
            return base | R(b), R(a) if fploads[xo] else frozenset(), nxt, ""
        stores = {151: 0, 215: 0, 407: 0, 662: 0, 918: 0, 183: 1, 247: 1, 439: 1}
        if xo in stores:
            return R(d, b) | base, R(a) if stores[xo] else frozenset(), nxt, ""
        if xo == 150:                          # stwcx.
            return R(d, b) | base, frozenset(["cr0"]), nxt, ""
        fpstores = {663: 0, 727: 0, 983: 0, 695: 1, 759: 1}
        if xo in fpstores:
            return R(b) | base, R(a) if fpstores[xo] else frozenset(), nxt, ""
        if xo in (28, 60, 444, 412, 316, 124, 476, 284, 24, 536, 792):    # and andc or orc xor nor nand eqv slw srw sraw
            return R(d, b), R(a) | rec, nxt, ""
        if xo in (824, 26, 954, 922):          # srawi cntlzw extsb extsh
            return R(d), R(a) | rec, nxt, ""
        xo9 = xo & 0x1FF
        if xo9 in (266, 10, 138, 40, 8, 136, 235, 75, 11, 491, 459):      # add addc adde subf subfc subfe mullw mulhw mulhwu divw divwu
            return R(a, b), R(d) | rec, nxt, ""
        if xo9 in (234, 202, 232, 200, 104):   # addme addze subfme subfze neg
            return R(a), R(d) | rec, nxt, ""
        if xo in (339, 371):                   # mfspr, mftb
            spr = ((word >> 16) & 31) | ((word >> 6) & 0x3E0)
            reads = {8: frozenset(["lr"]), 9: frozenset(["ctr"])}.get(spr, frozenset())
            return reads, R(d), nxt, ""
        if xo == 467:                          # mtspr
            spr = ((word >> 16) & 31) | ((word >> 6) & 0x3E0)
            return R(d), {8: frozenset(["lr"]), 9: frozenset(["ctr"])}.get(spr, frozenset()), nxt, ""
        if xo == 19:                           # mfcr
            return frozenset(f"cr{i}" for i in range(8)), R(d), nxt, ""
        if xo == 144:                          # mtcrf: field i is written when bit (7 - i) of the mask is set
            mask = (word >> 12) & 0xFF
            return R(d), frozenset(f"cr{i}" for i in range(8) if mask & (0x80 >> i)), nxt, ""
        if xo == 83:                           # mfmsr
            return frozenset(), R(d), nxt, ""
        if xo == 146:                          # mtmsr
            return R(d), frozenset(), nxt, ""
        if xo in (86, 54, 278, 246, 1014, 982, 470):    # dcbf dcbst dcbt dcbtst dcbz icbi dcbi
            return base | R(b), frozenset(), nxt, ""
        if xo == 598:                          # sync
            return frozenset(), frozenset(), nxt, ""
        return ALL, frozenset(), nxt, "unknown"
    if 32 <= op <= 45:                         # lwz lwzu lbz lbzu stw stwu stb stbu lhz lhzu lha lhau sth sthu
        update = R(a) if op & 1 else frozenset()
        if op in (36, 37, 38, 39, 44, 45):
            return R(d) | base, update, nxt, ""
        return base, R(d) | update, nxt, ""
    if op == 46:                               # lmw
        return base, R(*range(d, 32)), nxt, ""
    if op == 47:                               # stmw
        return R(*range(d, 32)) | base, frozenset(), nxt, ""
    if 48 <= op <= 55 or op in (56, 57, 60, 61):   # lfs lfsu lfd lfdu stfs stfsu stfd stfdu, psq_l psq_lu psq_st psq_stu
        return base, R(a) if op & 1 else frozenset(), nxt, ""
    if op in (59, 63):                         # floating point: no general register; a compare writes its CR field
        xo = (word >> 1) & 0x3FF
        if op == 63 and xo in (0, 32):
            return frozenset(), frozenset([crf]), nxt, ""
        if op == 63 and xo in (64, 134, 711, 70, 38, 583):      # mcrfs mtfsfi mtfsf mtfsb0 mtfsb1 mffs
            return (ALL, frozenset(), nxt, "unknown") if xo == 64 else (frozenset(), frozenset(["cr1"]) if rc else frozenset(), nxt, "")
        return frozenset(), frozenset(["cr1"]) if rc else frozenset(), nxt, ""
    if op == 4:                                # paired singles
        xo = (word >> 1) & 0x3FF
        if xo & 0x3F in (6, 7, 38, 39):        # psq_lx psq_stx psq_lux psq_stux
            return base | R(b), R(a) if xo & 0x20 else frozenset(), nxt, ""
        if xo in (0, 32, 64, 96):              # ps_cmp*
            return frozenset(), frozenset([crf]), nxt, ""
        return frozenset(), frozenset(["cr1"]) if rc else frozenset(), nxt, ""
    return ALL, frozenset(), nxt, "unknown"


def dead(read_word, start, resource, limit=600):
    """True when, on every path from `start`, `resource` is written (or loses its meaning at a call or return) before
    it is read; otherwise a string saying where a read, an unknown instruction or the search's limit was met.
    read_word(address) gives the instruction word there, or None outside the code at hand."""
    seen, work = set(), [start]
    while work:
        address = work.pop()
        if address in seen:
            continue
        seen.add(address)
        if len(seen) > limit:
            return f"{resource}: no answer within {limit} instructions from {start:08X}"
        word = read_word(address)
        if word is None:
            return f"{resource}: the code leaves the image at {address:08X}"
        reads, writes, successors, kind = decode(word, address)
        if kind == "unknown":
            return f"{resource}: instruction {word:08X} at {address:08X} is not known to the decoder"
        if resource in reads:
            return f"{resource}: read at {address:08X} ({word:08X})"
        if resource in writes:
            continue
        if kind == "jump":
            return f"{resource}: an indirect jump at {address:08X}"
        if kind == "return":
            if resource in RETURN_READS:
                return f"{resource}: live at the return at {address:08X}"
            work.extend(successors)        # a conditional return: the fall-through path goes on
            continue
        work.extend(successors)
    return True
