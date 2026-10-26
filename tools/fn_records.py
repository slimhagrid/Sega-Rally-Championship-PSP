"""M8: function-level golden vectors of the record helpers that no screen of the oracle runs (digit writers, time_disp, record_insert, table writers, checksums).

    python tools/fn_records.py          -> extracted/oracle/fn_records.txt   (checked by tools/test_rec_fn.sh)

The original function is run on the SH-2 interpreter with the registers it expects (cpu.call) after the memory patches of the case; the result is the registers and the memory delta
(compared regions of tools/menu_oracle.py).  The C test applies the same patches to its own memory (after sat_boot, which matches the oracle's boot), calls the port and compares.
The cases run one after the other on the same memory, so the state accumulates the same way on both sides.
File format (text): `C fn r2 r4 r5 r6 r7 r8 r11 r14` / `P addr hex` ... / `O r0 r4 r9` / `D addr hex` ... / `E`.
"""
import os
import random
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from menu_drive import Drv  # noqa: E402
from menu_env import ROOT  # noqa: E402
from menu_oracle import REGIONS  # noqa: E402

FN = {
    "putnum_1x2": 0x0602CB48, "putnum_rec": 0x0602CB62, "putnum_name": 0x0602CBA4, "putnum_name2": 0x0602CBBA, "putnum_rank": 0x0602CBD8, "putnum_rank2": 0x0602CC0A,
    "putnum_Grn": 0x0602CC44, "putnum_Grn2": 0x0602CC5C, "time_disp": 0x0602CAF8, "name_disp": 0x0602EE68, "insert": 0x0602E8EA, "pal_set": 0x0602D330, "color_set": 0x0602D358,
    "vdp2_set_w": 0x0602D370, "tile16": 0x0602EF2E, "tile32": 0x0602EF4A, "check_main": 0x0602F5BC, "check_replays": 0x0602F5E8,
}
PUT = [FN[k] for k in ("putnum_1x2", "putnum_rec", "putnum_name", "putnum_name2", "putnum_rank", "putnum_rank2", "putnum_Grn", "putnum_Grn2")]


def snap(mem):
    out = []
    for a, n in REGIONS:
        if 0x06000000 <= a < 0x08000000:
            out.append(np.frombuffer(mem.hw, dtype=np.uint8)[a & 0xFFFFF:(a & 0xFFFFF) + n].copy())
        elif 0x00200000 <= a < 0x00300000:
            out.append(np.frombuffer(mem.lw, dtype=np.uint8)[a & 0xFFFFF:(a & 0xFFFFF) + n].copy())
        else:
            out.append(np.frombuffer(bytes(mem.read(a, n)), dtype=np.uint8).copy())
    return out


def delta(before, after):
    res = []
    for (a, n), b, c in zip(REGIONS, before, after):
        idx = np.flatnonzero(b != c)
        if not len(idx):
            continue
        cuts = np.flatnonzero(np.diff(idx) > 8)
        starts = np.concatenate(([0], cuts + 1))
        ends = np.concatenate((cuts, [len(idx) - 1]))
        for s_, e_ in zip(starts, ends):
            lo, hi = int(idx[s_]), int(idx[e_]) + 1
            res.append((a + lo, c[lo:hi].tobytes()))
    return res


