/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The port of tools/guests/gs.py. */
#include "gs.h"

#include <algorithm>
#include <array>

namespace guest {

namespace {

const uint8_t BLOCK32[4][8] = {{0, 1, 4, 5, 16, 17, 20, 21},
                               {2, 3, 6, 7, 18, 19, 22, 23},
                               {8, 9, 12, 13, 24, 25, 28, 29},
                               {10, 11, 14, 15, 26, 27, 30, 31}};
const uint8_t BLOCK4[8][4] = {{0, 2, 8, 10},   {1, 3, 9, 11},   {4, 6, 12, 14},  {5, 7, 13, 15},
                              {16, 18, 24, 26}, {17, 19, 25, 27}, {20, 22, 28, 30}, {21, 23, 29, 31}};
const uint8_t COLUMN32[8][8] = {{0, 1, 4, 5, 8, 9, 12, 13},         {2, 3, 6, 7, 10, 11, 14, 15},
                                {16, 17, 20, 21, 24, 25, 28, 29},   {18, 19, 22, 23, 26, 27, 30, 31},
                                {32, 33, 36, 37, 40, 41, 44, 45},   {34, 35, 38, 39, 42, 43, 46, 47},
                                {48, 49, 52, 53, 56, 57, 60, 61},   {50, 51, 54, 55, 58, 59, 62, 63}};

struct Tables {
    uint8_t column8[16][16];
    uint16_t column4[16][32];
    Tables()
    {
        static const uint8_t rows8[8][16] = {
            {0, 4, 16, 20, 32, 36, 48, 52, 2, 6, 18, 22, 34, 38, 50, 54},
            {8, 12, 24, 28, 40, 44, 56, 60, 10, 14, 26, 30, 42, 46, 58, 62},
            {33, 37, 49, 53, 1, 5, 17, 21, 35, 39, 51, 55, 3, 7, 19, 23},
            {41, 45, 57, 61, 9, 13, 25, 29, 43, 47, 59, 63, 11, 15, 27, 31},
            {96, 100, 112, 116, 64, 68, 80, 84, 98, 102, 114, 118, 66, 70, 82, 86},
            {104, 108, 120, 124, 72, 76, 88, 92, 106, 110, 122, 126, 74, 78, 90, 94},
            {65, 69, 81, 85, 97, 101, 113, 117, 67, 71, 83, 87, 99, 103, 115, 119},
            {73, 77, 89, 93, 105, 109, 121, 125, 75, 79, 91, 95, 107, 111, 123, 127}};
        for (int r = 0; r < 8; ++r)
            for (int c = 0; c < 16; ++c) {
                column8[r][c] = rows8[r][c];
                column8[r + 8][c] = uint8_t(rows8[r][c] + 128);
            }
        /* column 0's four rows (two pairs), column 1 the same with each row's halves swapped, 128 on;
           columns 2 and 3 are 0 and 1, 256 on */
        uint16_t c0[4][32];
        const int steps_a[8] = {0, 8, 32, 40, 64, 72, 96, 104}, steps_b[8] = {64, 72, 96, 104, 0, 8, 32, 40};
        const int starts_a[4] = {0, 2, 4, 6}, starts_b[4] = {1, 3, 5, 7};
        for (int s = 0; s < 4; ++s)
            for (int k = 0; k < 8; ++k) {
                c0[0][8 * s + k] = uint16_t(starts_a[s] + steps_a[k]);
                c0[1][8 * s + k] = uint16_t(starts_a[s] + steps_a[k] + 16);
                c0[2][8 * s + k] = uint16_t(starts_b[s] + steps_b[k]);
                c0[3][8 * s + k] = uint16_t(starts_b[s] + steps_b[k] + 16);
            }
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 32; ++c) {
                column4[r][c] = c0[r][c];
                column4[r + 4][c] = uint16_t((c0[r][c] ^ 64) + 128);
            }
        for (int r = 0; r < 8; ++r)
            for (int c = 0; c < 32; ++c) column4[r + 8][c] = uint16_t(column4[r][c] + 256);
    }
};

const Tables &tables()
{
    static const Tables t;
    return t;
}

uint32_t word32(uint32_t bp, uint32_t bw, uint32_t x, uint32_t y)
{
    const uint32_t page = (y >> 5) * bw + (x >> 6);
    const uint32_t block = bp + page * 32 + BLOCK32[(y >> 3) & 3][(x >> 3) & 7];
    return (block * 64 + COLUMN32[y & 7][x & 7]) & 0xfffff;
}

uint32_t byte8(uint32_t bp, uint32_t bw, uint32_t x, uint32_t y)
{
    const uint32_t page = (y >> 6) * (bw >> 1) + (x >> 7);
    const uint32_t block = bp + page * 32 + BLOCK32[(y >> 4) & 3][(x >> 4) & 7];
    return (block * 256 + tables().column8[y & 15][x & 15]) & 0x3fffff;
}

uint32_t nibble4(uint32_t bp, uint32_t bw, uint32_t x, uint32_t y)
{
    const uint32_t page = (y >> 7) * (bw >> 1) + (x >> 7);
    const uint32_t block = bp + page * 32 + BLOCK4[(y >> 4) & 7][(x >> 5) & 3];
    return (block * 512 + tables().column4[y & 15][x & 31]) & 0x7fffff;
}

} // namespace

