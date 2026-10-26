"""M9: the CD audio of the disc (extracted/audio/trackNN.wav, tools/extract_disc.py) as MP3 for the PSP's hardware decoder (src/cdda.c):  python tools/conv_cdda.py
-> extracted/psp/cd/trackNN.mp3 (44.1 kHz stereo, 128 kbit/s: about 1 / 11 of the size of the WAV files).  Needs `pip install lameenc` (LAME).  Does not touch what is already converted
(delete the file to convert a track again).

The tracks are made for the PSP's little speaker: a low shelf (CDDA_BASS_DB, default -6 dB below CDDA_BASS_HZ, default 180 Hz: the drums and the bass distort and rattle it) and a lower level (CDDA_GAIN,
default 0.75).  Several tracks peak at full scale and an MP3 overshoots its input at the peaks, so the decoder's 16 bit output would clip: the peak is kept under 29000 after the filter.
Set CDDA_BASS_DB=0 CDDA_GAIN=1 for the disc's own sound (then delete the mp3 files first)."""
import os
import sys
import wave

import lameenc
import numpy as np
from scipy.signal import lfilter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "extracted", "audio")
DST = os.path.join(ROOT, "extracted", "psp", "cd")
BITRATE = int(os.environ.get("CDDA_KBPS", "128"))
GAIN = float(os.environ.get("CDDA_GAIN", "0.75"))
BASS_DB = float(os.environ.get("CDDA_BASS_DB", "-6"))
BASS_HZ = float(os.environ.get("CDDA_BASS_HZ", "180"))


def low_shelf(x, fs, f0, gain_db):             # RBJ audio EQ cookbook, shelf slope 1
    A = 10 ** (gain_db / 40.0)
    w0 = 2 * np.pi * f0 / fs
    cw, sw = np.cos(w0), np.sin(w0)
    alpha = sw / 2 * np.sqrt(2.0)
    t = 2 * np.sqrt(A) * alpha
    b0 = A * ((A + 1) - (A - 1) * cw + t)
    b1 = 2 * A * ((A - 1) - (A + 1) * cw)
    b2 = A * ((A + 1) - (A - 1) * cw - t)
    a0 = (A + 1) + (A - 1) * cw + t
    a1 = -2 * ((A - 1) + (A + 1) * cw)
    a2 = (A + 1) + (A - 1) * cw - t
    return lfilter([b0 / a0, b1 / a0, b2 / a0], [1.0, a1 / a0, a2 / a0], x, axis=0)

os.makedirs(DST, exist_ok=True)
total_in = total_out = 0
for name in sorted(os.listdir(SRC)):
    if not (name.startswith("track") and name.endswith(".wav")):
        continue
    out = os.path.join(DST, name[:-4] + ".mp3")
    if os.path.exists(out):
        continue
    with wave.open(os.path.join(SRC, name), "rb") as w:
        assert w.getnchannels() == 2 and w.getsampwidth() == 2 and w.getframerate() == 44100, name
        pcm = w.readframes(w.getnframes())
    x = np.frombuffer(pcm, dtype="<i2").astype(np.float64).reshape(-1, 2)
    if BASS_DB != 0:
        x = low_shelf(x, 44100, BASS_HZ, BASS_DB)
    x = x * GAIN
    pk = float(np.abs(x).max())
    if pk > 29000.0:
        x = x * (29000.0 / pk)
    pcm = np.rint(x).astype("<i2").tobytes()
    enc = lameenc.Encoder()
    enc.set_bit_rate(BITRATE)
    enc.set_in_sample_rate(44100)
    enc.set_channels(2)
    enc.set_quality(2)
    data = enc.encode(pcm) + enc.flush()
    with open(out, "wb") as f:
        f.write(data)
    total_in += len(pcm)
    total_out += len(data)
    print("%s: %.1f s, %.1f MB -> %.1f MB" % (name, len(pcm) / 176400.0, len(pcm) / 1e6, len(data) / 1e6))
    sys.stdout.flush()
print("done: %.0f MB -> %.0f MB" % (total_in / 1e6, total_out / 1e6))
