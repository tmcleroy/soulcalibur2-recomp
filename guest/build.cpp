/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The port of tools/guests/build_guest.py: the same root.olk out. */
#include "build.h"

#include "motion.h"
#include "ps2tex.h"
#include "sound.h"
#include "textures.h"
#include "vmp.h"
#include "vmx.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <set>

namespace guest {

namespace {

constexpr uint32_t GUEST_BLOCK = 46 + 39 * 23;

/* The Xbox disc's /2 (menus, their pictures and text), as the US disc numbers it, or the European one (Europe,
   Australia: 585 entries to the US disc's 516). The European disc has the same entries up to 87, then the Arcade
   endings' text in its four other languages where the US disc has empty entries (88-91, the US disc's 88-94
   making 88-93), so every entry from the US disc's 95 on is one lower; its other languages' text and pictures
   follow the US disc's last entry. Its large select-grid icons (/2/10 part 0) are 6 more, before the characters'
   (Spawn's 100 and 101 are its 106 and 107); /2/9, the large icons' other bank, is as the US disc's. */
struct Layout {
    bool european = false;
    explicit Layout(const Olnk &x2) : european(x2.count() == 585) {
        if (!european && x2.count() != 516) throw Error("the Xbox disc's menus are not Soulcalibur II's (USA or Europe)");
    }
    uint32_t ui(uint32_t us) const { return european && us >= 88 ? us - 1 : us; }      /* a /2 entry */
    uint32_t large(uint32_t us) const { return european ? us + 6 : us; }              /* a /2/10 part 0 icon */
};

struct UiEntry {
    uint32_t gc, xbox;
    enum { Profile, Sound, Paletted, Rgba8 } kind;
};
const UiEntry GUEST_UI[] = {
    {84, 86, UiEntry::Profile},    {146, 148, UiEntry::Sound},  {178, 180, UiEntry::Sound},
    {284, 286, UiEntry::Paletted}, {343, 343, UiEntry::Rgba8},  {344, 344, UiEntry::Rgba8},
    {422, 424, UiEntry::Rgba8},    {470, 475, UiEntry::Rgba8},  {471, 476, UiEntry::Rgba8},
    {472, 477, UiEntry::Rgba8},
};
const uint32_t GUEST_ICONS[][2] = {{51, 51}, {52, 52}};   /* (Xbox, GameCube) in /2/5 part 2 */
struct GuestTexture {
    uint32_t entry;
    int part;   /* -1: the entry is the bank */
    uint32_t texture;
};
const GuestTexture GUEST_TEXTURES[] = {{203, 0, 19}, {395, -1, 20}, {396, -1, 20}, {396, -1, 35}, {396, -1, 41}};
struct GuestText {
    uint32_t entry, spawn, link, count;
};
const GuestText GUEST_TEXT[] = {{87, 84, 76, 4}, {399, 240, 216, 12}, {399, 493, 469, 12}, {503, 21, 19, 1}};
const GuestText HEIHACHI_TEXT[] = {{87, 84, 80, 4}, {399, 240, 228, 12}, {399, 493, 481, 12}, {503, 21, 20, 1}};

/* Heihachi's /2 entries: PS2 -> Xbox (Spawn's third pose takes his first) */
const UiEntry HEIHACHI_UI[] = {
    {85, 86, UiEntry::Profile},    {147, 148, UiEntry::Sound},  {179, 180, UiEntry::Sound},
    {285, 286, UiEntry::Paletted}, {343, 343, UiEntry::Rgba8},  {344, 344, UiEntry::Rgba8},
    {423, 424, UiEntry::Rgba8},    {473, 475, UiEntry::Rgba8},  {474, 476, UiEntry::Rgba8},
    {473, 477, UiEntry::Rgba8},
};
struct Ps2Texture_ {
    uint32_t entry;
    int part;   /* -1: the entry is the bank */
    uint32_t texture, ps2_texture;
};
const Ps2Texture_ HEIHACHI_TEXTURES[] = {{203, 0, 19, 19}, {395, -1, 20, 20}, {396, -1, 20, 20}, {396, -1, 35, 35},
                                         {396, -1, 41, 20},
                                         {9, -1, 98, 98},   {9, -1, 99, 99},   {10, 0, 100, 100}, {10, 0, 101, 101}};   /* silhouettes */

std::vector<Bytes> parts_of(const Bytes &data, bool big_endian)
{
    auto p = pack_parts(data, big_endian);
    if (!p) throw Error("a pack the converter expected is not one");
    return *p;
}

Bytes keep_flags(Bytes converted, const Bytes &xbox)
{
    const uint32_t count = std::min(le32(converted, 8), le32(xbox, 8));
    for (uint32_t k = 0; k < count; ++k)
        for (int i = 0; i < 4; ++i) converted.at(0x18 + 0x24 * k + i) = xbox.at(0x18 + 0x24 * k + i);
    return converted;
}

Bytes weapon(const Bytes &gc_pack)
{
    const auto gp = parts_of(gc_pack, true);
    return build_pack({convert_model(gp.at(0)), convert_sound_container(gp.at(1)), convert_records(gp.at(2))});
}

/* A converted costume pack: Spawn's parts (`templ`) with Link's over them. */
Bytes link_costume(const std::vector<Bytes> &templ, const Bytes &gc_costume)
{
    const auto costume = parts_of(gc_costume, true);
    std::vector<Bytes> parts = templ;
    parts.at(0) = convert_model(costume.at(0));
    parts.at(1) = convert_records(costume.at(1));
    parts.at(2) = convert_sound_container(costume.at(2));
    parts.at(3) = convert_pose_bank(costume.at(3));
    for (int i = 4; i < 7; ++i) parts.at(i) = costume.at(i);
    parts.at(12) = convert_sound_container(costume.at(12));
    parts.at(13) = weapon(costume.at(13));
    return build_pack(parts);
}

/* Link's weapons, movesets and sound banks in the guest's block of `entries`. */
void link_rest(const Olnk &g4, std::vector<Bytes> &entries, const std::function<void()> &step)
{
    for (uint32_t slot = 16; slot < 28; ++slot) {
        const Bytes g = g4.bytes(GUEST_BLOCK + slot);
        if (pack_parts(g, true)) entries.at(GUEST_BLOCK + slot) = weapon(g);
        step();
    }
    for (uint32_t slot : {28u, 29u})
        entries.at(GUEST_BLOCK + slot) = build_pack(convert_moveset(parts_of(g4.bytes(GUEST_BLOCK + slot), true)));
    step();
    for (uint32_t slot = 30; slot < 37; ++slot) {
        const Bytes g = g4.bytes(GUEST_BLOCK + slot);
        if (starts(g, "mmg", 3)) entries.at(GUEST_BLOCK + slot) = convert_sound_container(g);
    }
    step();
}

void guest_block(const Olnk &x4, const Olnk &g4, std::vector<Bytes> &entries, const std::function<void()> &step)
{
    Bytes converted;
    for (uint32_t slot : {0u, 1u, 2u, 8u, 9u, 10u}) {
        const auto xp = pack_parts(x4.bytes(GUEST_BLOCK + slot), false);
        if (!xp) continue;
        if (converted.empty()) {
            converted = link_costume(*xp, g4.bytes(GUEST_BLOCK));
            step();
        }
        entries.at(GUEST_BLOCK + slot) = converted;
    }
    link_rest(g4, entries, step);
}

/* Link's own block in the guest's place of `entries`: his four costumes (slots 0-3, none in 8-15) on Spawn's
   first costume's parts. */
void link_block(const Olnk &x4, const Olnk &g4, std::vector<Bytes> &entries, const std::function<void()> &step)
{
    const auto templ = parts_of(x4.bytes(GUEST_BLOCK), false);
    for (uint32_t slot = 0; slot < 16; ++slot) entries.at(GUEST_BLOCK + slot).clear();
    for (uint32_t slot = 0; slot < 4; ++slot) {
        const Bytes g = g4.bytes(GUEST_BLOCK + slot);
        if (pack_parts(g, true)) entries.at(GUEST_BLOCK + slot) = link_costume(templ, g);
        step();
    }
    link_rest(g4, entries, step);
}

Bytes names_as(Bytes table, const std::string &name)
{
    const uint32_t count = le32(table, 4);
    const size_t base = 8 + 4 * size_t(count);
    std::vector<uint32_t> offsets;
    std::vector<std::string> strings;
    for (uint32_t i = 0; i < count; ++i) {
        offsets.push_back(le32(table, 8 + 4 * i));
        std::string s;
        for (size_t p = base + offsets.back(); p < table.size() && table[p]; ++p) s.push_back(char(table[p]));
        strings.push_back(s);
    }
    const uint32_t back = name == "Link" ? 2 : 1;   /* (the Xbox's tables name all three: Link, Heihachi, Spawn) */
    for (uint32_t i = back; i < count; ++i)
        if (strings[i] == "Spawn" && strings[i - back] == name) put32(table, 8 + 4 * i, offsets[i - back]);
    return table;
}

Bytes names_as_link(Bytes table) { return names_as(std::move(table), "Link"); }

Bytes redirect(Bytes table, const GuestText &t)
{
    const uint32_t total = le32(table, 4);
    if (t.spawn + t.count > total || t.link + t.count > total) throw Error("text table too short");
    for (uint32_t k = 0; k < t.count; ++k) put32(table, 8 + 4 * (t.spawn + k), le32(table, 8 + 4 * (t.link + k)));
    return table;
}

void apply_text(const Olnk &x2, std::vector<Bytes> &entries, const GuestText *table, size_t n);

/* Link's select-screen entries (GUEST_UI), each in its Xbox entry's place (as `layout` numbers it). */
void guest_ui_entries(const Olnk &x2, const Olnk &g2, std::vector<Bytes> &entries, const Layout &layout)
{
    for (const UiEntry &u : GUEST_UI) {
        const uint32_t xbox = layout.ui(u.xbox);
        const Bytes g = g2.bytes(u.gc), x = x2.bytes(xbox);
        switch (u.kind) {
        case UiEntry::Sound: entries.at(xbox) = convert_sound_container(g); break;
        case UiEntry::Paletted: entries.at(xbox) = keep_flags(convert_paletted_bank(g), x); break;
        case UiEntry::Rgba8: entries.at(xbox) = keep_flags(convert_rgba8_bank(g), x); break;
        case UiEntry::Profile: {
            const auto gp = parts_of(g, true), xp = parts_of(x, false);
            Bytes picture = keep_flags(convert_paletted_bank(gp.at(0)), xp.at(0));
            pad(picture, 0x800);
            std::vector<Bytes> parts = {picture};
            parts.insert(parts.end(), gp.begin() + 1, gp.end());
            entries.at(xbox) = build_pack(parts, pack_alignment(x));
            break;
        }
        }
    }
}

/* Link in Spawn's place (the US disc only): his entries, icons, gallery pictures, name and text over Spawn's. */
void guest_ui(const Olnk &x2, const Olnk &g2, std::vector<Bytes> &entries)
{
    guest_ui_entries(x2, g2, entries, Layout(x2));
    {
        const Bytes x5 = x2.bytes(5);
        auto xp = parts_of(x5, false);
        const auto gp = parts_of(g2.bytes(5), true);
        Bytes bank = xp.at(2);
        for (const auto &pair : GUEST_ICONS) bank = replace_p8(bank, pair[0], c8_texture(gp.at(2), pair[1]));
        xp.at(2) = bank;
        entries.at(5) = build_pack(xp, pack_alignment(x5));
    }
    std::set<uint32_t> banks;
    for (const GuestTexture &t : GUEST_TEXTURES) banks.insert(t.entry);
    for (uint32_t index : banks) {
        Bytes xd = entries.at(index).empty() ? x2.bytes(index) : entries.at(index);
        const Bytes gd = g2.bytes(index);
        bool packed = false;
        std::vector<Bytes> xparts, gparts;
        for (const GuestTexture &t : GUEST_TEXTURES)
            if (t.entry == index && t.part >= 0) packed = true;
        if (packed) {
            xparts = parts_of(xd, false);
            gparts = parts_of(gd, true);
        }
        for (const GuestTexture &t : GUEST_TEXTURES) {
            if (t.entry != index) continue;
            if (t.part < 0) xd = replace_p8(xd, t.texture, c8_texture(gd, t.texture));
            else xparts.at(t.part) = replace_p8(xparts.at(t.part), t.texture, c8_texture(gparts.at(t.part), t.texture));
        }
        entries.at(index) = packed ? build_pack(xparts, pack_alignment(xd)) : xd;
    }
    {
        const Bytes x26 = x2.bytes(26);
        auto parts = parts_of(x26, false);
        for (Bytes &p : parts) p = names_as_link(p);
        entries.at(26) = build_pack(parts, pack_alignment(x26));
    }
    apply_text(x2, entries, GUEST_TEXT, sizeof GUEST_TEXT / sizeof GUEST_TEXT[0]);
}

void apply_text(const Olnk &x2, std::vector<Bytes> &entries, const GuestText *table, size_t n)
{
    std::map<uint32_t, Bytes> text;
    for (size_t k = 0; k < n; ++k) {
        const GuestText &t = table[k];
        const Bytes data = text.count(t.entry) ? text[t.entry] : x2.bytes(t.entry);
        if (auto parts = pack_parts(data, false)) {
            parts->at(0) = redirect(parts->at(0), t);
            text[t.entry] = build_pack(*parts, pack_alignment(data));
        } else {
            text[t.entry] = redirect(data, t);
        }
    }
    for (auto &[index, data] : text) entries.at(index) = data;
}

void write_file(const Bytes &out, const std::string &output)
{
    const std::string partial = output + ".part";
    std::FILE *f = std::fopen(partial.c_str(), "wb");
    if (!f) throw Error("cannot write " + partial);
    const bool written = std::fwrite(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
#ifdef _WIN32
    std::remove(output.c_str());   /* (rename does not replace a file there) */
#endif
    if (!written || std::rename(partial.c_str(), output.c_str()) != 0) {
        std::remove(partial.c_str());
        throw Error("cannot write " + output);
    }
}

std::pair<std::vector<Bytes>, std::vector<uint32_t>> entries_of(const Olnk &c)
{
    std::vector<Bytes> entries;
    std::vector<uint32_t> times;
    for (uint32_t i = 0; i < c.count(); ++i) {
        entries.push_back(c.bytes(i));
        times.push_back(c.entry_time(i));
    }
    return {entries, times};
}

/* ---- Heihachi, from the PlayStation 2 disc ---- */

Bytes ps2_weapon(const Bytes &pack, const std::function<Bytes(const Bytes &)> &models)
{
    const auto parts = parts_of(pack, false);
    std::vector<Bytes> out = {models(parts.at(0)), convert_sound_container(parts.at(1))};
    out.insert(out.end(), parts.begin() + 2, parts.end());
    return build_pack(out);
}

void heihachi_block(const Olnk &x4, const Olnk &p4, std::vector<Bytes> &entries, const std::function<void()> &step)
{
    std::map<Bytes, Bytes> cache;
    auto models = [&](const Bytes &data) -> Bytes {
        auto found = cache.find(data);
        if (found == cache.end()) found = cache.emplace(data, convert_ps2_model(data)).first;
        return found->second;
    };
    for (uint32_t slot : {0u, 1u, 2u, 8u, 9u, 10u}) {
        if (!pack_parts(x4.bytes(GUEST_BLOCK + slot), false)) continue;
        Bytes source = p4.bytes(GUEST_BLOCK + slot);
        if (!pack_parts(source, false)) source = p4.bytes(GUEST_BLOCK + (slot & 8));
        auto parts = parts_of(source, false);
        parts.at(0) = models(parts.at(0));
        parts.at(2) = convert_sound_container(parts.at(2));
        parts.at(12) = convert_sound_container(parts.at(12));
        parts.at(13) = ps2_weapon(parts.at(13), models);
        entries.at(GUEST_BLOCK + slot) = build_pack(parts);
        step();
    }
    for (uint32_t slot = 16; slot < 28; ++slot) {
        const Bytes p = p4.bytes(GUEST_BLOCK + slot);
        if (pack_parts(p, false)) entries.at(GUEST_BLOCK + slot) = ps2_weapon(p, models);
        step();
    }
    for (uint32_t slot : {28u, 29u}) entries.at(GUEST_BLOCK + slot) = p4.bytes(GUEST_BLOCK + slot);
    for (uint32_t slot = 30; slot < 37; ++slot) {
        const Bytes p = p4.bytes(GUEST_BLOCK + slot);
        if (starts(p, "mmp", 3)) entries.at(GUEST_BLOCK + slot) = convert_sound_container(p);
    }
    step();
}

std::vector<std::pair<uint32_t, Paletted>> paletted_images(Ps2Bank &bank)
{
    std::vector<std::pair<uint32_t, Paletted>> images;
    for (uint32_t k = 0; k < bank.count(); ++k) images.emplace_back(0, bank.texture(k).paletted());
    return images;
}

/* Heihachi's select-screen entries (HEIHACHI_UI), each in its Xbox entry's place (as `layout` numbers it). */
void heihachi_ui_entries(const Olnk &x2, const Olnk &p2, std::vector<Bytes> &entries, const Layout &layout)
{
    for (const UiEntry &u : HEIHACHI_UI) {
        const uint32_t xbox = layout.ui(u.xbox);
        const Bytes p = p2.bytes(u.gc), x = x2.bytes(xbox);
        switch (u.kind) {
        case UiEntry::Sound: entries.at(xbox) = convert_sound_container(p); break;
        case UiEntry::Paletted: {
            Ps2Bank bank(p);
            entries.at(xbox) = keep_flags(paletted_bank(paletted_images(bank)), x);
            break;
        }
        case UiEntry::Rgba8: {
            Ps2Bank bank(p);
            std::vector<Rgba8Image> images;
            for (uint32_t k = 0; k < bank.count(); ++k) {
                const Ps2Texture t = bank.texture(k);
                images.push_back({0, t.width, t.height, t.rows()});
            }
            entries.at(xbox) = keep_flags(rgba8_bank(images), x);
            break;
        }
        case UiEntry::Profile: {
            const auto pp = parts_of(p, false), xp = parts_of(x, false);
            Ps2Bank bank(pp.at(0));
            Bytes picture = keep_flags(paletted_bank(paletted_images(bank)), xp.at(0));
            pad(picture, 0x800);
            std::vector<Bytes> parts = {picture};
            parts.insert(parts.end(), pp.begin() + 1, pp.end());
            entries.at(xbox) = build_pack(parts, pack_alignment(x));
            break;
        }
        }
    }
}

/* Heihachi in Spawn's place (the US disc only): as guest_ui. */
void heihachi_ui(const Olnk &x2, const Olnk &p2, std::vector<Bytes> &entries)
{
    heihachi_ui_entries(x2, p2, entries, Layout(x2));
    {
        const Bytes x5 = x2.bytes(5);
        auto xp = parts_of(x5, false);
        const auto pp = parts_of(p2.bytes(5), false);
        Ps2Bank icons(pp.at(2));
        for (uint32_t k : {51u, 52u}) xp.at(2) = replace_p8(xp.at(2), k, icons.texture(k).paletted());
        entries.at(5) = build_pack(xp, pack_alignment(x5));
    }
    std::set<uint32_t> banks;
    for (const auto &t : HEIHACHI_TEXTURES) banks.insert(t.entry);
    for (uint32_t index : banks) {
        Bytes xd = entries.at(index).empty() ? x2.bytes(index) : entries.at(index);
        const Bytes pd = p2.bytes(index);
        bool packed = false;
        for (const auto &t : HEIHACHI_TEXTURES)
            if (t.entry == index && t.part >= 0) packed = true;
        std::vector<Bytes> xparts, pparts;
        if (packed) {
            xparts = parts_of(xd, false);
            pparts = parts_of(pd, false);
        }
        std::map<int, std::unique_ptr<Ps2Bank>> sources;
        for (const auto &t : HEIHACHI_TEXTURES) {
            if (t.entry != index) continue;
            auto &bank = sources[t.part];
            if (!bank) bank = std::make_unique<Ps2Bank>(t.part >= 0 ? pparts.at(t.part) : pd);
            const Paletted image = bank->texture(t.ps2_texture).paletted();
            if (t.part < 0) xd = replace_p8(xd, t.texture, image);
            else xparts.at(t.part) = replace_p8(xparts.at(t.part), t.texture, image);
        }
        entries.at(index) = packed ? build_pack(xparts, pack_alignment(xd)) : xd;
    }
    {
        const Bytes x26 = x2.bytes(26);
        auto parts = parts_of(x26, false);
        for (Bytes &part : parts) part = names_as(part, "Heihachi");
        entries.at(26) = build_pack(parts, pack_alignment(x26));
    }
    apply_text(x2, entries, HEIHACHI_TEXT, sizeof HEIHACHI_TEXT / sizeof HEIHACHI_TEXT[0]);
}


/* ---- Link and Heihachi beside Spawn (the port of tools/guests/roster.py) ---- */

constexpr uint32_t ROSTER_LINK = 27, ROSTER_HEIHACHI = 28;
constexpr uint32_t ROSTER_LINK_BLOCK = 46 + 39 * 24, ROSTER_HEIHACHI_BLOCK = 46 + 39 * 25;
/* (/2 index the conversion gives, the guest's own; the last, the picture its profile's unlock shows: /2/297 + its profile
   item, 48 Link and 49 Heihachi, runtime/sc2_guest.cpp. As the US disc numbers them: Layout gives the European's.) */
const uint32_t LINK_OWN_UI[][2] = {{86, 84}, {148, 146}, {180, 178}, {286, 284}, {424, 422}, {475, 470}, {476, 471}, {477, 472},
    {343, 345}};
const uint32_t HEIHACHI_OWN_UI[][2] = {{86, 85}, {148, 147}, {180, 179}, {286, 285}, {424, 423}, {475, 473}, {476, 474},
    {343, 346}};

/* A program's memory: (file offset, address, size) a section, in its byte order. */
struct Program {
    Bytes data;
    std::vector<std::array<uint32_t, 3>> sections;
    bool big_endian = false;

    Bytes read(uint32_t address, size_t size) const
    {
        for (const auto &[offset, start, length] : sections)
            if (length && start <= address && address < start + length) return slice(data, size_t(offset) + (address - start), size);
        throw Error("an address the guest's program does not have");
    }
    uint32_t u32(uint32_t address) const
    {
        const Bytes b = read(address, 4);
        return big_endian ? be32(b, 0) : le32(b, 0);
    }
    Bytes string(uint32_t address) const
    {
        Bytes out;
        for (;; ++address) {
            const uint8_t c = read(address, 1).at(0);
            if (!c) return out;
            out.push_back(c);
        }
    }
};

Program dol_program(Bytes data)
{
    Program p;
    for (int i = 0; i < 18; ++i) p.sections.push_back({be32(data, 4 * i), be32(data, 0x48 + 4 * i), be32(data, 0x90 + 4 * i)});
    p.big_endian = true;
    p.data = std::move(data);
    return p;
}

Program elf_program(Bytes data)
{
    Program p;
    const uint32_t at = le32(data, 0x1c), count = le16(data, 0x2c);
    for (uint32_t i = 0; i < count; ++i) {
        const size_t h = at + 32 * size_t(i);
        p.sections.push_back({le32(data, h + 4), le32(data, h + 8), le32(data, h + 16)});
    }
    p.data = std::move(data);
    return p;
}

/* The profile entry (block slot 38) for a character, from its own program. */
Bytes roster_profile(const Program &program, uint32_t table, uint32_t names, uint32_t grid, uint32_t cell,
                     uint32_t character)
{
    Bytes record = program.read(table + 0xf8 * character, 0xf8);
    Bytes out(0x10 + 0xf8, 0);
    std::map<Bytes, uint32_t> strings;
    auto place = [&](const Bytes &text) -> uint32_t {
        auto found = strings.find(text);
        if (found != strings.end()) return found->second;
        const uint32_t at = uint32_t(out.size());
        strings.emplace(text, at);
        out.insert(out.end(), text.begin(), text.end());
        out.push_back(0);
        return at;
    };
    for (uint32_t at = 0; at < 0x78; at += 4) {
        const uint32_t pointer = program.big_endian ? be32(record, at) : le32(record, at);
        put32(record, at, pointer ? place(program.string(pointer)) : 0);
    }
    if (program.big_endian) {
        for (size_t at = 0x78; at < 0xa8; at += 2) std::swap(record[at], record[at + 1]);
        for (size_t at = 0xa8; at < 0xb8; at += 4) std::reverse(record.begin() + long(at), record.begin() + long(at + 4));
    }
    uint32_t name = 0;
    if (names) name = place(program.string(program.u32(names + 4 * character)));
    Bytes raw = program.read(grid + 36 * cell, 36);
    if (program.big_endian) {
        for (size_t at : {size_t(0), size_t(2), size_t(32), size_t(34)}) std::swap(raw[at], raw[at + 1]);
        for (size_t at : {size_t(4), size_t(24), size_t(28)}) std::reverse(raw.begin() + long(at), raw.begin() + long(at + 4));
    }
    if (int16_t(le16(raw, 0)) != int16_t(character)) throw Error("the guest's select-grid cell is not the guest's");
    const uint32_t cell_at = uint32_t(out.size());
    out.insert(out.end(), raw.begin(), raw.end());
    std::memcpy(out.data(), "GPRF", 4);
    put32(out, 4, character);
    put32(out, 8, name);
    put32(out, 12, cell_at);
    std::copy(record.begin(), record.end(), out.begin() + 0x10);
    return out;
}

uint32_t entry_flags(const Bytes &vxt, uint32_t k)
{
    const size_t stride = vxt.at(4) == 2 ? 0x24 : 0x20;
    return le32(vxt, 0x14 + stride * k + 4);
}

} // namespace

void build_guests_root(const DiscFile &xbox, const DiscFile *gamecube, const DiscFile *ps2, const std::string &output,
                       const Progress &progress)
{
    if (gamecube && gamecube->game_id != GAMECUBE_ID)
        throw Error("the GameCube disc is " + gamecube->game_id + ", not Soulcalibur II (USA, " + GAMECUBE_ID + ")");
    if (ps2 && !ps2->game_id.empty() && ps2->game_id != PS2_ID)
        throw Error("the PlayStation 2 disc is " + ps2->game_id + ", not Soulcalibur II (USA, " + PS2_ID + ")");
    const Olnk xroot(xbox.read, 0);
    int done = 0;
    const int total = (gamecube ? 20 : 0) + (ps2 ? 20 : 0) + 2;
    auto step = [&]() {
        if (progress) progress(++done, total);
    };
    auto [top, top_times] = entries_of(xroot);
    const Olnk x4 = xroot.child(4), x2 = xroot.child(2);
    const Layout layout(x2);
    auto [e4, t4] = entries_of(x4);
    auto [e2, t2] = entries_of(x2);
    e4.resize(e4.size() + 78);
    for (int k = 0; k < 2; ++k)
        for (uint32_t i = 0; i < 39; ++i) t4.push_back(t4.at(GUEST_BLOCK + i));
    auto icons = parts_of(x2.bytes(5), false);
    auto large10 = parts_of(x2.bytes(10), false);
    const Bytes small = icons.at(2), large9 = x2.bytes(9);
    std::vector<std::pair<uint32_t, Paletted>> small_new, large9_new, large10_new;
    /* The Art Gallery's thumbnails (runtime/sc2_guest.cpp, ApplyGallery): Hi-Res's (/2/395) gets Link's portrait,
       then Heihachi's and his two illustrations; Illustrations' (/2/396) gets Link's three. */
    const uint32_t hires_entry = layout.ui(395), illus_entry = layout.ui(396);
    const Bytes hires = x2.bytes(hires_entry), illus = x2.bytes(illus_entry);
    std::vector<std::pair<uint32_t, Paletted>> hires_new, illus_new;

    if (gamecube) {
        const Olnk groot(gamecube->read, 0), g4 = groot.child(4), g2 = groot.child(2);
        std::vector<Bytes> work(e4.begin(), e4.begin() + 982);
        link_block(x4, g4, work, step);
        std::vector<Bytes> block(work.begin() + GUEST_BLOCK, work.begin() + GUEST_BLOCK + 39);
        block.at(38) = roster_profile(dol_program(gamecube->program()), 0x80275A60u, 0x8028E9D8u, 0x80250520u, 1, ROSTER_LINK);
        std::copy(block.begin(), block.end(), e4.begin() + ROSTER_LINK_BLOCK);
        std::vector<Bytes> ui = e2;
        guest_ui_entries(x2, g2, ui, layout);
        for (const auto &pair : LINK_OWN_UI) e2.at(layout.ui(pair[1])) = ui.at(layout.ui(pair[0]));
        const Bytes gicons = parts_of(g2.bytes(5), true).at(2), g9 = g2.bytes(9), g10 = parts_of(g2.bytes(10), true).at(0);
        for (uint32_t k = 0; k < 4; ++k) {
            small_new.emplace_back(entry_flags(small, 51), c8_texture(gicons, 51 + k));
            large9_new.emplace_back(entry_flags(large9, 98), c8_texture(g9, 98 + k));
            large10_new.emplace_back(entry_flags(large10.at(0), layout.large(100)), c8_texture(g10, 100 + k));
        }
        const Bytes g395 = g2.bytes(395), g396 = g2.bytes(396);
        hires_new.emplace_back(entry_flags(hires, 20), c8_texture(g395, 20));
        for (uint32_t k : {20u, 35u, 41u}) illus_new.emplace_back(entry_flags(illus, k), c8_texture(g396, k));
        step();
    }
    if (ps2) {
        const Olnk proot(ps2->read, 0), p4 = proot.child(4), p2 = proot.child(2);
        std::vector<Bytes> work(e4.begin(), e4.begin() + 982);
        heihachi_block(x4, p4, work, step);
        std::vector<Bytes> block(work.begin() + GUEST_BLOCK, work.begin() + GUEST_BLOCK + 39);
        block.at(38) = roster_profile(elf_program(ps2->program()), 0x003FABB8u, 0, 0x0048F958u, 1, ROSTER_HEIHACHI);
        std::copy(block.begin(), block.end(), e4.begin() + ROSTER_HEIHACHI_BLOCK);
        std::vector<Bytes> ui = e2;
        heihachi_ui_entries(x2, p2, ui, layout);
        for (const auto &pair : HEIHACHI_OWN_UI) e2.at(layout.ui(pair[1])) = ui.at(layout.ui(pair[0]));
        /* (the banks keep a reference to their bytes) */
        const Bytes picons_data = parts_of(p2.bytes(5), false).at(2), p9_data = p2.bytes(9),
                    p10_data = parts_of(p2.bytes(10), false).at(0);
        Ps2Bank picons(picons_data), p9(p9_data), p10(p10_data);
        for (uint32_t k = 0; k < 2; ++k) {
            small_new.emplace_back(entry_flags(small, 51), picons.texture(51 + k).paletted());
            large9_new.emplace_back(entry_flags(large9, 98), p9.texture(98 + k).paletted());
            large10_new.emplace_back(entry_flags(large10.at(0), layout.large(100)), p10.texture(100 + k).paletted());
        }
        const Bytes p395_data = p2.bytes(395), p396_data = p2.bytes(396);
        Ps2Bank p395(p395_data), p396(p396_data);
        hires_new.emplace_back(entry_flags(hires, 20), p395.texture(20).paletted());
        for (uint32_t k : {20u, 35u}) hires_new.emplace_back(entry_flags(hires, 20), p396.texture(k).paletted());
        step();
    }
    if (!hires_new.empty()) e2.at(hires_entry) = append_p8(hires, hires_new);
    if (!illus_new.empty()) e2.at(illus_entry) = append_p8(illus, illus_new);
    icons.at(2) = append_p8(small, small_new);
    e2.at(5) = build_pack(icons, pack_alignment(x2.bytes(5)));
    e2.at(9) = append_p8(large9, large9_new);
    large10.at(0) = append_p8(large10.at(0), large10_new);
    e2.at(10) = build_pack(large10, pack_alignment(x2.bytes(10)));
    top.at(4) = build_olnk(e4, t4, x4.alignment(), x4.time());
    top.at(2) = build_olnk(e2, t2, x2.alignment(), x2.time());
    step();
    const std::string stamp = output + ".version";
    std::remove(stamp.c_str()); // (no stamp while the file is being replaced)
    write_file(build_olnk(top, top_times, xroot.alignment(), xroot.time()), output);
    {
        std::FILE *f = std::fopen(stamp.c_str(), "wb");
        if (!f) throw Error("couldn't write " + stamp);
        std::fprintf(f, "sc2-guests %d\n", GUESTS_VERSION);
        std::fclose(f);
    }
    if (progress) progress(total, total);
}

int guests_version(const std::string &output)
{
    std::FILE *f = std::fopen((output + ".version").c_str(), "rb");
    if (!f) return 0;
    int version = 0;
    if (std::fscanf(f, "sc2-guests %d", &version) != 1) version = 0;
    std::fclose(f);
    return version;
}

namespace {
} // namespace

void build_guest_root(const DiscFile &xbox, const DiscFile &gamecube, const std::string &output,
                      const Progress &progress)
{
    if (gamecube.game_id != GAMECUBE_ID)
        throw Error("the GameCube disc is " + gamecube.game_id + ", not Soulcalibur II (USA, " + GAMECUBE_ID + ")");
    const Olnk xroot(xbox.read, 0), groot(gamecube.read, 0);
    int done = 0;
    const int total = 18;
    auto step = [&]() {
        if (progress) progress(++done, total);
    };
    auto [top, top_times] = entries_of(xroot);
    {
        const Olnk x4 = xroot.child(4), g4 = groot.child(4);
        auto [entries, times] = entries_of(x4);
        guest_block(x4, g4, entries, step);
        top.at(4) = build_olnk(entries, times, x4.alignment(), x4.time());
    }
    {
        const Olnk x2 = xroot.child(2), g2 = groot.child(2);
        auto [entries, times] = entries_of(x2);
        guest_ui(x2, g2, entries);
        step();
        top.at(2) = build_olnk(entries, times, x2.alignment(), x2.time());
    }
    write_file(build_olnk(top, top_times, xroot.alignment(), xroot.time()), output);
    if (progress) progress(total, total);
}

} // namespace guest

namespace guest {

void build_heihachi_root(const DiscFile &xbox, const DiscFile &ps2, const std::string &output, const Progress &progress)
{
    if (!ps2.game_id.empty() && ps2.game_id != PS2_ID)
        throw Error("the PlayStation 2 disc is " + ps2.game_id + ", not Soulcalibur II (USA, " + PS2_ID + ")");
    const Olnk xroot(xbox.read, 0), proot(ps2.read, 0);
    int done = 0;
    const int total = 20;
    auto step = [&]() {
        if (progress) progress(++done, total);
    };
    auto [top, top_times] = entries_of(xroot);
    {
        const Olnk x4 = xroot.child(4), p4 = proot.child(4);
        auto [entries, times] = entries_of(x4);
        heihachi_block(x4, p4, entries, step);
        top.at(4) = build_olnk(entries, times, x4.alignment(), x4.time());
    }
    {
        const Olnk x2 = xroot.child(2), p2 = proot.child(2);
        auto [entries, times] = entries_of(x2);
        heihachi_ui(x2, p2, entries);
        step();
        top.at(2) = build_olnk(entries, times, x2.alignment(), x2.time());
    }
    write_file(build_olnk(top, top_times, xroot.alignment(), xroot.time()), output);
    if (progress) progress(total, total);
}

} // namespace guest
