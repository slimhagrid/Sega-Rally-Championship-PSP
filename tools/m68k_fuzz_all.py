"""M9: the whole opcode space of tools/m68k_fuzz.py in crash-proof pieces (unicorn dies natively on a few states, taking the process with it):

    python tools/m68k_fuzz_all.py out.bin [states_per_opcode] [workers]

Runs m68k_fuzz.py on chunks of opcodes in worker processes; a chunk whose process crashes is split until the failing opcode is alone, and that opcode is written as "no result" records (the C test skips those).
"""
import os
import struct
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.path.join(HERE, "m68k_fuzz.py")


def run(lo, hi, per, tmpdir):
    path = os.path.join(tmpdir, "c_%05x_%05x.bin" % (lo, hi))
    try:
        r = subprocess.run([sys.executable, SCRIPT, path, str(per), str(lo), str(hi)], capture_output=True, timeout=15 + (hi - lo + 1) // 8)        # (a state that makes unicorn loop for ever is a failure too)
        ok = r.returncode == 0 and os.path.exists(path)
    except subprocess.TimeoutExpired:
        ok = False
    if ok:
        return open(path, "rb").read()
    if lo == hi:                                            # the crashing opcode: no results for any of its states
        out = b""
        for k in range(per):
            seed = (lo * 7919 + k * 104729 + 12345) & 0xFFFFFFFF
            out += struct.pack(">III", lo, seed, 0)
        return out
    mid = (lo + hi) // 2
    return run(lo, mid, per, tmpdir) + run(mid + 1, hi, per, tmpdir)


def main():
    out, per = sys.argv[1], int(sys.argv[2]) if len(sys.argv) > 2 else 4
    workers = int(sys.argv[3]) if len(sys.argv) > 3 else 6
    chunk = 512
    tmp = tempfile.mkdtemp(prefix="fuzz")
    ranges = [(lo, min(lo + chunk - 1, 0xFFFF)) for lo in range(0, 0x10000, chunk)]
    with ThreadPoolExecutor(workers) as ex:
        parts = list(ex.map(lambda r: run(r[0], r[1], per, tmp), ranges))
    with open(out, "wb") as f:
        for p in parts:
            f.write(p)
    print("done:", sum(len(p) for p in parts), "bytes")


main()
