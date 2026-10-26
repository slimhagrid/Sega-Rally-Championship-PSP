"""M3: write every block of every scatter-load file (`*.DAT`, `*.MAD`) to `extracted/blocks/<FILE>/<nn>_<dest>.bin`, type-2 blocks BPE-expanded (what the loader leaves in memory).

    python tools/dump_blocks.py [FILE.DAT ...]          (default: all files whose header parses and whose size adds up)
Prints one line per file: block count and the destinations (VDP1/VDP2 VRAM 0x25Cxxxxx / 0x25Exxxxx, sound RAM 0x25Axxxxx, Low / High WRAM).
Also writes extracted/blocks/index.json (file -> list of {index, dest, type, arg, stored, expanded}).
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
from datfile import Scatter  # noqa: E402

DISC = os.path.join(ROOT, "extracted", "disc", "files")
OUT = os.path.join(ROOT, "extracted", "blocks")


def main():
    names = sys.argv[1:] or sorted(f for f in os.listdir(DISC) if f.endswith((".DAT", ".MAD")))
    index = {}
    total = 0
    for f in names:
        path = os.path.join(DISC, f)
        if os.path.getsize(path) < 0x800:
            continue
        s = Scatter(path)
        if not s.blocks or len(s.blocks) > 40 or not s.check() or any(b.size > len(s.raw) for b in s.blocks):
            continue                      # not a scatter-load file (e.g. EVENTTBL.DAT, ASCII.DAT)
        d = os.path.join(OUT, f)
        os.makedirs(d, exist_ok=True)
        entries = []
        for b in s.blocks:
            data = s.expanded(b)
            with open(os.path.join(d, "%02d_%08x.bin" % (b.index, b.dest)), "wb") as fh:
                fh.write(data)
            entries.append({"index": b.index, "dest": "0x%08x" % b.dest, "type": b.type, "arg": b.arg, "stored": b.size, "expanded": len(data)})
            total += len(data)
        index[f] = entries
        print("%-14s %2d blocks: %s" % (f, len(entries), " ".join("%x%s" % (b.dest >> 12 if b.dest >= 0x1000000 else b.dest, "*" if b.type == 2 else "") for b in s.blocks)))
    with open(os.path.join(OUT, "index.json"), "w") as fh:
        json.dump(index, fh, indent=1)
    print("%d scatter-load files, %.1f MB of blocks (* = type 2, expanded)" % (len(index), total / 1e6))


if __name__ == "__main__":
    main()
