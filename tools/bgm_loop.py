"""M9: find the loop of a rendered song (float32 stereo, 89 frames per driver tick, tools/bgm_render.c):  python tools/bgm_loop.py render.raw [expected period in ticks]
Prints where the sound ends (one-shot songs) or the loop period in ticks: the lag with the smallest waveform error between the signal and itself (a lag of a few seconds up to half the render)."""
import sys
import numpy as np

TICK = 89
a = np.fromfile(sys.argv[1], dtype="<f4").reshape(-1, 2)[:, 0]
n = len(a) // TICK
x = a[: n * TICK]
blocks = x.reshape(n, TICK)
peak = np.abs(blocks).max(axis=1)
nz = np.nonzero(peak > 1e-5)[0]
last = int(nz[-1]) if len(nz) else -1
print("ticks", n, "= %.1f s" % (n * TICK / 44100.0), "first sound", int(nz[0]) if len(nz) else None, "last sound", last, "= %.1f s" % (last * TICK / 44100.0))
if last < n - 2000:
    print("one-shot: the sound ends at tick", last)
    sys.exit(0)
env = (blocks ** 2).sum(axis=1)
e = env[: (n // 8) * 8].reshape(-1, 8).sum(axis=1)          # 8 ticks = 16 ms
e = e / e.mean()
m = len(e)
lo = int(8.0 * 44100 / TICK / 8)                              # at least 8 s
cands = []
for lag in range(lo, m // 2):
    d = np.abs(e[lag:] - e[: m - lag]).mean()
    cands.append((d, lag))
cands.sort()
print("envelope candidates (error, ticks):", [(round(float(d), 3), l * 8) for d, l in cands[:6]])


def werr(d, at, L=15000):
    A = x[at * TICK: (at + L) * TICK]
    B = x[(at + d) * TICK: (at + d + L) * TICK]
    return float(np.abs(A - B).mean() / max(np.abs(A).mean(), 1e-9))


res = []
for _, lag in cands[:6]:
    best = min((werr(d, n // 3 if False else 3000), d) for d in range(lag * 8 - 12, lag * 8 + 12) if (3000 + d + 15000) < n)
    res.append(best)
res.sort()
print("waveform error (relative), period in ticks:", [(round(r, 3), d) for r, d in res])
