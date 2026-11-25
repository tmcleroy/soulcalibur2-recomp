/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The port of tools/guests/textures.py: see that file for the formats. */
#include "textures.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <tuple>

namespace guest {

namespace {

enum : uint32_t { I4 = 0, RGBA8 = 6, C4 = 8, C8 = 9, CMPR = 14 };
enum : uint32_t { D3D_A8R8G8B8 = 0x06, D3D_P8 = 0x0b, D3D_DXT1 = 0x0c, D3D_DXT3 = 0x0e, D3D_DXT5 = 0x0f };

using RGBA = std::array<int, 4>;

struct Gx {
    uint32_t format, width, height;
};
Gx gx(uint32_t word) { return {(word >> 20) & 15, (word & 0x3ff) + 1, ((word >> 10) & 0x3ff) + 1}; }

/* The Xbox's flags for the GameCube's TEX_MODE0, and whether mip-mapped. */
bool mode_of(uint32_t mode, uint32_t &flags, bool &mipmapped)
{
    switch (mode) {
    case 0x190: flags = 0x241b; mipmapped = false; return true;
    case 0x1d0: flags = 0x241b; mipmapped = true; return true;
    case 0x195: flags = 0x2409; mipmapped = false; return true;
    case 0x1b0: flags = 0x241b; mipmapped = true; return true;
    default: return false;
    }
}

uint32_t level(uint32_t size, uint32_t l) { uint32_t v = size >> l; return v ? v : 1; }
uint32_t blocks_across(uint32_t n) { return n >= 4 ? (n + 3) / 4 : 1; }

uint8_t reverse_pairs(uint8_t b)
{
    return uint8_t((b & 0x03) << 6 | (b & 0x0c) << 2 | (b & 0x30) >> 2 | (b & 0xc0) >> 6);
}

/* A CMPR image's blocks in DXT1 form and order. */
Bytes cmpr_blocks(const Bytes &data, size_t at, uint32_t width, uint32_t height)
{
    const uint32_t bw = blocks_across(width), bh = blocks_across(height);
    Bytes out(size_t(bw) * bh * 8);
    for (uint32_t ty = 0; ty < bh; ty += 2) {
        for (uint32_t tx = 0; tx < bw; tx += 2) {
            for (uint32_t sub = 0; sub < 4; ++sub) {
                const uint32_t bx = tx + (sub & 1), by = ty + (sub >> 1);
                if (bx < bw && by < bh) {
                    const size_t dst = (size_t(by) * bw + bx) * 8;
                    put16(out, dst, be16(data, at));
                    put16(out, dst + 2, be16(data, at + 2));
                    for (int i = 0; i < 4; ++i) out[dst + 4 + i] = reverse_pairs(data.at(at + 4 + i));
                }
                at += 8;
            }
        }
    }
    return out;
}

uint32_t cmpr_size(uint32_t w, uint32_t h)
{
    uint32_t v = ((w + 7) / 8) * ((h + 7) / 8) * 32;
    return v < 8 ? 8 : v;
}

std::array<int, 3> rgb565(uint32_t c)
{
    const int r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    return {r << 3 | r >> 2, g << 2 | g >> 4, b << 3 | b >> 2};
}

std::array<RGBA, 4> dxt1_palette(uint32_t c0, uint32_t c1)
{
    const auto a = rgb565(c0), b = rgb565(c1);
    std::array<RGBA, 4> p;
    p[0] = {a[0], a[1], a[2], 255};
    p[1] = {b[0], b[1], b[2], 255};
    if (c0 > c1) {
        for (int i = 0; i < 3; ++i) {
            p[2][i] = (2 * a[i] + b[i]) / 3;
            p[3][i] = (a[i] + 2 * b[i]) / 3;
        }
        p[2][3] = p[3][3] = 255;
    } else {
        for (int i = 0; i < 3; ++i) p[2][i] = (a[i] + b[i]) / 2;
        p[2][3] = 255;
        p[3] = {0, 0, 0, 0};
    }
    return p;
}

/* Decoded RGBA texels of DXT1-form colour blocks (row-major). */
std::vector<RGBA> decode_colour(const Bytes &blocks, uint32_t width, uint32_t height)
{
    const uint32_t bw = blocks_across(width);
    std::vector<RGBA> out(size_t(width) * height);
    for (uint32_t by = 0; by < height; by += 4) {
        for (uint32_t bx = 0; bx < width; bx += 4) {
            const size_t p = (size_t(by / 4) * bw + bx / 4) * 8;
            const auto palette = dxt1_palette(le16(blocks, p), le16(blocks, p + 2));
            const uint32_t bits = le32(blocks, p + 4);
            for (uint32_t y = 0; y < 4; ++y)
                for (uint32_t x = 0; x < 4; ++x)
                    if (by + y < height && bx + x < width)
                        out[size_t(by + y) * width + bx + x] = palette[(bits >> (2 * (4 * y + x))) & 3];
        }
    }
    return out;
}

std::vector<int> i4_texels(const Bytes &data, size_t at, uint32_t width, uint32_t height)
{
    std::vector<int> out(size_t(width) * height);
    for (uint32_t ty = 0; ty < height; ty += 8)
        for (uint32_t tx = 0; tx < width; tx += 8)
            for (uint32_t y = 0; y < 8; ++y)
                for (uint32_t x = 0; x < 8; x += 2) {
                    const uint8_t byte = data.at(at++);
                    const int v[2] = {byte >> 4, byte & 15};
                    for (uint32_t dx = 0; dx < 2; ++dx)
                        if (ty + y < height && tx + x + dx < width) out[size_t(ty + y) * width + tx + x + dx] = v[dx];
                }
    return out;
}

std::vector<RGBA> rgba8_texels(const Bytes &data, size_t at, uint32_t width, uint32_t height)
{
    std::vector<RGBA> out(size_t(width) * height, RGBA{0, 0, 0, 0});
    for (uint32_t ty = 0; ty < height; ty += 4)
        for (uint32_t tx = 0; tx < width; tx += 4) {
            for (uint32_t t = 0; t < 16; ++t) {
                const uint32_t y = ty + t / 4, x = tx + t % 4;
                const int a = data.at(at + 2 * t), r = data.at(at + 2 * t + 1);
                const int g = data.at(at + 32 + 2 * t), b = data.at(at + 33 + 2 * t);
                if (y < height && x < width) out[size_t(y) * width + x] = {r, g, b, a};
            }
            at += 64;
        }
    return out;
}

uint32_t rgba8_size(uint32_t w, uint32_t h) { return ((w + 3) / 4) * ((h + 3) / 4) * 64; }

std::array<RGBA, 16> block_of(const std::vector<RGBA> &rows, uint32_t w, uint32_t h, uint32_t bx, uint32_t by)
{
    std::array<RGBA, 16> out;
    for (uint32_t y = 0; y < 4; ++y)
        for (uint32_t x = 0; x < 4; ++x)
            out[4 * y + x] = rows[size_t(std::min(by + y, h - 1)) * w + std::min(bx + x, w - 1)];
    return out;
}

void encode_alpha(Bytes &out, const std::array<int, 16> &values)
{
    int hi = values[0], lo = values[0];
    for (int v : values) {
        if (v > hi) hi = v;
        if (v < lo) lo = v;
    }
    out.push_back(uint8_t(hi));
    out.push_back(uint8_t(lo));
    if (hi == lo) {
        for (int i = 0; i < 6; ++i) out.push_back(0);
        return;
    }
    int levels[8] = {hi, lo};
    for (int i = 1; i < 7; ++i) levels[i + 1] = ((7 - i) * hi + i * lo) / 7;
    uint64_t bits = 0;
    for (int n = 0; n < 16; ++n) {
        int best = 0;
        for (int i = 1; i < 8; ++i)
            if (std::abs(levels[i] - values[n]) < std::abs(levels[best] - values[n])) best = i;
        bits |= uint64_t(best) << (3 * n);
    }
    for (int i = 0; i < 6; ++i) out.push_back(uint8_t(bits >> (8 * i)));
}

uint32_t to565(const RGBA &c)
{
    return uint32_t((c[0] * 31 + 127) / 255) << 11 | uint32_t((c[1] * 63 + 127) / 255) << 5 |
           uint32_t((c[2] * 31 + 127) / 255);
}

void encode_colour(Bytes &out, const std::array<RGBA, 16> &texels)
{
    int channel = 0, widest = -1;
    for (int c = 0; c < 3; ++c) {
        int hi = texels[0][c], lo = texels[0][c];
        for (const RGBA &t : texels) {
            if (t[c] > hi) hi = t[c];
            if (t[c] < lo) lo = t[c];
        }
        if (hi - lo > widest) {
            widest = hi - lo;
            channel = c;
        }
    }
    size_t imax = 0, imin = 0;
    for (size_t i = 1; i < 16; ++i) {
        if (texels[i][channel] > texels[imax][channel]) imax = i;
        if (texels[i][channel] < texels[imin][channel]) imin = i;
    }
    uint32_t c0 = to565(texels[imax]), c1 = to565(texels[imin]);
    if (c0 < c1) std::swap(c0, c1);
    add16(out, uint16_t(c0));
    add16(out, uint16_t(c1));
    if (c0 == c1) {
        add32(out, 0);
        return;
    }
    const auto palette = dxt1_palette(c0, c1);
    uint32_t bits = 0;
    for (int n = 0; n < 16; ++n) {
        int best = 0, best_d = -1;
        for (int i = 0; i < 4; ++i) {
            int d = 0;
            for (int c = 0; c < 3; ++c) d += (palette[i][c] - texels[n][c]) * (palette[i][c] - texels[n][c]);
            if (best_d < 0 || d < best_d) {
                best_d = d;
                best = i;
            }
        }
        bits |= uint32_t(best) << (2 * n);
    }
    add32(out, bits);
}

struct Converted {
    uint32_t d3d, levels;
    Bytes data;
};

Converted convert_texture(const Bytes &vgt, const std::array<uint32_t, 17> &entry)
{
    const uint32_t colour = entry[3], alpha = entry[11];
    const Gx g = gx(colour);
    const uint32_t levels = ((entry[2] >> 8) & 0xff) / 16 + 1;
    size_t at = entry[4];
    const bool has_alpha = alpha != 0;
    const uint32_t afmt = has_alpha ? gx(alpha).format : 0;
    size_t alpha_at = has_alpha ? entry[12] : 0;
    Bytes out;
    for (uint32_t l = 0; l < levels; ++l) {
        const uint32_t w = level(g.width, l), h = level(g.height, l);
        if (g.format == CMPR) {
            const Bytes blocks = cmpr_blocks(vgt, at, w, h);
            at += cmpr_size(w, h);
            if (!has_alpha) {
                out.insert(out.end(), blocks.begin(), blocks.end());
                continue;
            }
            const uint32_t bw = blocks_across(w);
            if (afmt == I4) {
                const auto values = i4_texels(vgt, alpha_at, w, h);
                uint32_t step = ((w + 7) / 8) * ((h + 7) / 8) * 32;
                alpha_at += step < 32 ? 32 : step;
                for (size_t b = 0; b < blocks.size() / 8; ++b) {
                    const uint32_t bx = uint32_t(b % bw) * 4, by = uint32_t(b / bw) * 4;
                    for (uint32_t y = 0; y < 4; ++y) {
                        uint32_t row = 0;
                        for (uint32_t x = 0; x < 4; ++x)
                            row |= uint32_t(values[size_t(std::min(by + y, h - 1)) * w + std::min(bx + x, w - 1)])
                                   << (4 * x);
                        add16(out, uint16_t(row));
                    }
                    out.insert(out.end(), blocks.begin() + long(8 * b), blocks.begin() + long(8 * b + 8));
                }
            } else if (afmt == CMPR) {
                const Bytes ablocks = cmpr_blocks(vgt, alpha_at, w, h);
                alpha_at += cmpr_size(w, h);
                const auto grey = decode_colour(ablocks, w, h);
                for (size_t b = 0; b < blocks.size() / 8; ++b) {
                    const uint32_t bx = uint32_t(b % bw) * 4, by = uint32_t(b / bw) * 4;
                    const auto texels = block_of(grey, w, h, bx, by);
                    std::array<int, 16> values;
                    for (int i = 0; i < 16; ++i) values[i] = texels[i][1];
                    encode_alpha(out, values);
                    out.insert(out.end(), blocks.begin() + long(8 * b), blocks.begin() + long(8 * b + 8));
                }
            } else {
                throw Error("texture alpha image format " + std::to_string(afmt));
            }
        } else if (g.format == RGBA8 && !has_alpha) {
            const auto rows = rgba8_texels(vgt, at, w, h);
            at += rgba8_size(w, h);
            for (uint32_t by = 0; by < h; by += 4)
                for (uint32_t bx = 0; bx < w; bx += 4) {
                    const auto texels = block_of(rows, w, h, bx, by);
                    std::array<int, 16> values;
                    for (int i = 0; i < 16; ++i) values[i] = texels[i][3];
                    encode_alpha(out, values);
                    encode_colour(out, texels);
                }
        } else {
            throw Error("texture format " + std::to_string(g.format));
        }
    }
    const uint32_t d3d = g.format == CMPR && !has_alpha ? D3D_DXT1 : has_alpha && afmt == I4 ? D3D_DXT3 : D3D_DXT5;
    return {d3d, levels, std::move(out)};
}

std::array<uint32_t, 17> vgt_entry(const Bytes &vgt, uint32_t k)
{
    std::array<uint32_t, 17> e;
    for (uint32_t i = 0; i < 17; ++i) e[i] = be32(vgt, 0x18 + k * 0x44 + 4 * i);
    return e;
}

uint32_t swizzle(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
    uint32_t out = 0, bit = 0, w = width, h = height;
    while (w > 1 || h > 1) {
        if (w > 1) {
            out |= (x & 1) << bit;
            x >>= 1;
            w >>= 1;
            ++bit;
        }
        if (h > 1) {
            out |= (y & 1) << bit;
            y >>= 1;
            h >>= 1;
            ++bit;
        }
    }
    return out;
}

uint32_t next_pow2(uint32_t n)
{
    uint32_t p = 1;
    while (p < n) p <<= 1;
    return p;
}

void put_words(Bytes &b, size_t at, std::initializer_list<uint32_t> words)
{
    for (uint32_t w : words) {
        put32(b, at, w);
        at += 4;
    }
}

} // namespace

/* "not a VGT bank", with what the entry holds instead (all zeros: an image with the file's data taken out). */
[[noreturn]] static void not_vgt(const Bytes &b)
{
    if (b.empty()) throw Error("not a VGT bank (empty)");
    if (std::all_of(b.begin(), b.end(), [](uint8_t c) { return c == 0; }))
        throw Error("not a VGT bank (" + std::to_string(b.size()) + " bytes of zeros)");
    char head[16];
    std::snprintf(head, sizeof head, "%02x %02x %02x %02x", b[0], b.size() > 1 ? b[1] : 0, b.size() > 2 ? b[2] : 0,
                  b.size() > 3 ? b[3] : 0);
    throw Error(std::string("not a VGT bank (starts ") + head + ")");
}

Bytes convert_bank(const Bytes &vgt)
{
    if (!starts(vgt, "VGT.", 4)) not_vgt(vgt);
    const uint32_t count = be32(vgt, 8);
    const size_t table = 0x14 + 0x24 * size_t(count);
    const size_t data_at = align(table, 0x80);
    const size_t gc_data_at = align(0x18 + 0x44 * size_t(count), 0x20);
    Bytes header = {'V', 'X', 'T', '.', 2, 4, 2, 0};
    add32(header, count);
    add32(header, 0x14);
    add32(header, uint32_t(table));
    add32(header, 0);
    Bytes entries, data;
    std::map<std::tuple<uint32_t, uint32_t, uint32_t, uint32_t, uint32_t>, std::tuple<uint32_t, uint32_t, uint32_t>> seen;
    for (uint32_t e = 0; e < count; ++e) {
        const auto entry = vgt_entry(vgt, e);
        uint32_t flags;
        bool mipmapped;
        if (!mode_of(entry[1], flags, mipmapped)) throw Error("TEX_MODE0 " + std::to_string(entry[1]));
        if (entry[5] & 0x200000) flags |= 0x100;
        const Gx g = gx(entry[3]);
        const auto key = std::make_tuple(entry[3], entry[4], entry[11], entry[12], entry[2]);
        if (!seen.count(key)) {
            Converted c = convert_texture(vgt, entry);
            if (entry[4] < gc_data_at) throw Error("texture data before the table's end");
            const size_t at = entry[4] - gc_data_at;
            if (data.size() < at + c.data.size()) data.resize(at + c.data.size());
            std::copy(c.data.begin(), c.data.end(), data.begin() + long(at));
            seen[key] = {uint32_t(data_at + at), c.d3d, c.levels};
        }
        const auto [offset, d3d, levels] = seen[key];
        const uint32_t filtering = mipmapped ? 0xa0 + levels : 0;
        const uint32_t size = g.height << 16 | g.width;
        for (uint32_t w : {flags, filtering, size, offset, d3d, size, levels, uint32_t(0), uint32_t(0)}) add32(entries, w);
    }
    if (data.size() < vgt.size() - gc_data_at) data.resize(vgt.size() - gc_data_at);
    Bytes out = header;
    out.insert(out.end(), entries.begin(), entries.end());
    out.resize(table);
    out.resize(data_at);
    out.insert(out.end(), data.begin(), data.end());
    return out;
}

Paletted c8_texture(const Bytes &vgt, uint32_t k)
{
    Gx g;
    uint32_t first, second;
    size_t at;
    if (vgt.at(4) == 3) {   /* version 3 (the silhouettes): 0x20-byte entries */
        const size_t e = 0x18 + 0x20 * size_t(k);
        g.format = be32(vgt, e + 20);
        g.width = be32(vgt, e + 24) >> 16;
        g.height = be32(vgt, e + 24) & 0xffff;
        first = be32(vgt, be32(vgt, e));
        second = be32(vgt, be32(vgt, e) + 4);
        at = be32(vgt, e + 12);
    } else {
        const auto entry = vgt_entry(vgt, k);
        g = gx(entry[3]);
        first = be32(vgt, entry[0] + 4);
        second = be32(vgt, entry[0] + 0x10);
        at = entry[4];
    }
    if (g.format != C8 && g.format != C4) throw Error("texture format " + std::to_string(g.format));
    const uint32_t colours = g.format == C8 ? 256 : 16;
    Paletted out;
    out.width = g.width;
    out.height = g.height;
    out.palette.assign(0x400, 0);
    for (uint32_t i = 0; i < colours; ++i) {
        const uint8_t gg = vgt.at(first + 2 * i), r = vgt.at(first + 2 * i + 1);
        const uint8_t a = vgt.at(second + 2 * i), b = vgt.at(second + 2 * i + 1);
        out.palette[4 * i] = b;
        out.palette[4 * i + 1] = gg;
        out.palette[4 * i + 2] = r;
        out.palette[4 * i + 3] = a;
    }
    out.indices.assign(size_t(g.width) * g.height, 0);
    const uint32_t tw = 8, th = g.format == C8 ? 4 : 8;
    for (uint32_t ty = 0; ty < g.height; ty += th)
        for (uint32_t tx = 0; tx < g.width; tx += tw) {
            for (uint32_t y = 0; y < th; ++y)
                for (uint32_t x = 0; x < tw; ++x) {
                    uint8_t value;
                    if (g.format == C8) {
                        value = vgt.at(at++);
                    } else {
                        const uint8_t byte = vgt.at(at + (y * tw + x) / 2);
                        value = x % 2 == 0 ? byte >> 4 : byte & 15;
                    }
                    if (ty + y < g.height && tx + x < g.width) out.indices[size_t(ty + y) * g.width + tx + x] = value;
                }
            if (g.format == C4) at += tw * th / 2;
        }
    return out;
}

Bytes paletted_bank(const std::vector<std::pair<uint32_t, Paletted>> &images, uint32_t size)
{
    const uint32_t count = uint32_t(images.size());
    const size_t table = 0x14 + 0x24 * size_t(count);
    const size_t descriptors = table;
    const size_t palettes_at = align(descriptors + 12 * size_t(count), 0x80);
    const size_t data_at = align(palettes_at + 0x400 * size_t(count), 0x80);
    Bytes out(data_at + size_t(size) * size * count);
    const uint8_t head[8] = {'V', 'X', 'T', '.', 2, 4, 2, 0};
    std::memcpy(out.data(), head, 8);
    put32(out, 8, count);
    put32(out, 0xc, 0x14);
    put32(out, 0x10, uint32_t(descriptors + 12 * count));
    const uint32_t dims = size << 16 | size;
    for (uint32_t k = 0; k < count; ++k) {
        const uint32_t entry_flags = images[k].first;
        const Paletted &image = images[k].second;
        std::copy(image.palette.begin(), image.palette.end(), out.begin() + long(palettes_at + 0x400 * k));
        const size_t base = data_at + size_t(size) * size * k;
        for (uint32_t y = 0; y < size; ++y) {
            const size_t row = size_t(y * image.height / size) * image.width;
            for (uint32_t x = 0; x < size; ++x)
                out[base + swizzle(x, y, size, size)] = image.indices[row + size_t(x) * image.width / size];
        }
        put_words(out, 0x14 + 0x24 * k,
                  {uint32_t(descriptors + 12 * k), entry_flags, 0, dims, uint32_t(base), D3D_P8, dims, 1, 0});
        put_words(out, descriptors + 12 * k, {uint32_t(palettes_at + 0x400 * k), 0x100, 0});
    }
    return out;
}

Bytes convert_paletted_bank(const Bytes &vgt, uint32_t size, long flags)
{
    if (!starts(vgt, "VGT.", 4)) not_vgt(vgt);
    const uint32_t count = be32(vgt, 8);
    std::vector<std::pair<uint32_t, Paletted>> images;
    for (uint32_t k = 0; k < count; ++k) {
        const auto entry = vgt_entry(vgt, k);
        if (gx(entry[3]).format != C8) throw Error("not a C8 image");
        uint32_t entry_flags;
        bool mipmapped;
        if (flags >= 0) entry_flags = uint32_t(flags);
        else if (!mode_of(entry[1], entry_flags, mipmapped)) entry_flags = 0x241b;
        images.emplace_back(entry_flags, c8_texture(vgt, k));
    }
    return paletted_bank(images, size);
}

Bytes rgba8_bank(const std::vector<Rgba8Image> &images)
{
    const uint32_t count = uint32_t(images.size());
    const size_t table = 0x14 + 0x24 * size_t(count);
    const size_t data_at = align(table, 0x80);
    Bytes header = {'V', 'X', 'T', '.', 2, 4, 2, 0};
    add32(header, count);
    add32(header, 0x14);
    add32(header, uint32_t(table));
    add32(header, 0);
    Bytes entries, data;
    for (const Rgba8Image &image : images) {
        const uint32_t sw = next_pow2(image.width), sh = next_pow2(image.height);
        Bytes pixels(size_t(sw) * sh * 4);
        for (uint32_t y = 0; y < image.height; ++y)
            for (uint32_t x = 0; x < image.width; ++x) {
                const Texel &p = image.rows[size_t(y) * image.width + x];
                put32(pixels, 4 * size_t(swizzle(x, y, sw, sh)),
                      uint32_t(p[3]) << 24 | uint32_t(p[0]) << 16 | uint32_t(p[1]) << 8 | uint32_t(p[2]));
            }
        for (uint32_t w : {image.flags, 0u, image.height << 16 | image.width, uint32_t(data_at + data.size()),
                           uint32_t(D3D_A8R8G8B8), sh << 16 | sw, 1u, 0u, 0u})
            add32(entries, w);
        data.insert(data.end(), pixels.begin(), pixels.end());
        pad(data, 0x80);
    }
    Bytes out = header;
    out.insert(out.end(), entries.begin(), entries.end());
    out.resize(table);
    out.resize(data_at);
    out.insert(out.end(), data.begin(), data.end());
    return out;
}

Bytes convert_rgba8_bank(const Bytes &vgt, long flags)
{
    if (!starts(vgt, "VGT.", 4)) not_vgt(vgt);
    const uint32_t count = be32(vgt, 8);
    std::vector<Rgba8Image> images;
    for (uint32_t k = 0; k < count; ++k) {
        const auto entry = vgt_entry(vgt, k);
        const Gx g = gx(entry[3]);
        if (g.format != RGBA8) throw Error("texture format " + std::to_string(g.format));
        Rgba8Image image;
        bool mipmapped;
        if (flags >= 0) image.flags = uint32_t(flags);
        else if (!mode_of(entry[1], image.flags, mipmapped)) image.flags = 0x241b;
        image.width = g.width;
        image.height = g.height;
        for (const RGBA &p : rgba8_texels(vgt, entry[4], g.width, g.height))
            image.rows.push_back({uint8_t(p[0]), uint8_t(p[1]), uint8_t(p[2]), uint8_t(p[3])});
        images.push_back(std::move(image));
    }
    return rgba8_bank(images);
}

Bytes dxt_image(const std::vector<Texel> &texels, uint32_t width, uint32_t height, uint32_t &format)
{
    std::vector<RGBA> rows;
    rows.reserve(texels.size());
    bool opaque = true;
    for (const Texel &t : texels) {
        rows.push_back({t[0], t[1], t[2], t[3]});
        if (t[3] != 255) opaque = false;
    }
    Bytes out;
    for (uint32_t by = 0; by < height; by += 4)
        for (uint32_t bx = 0; bx < width; bx += 4) {
            const auto block = block_of(rows, width, height, bx, by);
            if (!opaque) {
                std::array<int, 16> values;
                for (int i = 0; i < 16; ++i) values[i] = block[i][3];
                encode_alpha(out, values);
            }
            encode_colour(out, block);
        }
    format = opaque ? D3D_DXT1 : D3D_DXT5;
    return out;
}

Bytes replace_p8(const Bytes &vxt, uint32_t k, const Paletted &image)
{
    Bytes out = vxt;
    const size_t stride = vxt.at(4) == 2 ? 0x24 : 0x20;
    const size_t at = 0x14 + stride * k;
    const uint32_t descriptor = le32(out, at);
    uint32_t offset, fmt, stored, logical;
    if (vxt[4] == 2) {
        logical = le32(out, at + 12);
        offset = le32(out, at + 16);
        fmt = le32(out, at + 20);
        stored = le32(out, at + 24);
    } else {
        offset = le32(out, at + 12);
        fmt = le32(out, at + 16);
        stored = le32(out, at + 20);
        logical = stored;
    }
    const uint32_t sw = stored & 0xffff, sh = stored >> 16, iw = logical & 0xffff, ih = logical >> 16;
    if (fmt != D3D_P8) throw Error("not a P8 texture");
    const uint32_t palette_at = le32(out, descriptor);
    need(out, palette_at, 0x400);
    std::copy(image.palette.begin(), image.palette.end(), out.begin() + palette_at);
    need(out, offset, size_t(sw) * sh);
    std::fill(out.begin() + offset, out.begin() + offset + long(size_t(sw) * sh), 0);
    for (uint32_t y = 0; y < ih; ++y) {
        const size_t row = size_t(y * image.height / ih) * image.width;
        for (uint32_t x = 0; x < iw; ++x)
            out.at(offset + swizzle(x, y, sw, sh)) = image.indices[row + size_t(x) * image.width / iw];
    }
    return out;
}

Bytes append_p8(const Bytes &vxt, const std::vector<std::pair<uint32_t, Paletted>> &images)
{
    struct Texture {
        uint32_t flags, image, stored;
        Bytes palette, pixels;
    };
    const uint8_t version = vxt.at(4);
    const size_t stride = version == 2 ? 0x24 : 0x20;
    const uint32_t count = le32(vxt, 8);
    std::vector<Texture> all;
    for (uint32_t k = 0; k < count; ++k) {
        const size_t at = 0x14 + stride * k;
        const uint32_t descriptor = le32(vxt, at), flags = le32(vxt, at + 4);
        uint32_t image, offset, fmt, stored, levels;
        if (version == 2) {
            image = le32(vxt, at + 12);
            offset = le32(vxt, at + 16);
            fmt = le32(vxt, at + 20);
            stored = le32(vxt, at + 24);
            levels = le32(vxt, at + 28);
        } else {
            offset = le32(vxt, at + 12);
            fmt = le32(vxt, at + 16);
            stored = le32(vxt, at + 20);
            levels = le32(vxt, at + 24);
            image = stored;
        }
        if (fmt != D3D_P8 || levels != 1) throw Error("texture " + std::to_string(k) + " is not a one-level P8");
        const uint32_t palette_at = le32(vxt, descriptor);
        const size_t size = size_t(stored & 0xffff) * (stored >> 16);
        need(vxt, palette_at, 0x400);
        need(vxt, offset, size);
        all.push_back({flags, image, stored, Bytes(vxt.begin() + palette_at, vxt.begin() + palette_at + 0x400),
                       Bytes(vxt.begin() + offset, vxt.begin() + long(offset + size))});
    }
    for (const auto &[flags, p] : images) {
        /* (stored at the next power of two, the picture in its top left: the table keeps both sizes) */
        uint32_t sw = 1, sh = 1;
        while (sw < p.width) sw <<= 1;
        while (sh < p.height) sh <<= 1;
        Bytes pixels(size_t(sw) * sh);
        for (uint32_t y = 0; y < p.height; ++y)
            for (uint32_t x = 0; x < p.width; ++x)
                pixels[swizzle(x, y, sw, sh)] = p.indices.at(size_t(y) * p.width + x);
        all.push_back({flags, p.height << 16 | p.width, sh << 16 | sw, p.palette, pixels});
    }
    const size_t total = all.size();
    const size_t descriptors = 0x14 + stride * total;
    const size_t palettes = align(descriptors + 12 * total, 0x80);
    size_t data = align(palettes + 0x400 * total, 0x80);
    std::vector<size_t> offsets;
    for (const Texture &t : all) {
        offsets.push_back(data);
        data = align(data + t.pixels.size(), 0x80);
    }
    Bytes out(data, 0);
    std::copy(vxt.begin(), vxt.begin() + 8, out.begin());
    put32(out, 8, uint32_t(total));
    put32(out, 0xc, 0x14);
    put32(out, 0x10, uint32_t(descriptors + 12 * total));
    for (size_t k = 0; k < total; ++k) {
        const Texture &t = all[k];
        if (version == 2)
            put_words(out, 0x14 + stride * k, {uint32_t(descriptors + 12 * k), t.flags, 0, t.image, uint32_t(offsets[k]),
                                               D3D_P8, t.stored, 1, 0});
        else
            put_words(out, 0x14 + stride * k, {uint32_t(descriptors + 12 * k), t.flags, 0, uint32_t(offsets[k]), D3D_P8,
                                               t.stored, 1, 0});
        put_words(out, descriptors + 12 * k, {uint32_t(palettes + 0x400 * k), 0x100, 0});
        std::copy(t.palette.begin(), t.palette.end(), out.begin() + long(palettes + 0x400 * k));
        std::copy(t.pixels.begin(), t.pixels.end(), out.begin() + long(offsets[k]));
    }
    return out;
}

} // namespace guest
