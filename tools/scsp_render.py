"""M9: a small SCSP model that turns the register log of tools/scsp_sim.py into 44.1 kHz stereo samples.

Per slot (32 registers of 32 bytes at 0x100000 + 0x20 * n, see the SCSP manual): PCM8 / PCM16 samples from sound RAM with loop modes, the pitch (OCT / FNS), the envelope generator
(AR / D1R / D2R / RR / DL, key rate scaling approximated), the total level and the direct send level / pan.  Not modelled: the LFO, the FM / modulation, the DSP effects (reverb) and
the effect sends.  The envelope times come from the SCSP manual's tables (as MAME's scsp.c lists them), the curves are approximated (attack linear in amplitude, decay linear in dB).

    audio = render(ram_bytes, log, ticks)            # numpy int16 array (n, 2)
"""
import numpy as np

RATE = 44100
TICK = 89                                               # samples per driver tick (timer A: 0xA7)

AR_TIMES = [100000, 100000, 8100.0, 6900.0, 6000.0, 4800.0, 4000.0, 3400.0, 3000.0, 2400.0, 2000.0, 1700.0, 1500.0, 1200.0, 1000.0, 860.0, 760.0, 600.0, 500.0, 430.0, 380.0, 300.0,
            250.0, 220.0, 190.0, 150.0, 130.0, 110.0, 95.0, 76.0, 63.0, 55.0, 47.0, 38.0, 31.0, 27.0, 24.0, 19.0, 16.0, 14.0, 12.0, 9.4, 7.9, 6.8, 6.0, 4.7, 3.9, 3.4, 3.0, 2.4, 2.0, 1.7,
            1.5, 1.2, 1.0, 0.86, 0.76, 0.60, 0.50, 0.43, 0.38, 0.30, 0.25, 0.22, 0.0]
DR_TIMES = [100000, 100000, 118200.0, 101300.0, 88600.0, 70900.0, 59100.0, 50700.0, 44300.0, 35500.0, 29600.0, 25300.0, 22200.0, 17700.0, 14800.0, 12700.0, 11100.0, 8900.0, 7400.0, 6300.0,
            5500.0, 4400.0, 3700.0, 3200.0, 2800.0, 2200.0, 1800.0, 1600.0, 1400.0, 1100.0, 920.0, 790.0, 690.0, 550.0, 460.0, 390.0, 340.0, 270.0, 230.0, 200.0, 170.0, 140.0, 110.0, 98.0,
            85.0, 68.0, 57.0, 49.0, 43.0, 34.0, 28.0, 25.0, 22.0, 18.0, 14.0, 12.0, 11.0, 8.5, 7.1, 6.1, 5.4, 4.3, 3.6, 3.1]


def _db(x):
    return 10.0 ** (x / 20.0)


class Slot:
    def __init__(self):
        self.active = False
        self.pos = 0.0
        self.rev = False
        self.state = "off"                              # attack, decay1, decay2, release, off
        self.level_db = -96.0                           # envelope attenuation in dB (0 = full)
        self.amp = 0.0                                  # attack: linear amplitude 0 .. 1
        self.hold = 0


