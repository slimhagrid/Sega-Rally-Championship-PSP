"""M2: decode RALLY.DBG (Hitachi / Sega linker debug file shipped on the disc) into a symbol table.

    python tools/dbg_parse.py     -> extracted/analysis/symbols.json, symbols.tsv, modules.tsv

Format as far as it is understood (no public spec; worked out from the file and checked against 0RALLY.DAT):
- file header record `84`, then one `86` record per source module: `86 len 00 00 01 .. .. [len]name  04 "A_SH" <12-digit stamp>  06 "L_GX00" <12-digit stamp>`
- after the module header: `88` section records `88 len 00 <addr:4> <size:4> <align:4> <flag> ff c0 <len>name` (P = code, WRAM / C / D / B ...)
- `c0 <len> name` strings: external references (names the module imports)
- symbol entries `<flags:3> <value:4> <len> name` (big-endian):
      00 00 20 value    symbol in the module's first P section (value = offset): functions and constant tables
      00 00 00 value    symbol of a library part linked into the module (offset inside the 2nd, 3rd ... P section; offsets restart per section)
      00 40 04 value    data symbol with an absolute address
- the bulk of the file (~1.5 MB) is line-number tables (`<line:2> <start:4> <end:4>`, section relative): not decoded here.
"""
import json
import os
import re
import struct
from collections import Counter

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
FILES = os.path.join(ROOT, "extracted", "disc", "files")
OUT = os.path.join(ROOT, "extracted", "analysis")

IDENT = re.compile(rb"[A-Za-z_][A-Za-z0-9_$]*")
MODHDR = re.compile(rb"\x04A_SH(\d{12})\x06L_GX\d\d(\d{12})")
SECT = re.compile(rb"\x88([\x10-\x30])\x00(.{4})(.{4})(.{4})(.)\xff\xc0([\x01-\x20])", re.S)
BASE = 0x06004000
# The DBG describes a build in which every module from `slave` on lies 0x34 bytes lower than in the disc's 0RALLY.DAT (a 0x34-byte routine
# was added before `slave`: the SSHON command wrapper at 0x06017a08).  Evidence: `slave_start` is at DBG 0x06017a7c but the slave's idle loop is
# at 0x06017ab0 (and wait_slave_end / set_slave_exec / initial_slave each +0x34); over all 656 code symbols from `slave` on, function-style
# prologues at the symbol go from 5 to 197 with the shift (before `slave`: 79 without, 13 with).
SHIFT_FROM, SHIFT = 0x06017A7C, 0x34
# The disc's 0RALLY.DAT is a release build: between 0x0600CDB8 and ~0x0601E4B4 its modules are NOT where the DBG puts them (reordered, debug-only code dropped).
# Verified by reading the code (docs/collision_world_ai.md section 0.2, docs/render_pipeline.md section 9): real base of these modules, as a shift of the DBG base.
# (camera..laptime +0xBD4 and start..gamesd +0xBE4 also confirmed by function prologues at the shifted symbols; the 0x0600EE88-0x0600FA5C stretch is the CD loader, not in the DBG.)
MODULE_SHIFT = {"otherdat": 0x0600CDB8 - 0x0600EA3C, "adv": 0x44C, "advctrl": 0x44C, "photo": 0x44C, "photoctrl": 0x44C,
                "camera": 0xBD4, "dispcar": 0xBD4, "game": 0xBD4, "display": 0xBD4, "display2": 0xBD4, "display3": 0xBD4, "laptime": 0xBD4,
                "start": 0xBE4, "extract": 0xBE4, "extdata": 0xBE4, "debug": 0xBE4, "scroll": 0xBE4, "gamesd": 0xBE4}
# Modules in that stretch whose real place is not known yet: their symbols are written to symbols_unverified.tsv and NOT labelled in Ghidra.
UNVERIFIED = {"cache", "etcsub", "dsp", "action", "event", "generator", "public", "animal", "motion", "splash", "heli"}
UNVERIFIED_CODE_ONLY = {"sound"}      # its code-section names (sd_*, ...) do not fit the shifts above; the library part (_gfc_*, _res_ei ...) does
PUSH = {"4f22", "2fe6", "2fd6", "2fc6", "2fb6", "2fa6", "2f96", "2f86", "7ff0", "7ff4", "7ff8", "7ffc"}
KINDS = {b"\x00\x00\x20": "code", b"\x00\x00\x00": "lib", b"\x00\x40\x04": "data"}


def u32(b, o):
    return struct.unpack(">I", b[o:o + 4])[0]


def find_modules(b):
    mods = []
    for m in MODHDR.finditer(b):
        p = m.start()
        for n in range(1, 40):
            if p - 1 - n >= 0 and b[p - 1 - n] == n and IDENT.fullmatch(b[p - n:p]):
                mods.append({"name": b[p - n:p].decode(), "pos": p - 1 - n, "end": m.end(),
                             "asm_stamp": m.group(1).decode(), "link_stamp": m.group(2).decode()})
                break
    for a, c in zip(mods, mods[1:] + [None]):
        a["limit"] = c["pos"] if c else None
    return mods


