"""Readers for the game's file formats (M3).

Scatter-load files (`*.DAT`, `*.MAD`): a 2048-byte header sector of 16-byte entries `<dest:4> <size:4> <type:2> <arg:2> <pad:4>`
(big endian, ended by a zero entry), then each block starts on its own 2048-byte boundary (the loader is `FUN_0600F354`).
`type 2` blocks are BPE-compressed (see `bpe_expand`); the other types are plain.

    from datfile import Scatter, bpe_expand
    s = Scatter("extracted/disc/files/DESERT.MAD")
    for b in s.blocks: print(hex(b.dest), b.size, b.type)
    data = s.data(b)

BPE (byte pair encoding, the classic Gage scheme as implemented by `extract::_bpe_expand` @0x06015870): input = `u32 length`, then blocks of
  [pair table][u16 packed size][packed bytes];  pair table = runs `count` (>127: skip count-127 symbols) then for each symbol `left` and, if left != symbol, `right`;
a packed byte c expands to c if left[c] == c, else expand(left[c]) then expand(right[c]).  The game writes the output as 16-bit words; the third argument it takes is added
to every output word (it is 0 for the .TEX streams).
"""
import os
import struct
from dataclasses import dataclass


def align(x, a=2048):
    return (x + a - 1) & ~(a - 1)


@dataclass
class Block:
    index: int
    dest: int
    size: int
    type: int
    arg: int
    offset: int       # file offset of the data


class Scatter:
    def __init__(self, path):
        self.path = path
        self.raw = open(path, "rb").read()
        self.blocks = []
        off = 0x800
        for i in range(0, 0x800, 16):
            dest, size, typ, arg, _ = struct.unpack(">IIHHI", self.raw[i:i + 16])
            if dest == 0 and size == 0:
                break
            self.blocks.append(Block(len(self.blocks), dest, size, typ, arg, off))
            off += align(size)
        self.end = off

    def data(self, b):
        return self.raw[b.offset:b.offset + b.size]

    def expanded(self, b):
        """The block as the loader leaves it in memory: type 2 = BPE-expanded, `arg` added to every 16-bit big-endian output word (VDP2 name tables)."""
        d = self.data(b)
        if b.type != 2:
            return d
        out = bpe_expand(d)
        if b.arg:
            n = len(out) // 2
            words = struct.unpack(">%dH" % n, out[:n * 2])
            out = struct.pack(">%dH" % n, *[(w + b.arg) & 0xFFFF for w in words]) + out[n * 2:]
        return out

    def check(self):
        """True when header + sector-rounded blocks account for the whole file (up to the final sector)."""
        return align(len(self.raw)) == align(self.end) or len(self.raw) == self.end


def bpe_expand(buf, pos=0):
    """Expand one BPE stream (`u32 length` + blocks) starting at buf[pos]; returns the expanded bytes."""
    n = struct.unpack(">I", buf[pos:pos + 4])[0]
    p = pos + 4
    end = p + n
    out = bytearray()
    while p < end:
        left = list(range(256))
        right = [0] * 256
        c = 0
        count = buf[p]
        p += 1
        while True:
            if count > 127:
                c += count - 127
                count = 0
            if c == 256:
                break
            for _ in range(count + 1):
                left[c] = buf[p]
                p += 1
                if c != left[c]:
                    right[c] = buf[p]
                    p += 1
                c += 1
            if c == 256:
                break
            count = buf[p]
            p += 1
        size = (buf[p] << 8) | buf[p + 1]
        p += 2
        stack = []
        while True:
            if stack:
                ch = stack.pop()
            elif size:
                size -= 1
                ch = buf[p]
                p += 1
            else:
                break
            if left[ch] == ch:
                out.append(ch)
            else:
                stack.append(right[ch])
                stack.append(left[ch])
    return bytes(out)