def render(ram, log, ticks, tail=0):
    """ram: bytes of the sound RAM (512 KB).  log: [(tick, 'w', offset, value, size)].  Renders ticks + tail driver ticks."""
    regs = bytearray(0x1000)
    slots = [Slot() for _ in range(32)]
    n_total = (ticks + tail) * TICK
    out = np.zeros((n_total + TICK, 2), dtype=np.float32)
    ev = [e for e in log if e[2] < 0x400 and e[1] == "w"]
    ei = 0
    mem = np.frombuffer(bytes(ram), dtype=np.uint8)

    def r16(o):
        return (regs[o] << 8) | regs[o + 1]

    def slot_params(n):
        b = n * 0x20
        c = r16(b)
        p = {}
        p["pcm8"] = (c >> 4) & 1
        p["lpctl"] = (c >> 5) & 3
        p["sa"] = ((c & 0xF) << 16) | r16(b + 2)
        p["lsa"] = r16(b + 4)
        p["lea"] = r16(b + 6)
        v = r16(b + 8)
        p["ar"] = v & 31
        p["d1r"] = (v >> 6) & 31
        p["d2r"] = (v >> 11) & 31
        v = r16(b + 0xA)
        p["rr"] = v & 31
        p["dl"] = (v >> 5) & 31
        p["krs"] = (v >> 10) & 15
        v = r16(b + 0xC)
        p["tl"] = v & 255
        v = r16(b + 0x10)
        o = (v >> 11) & 15
        if o >= 8:
            o -= 16
        p["oct"] = o
        p["fns"] = v & 1023
        v = r16(b + 0x16)
        p["disdl"] = (v >> 13) & 7
        p["dipan"] = (v >> 8) & 31
        return p

    def rate_index(r, p):                               # 0 .. 63: the rate register * 2 plus the key rate scaling offset
        if r == 0:
            return 0
        base = 0
        if p["krs"] != 15:
            base = (p["oct"] + 8 + p["krs"]) // 2 if False else 0
        return max(0, min(63, 2 * r + base))

    for t in range(ticks + tail):
        # register writes of this tick
        while ei < len(ev) and ev[ei][0] <= t:
            _, _, off, val, size = ev[ei]
            ei += 1
            for k in range(size):
                regs[(off + k) & 0xFFF] = (val >> (8 * (size - 1 - k))) & 0xFF
            n, o = divmod(off, 0x20)
            if n < 32 and o <= 1 and (size >= 2 or o == 0):
                c = r16(n * 0x20)
                if c & 0x1000:                          # KYONEX: apply every slot's KYONB
                    for s in range(32):
                        sc = r16(s * 0x20)
                        kb = (sc >> 11) & 1
                        sl = slots[s]
                        if kb and sl.state in ("off", "release"):
                            sl.state = "attack"
                            sl.pos = 0.0
                            sl.rev = False
                            sl.amp = 0.0
                            sl.level_db = 0.0
                            sl.active = True
                        elif not kb and sl.state in ("attack", "decay1", "decay2"):
                            sl.state = "release"
                    regs[n * 0x20] &= 0xEF              # KYONEX self-clears
        base = t * TICK
        for si, sl in enumerate(slots):
            if not sl.active:
                continue
            p = slot_params(si)
            nsamp = TICK
            # envelope per sample, vectorised per tick
            amp = np.empty(nsamp, dtype=np.float32)
            for i in range(nsamp):
                if sl.state == "attack":
                    ar = AR_TIMES[rate_index(p["ar"], p)]
                    if ar <= 0.0:
                        sl.amp = 1.0
                    else:
                        sl.amp += 1000.0 / RATE / (ar if ar < 100000 else 1e9)
                    if sl.amp >= 1.0:
                        sl.amp = 1.0
                        sl.state = "decay1"
                    lvl = sl.amp
                elif sl.state == "decay1":
                    d = DR_TIMES[rate_index(p["d1r"], p)]
                    sl.level_db += 96.0 * 1000.0 / RATE / d if d < 100000 else 0.0
                    target = p["dl"] * 3.0 if p["dl"] < 31 else 96.0
                    if sl.level_db >= target:
                        sl.level_db = target
                        sl.state = "decay2"
                    lvl = _db(-sl.level_db)
                elif sl.state == "decay2":
                    d = DR_TIMES[rate_index(p["d2r"], p)]
                    sl.level_db += 96.0 * 1000.0 / RATE / d if d < 100000 else 0.0
                    lvl = _db(-min(sl.level_db, 96.0))
                    if sl.level_db >= 96.0:
                        sl.state = "off"
                        sl.active = False
                else:                                   # release
                    d = DR_TIMES[rate_index(p["rr"], p)]
                    sl.level_db += 96.0 * 1000.0 / RATE / d if d < 100000 else 0.0
                    if sl.state == "release" and sl.amp < 1.0 and sl.level_db == 0.0:
                        pass
                    lvl = _db(-min(sl.level_db, 96.0)) if sl.level_db > 0 or sl.amp >= 1.0 else sl.amp
                    if sl.level_db >= 96.0:
                        sl.state = "off"
                        sl.active = False
                amp[i] = lvl
                if not sl.active:
                    amp[i + 1:] = 0.0
                    break
            # samples
            step = (2.0 ** p["oct"]) * (1.0 + p["fns"] / 1024.0)
            idx = sl.pos + step * np.arange(nsamp, dtype=np.float64)
            lsa, lea = p["lsa"], p["lea"]
            if p["lpctl"] == 0:
                alive = idx < lea
                idx = np.minimum(idx, max(lea - 1, 0))
                if not alive.all():
                    first = int(np.argmin(alive)) if not alive.all() else nsamp
                    amp[first:] = 0.0
                    sl.active = False
                    sl.state = "off"
                sl.pos = float(idx[-1] + step) if sl.active else 0.0
            else:
                span = max(lea - lsa, 1)
                over = idx >= lea
                if over.any():
                    idx = np.where(over, lsa + np.mod(idx - lsa, span), idx)
                sl.pos = float(sl.pos + step * nsamp)
                if sl.pos >= lea:
                    sl.pos = lsa + (sl.pos - lsa) % span
            i0 = np.floor(idx).astype(np.int64)
            fr = (idx - i0).astype(np.float32)
            if p["pcm8"]:
                a = p["sa"] + i0
                s0 = mem[np.minimum(a, len(mem) - 1)].astype(np.int8).astype(np.float32) / 128.0
                s1 = mem[np.minimum(a + 1, len(mem) - 1)].astype(np.int8).astype(np.float32) / 128.0
            else:
                a = p["sa"] + 2 * i0
                hi0 = mem[np.minimum(a, len(mem) - 2)].astype(np.int16)
                lo0 = mem[np.minimum(a + 1, len(mem) - 1)].astype(np.int16)
                s0 = ((hi0 << 8) | lo0).astype(np.int16).astype(np.float32) / 32768.0
                hi1 = mem[np.minimum(a + 2, len(mem) - 2)].astype(np.int16)
                lo1 = mem[np.minimum(a + 3, len(mem) - 1)].astype(np.int16)
                s1 = ((hi1 << 8) | lo1).astype(np.int16).astype(np.float32) / 32768.0
            smp = s0 + (s1 - s0) * fr
            tl = _db(-p["tl"] * 0.375)
            dl = 0.0 if p["disdl"] == 0 else _db(-(7 - p["disdl"]) * 6.0)
            pan = p["dipan"]
            att = _db(-(pan & 15) * 3.0) if (pan & 15) != 15 else 0.0
            left = 1.0 if (pan & 0x10) == 0 else att     # bit 4 clear: attenuate the right side
            right = att if (pan & 0x10) == 0 else 1.0
            v = smp * amp * tl * dl
            out[base:base + nsamp, 0] += v * left
            out[base:base + nsamp, 1] += v * right
    return out[:n_total]


def to_int16(out, gain=1.0):
    return np.clip(out * gain * 32767.0, -32768, 32767).astype(np.int16)