void GsMemory::upload(uint32_t psm, uint32_t bp, uint32_t bw, uint32_t x0, uint32_t y0, uint32_t w, uint32_t h,
                      const uint8_t *pixels, size_t size)
{
    const size_t need = psm == 0 ? size_t(w) * h * 4 : psm == 0x13 ? size_t(w) * h : (size_t(w) * h + 1) / 2;
    Bytes p(need, 0);
    std::copy(pixels, pixels + std::min(size, need), p.begin());
    size_t i = 0;
    if (psm == 0) {
        for (uint32_t y = y0; y < y0 + h; ++y)
            for (uint32_t x = x0; x < x0 + w; ++x, i += 4) std::copy(&p[i], &p[i] + 4, &data_[size_t(word32(bp, bw, x, y)) * 4]);
    } else if (psm == 0x13) {
        for (uint32_t y = y0; y < y0 + h; ++y)
            for (uint32_t x = x0; x < x0 + w; ++x) data_[byte8(bp, bw, x, y)] = p[i++];
    } else if (psm == 0x14) {
        for (uint32_t y = y0; y < y0 + h; ++y)
            for (uint32_t x = x0; x < x0 + w; ++x, ++i) {
                const uint8_t v = (p[i >> 1] >> (4 * (i & 1))) & 15;
                const uint32_t n = nibble4(bp, bw, x, y);
                uint8_t &b = data_[n >> 1];
                b = uint8_t((b & (0xf0 >> (4 * (n & 1)))) | (v << (4 * (n & 1))));
            }
    } else {
        throw Error("GS upload format " + std::to_string(psm));
    }
}

Bytes GsMemory::read32(uint32_t bp, uint32_t bw, uint32_t w, uint32_t h) const
{
    Bytes out;
    out.reserve(size_t(w) * h * 4);
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) {
            const size_t a = size_t(word32(bp, bw, x, y)) * 4;
            out.insert(out.end(), data_.begin() + long(a), data_.begin() + long(a + 4));
        }
    return out;
}

Bytes GsMemory::read8(uint32_t bp, uint32_t bw, uint32_t w, uint32_t h) const
{
    Bytes out;
    out.reserve(size_t(w) * h);
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) out.push_back(data_[byte8(bp, bw, x, y)]);
    return out;
}

Bytes GsMemory::read4(uint32_t bp, uint32_t bw, uint32_t w, uint32_t h) const
{
    Bytes out;
    out.reserve(size_t(w) * h);
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) {
            const uint32_t n = nibble4(bp, bw, x, y);
            out.push_back((data_[n >> 1] >> (4 * (n & 1))) & 15);
        }
    return out;
}

Bytes GsMemory::clut(uint32_t cbp, uint32_t psm, uint32_t csa) const
{
    if (psm == 0x13) {
        const Bytes raw = read32(cbp, 1, 16, 16);
        Bytes out;
        for (uint32_t i = 0; i < 256; ++i) {
            const uint32_t p = (i & 0xe7) | ((i & 0x08) << 1) | ((i & 0x10) >> 1);
            out.insert(out.end(), raw.begin() + long(4 * p), raw.begin() + long(4 * p + 4));
        }
        return out;
    }
    const Bytes raw = read32(cbp, 1, 8, 2 * (csa + 1));
    return Bytes(raw.begin() + long(64 * csa), raw.begin() + long(64 * csa + 64));
}

} // namespace guest
