#!/usr/bin/env python3
"""Find where the functions of the game's program (0RALLY.DAT, SH-2) are: runs Ghidra headless on it and writes extracted/analysis/functions.tsv (address, size, name, module).

    python tools/ghidra_import.py          (needs extracted/disc from tools/extract_disc.py and extracted/analysis/symbols.tsv from tools/dbg_parse.py)

Needs Ghidra 12.1.x (https://ghidra-sre.org/, its SuperH-2 language is part of it) and a JDK 21:  set GHIDRA_HOME to the Ghidra folder; JAVA_HOME to the JDK (or have `java` on the PATH).
It takes about 7 minutes (almost all of it Ghidra's switch analysis).  The ported menu screens use the result to tell the program's code (which the port does not carry) from its data tables
(tools/conv_menu.py); the oracle tools use the names.  The Ghidra project is written to ghidra/ (git-ignored) and can be opened with ghidraRun to look at the program.
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)


def main():
    gh = os.environ.get("GHIDRA_HOME")
    if not gh or not os.path.isdir(gh):
        print("set GHIDRA_HOME to the Ghidra folder (the one with ghidraRun), e.g.  export GHIDRA_HOME=~/ghidra_12.1.3_PUBLIC")
        return 2
    exe = os.path.join(gh, "support", "analyzeHeadless" + (".bat" if os.name == "nt" else ""))
    image = os.path.join(ROOT, "extracted", "disc", "files", "0RALLY.DAT")
    if not os.path.exists(image) or not os.path.exists(os.path.join(ROOT, "extracted", "analysis", "symbols.tsv")):
        print("run  python tools/extract_disc.py  and  python tools/dbg_parse.py  first")
        return 2
    proj = os.path.join(ROOT, "ghidra")
    os.makedirs(proj, exist_ok=True)
    env = dict(os.environ)
    if env.get("JAVA_HOME"):
        env["PATH"] = os.path.join(env["JAVA_HOME"], "bin") + os.pathsep + env["PATH"]
    cmd = [exe, proj, "SegaRally", "-import", image, "-overwrite", "-processor", "SuperH:BE:32:SH-2", "-loader", "BinaryLoader", "-loader-baseAddr", "0x06004000",
           "-scriptPath", os.path.join(HERE, "ghidra"), "-preScript", "SetupRally.java", ROOT, "-postScript", "ReportRally.java", ROOT]
    r = subprocess.run(cmd, env=env, cwd=ROOT)
    out = os.path.join(ROOT, "extracted", "analysis", "functions.tsv")
    if r.returncode != 0 or not os.path.exists(out):
        print("Ghidra failed (exit %d)" % r.returncode)
        return 1
    print("functions.tsv: %d functions" % sum(1 for _ in open(out)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
