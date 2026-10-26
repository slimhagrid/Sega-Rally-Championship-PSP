"""M5: compare a PPSSPP screenshot of the frame viewer (mode 0: 352x224 picture at the top-left of the 480x272 screen) with the reference frame made from the original code.

    python tools/compare_frames.py <reference.png> <ppsspp_screenshot.png> [out.png]

The screenshot (client area of the PPSSPP window, any integer scale of 480x272) is box-resized to 480x272 and cropped to 352x224.  Prints the mean absolute
difference per channel and the share of pixels that differ by more than 24 in any channel, split into "both drawn" / "only one drawn" (black = nothing drawn in
this image); writes ref | PSP | difference side by side.
"""
import sys

import numpy as np
from PIL import Image


def load_psp(path):
    im = Image.open(path).convert("RGB")
    if im.size != (480, 272):
        im = im.resize((480, 272), Image.BOX)
    return np.asarray(im.crop((0, 0, 352, 224)), dtype=np.int32)


def main():
    ref = np.asarray(Image.open(sys.argv[1]).convert("RGB"), dtype=np.int32)
    psp = load_psp(sys.argv[2])
    d = np.abs(ref - psp)
    big = (d.max(axis=2) > 24)
    ref_on = ref.max(axis=2) > 0
    psp_on = psp.max(axis=2) > 0
    both = ref_on & psp_on
    print("mean abs diff %.2f   differing >24: %.1f%% of all pixels" % (d.mean(), 100.0 * big.mean()))
    print("coverage: reference %.1f%%, PSP %.1f%%, both %.1f%%, only reference %.1f%%, only PSP %.1f%%" % (
        100 * ref_on.mean(), 100 * psp_on.mean(), 100 * both.mean(), 100 * (ref_on & ~psp_on).mean(), 100 * (psp_on & ~ref_on).mean()))
    if both.any():
        print("where both drawn: mean abs diff %.2f, %.1f%% differ >24" % (d[both].mean(), 100.0 * big[both].mean()))
    if len(sys.argv) > 3:
        diff = np.clip(d * 3, 0, 255).astype(np.uint8)
        out = np.concatenate([ref.astype(np.uint8), psp.astype(np.uint8), diff], axis=1)
        Image.fromarray(out).save(sys.argv[3])
        print("->", sys.argv[3])


if __name__ == "__main__":
    main()
