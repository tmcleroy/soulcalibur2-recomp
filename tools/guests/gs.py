# SPDX-License-Identifier: GPL-3.0-or-later
"""The PlayStation 2 Graphics Synthesizer's local memory, as far as reading the PS2 disc's textures needs: images
are uploaded in one pixel format (often 32-bit, already arranged for another) and read back in the texture's own.

Addresses and layouts (the GS manual; the tables as GSdx has them):
  memory: 4 MiB, in pages of 8 KiB, each 32 blocks of 256 bytes; a buffer pointer (BP) counts blocks, a buffer
  width (BW) counts 64 pixels.
  PSMCT32: a page is 64x32 pixels, a block 8x8;  PSMT8: 128x64, block 16x16;  PSMT4: 128x128, block 32x16.
Only reads what it is given; nothing about the game is in here.
"""

_BLOCK32 = [[0, 1, 4, 5, 16, 17, 20, 21],
            [2, 3, 6, 7, 18, 19, 22, 23],
            [8, 9, 12, 13, 24, 25, 28, 29],
            [10, 11, 14, 15, 26, 27, 30, 31]]
_BLOCK8 = _BLOCK32
_BLOCK4 = [[0, 2, 8, 10], [1, 3, 9, 11], [4, 6, 12, 14], [5, 7, 13, 15],
           [16, 18, 24, 26], [17, 19, 25, 27], [20, 22, 28, 30], [21, 23, 29, 31]]
_COLUMN32 = [[0, 1, 4, 5, 8, 9, 12, 13],
             [2, 3, 6, 7, 10, 11, 14, 15],
             [16, 17, 20, 21, 24, 25, 28, 29],
             [18, 19, 22, 23, 26, 27, 30, 31],
             [32, 33, 36, 37, 40, 41, 44, 45],
             [34, 35, 38, 39, 42, 43, 46, 47],
             [48, 49, 52, 53, 56, 57, 60, 61],
             [50, 51, 54, 55, 58, 59, 62, 63]]


def _column8():
    """Byte of a 16x16 PSMT8 block's pixel: four columns of 16x4, columns 2 and 3 as 0 and 1 a half block on."""
    rows = [[0, 4, 16, 20, 32, 36, 48, 52, 2, 6, 18, 22, 34, 38, 50, 54],
            [8, 12, 24, 28, 40, 44, 56, 60, 10, 14, 26, 30, 42, 46, 58, 62],
            [33, 37, 49, 53, 1, 5, 17, 21, 35, 39, 51, 55, 3, 7, 19, 23],
            [41, 45, 57, 61, 9, 13, 25, 29, 43, 47, 59, 63, 11, 15, 27, 31],
            [96, 100, 112, 116, 64, 68, 80, 84, 98, 102, 114, 118, 66, 70, 82, 86],
            [104, 108, 120, 124, 72, 76, 88, 92, 106, 110, 122, 126, 74, 78, 90, 94],
            [65, 69, 81, 85, 97, 101, 113, 117, 67, 71, 83, 87, 99, 103, 115, 119],
            [73, 77, 89, 93, 105, 109, 121, 125, 75, 79, 91, 95, 107, 111, 123, 127]]
    return rows + [[b + 128 for b in r] for r in rows]


def _column4():
    """Nibble of a 32x16 PSMT4 block's pixel: column 0's rows; column 1 is column 0 with the halves of each row
    swapped, 128 on; columns 2 and 3 are 0 and 1, 256 on."""
    def pair(starts, steps):
        row = [s + k for s in starts for k in steps]
        return [row, [n + 16 for n in row]]
    c0 = pair((0, 2, 4, 6), (0, 8, 32, 40, 64, 72, 96, 104)) + pair((1, 3, 5, 7), (64, 72, 96, 104, 0, 8, 32, 40))
    c1 = [[(n ^ 64) + 128 for n in row] for row in c0]
    rows = c0 + c1
    return rows + [[n + 256 for n in r] for r in rows]


