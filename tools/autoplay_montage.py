import sys, glob, os
from PIL import Image, ImageDraw
d, out = sys.argv[1], sys.argv[2]
cols = int(os.environ.get("COLS", "5")); sc = float(os.environ.get("SC", "0.4"))
files = sorted(glob.glob(d + "/*.png"))
w, h = int(480 * sc), int(272 * sc)
rows = (len(files) + cols - 1) // cols
W = Image.new("RGB", (w * cols, (h + 12) * rows), (40, 40, 40))
dr = ImageDraw.Draw(W)
for i, f in enumerate(files):
    im = Image.open(f).convert("RGB").resize((w, h))
    x, y = (i % cols) * w, (i // cols) * (h + 12)
    W.paste(im, (x, y + 12)); dr.text((x + 2, y), os.path.basename(f)[:-4][:34], fill=(255, 255, 0))
W.save(out); print(len(files), W.size)
