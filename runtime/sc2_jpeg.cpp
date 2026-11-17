/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Baseline JPEG (sc2_jpeg.h, from satrecomp's port/jpeg_write.cpp): YCbCr in 8x8 blocks, each through the
   discrete cosine transform, quantized, in zigzag order, and Huffman coded with the standard's tables. */
#include "sc2_jpeg.h"

#include <algorithm>
#include <cmath>

namespace sc2 {

namespace {

// The order the 64 coefficients of a block are written in (by their place in the block, row after row)
constexpr uint8_t kZigzag[64] = {0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,  12, 19, 26, 33, 40, 48,
                                 41, 34, 27, 20, 13, 6,  7,  14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23,
                                 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};

// Annex K's quantization tables (in the block's order, row after row)
constexpr uint8_t kLumaQ[64] = {16, 11, 10, 16, 24,  40,  51,  61,  12, 12, 14, 19, 26,  58,  60,  55,
                                14, 13, 16, 24, 40,  57,  69,  56,  14, 17, 22, 29, 51,  87,  80,  62,
                                18, 22, 37, 56, 68,  109, 103, 77,  24, 35, 55, 64, 81,  104, 113, 92,
                                49, 64, 78, 87, 103, 121, 120, 101, 72, 92, 95, 98, 112, 100, 103, 99};
constexpr uint8_t kChromaQ[64] = {17, 18, 24, 47, 99, 99, 99, 99, 18, 21, 26, 66, 99, 99, 99, 99, 24, 26, 56, 99, 99, 99,
                                  99, 99, 47, 66, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
                                  99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99};

// Annex K's Huffman tables: the number of codes of each length 1-16, then the values in that order
constexpr uint8_t kDcLumaBits[16] = {0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0};
constexpr uint8_t kDcLumaVals[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
constexpr uint8_t kDcChromaBits[16] = {0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0};
constexpr uint8_t kDcChromaVals[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
constexpr uint8_t kAcLumaBits[16] = {0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7D};
constexpr uint8_t kAcLumaVals[162] = {
    0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07, 0x22, 0x71, 0x14,
    0x32, 0x81, 0x91, 0xA1, 0x08, 0x23, 0x42, 0xB1, 0xC1, 0x15, 0x52, 0xD1, 0xF0, 0x24, 0x33, 0x62, 0x72, 0x82, 0x09,
    0x0A, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A,
    0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x63, 0x64, 0x65,
    0x66, 0x67, 0x68, 0x69, 0x6A, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88,
    0x89, 0x8A, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9,
    0xAA, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA,
    0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA,
    0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA};
constexpr uint8_t kAcChromaBits[16] = {0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77};
constexpr uint8_t kAcChromaVals[162] = {
    0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71, 0x13, 0x22, 0x32,
    0x81, 0x08, 0x14, 0x42, 0x91, 0xA1, 0xB1, 0xC1, 0x09, 0x23, 0x33, 0x52, 0xF0, 0x15, 0x62, 0x72, 0xD1, 0x0A, 0x16,
    0x24, 0x34, 0xE1, 0x25, 0xF1, 0x17, 0x18, 0x19, 0x1A, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x35, 0x36, 0x37, 0x38, 0x39,
    0x3A, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x63, 0x64,
    0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x82, 0x83, 0x84, 0x85, 0x86,
    0x87, 0x88, 0x89, 0x8A, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7,
    0xA8, 0xA9, 0xAA, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8,
    0xC9, 0xCA, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9,
    0xEA, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA};

// A Huffman table as codes: for each value, its code and the code's length
struct Huffman {
    uint16_t code[256] = {};
    uint8_t length[256] = {};
    Huffman(const uint8_t *bits, const uint8_t *vals) {
        uint16_t next = 0;
        int k = 0;
        for (int len = 1; len <= 16; len++) {
            for (int i = 0; i < bits[len - 1]; i++) {
                code[vals[k]] = next++;
                length[vals[k]] = static_cast<uint8_t>(len);
                k++;
            }
            next = static_cast<uint16_t>(next << 1);
        }
    }
};

class Writer {
public:
    std::vector<uint8_t> out;
    void Byte(uint8_t b) {
        out.push_back(b);
    }
    void Word(uint16_t w) {
        Byte(static_cast<uint8_t>(w >> 8));
        Byte(static_cast<uint8_t>(w));
    }
    // Bits of the entropy-coded data, most significant first; a 0xFF byte is followed by a 0x00
    void Bits(uint32_t value, int count) {
        for (int i = count - 1; i >= 0; i--) {
            m_byte = static_cast<uint8_t>((m_byte << 1) | ((value >> i) & 1));
            if (++m_count == 8) {
                Byte(m_byte);
                if (m_byte == 0xFF) {
                    Byte(0x00);
                }
                m_byte = 0;
                m_count = 0;
            }
        }
    }
    void Flush() { // (the last byte filled with ones)
        while (m_count != 0) {
            Bits(1, 1);
        }
    }

private:
    uint8_t m_byte = 0;
    int m_count = 0;
};

// How many bits a coefficient's value takes, and those bits (negative values as one less, in that many bits)
int Category(int v) {
    int a = v < 0 ? -v : v, n = 0;
    while (a) {
        n++;
        a >>= 1;
    }
    return n;
}
uint32_t ValueBits(int v, int n) {
    return static_cast<uint32_t>(v < 0 ? v + (1 << n) - 1 : v);
}

void ScaledTable(const uint8_t *base, int quality, uint8_t *out) {
    quality = std::clamp(quality, 1, 100);
    const int scale = quality < 50 ? 5000 / quality : 200 - quality * 2;
    for (int i = 0; i < 64; i++) {
        out[i] = static_cast<uint8_t>(std::clamp((base[i] * scale + 50) / 100, 1, 255));
    }
}

void WriteHuffmanTable(Writer &w, int classAndId, const uint8_t *bits, const uint8_t *vals) {
    int count = 0;
    for (int i = 0; i < 16; i++) {
        count += bits[i];
    }
    w.Word(0xFFC4);
    w.Word(static_cast<uint16_t>(2 + 1 + 16 + count));
    w.Byte(static_cast<uint8_t>(classAndId));
    for (int i = 0; i < 16; i++) {
        w.Byte(bits[i]);
    }
    for (int i = 0; i < count; i++) {
        w.Byte(vals[i]);
    }
}

} // namespace

std::vector<uint8_t> EncodeJpeg(const uint8_t *rgb, int width, int height, int quality) {
    uint8_t lumaQ[64], chromaQ[64];
    ScaledTable(kLumaQ, quality, lumaQ);
    ScaledTable(kChromaQ, quality, chromaQ);
    static const Huffman dcLuma(kDcLumaBits, kDcLumaVals), dcChroma(kDcChromaBits, kDcChromaVals);
    static const Huffman acLuma(kAcLumaBits, kAcLumaVals), acChroma(kAcChromaBits, kAcChromaVals);
    // (the transform's cosines: c[x][u] = C(u)/2 cos((2x+1)u pi/16))
    static double cosines[8][8];
    static bool made = false;
    if (!made) {
        for (int x = 0; x < 8; x++) {
            for (int u = 0; u < 8; u++) {
                cosines[x][u] = (u == 0 ? std::sqrt(0.5) : 1.0) * 0.5 * std::cos((2 * x + 1) * u * M_PI / 16);
            }
        }
        made = true;
    }

    Writer w;
    w.Word(0xFFD8); // start of image
    // JFIF: version 1.1, no density
    w.Word(0xFFE0);
    w.Word(16);
    for (char c : {'J', 'F', 'I', 'F', '\0'}) {
        w.Byte(static_cast<uint8_t>(c));
    }
    w.Word(0x0101);
    w.Byte(0);
    w.Word(1);
    w.Word(1);
    w.Byte(0);
    w.Byte(0);
    // the quantization tables, in zigzag order
    for (int t = 0; t < 2; t++) {
        const uint8_t *q = t ? chromaQ : lumaQ;
        w.Word(0xFFDB);
        w.Word(67);
        w.Byte(static_cast<uint8_t>(t));
        for (int i = 0; i < 64; i++) {
            w.Byte(q[kZigzag[i]]);
        }
    }
    // the frame: 8-bit, three components, none subsampled
    w.Word(0xFFC0);
    w.Word(17);
    w.Byte(8);
    w.Word(static_cast<uint16_t>(height));
    w.Word(static_cast<uint16_t>(width));
    w.Byte(3);
    for (int c = 1; c <= 3; c++) {
        w.Byte(static_cast<uint8_t>(c));
        w.Byte(0x11);
        w.Byte(c == 1 ? 0 : 1);
    }
    WriteHuffmanTable(w, 0x00, kDcLumaBits, kDcLumaVals);
    WriteHuffmanTable(w, 0x10, kAcLumaBits, kAcLumaVals);
    WriteHuffmanTable(w, 0x01, kDcChromaBits, kDcChromaVals);
    WriteHuffmanTable(w, 0x11, kAcChromaBits, kAcChromaVals);
    // the scan: all three components, interleaved block by block
    w.Word(0xFFDA);
    w.Word(12);
    w.Byte(3);
    for (int c = 1; c <= 3; c++) {
        w.Byte(static_cast<uint8_t>(c));
        w.Byte(c == 1 ? 0x00 : 0x11);
    }
    w.Byte(0);
    w.Byte(63);
    w.Byte(0);

    int lastDc[3] = {0, 0, 0};
    for (int by = 0; by < height; by += 8) {
        for (int bx = 0; bx < width; bx += 8) {
            double block[3][64];
            for (int y = 0; y < 8; y++) {
                for (int x = 0; x < 8; x++) {
                    // (past the picture's edge: its last row or column again)
                    const int px = std::min(bx + x, width - 1), py = std::min(by + y, height - 1);
                    const uint8_t *p = rgb + (static_cast<size_t>(py) * width + px) * 3;
                    const double r = p[0], g = p[1], b = p[2];
                    block[0][y * 8 + x] = 0.299 * r + 0.587 * g + 0.114 * b - 128;
                    block[1][y * 8 + x] = -0.168736 * r - 0.331264 * g + 0.5 * b;
                    block[2][y * 8 + x] = 0.5 * r - 0.418688 * g - 0.081312 * b;
                }
            }
            for (int c = 0; c < 3; c++) {
                const uint8_t *q = c ? chromaQ : lumaQ;
                const Huffman &dc = c ? dcChroma : dcLuma, &ac = c ? acChroma : acLuma;
                int coef[64];
                for (int v = 0; v < 8; v++) {
                    for (int u = 0; u < 8; u++) {
                        double s = 0;
                        for (int y = 0; y < 8; y++) {
                            for (int x = 0; x < 8; x++) {
                                s += block[c][y * 8 + x] * cosines[x][u] * cosines[y][v];
                            }
                        }
                        coef[v * 8 + u] = static_cast<int>(std::lround(s / q[v * 8 + u]));
                    }
                }
                // the DC coefficient as its difference from the last block's of the component
                const int diff = coef[0] - lastDc[c];
                lastDc[c] = coef[0];
                const int dn = Category(diff);
                w.Bits(dc.code[dn], dc.length[dn]);
                w.Bits(ValueBits(diff, dn), dn);
                // the AC coefficients: runs of zeros and the value after each
                int zeros = 0;
                for (int i = 1; i < 64; i++) {
                    const int v = coef[kZigzag[i]];
                    if (v == 0) {
                        zeros++;
                        continue;
                    }
                    while (zeros > 15) { // (sixteen zeros)
                        w.Bits(ac.code[0xF0], ac.length[0xF0]);
                        zeros -= 16;
                    }
                    const int n = Category(v);
                    const int symbol = (zeros << 4) | n;
                    w.Bits(ac.code[symbol], ac.length[symbol]);
                    w.Bits(ValueBits(v, n), n);
                    zeros = 0;
                }
                if (zeros) { // (the rest are zeros)
                    w.Bits(ac.code[0x00], ac.length[0x00]);
                }
            }
        }
    }
    w.Flush();
    w.Word(0xFFD9); // end of image
    return std::move(w.out);
}

} // namespace sc2