def parse_sections(b, mod, limit):
    secs = []
    for m in SECT.finditer(b, mod["end"], limit):
        name = b[m.end():m.end() + b[m.end() - 1]].decode("latin-1")
        secs.append({"name": name, "addr": u32(m.group(2), 0), "size": u32(m.group(3), 0), "align": u32(m.group(4), 0),
                     "flag": m.group(5)[0], "pos": m.start()})
    return secs


def parse_symbols(b, start, limit):
    """Symbol entries `<flags:3><value:4><len>name` between `start` and `limit` (a scan: the layout around them is not fully known)."""
    out = []
    p = start
    while p < limit - 9:
        n = b[p + 7]
        if 2 <= n <= 48 and p + 8 + n <= limit and b[p:p + 3] in KINDS and IDENT.fullmatch(b[p + 8:p + 8 + n]):
            out.append((KINDS[b[p:p + 3]], u32(b, p + 3), b[p + 8:p + 8 + n].decode(), p))
            p += 8 + n
            continue
        p += 1
    return out


def main():
    b = open(os.path.join(FILES, "RALLY.DBG"), "rb").read()
    mods = find_modules(b)
    syms, kinds = [], Counter()
    for m in mods:
        limit = m["limit"] or len(b)
        secs = parse_sections(b, m, limit)
        for sc in secs:
            sc["dbg_addr"] = sc["addr"]
            if sc["name"] == "P" and m["name"] in MODULE_SHIFT:
                sc["addr"] += MODULE_SHIFT[m["name"]]
            elif sc["name"] == "P" and sc["addr"] >= SHIFT_FROM:
                sc["addr"] += SHIFT
        m["sections"] = secs
        psecs = [s for s in secs if s["name"] == "P" and s["addr"]]
        psec = psecs[0] if psecs else None
        m["P"] = psec or (secs[0] if secs else None)
        lib_group, last_off = -1, None   # `00 00 00` symbols: offsets grow inside one section and restart when the next library P section begins
        for kind, val, name, pos in parse_symbols(b, m["end"], limit):
            if kind == "data":
                addr = val
            elif kind == "code":
                addr = psec["addr"] + val if psec else None
            else:
                if last_off is None or val < last_off:
                    lib_group += 1
                last_off = val
                sec = psecs[1 + lib_group] if 1 + lib_group < len(psecs) else None
                if sec is None or val >= sec["size"]:
                    continue                    # noise: no section of that size to hold it
                addr = sec["addr"] + val
            syms.append({"module": m["name"], "kind": kind, "name": name, "addr": addr, "offset": val, "pos": pos,
                         "verified": not (kind != "data" and (m["name"] in UNVERIFIED or (m["name"] in UNVERIFIED_CODE_ONLY and kind == "code")))})
            kinds[kind] += 1
    os.makedirs(OUT, exist_ok=True)
    json.dump({"modules": [{k: v for k, v in m.items() if k != "end"} for m in mods], "symbols": syms},
              open(os.path.join(OUT, "symbols.json"), "w"), indent=1)
    print("modules", len(mods), "symbols", len(syms), dict(kinds))
    # addr kind name module, sorted by address: read by tools/ghidra/ImportRallySymbols.java
    # symbols whose module position is not verified go to symbols_unverified.tsv instead (not labelled in Ghidra)
    with open(os.path.join(OUT, "symbols.tsv"), "w", newline="\n") as f, \
            open(os.path.join(OUT, "symbols_unverified.tsv"), "w", newline="\n") as fu:
        for sy in sorted(syms, key=lambda x: (x["addr"] or 0, x["name"])):
            if sy["addr"] is not None:
                (f if sy["verified"] else fu).write("%08x\t%s\t%s\t%s\n" % (sy["addr"], sy["kind"], sy["name"], sy["module"]))
    # module section addr size
    with open(os.path.join(OUT, "modules.tsv"), "w", newline="\n") as f:
        for m in mods:
            for sc in m["sections"]:
                f.write("%s\t%s\t%08x\t%x\n" % (m["name"], sc["name"], sc["addr"], sc["size"]))

    # check: code symbols must land inside the loaded image (and, here, how many start like a function)
    img = open(os.path.join(FILES, "0RALLY.DAT"), "rb").read()
    inside = bad = push = 0
    for s in syms:
        if s["kind"] in ("code", "lib") and s["addr"] is not None:
            if BASE <= s["addr"] < BASE + len(img) and s["addr"] % 2 == 0:
                inside += 1
                push += img[s["addr"] - BASE:s["addr"] - BASE + 2].hex() in PUSH
            else:
                bad += 1
    print("code/lib symbols inside the image:", inside, " outside / odd:", bad, " starting with a register push / sts pr:", push)


if __name__ == "__main__":
    main()
