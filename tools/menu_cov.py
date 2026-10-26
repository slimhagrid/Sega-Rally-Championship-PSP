"""M8: code coverage of oracle scenarios over a range of the original program: which instructions did the scenarios execute?

    python tools/menu_cov.py <scenario>[,<scenario>...] <lo> <hi> [-r]     (hex addresses, e.g. 06022894 06023500; -r = report the range as one block, not per function)

Runs the scenarios (tools/menu_oracle.py SCENARIOS / tools/scn_*.py) on the SH-2 harness with the program counter recorded, then prints, per function of
tools/analysis, the share of its instruction addresses that ran, and the address ranges that never ran.  A port is only as well tested as the paths its scenarios reach:
add scenarios until every branch you ported is covered (or understood to be unreachable).
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import menu_oracle  # noqa: E402
import rd  # noqa: E402
import sh2dis  # noqa: E402
from menu_drive import Drv  # noqa: E402


def run_scenario(name, pcs):
    d = Drv()
    for a, data in menu_oracle.PATCHES.get(name, []):
        d.patch(a, data)
    if menu_oracle.START_AT.get(name):
        d.m.start(menu_oracle.START_AT[name])
    cpu = d.m.cpu
    cpu.hist = []
    orig_run = d.m.run

    def run(maxsteps=100_000_000):
        try:
            return orig_run(maxsteps)
        finally:
            pcs.update(cpu.hist)
            cpu.hist.clear()
    d.m.run = run
    menu_oracle.SCENARIOS[name](d)
    if menu_oracle.FINAL.get(name):                         # the exit stores up to the dispatch of the next screen (see Rec.finish)
        class Reached(Exception):
            pass

        def hook(c):
            raise Reached()
        cpu.hooks[menu_oracle.FINAL[name]] = hook
        try:
            for _ in range(5000):
                d.run(1)
        except Reached:
            pass
    pcs.update(cpu.hist)


def code_addresses(lo, hi):
    """instruction addresses in [lo, hi): everything except literal pool words of pc-relative loads"""
    pool = set()
    a = lo
    while a < hi:
        o = sh2dis.w(a)
        if (o >> 12) == 9:
            pool.add(a + 4 + (o & 0xFF) * 2)
        elif (o >> 12) == 13 or ((o >> 12) == 12 and ((o >> 8) & 15) == 7):
            t = ((a + 4) & ~3) + (o & 0xFF) * 4
            pool.update((t, t + 2))
        a += 2
    return [x for x in range(lo, hi, 2) if x not in pool]


def entries_cov(names, path):
    """python tools/menu_cov.py <scenarios> -e <file>: coverage of the code reachable from the entry points in <file> (liftseg syntax: entries, -t table:n, -lo, -hi), every never-run range listed"""
    import liftseg
    toks = open(path).read().split()
    ents, tabs, lo, hi, i = [], [], 0x06004000, 0x06036000, 0
    keep = []                                            # -keep <lo> <hi>: only these ranges are reported (the code the walk reaches through calls outside them is somebody else's)
    while i < len(toks):
        if toks[i] == "-t":
            t, n = toks[i + 1].split(":"); tabs.append((int(t, 16), int(n))); i += 2
        elif toks[i] == "-lo":
            lo = int(toks[i + 1], 16); i += 2
        elif toks[i] == "-hi":
            hi = int(toks[i + 1], 16); i += 2
        elif toks[i] == "-keep":
            keep.append((int(toks[i + 1], 16), int(toks[i + 2], 16))); i += 3
        else:
            ents.append(int(toks[i], 16)); i += 1
    code = sorted(a for a in liftseg.reachable(ents, tabs, lo, hi) if not keep or any(x <= a < y for x, y in keep))
    pcs = set()
    for n in names:
        run_scenario(n, pcs)
    miss = [a for a in code if a not in pcs and sh2dis.w(a) != 0x0009]       # delay slot nops of calls the harness hooks never run
    runs = []
    for a in miss:
        if runs and a - runs[-1][1] <= 2:
            runs[-1][1] = a
        else:
            runs.append([a, a])
    print("%d / %d reachable instruction addresses ran" % (len(code) - len(miss), len(code)))
    print("never ran: " + " ".join("%x-%x" % (x, y) for x, y in runs))


def main():
    names = sys.argv[1].split(",")
    if sys.argv[2] == "-e":
        entries_cov(names, sys.argv[3])
        return
    lo, hi = int(sys.argv[2], 16), int(sys.argv[3], 16)
    pcs = set()
    for n in names:
        run_scenario(n, pcs)
    funcs = [f for f in rd.FUNCS if lo <= f[0] < hi]
    if len(sys.argv) > 4 and sys.argv[4] == "-r":            # one block: code that the function table cuts into pieces or does not know (screen state handlers)
        funcs = [(lo, hi - lo, "range")]
    tot = hit = 0
    for f in funcs:
        addrs = code_addresses(f[0], f[0] + f[1])
        ran = [a for a in addrs if a in pcs]
        tot += len(addrs)
        hit += len(ran)
        miss = [a for a in addrs if a not in pcs]
        runs = []
        for a in miss:
            if runs and a - runs[-1][1] <= 2:
                runs[-1][1] = a
            else:
                runs.append([a, a])
        print("%08x %-28s %3d%% (%d / %d)%s" % (f[0], f[2], 100 * len(ran) // max(1, len(addrs)), len(ran), len(addrs),
                                                  ("  never ran: " + " ".join("%x-%x" % (x, y) for x, y in runs[:8])) if runs and ran else ("  NOT RUN" if not ran else "")))
    print("total %d / %d instruction addresses" % (hit, tot))


if __name__ == "__main__":
    main()
