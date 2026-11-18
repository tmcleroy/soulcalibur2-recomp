/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The port of tools/guests/motion.py and records.py: see those for the formats. */
#include "motion.h"

#include <algorithm>

namespace guest {

namespace {

void swap16(Bytes &out, size_t at, size_t count)
{
    for (size_t i = at; i < at + 2 * count; i += 2) {
        need(out, i, 2);
        std::swap(out[i], out[i + 1]);
    }
}

/* (as Python's slice reversal: a last short word is reversed as far as it goes) */
void swap32(Bytes &out, size_t at, size_t count)
{
    for (size_t i = at; i < at + 4 * count; i += 4) {
        if (i >= out.size()) break;
        const size_t end = std::min(out.size(), i + 4);
        std::reverse(out.begin() + long(i), out.begin() + long(end));
    }
}

uint32_t motion_blocks(uint32_t frames, uint32_t flags)
{
    if (flags & 8) frames = (frames + 3) / 4;
    else if (flags & 4) frames = (frames + 1) / 2;
    return (frames + 7) / 8;
}

Bytes convert_motion(const Bytes &gc)
{
    Bytes out = gc;
    if (gc.size() < 0x1c) return out;
    const uint32_t frames = be16(gc, 0), channel_bytes = be16(gc, 2), flags = be32(gc, 4);
    swap16(out, 0, 2);
    swap32(out, 4, 1);
    std::reverse(out.begin() + 8, out.begin() + 16);
    swap16(out, 0x10, 1);
    const uint32_t blocks = motion_blocks(frames, flags);
    if (0x1c + 2 * size_t(blocks) > gc.size()) throw Error("motion too short for its block table");
    swap16(out, 0x1c, blocks);
    long position = 0x1c;
    const size_t channels = channel_bytes / 2;
    for (uint32_t i = 0; i < blocks; ++i) {
        position += int16_t(be16(gc, 0x1c + 2 * i));
        if (position < 0 || size_t(position) + 2 * channels > gc.size()) throw Error("motion block outside the motion");
        swap16(out, size_t(position), channels);
    }
    return out;
}

Bytes convert_moveset_tables(const Bytes &gc)
{
    const uint32_t count = be32(gc, 0);
    std::vector<uint32_t> offsets;
    for (uint32_t i = 0; i <= count; ++i) offsets.push_back(be32(gc, 4 + 4 * i));
    Bytes out = gc;
    swap32(out, 0, count + 2);
    for (uint32_t part = 0; part < count; ++part) {
        const long a = offsets[part], b = offsets[part + 1];
        if (part == 0) {
            for (long r = a; r < b - 59; r += 60) swap16(out, size_t(r) + 48, 6);
        } else if (part == 1) {
            swap16(out, size_t(a), 2);
            swap32(out, size_t(a) + 4, 1);
            for (long r = a + 8; r < b - 39; r += 40) swap16(out, size_t(r) + 16, 12);
        } else {
            swap16(out, size_t(a), 4);
            if (b - a - 8 > 0) swap32(out, size_t(a) + 8, size_t(b - a - 8) / 4);
        }
    }
    return out;
}

} // namespace

Bytes convert_motion_bank(const Bytes &gc)
{
    if (gc.size() < 8) return gc;
    const uint32_t count = be32(gc, 0);
    std::vector<uint32_t> offsets;
    for (uint32_t i = 0; i < count; ++i) offsets.push_back(be32(gc, 8 + 4 * i));
    offsets.push_back(uint32_t(gc.size()));
    Bytes out = gc;
    swap32(out, 0, 2 + count);
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t a = offsets[i], b = offsets[i + 1];
        if (b > a) {
            const Bytes m = convert_motion(slice(gc, a, b - a));
            std::copy(m.begin(), m.end(), out.begin() + a);
        }
    }
    return out;
}

std::vector<Bytes> convert_moveset(std::vector<Bytes> parts)
{
    if (parts.size() > 1 && !parts[1].empty()) parts[1] = convert_motion_bank(parts[1]);
    if (parts.size() > 2 && !parts[2].empty()) parts[2] = convert_moveset_tables(parts[2]);
    return parts;
}

Bytes convert_pose_bank(const Bytes &gc)
{
    Bytes out = convert_motion_bank(gc);
    if (gc.size() < 8) return out;
    const uint32_t count = be32(gc, 0);
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t offset = be32(gc, 8 + 4 * i);
        if (size_t(offset) + 0x1a <= out.size()) put16(out, offset + 0x18, uint16_t(le16(out, offset + 0x18) + 0x20));
    }
    return out;
}

Bytes convert_records(const Bytes &gc)
{
    Bytes out = gc;
    size_t at = 0, size = 32;
    while (at + 4 <= gc.size()) {
        const uint32_t kind = be16(gc, at);
        if (kind == 0) size = 32;
        else if (kind == 1) size = 40;
        if (at + size > gc.size()) size = gc.size() - at;
        swap16(out, at, 2);
        swap32(out, at + 4, (size - 4) / 4);
        at += size;
    }
    return out;
}

/* (tools/guests/forms.py, convert_names) */
Bytes convert_form_names(const Bytes &gc)
{
    Bytes out = gc;
    if (gc.size() < 0x20) return out;
    const size_t table = be32(gc, 4);
    swap32(out, 0, 4);
    const size_t first_name = be32(gc, 0x1c);
    for (size_t record = 0x10; record + 16 <= std::min(first_name, gc.size()); record += 16) {
        swap16(out, record, 2);
        swap32(out, record + 4, 3);
    }
    if (table + 4 <= gc.size()) {
        const size_t names = be32(gc, table);
        swap32(out, table, std::min(names, gc.size() - table) / 4);
    }
    return out;
}

/* (tools/guests/forms.py, convert_camera) */
Bytes convert_form_camera(const Bytes &gc)
{
    Bytes out = gc;
    swap32(out, 0, gc.size() / 4);
    if (gc.size() >= 0x50) {
        std::copy(gc.begin() + 0x48, gc.begin() + 0x50, out.begin() + 0x48);
        swap16(out, 0x48, 4);
    }
    return out;
}

} // namespace guest
