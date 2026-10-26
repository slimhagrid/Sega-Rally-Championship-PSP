"""M5: enlarge a region of a reference frame and of its PPSSPP screenshot side by side (reference | PSP | difference).

    python tools/zoom_compare.py <frame name> x0 y0 x1 y1 [scale] [out.png]        region in 352x224 game pixels; needs extracted/ref/<name>.png and shot_<name>.png
"""
import os
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
from compare_frames import load_psp  # noqa: E402


def main():
    name = sys.argv[1]
    x0, y0, x1, y1 = (int(v) for v in sys.argv[2:6])
    scale = int(sys.argv[6]) if len(sys.argv) > 6 else 4
    out = sys.argv[7] if len(sys.argv) > 7 else os.path.join(ROOT, "extracted", "ref", "zoom_%s.png" % name)
    ref = np.asarray(Image.open(os.path.join(ROOT, "extracted", "ref", name + ".png")).convert("RGB"), dtype=np.int32)
    psp = load_psp(os.path.join(ROOT, "extracted", "ref", "shot_%s.png" % name))
    d = np.clip(np.abs(ref - psp) * 3, 0, 255)
    tiles = []
    for a in (ref, psp, d):
        t = Image.fromarray(a[y0:y1, x0:x1].astype(np.uint8))
        tiles.append(np.asarray(t.resize(((x1 - x0) * scale, (y1 - y0) * scale), Image.NEAREST)))
        tiles.append(np.full((tiles[-1].shape[0], 6, 3), 128, np.uint8))
    Image.fromarray(np.concatenate(tiles[:-1], axis=1)).save(out)
    print(out)


if __name__ == "__main__":
    main()
