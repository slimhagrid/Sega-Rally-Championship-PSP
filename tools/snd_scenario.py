"""M9: a sound scenario for the C core (tools/test_snd.c) and the same run in unicorn (tools/scsp_sim.py):

    python tools/snd_scenario.py <out prefix> <map> <ticks> "<file> <file> .." "<tick:cmd:p0:p1:p2:p3> .."     e.g.  desert 1 400 "SDDRVS SDDESERT CELICA1" "20:8:1:0:0:0 40:1:7:0:1:0"

writes <prefix>.ram (the sound RAM after the files were loaded), <prefix>.cmds (for test_snd), <prefix>.ref (the SCSP register writes the unicorn run made: "tick offset size value", only the
slot registers < 0x400 and the common ones the driver sets), and the run's audio through tools/scsp_render.py as <prefix>.wav
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from scsp_sim import SoundSim  # noqa: E402


def main():
    prefix, ticks = sys.argv[1], int(sys.argv[3])
    files = sys.argv[4].split()
    cmds = []
    for c in sys.argv[5].split():
        cmds.append([int(x) for x in c.split(":")])
    s = SoundSim()
    for f in files:
        s.load(f)
    open(prefix + ".ram", "wb").write(s.read_ram(0, 0x80000))
    with open(prefix + ".cmds", "w") as f:
        for c in cmds:
            f.write("%d %d %d %d %d %d\n" % tuple(c))
    s.boot()
    tr = []
    for t in range(ticks):
        for c in cmds:
            if c[0] == t:
                s.command(c[1], bytes(c[2:6]))
        try:
            s.run_ticks(1, trace=tr)
        except Exception as e:
            from unicorn.m68k_const import UC_M68K_REG_PC
            pc = s.uc.reg_read(UC_M68K_REG_PC)
            print("exception at tick", t, "pc %x" % pc, bytes(s.uc.mem_read(pc, 8)).hex(), [hex(x[0]) for x in tr[-12:]])
            break
    import struct
    with open(prefix + ".trace", "wb") as f:                # per instruction: pc, d0-d7, a0-a7 (little endian u32), as test_snd.c writes them
        for r in tr:
            f.write(struct.pack("<18I", *r))
    print(len(tr), "instructions")
    with open(prefix + ".ref", "w") as f:
        for tick, k, off, v, sz in s.log:
            f.write("%d %x %d %x\n" % (tick, off, sz, v))
    print(len(s.log), "register writes,", s.tick, "ticks")


main()