def main():
    d = Drv()
    m = d.m
    mem = d.mem
    cpu = m.cpu
    rnd = random.Random(2024)
    lines = []

    def run(fn, r2=0, r4=0, r5=0, r6=0, r7=0, r8=0, r11=0, r14=0, patches=()):
        for a, data in patches:
            for k, b in enumerate(data):
                mem.w8(a + k, b)
        before = snap(mem)
        regs = {2: r2 & 0xFFFFFFFF, 4: r4 & 0xFFFFFFFF, 5: r5 & 0xFFFFFFFF, 6: r6 & 0xFFFFFFFF, 7: r7 & 0xFFFFFFFF, 8: r8 & 0xFFFFFFFF, 11: r11 & 0xFFFFFFFF, 14: r14 & 0xFFFFFFFF, 15: 0x060FF000}
        cpu.call(fn, regs)
        after = snap(mem)
        lines.append("C %x %d %d %d %d %d %d %d %d" % (fn, r2 & 0xFFFFFFFF, r4 & 0xFFFFFFFF, r5 & 0xFFFFFFFF, r6 & 0xFFFFFFFF, r7 & 0xFFFFFFFF, r8 & 0xFFFFFFFF, r11 & 0xFFFFFFFF, r14 & 0xFFFFFFFF))
        for a, data in patches:
            lines.append("P %x %s" % (a, data.hex()))
        lines.append("O %d %d %d" % (cpu.r[0] & 0xFFFFFFFF, cpu.r[4] & 0xFFFFFFFF, cpu.r[9] & 0xFFFFFFFF))
        for a, data in delta(before, after):
            lines.append("D %x %s" % (a, data.hex()))
        lines.append("E")

    def cell(align=4):
        return (0x25E30000 + rnd.randrange(0, 0xF000, align)) & ~(align - 1)

    # digit writers: every digit, both cell sizes, highlight on / off
    for fn in PUT:
        for _ in range(40):
            r11 = rnd.choice((0, 0, 1))
            r14 = rnd.choice((-1, 0, 7))
            run(fn, r2=rnd.randrange(0, 12), r4=cell(0x10), r11=r11, r14=r14 & 0xFFFFFFFF)
    # time_disp with every writer: BCD times (also non-BCD nibbles and 0 minutes)
    for cb in PUT:
        for _ in range(25):
            t = rnd.choice((0, rnd.randrange(0, 0x100) << 16 | rnd.randrange(0, 0x10000), rnd.getrandbits(24)))
            run(FN["time_disp"], r4=cell(0x10), r5=t, r6=cb, r11=rnd.choice((0, 1)), r14=rnd.choice((0xFFFFFFFF, 0, 3)))
    # names: three letters, 32-bit and 16-bit cells
    for _ in range(60):
        run(FN["name_disp"], r4=cell(0x10), r5=rnd.getrandbits(32) & rnd.choice((0xFFFFFFFF, 0x001F1F1F, 0x00001F1F, 0)), r11=rnd.choice((0, 1)), r14=rnd.choice((0xFFFFFFFF, 0, 3)))
    # record insertion: a sorted list of 16 entries at 0x060F2000, the new time at 0x060F1000
    for k in range(60):
        n = rnd.choice((6, 8, 16))
        keys = sorted(rnd.randrange(0x00010000, 0x00090000) for _ in range(n + 2))
        lst = b"".join(keys[i].to_bytes(4, "big") + rnd.getrandbits(32).to_bytes(4, "big") for i in range(n + 2))
        new = rnd.choice((keys[0] - 1, keys[rnd.randrange(len(keys))], keys[-1] + 5, rnd.randrange(0x00010000, 0x00090000)))
        pat = [(0x060F2000, lst), (0x060F1000, (new & 0xFFFFFFFF).to_bytes(4, "big")), (0x060AA809, bytes([rnd.randrange(0, 2)])), (0x060AA811, bytes([rnd.randrange(0, 4)])),
               (0x060AA80A, bytes([rnd.randrange(0, 4)]))]
        run(FN["insert"], r4=0x060F1000, r5=0x060F2000, r6=n, r8=rnd.choice((0, 4, 8, 12)), patches=pat)
    # table writers
    for k in range(30):
        n = rnd.randrange(1, 5)
        tbl = n.to_bytes(4, "big") + b"".join((0x00250000 + rnd.randrange(0, 0x1000, 4)).to_bytes(4, "big") + rnd.randrange(0, 0x100, 2).to_bytes(2, "big") + rnd.randrange(1, 20).to_bytes(2, "big") for _ in range(n))
        run(FN["pal_set"], r8=0x060F3000, r11=rnd.choice((0, 0x100, 0x1000)), patches=[(0x060F3000, tbl)])
        tbl = n.to_bytes(4, "big") + b"".join(rnd.randrange(0, 0x200, 2).to_bytes(2, "big") + rnd.getrandbits(16).to_bytes(2, "big") for _ in range(n))
        run(FN["color_set"], r8=0x060F3000, patches=[(0x060F3000, tbl)])
        tbl = n.to_bytes(4, "big") + b"".join(rnd.randrange(0, 0x11E, 2).to_bytes(2, "big") + rnd.getrandbits(16).to_bytes(2, "big") for _ in range(n))
        run(FN["vdp2_set_w"], r8=0x060F3000, patches=[(0x060F3000, tbl)])
    # tile fills
    for fn in (FN["tile16"], FN["tile32"]):
        for _ in range(30):
            run(fn, r4=rnd.getrandbits(16), r5=cell(0x10), r6=rnd.randrange(1, 14), r7=rnd.randrange(1, 4))
    # checksums: the save image and the replays
    img = bytes(rnd.getrandbits(8) for _ in range(0x13C0))
    s = (9 + sum(int.from_bytes(img[i:i + 4], "big") for i in range(0, 0x13BC, 4))) & 0xFFFFFFFF
    good = img[:0x13BC] + s.to_bytes(4, "big")
    run(FN["check_main"], patches=[(0x060EEC00, good)])
    run(FN["check_main"], patches=[(0x060EEC00 + 100, b"\x55")])
    reps = []
    for k in range(4):
        body = bytes(rnd.getrandbits(8) for _ in range(0x3D00 - 4))
        sm = (9 + sum(int.from_bytes(body[i:i + 4], "big") for i in range(0, len(body), 4))) & 0xFFFFFFFF
        reps.append((0x002C0000 + k * 0x3D00, sm.to_bytes(4, "big") + body))
    run(FN["check_replays"], patches=reps)
    run(FN["check_replays"], patches=[(0x002C0000 + 2 * 0x3D00 + 50, b"\x77")])
    run(FN["check_replays"], patches=[(0x002C0000 + 3 * 0x3D00, b"\x00\x00\x00\x00"), (0x002C0000 + 2 * 0x3D00 + 50, b"\x00")])
    out = os.path.join(ROOT, "extracted", "oracle", "fn_records.txt")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w") as f:
        f.write("\n".join(lines) + "\n")
    print("%d cases -> %s" % (sum(1 for ln in lines if ln.startswith("C ")), out))


if __name__ == "__main__":
    main()
