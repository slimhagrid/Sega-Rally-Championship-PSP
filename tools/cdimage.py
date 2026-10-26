"""Read a CD image given as a .cue + .bin files (one .bin per track, "MODE1/2352" data track + AUDIO tracks).

Only the standard library is needed.

    disc = Disc("Snatcher (USA).cue")
    disc.system_area()              # sectors 0-15 of the data track (SEGA header + IP + SP)
    disc.iso_files()                # [IsoFile(path, lba, size, is_dir)], ISO9660 of the data track
    disc.read_file(f)               # bytes of one file
    disc.audio_pcm(2)               # raw 16-bit stereo 44.1 kHz little-endian PCM of audio track 2 (from INDEX 01)
"""
import os
import re
import struct
import wave
from dataclasses import dataclass

RAW_SECTOR = 2352
USER_SECTOR = 2048
CD_BYTES_PER_SECOND = 176400  # 44100 Hz * 2 channels * 2 bytes


@dataclass
class Track:
    number: int
    mode: str          # "MODE1/2352", "MODE1/2048", "AUDIO", ...
    path: str          # the .bin file holding it
    index00: int       # sector (within the file) of INDEX 00, or None
    index01: int       # sector (within the file) where the track proper starts
    end: int = None    # sector (within the file) where the track ends (exclusive); None = end of the file
    pregap_virtual: int = 0   # cue PREGAP: silent sectors that exist on the disc but are not stored in the file

    @property
    def is_audio(self):
        return self.mode == "AUDIO"

    @property
    def sector_size(self):
        return USER_SECTOR if self.mode.endswith("/2048") else RAW_SECTOR

    @property
    def user_offset(self):
        """Offset of the 2048 user bytes inside a sector (16 = sync + header of a raw MODE1 sector)."""
        return 16 if self.mode == "MODE1/2352" else 0

    @property
    def sectors(self):
        """Sector (within the file) where the track ends, so `sectors - index01` is the length of the track proper.
        With one .bin per track this is the size of the file; in a single-.bin image it is where the next track begins."""
        if self.end is not None:
            return self.end
        return os.path.getsize(self.path) // self.sector_size

    @property
    def pregap_sectors(self):
        return self.index01 - (self.index00 or 0) + self.pregap_virtual


@dataclass
class IsoFile:
    path: str
    lba: int
    size: int
    is_dir: bool


def _msf(text):
    m, s, f = (int(x) for x in text.split(":"))
    return (m * 60 + s) * 75 + f


def parse_cue(cue_path):
    base = os.path.dirname(os.path.abspath(cue_path))
    tracks, cur, cur_file = [], None, None
    with open(cue_path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            m = re.match(r'FILE\s+"(.+)"\s+BINARY', line, re.I)
            if m:
                cur_file = os.path.join(base, m.group(1))
                continue
            m = re.match(r"TRACK\s+(\d+)\s+(\S+)", line, re.I)
            if m:
                cur = Track(int(m.group(1)), m.group(2).upper(), cur_file, None, 0)
                tracks.append(cur)
                continue
            m = re.match(r"PREGAP\s+(\d+:\d+:\d+)", line, re.I)
            if m and cur is not None:
                cur.pregap_virtual = _msf(m.group(1))
                continue
            m = re.match(r"INDEX\s+(\d+)\s+(\d+:\d+:\d+)", line, re.I)
            if m and cur is not None:
                if m.group(1) == "00":
                    cur.index00 = _msf(m.group(2))
                elif m.group(1) == "01":
                    cur.index01 = _msf(m.group(2))
    # single-.bin images: a track ends where the next track in the same file begins (its INDEX 00 if it has one)
    for a, b in zip(tracks, tracks[1:]):
        if a.path == b.path:
            a.end = b.index00 if b.index00 is not None else b.index01
    return tracks


class Disc:
    def __init__(self, cue_path):
        self.cue_path = cue_path
        self.tracks = parse_cue(cue_path)
        data = [t for t in self.tracks if not t.is_audio]
        if not data:
            raise ValueError("no data track in " + cue_path)
        self.data_track = data[0]
        self._fh = open(self.data_track.path, "rb")

    def close(self):
        self._fh.close()

    # --- data track ---------------------------------------------------------------------------
    def read_sector(self, lba):
        """2048 user bytes of sector `lba` of the data track."""
        t = self.data_track
        self._fh.seek(lba * t.sector_size + t.user_offset)
        return self._fh.read(USER_SECTOR)

    def read_sectors(self, lba, count):
        return b"".join(self.read_sector(lba + i) for i in range(count))

    def system_area(self):
        """Sectors 0-15: the Saturn header, security code and IP (initial program)."""
        return self.read_sectors(0, 16)

    def volume_label(self):
        pvd = self.read_sector(16)
        if pvd[1:6] != b"CD001":
            raise ValueError("no ISO9660 primary volume descriptor at sector 16")
        return pvd[40:72].decode("ascii", "replace").strip()

    def iso_files(self):
        pvd = self.read_sector(16)
        if pvd[1:6] != b"CD001":
            raise ValueError("no ISO9660 primary volume descriptor at sector 16")
        root = pvd[156:190]
        out = []
        self._walk(struct.unpack("<I", root[2:6])[0], struct.unpack("<I", root[10:14])[0], "", out)
        return out

    def _walk(self, lba, length, prefix, out):
        data = self.read_sectors(lba, (length + USER_SECTOR - 1) // USER_SECTOR)
        p = 0
        while p < len(data):
            n = data[p]
            if n == 0:  # records never straddle a sector: skip the padding
                p = (p // USER_SECTOR + 1) * USER_SECTOR
                continue
            rec = data[p:p + n]
            p += n
            name_len = rec[32]
            raw = rec[33:33 + name_len]
            if raw in (b"\x00", b"\x01"):  # "." and ".."
                continue
            name = raw.decode("latin-1").split(";")[0]
            e_lba = struct.unpack("<I", rec[2:6])[0]
            e_size = struct.unpack("<I", rec[10:14])[0]
            is_dir = bool(rec[25] & 2)
            out.append(IsoFile(prefix + name, e_lba, e_size, is_dir))
            if is_dir:
                self._walk(e_lba, e_size, prefix + name + "/", out)

    def read_file(self, f, chunk_sectors=2048):
        """Yield-free read of a whole file (the biggest on a Sega CD disc is ~85 MB)."""
        parts, left, lba = [], f.size, f.lba
        while left > 0:
            n = min(chunk_sectors, (left + USER_SECTOR - 1) // USER_SECTOR)
            blob = self.read_sectors(lba, n)
            parts.append(blob[:left])
            left -= len(blob)
            lba += n
        return b"".join(parts)

    # --- audio tracks -------------------------------------------------------------------------
    def audio_tracks(self):
        return [t for t in self.tracks if t.is_audio]

    def audio_pcm(self, number, include_pregap=False):
        t = next(x for x in self.tracks if x.number == number and x.is_audio)
        start = (t.index00 or 0) if include_pregap else t.index01
        with open(t.path, "rb") as f:
            f.seek(start * RAW_SECTOR)
            return f.read((t.sectors - start) * RAW_SECTOR)

    def write_wav(self, number, out_path, include_pregap=False):
        pcm = self.audio_pcm(number, include_pregap)
        with wave.open(out_path, "wb") as w:
            w.setnchannels(2)
            w.setsampwidth(2)
            w.setframerate(44100)
            w.writeframes(pcm)
        return len(pcm)