_COLUMN8 = _column8()
_COLUMN4 = _column4()


class Memory:
    def __init__(self):
        self.data = bytearray(4 << 20)

    # addresses
    @staticmethod
    def word32(bp, bw, x, y):
        page = (y >> 5) * bw + (x >> 6)
        block = bp + page * 32 + _BLOCK32[(y >> 3) & 3][(x >> 3) & 7]
        return (block * 64 + _COLUMN32[y & 7][x & 7]) & 0xfffff

    @staticmethod
    def byte8(bp, bw, x, y):
        page = (y >> 6) * (bw >> 1) + (x >> 7)
        block = bp + page * 32 + _BLOCK8[(y >> 4) & 3][(x >> 4) & 7]
        return (block * 256 + _COLUMN8[y & 15][x & 15]) & 0x3fffff

    @staticmethod
    def nibble4(bp, bw, x, y):
        page = (y >> 7) * (bw >> 1) + (x >> 7)
        block = bp + page * 32 + _BLOCK4[(y >> 4) & 7][(x >> 5) & 3]
        return (block * 512 + _COLUMN4[y & 15][x & 31]) & 0x7fffff

    def upload32(self, bp, bw, x0, y0, w, h, pixels):
        """A host-to-GS transfer in PSMCT32: pixels row by row."""
        i = 0
        d = self.data
        for y in range(y0, y0 + h):
            for x in range(x0, x0 + w):
                a = self.word32(bp, bw, x, y) * 4
                d[a:a + 4] = pixels[i:i + 4]
                i += 4

    def upload(self, psm, bp, bw, x0, y0, w, h, pixels):
        size = w * h * 4 if psm == 0 else w * h if psm == 0x13 else (w * h + 1) // 2
        pixels = bytes(pixels[:size]).ljust(size, b"\0")   # (a short transfer leaves the rest cleared)
        if psm == 0:
            return self.upload32(bp, bw, x0, y0, w, h, pixels)
        d = self.data
        i = 0
        if psm == 0x13:
            for y in range(y0, y0 + h):
                for x in range(x0, x0 + w):
                    d[self.byte8(bp, bw, x, y)] = pixels[i]
                    i += 1
            return
        if psm == 0x14:
            for y in range(y0, y0 + h):
                for x in range(x0, x0 + w):
                    v = (pixels[i >> 1] >> (4 * (i & 1))) & 15
                    n = self.nibble4(bp, bw, x, y)
                    d[n >> 1] = (d[n >> 1] & (0xf0 >> (4 * (n & 1)))) | (v << (4 * (n & 1)))
                    i += 1
            return
        raise ValueError("upload format %#x" % psm)

    def read32(self, bp, bw, w, h):
        out = bytearray()
        for y in range(h):
            for x in range(w):
                a = self.word32(bp, bw, x, y) * 4
                out += self.data[a:a + 4]
        return bytes(out)

    def read8(self, bp, bw, w, h):
        return bytes(self.data[self.byte8(bp, bw, x, y)] for y in range(h) for x in range(w))

    def read4(self, bp, bw, w, h):
        out = bytearray()
        for y in range(h):
            for x in range(w):
                n = self.nibble4(bp, bw, x, y)
                out.append((self.data[n >> 1] >> (4 * (n & 1))) & 15)
        return bytes(out)

    def clut(self, cbp, psm, csa=0):
        """The palette (RGBA bytes, PS2 alpha 0..0x80) for an 8- or 4-bit texture, CSM1 PSMCT32."""
        if psm == 0x13:
            raw = self.read32(cbp, 1, 16, 16)
            out = bytearray()
            for i in range(256):
                p = (i & 0xe7) | ((i & 0x08) << 1) | ((i & 0x10) >> 1)
                out += raw[4 * p:4 * p + 4]
            return bytes(out)
        raw = self.read32(cbp, 1, 8, 2 * (csa + 1))
        return raw[64 * csa:64 * csa + 64]
