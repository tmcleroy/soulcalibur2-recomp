# SPDX-License-Identifier: GPL-3.0-or-later
"""Builds a root.olk with Link (from the player's GameCube disc) or Heihachi (from the PlayStation 2 disc) in the
guest's place (Spawn's, on the Xbox).

  build_guest.py XBOX_ROOT_OLK GAMECUBE_OR_PS2_ROOT_OLK OUTPUT


Only reads the two discs' files. What goes where:
  /4 block 23 (the guest's character block): costumes (model, records, poses, sound, the default weapon),
      the twelve weapons, both moveset packs, the voice and effect banks; the Xbox's own parts where the
      GameCube has none (the costumes Link does not have keep his first)
  /2 (the select screens, profile and voices): the entries Link has on the GameCube (Heihachi on the PS2) in
      the places Spawn's are on the Xbox (GUEST_UI, HEIHACHI_UI below); the menu strings (/2/26) name him
      where they name Spawn
The program's own strings (the profile) are rewritten when the game starts (runtime/sc2_guest.cpp).
"""
import struct
import sys

import motion
import ps2tex
import records
import sound
import textures
import vmg2vmx
import vmp2vmx
from olnk import Olnk, build, build_pack, pack_alignment, pack_parts

GUEST_BLOCK = 46 + 39 * 23

# /2 entries: GameCube (Link) -> Xbox (Spawn), and how each converts
GUEST_UI = [
    (84, 86, "profile"),        # profile: two C8 pictures and the profile text
    (146, 148, "sound"),        # select-screen voices
    (178, 180, "sound"),
    (284, 286, "paletted"),     # character select art (sepia)
    (343, 343, "rgba8"),        # costume pictures
    (344, 344, "rgba8"),
    (422, 424, "rgba8"),        # portrait
    (470, 475, "rgba8"),        # poses
    (471, 476, "rgba8"),
    (472, 477, "rgba8"),
]


def _keep_flags(converted, xbox):
    """The Xbox entry's flags (wrap and filter) in each converted texture entry."""
    out = bytearray(converted)
    count = min(struct.unpack_from("<I", out, 8)[0], struct.unpack_from("<I", xbox, 8)[0])
    for k in range(count):
        out[0x18 + 0x24 * k:0x1c + 0x24 * k] = xbox[0x18 + 0x24 * k:0x1c + 0x24 * k]
    return bytes(out)


def _pad(data, alignment):
    return data + bytes(-len(data) % alignment)


# The select grid's large icons (/2/5 part 2, P8 on the Xbox, C8 on the GameCube): the first 51 are in the
# same order on both discs, then the guest's (Spawn has two, Link four).
GUEST_ICONS = [(51, 51), (52, 52)]   # (Xbox entry, GameCube entry)

# Single guest textures inside banks both discs have (C8 on the GameCube, P8 on the Xbox, at the same place):
# (/2 entry, pack part or None, texture)
GUEST_TEXTURES = [
    (203, 0, 19),    # sepia art
    (395, None, 20), # small portrait
    (396, None, 20), # poses
    (396, None, 35),
    (396, None, 41),
]


def guest_textures(x2, g2, entries):
    """The guest textures of GUEST_TEXTURES put into `entries` (the /2 container's)."""
    for index in sorted(set(i for i, _, _ in GUEST_TEXTURES)):
        xd, gd = entries[index] or x2.bytes(index), g2.bytes(index)
        xparts, gparts = pack_parts(xd), pack_parts(gd, True)
        for i, part, k in GUEST_TEXTURES:
            if i != index:
                continue
            if part is None:
                xd = textures.replace_p8(xd, k, *textures.c8_texture(gd, k))
            else:
                xparts[part] = textures.replace_p8(xparts[part], k, *textures.c8_texture(gparts[part], k))
        if any(p is not None for i, p, _ in GUEST_TEXTURES if i == index):
            entries[index] = build_pack(xparts, alignment=pack_alignment(xd))
        else:
            entries[index] = xd


def guest_icons(x2, g2):
    xp, gp = pack_parts(x2.bytes(5)), pack_parts(g2.bytes(5), True)
    bank = xp[2]
    for xi, gi in GUEST_ICONS:
        bank = textures.replace_p8(bank, xi, *textures.c8_texture(gp[2], gi))
    xp[2] = bank
    return build_pack(xp, alignment=pack_alignment(x2.bytes(5)))


def guest_ui(x2, g2, ui=lambda index: index):
    """{Xbox /2 index: bytes} for the guest's UI entries, by the US disc's index; `ui` gives the Xbox disc's own
    (roster.py, Layout: the European disc numbers them otherwise)."""
    out = {}
    for gi, xi, kind in GUEST_UI:
        g, x = g2.bytes(gi), x2.bytes(ui(xi))
        if kind == "sound":
            out[xi] = sound.convert_container(g)
        elif kind == "paletted":
            out[xi] = _keep_flags(textures.convert_paletted_bank(g), x)
        elif kind == "rgba8":
            out[xi] = _keep_flags(textures.convert_rgba8_bank(g), x)
        elif kind == "profile":
            gp, xp = pack_parts(g, True), pack_parts(x)
            picture = _keep_flags(textures.convert_paletted_bank(gp[0]), xp[0])
            out[xi] = build_pack([_pad(picture, 0x800)] + list(gp[1:]), alignment=pack_alignment(x))
    return out


def _names_as_link(table, name=b"Link"):
    """A /2/26 string table (u32 1, u32 count, u32 offsets from the strings' start, the strings) with the
    guest's names pointing at Link's (or Heihachi's: the Xbox's tables name all three guests, Link, Heihachi,
    Spawn)."""
    out = bytearray(table)
    count, = struct.unpack_from("<I", out, 4)
    base = 8 + 4 * count
    offsets = list(struct.unpack_from("<%dI" % count, out, 8))
    strings = [bytes(out[base + o:out.index(b"\0", base + o)]) for o in offsets]
    back = 2 if name == b"Link" else 1
    for i, text in enumerate(strings):
        if text == b"Spawn" and i >= back and strings[i - back] == name:
            struct.pack_into("<I", out, 8 + 4 * i, offsets[i - back])
    return bytes(out)


# The Xbox's own text tables (/2: u32 1, u32 count, u32 offsets from the strings' start, the strings) already
# hold Link's texts (and Heihachi's) beside Spawn's; Spawn's entries are pointed at Link's.
# (entry, Spawn's first index, Link's first index, how many); Heihachi's are between the two
GUEST_TEXT = [
    (87, 84, 76, 4),       # ending story
    (399, 240, 216, 12),   # weapon names
    (399, 493, 469, 12),   # weapon descriptions
    (503, 21, 19, 1),      # profile
]
HEIHACHI_TEXT = [(87, 84, 80, 4), (399, 240, 228, 12), (399, 493, 481, 12), (503, 21, 20, 1)]


def _redirect(table, spawn, link, count):
    out = bytearray(table)
    total, = struct.unpack_from("<I", out, 4)
    if spawn + count > total or link + count > total:
        raise ValueError("text table too short")
    for k in range(count):
        struct.pack_into("<I", out, 8 + 4 * (spawn + k), struct.unpack_from("<I", out, 8 + 4 * (link + k))[0])
    return bytes(out)


def guest_text(x2, table=GUEST_TEXT):
    """{Xbox /2 index: bytes} for the text tables, the guest's texts in Spawn's places."""
    out = {}
    for index, spawn, link, count in table:
        data = out.get(index, x2.bytes(index))
        parts = pack_parts(data)
        if parts:
            parts[0] = _redirect(parts[0], spawn, link, count)
            out[index] = build_pack(parts, alignment=pack_alignment(data))
        else:
            out[index] = _redirect(data, spawn, link, count)
    return out


def _weapon(gc_pack):
    gp = pack_parts(gc_pack, True)
    return build_pack([vmg2vmx.convert(gp[0]), sound.convert_container(gp[1]), records.convert_records(gp[2])])


def _link_costume(template, gc_costume):
    """A converted costume pack: Spawn's parts (`template`) with Link's over them."""
    costume = pack_parts(gc_costume, True)
    converted = list(template)
    converted[0] = vmg2vmx.convert(costume[0])
    converted[1] = records.convert_records(costume[1])
    converted[2] = sound.convert_container(costume[2])
    converted[3] = motion.convert_pose_bank(costume[3])
    converted[4:7] = costume[4:7]
    converted[12] = sound.convert_container(costume[12])
    converted[13] = _weapon(costume[13])
    return build_pack(converted)


def _link_rest(g4, entries):
    """Link's weapons, movesets and sound banks in the guest's block of `entries`."""
    for slot in range(16, 28):
        if pack_parts(g4.bytes(GUEST_BLOCK + slot), True):
            entries[GUEST_BLOCK + slot] = _weapon(g4.bytes(GUEST_BLOCK + slot))
    for slot in (28, 29):
        entries[GUEST_BLOCK + slot] = build_pack(motion.convert_moveset(pack_parts(g4.bytes(GUEST_BLOCK + slot), True)))
    for slot in range(30, 37):
        data = g4.bytes(GUEST_BLOCK + slot)
        if data[:3] == b"mmg":
            entries[GUEST_BLOCK + slot] = sound.convert_container(data)


def guest_block(x4, g4, entries):
    """Link's character block over the guest's, in `entries` (the /4 container's): his first costume in each
    of Spawn's costume slots."""
    converted = None
    for slot in (0, 1, 2, 8, 9, 10):
        xp = pack_parts(x4.bytes(GUEST_BLOCK + slot))
        if not xp:
            continue
        if converted is None:
            converted = _link_costume(xp, g4.bytes(GUEST_BLOCK))
        entries[GUEST_BLOCK + slot] = converted
    _link_rest(g4, entries)


def link_block(x4, g4, entries):
    """Link's own character block in the guest's place of `entries`: his four costumes (slots 0-3, with no
    second versions in 8-15, as on the GameCube) on Spawn's first costume's parts."""
    template = pack_parts(x4.bytes(GUEST_BLOCK))
    for slot in range(16):
        entries[GUEST_BLOCK + slot] = None
    for slot in range(4):
        if pack_parts(g4.bytes(GUEST_BLOCK + slot), True):
            entries[GUEST_BLOCK + slot] = _link_costume(template, g4.bytes(GUEST_BLOCK + slot))
    _link_rest(g4, entries)


def _ps2_weapon(pack, models):
    parts = pack_parts(pack)
    return build_pack([models(parts[0]), sound.convert_container(parts[1])] + parts[2:])


def heihachi_block(x4, p4, entries):
    """Heihachi's character block (the PS2's) over the guest's: his models and sounds rebuilt, the rest (the
    records, poses, motions and moves, the same on both) as they are."""
    cache = {}

    def models(data):
        if data not in cache:
            cache[data] = vmp2vmx.convert(data)
        return cache[data]

    for slot in (0, 1, 2, 8, 9, 10):
        if not pack_parts(x4.bytes(GUEST_BLOCK + slot)):
            continue
        source = p4.bytes(GUEST_BLOCK + slot)
        if not pack_parts(source):
            source = p4.bytes(GUEST_BLOCK + (slot & 8))
        parts = pack_parts(source)
        parts[0] = models(parts[0])
        parts[2] = sound.convert_container(parts[2])
        parts[12] = sound.convert_container(parts[12])
        parts[13] = _ps2_weapon(parts[13], models)
        entries[GUEST_BLOCK + slot] = build_pack(parts)
    for slot in range(16, 28):
        if pack_parts(p4.bytes(GUEST_BLOCK + slot)):
            entries[GUEST_BLOCK + slot] = _ps2_weapon(p4.bytes(GUEST_BLOCK + slot), models)
    for slot in (28, 29):
        entries[GUEST_BLOCK + slot] = p4.bytes(GUEST_BLOCK + slot)
    for slot in range(30, 37):
        data = p4.bytes(GUEST_BLOCK + slot)
        if data[:3] == b"mmp":
            entries[GUEST_BLOCK + slot] = sound.convert_container(data)


# /2 entries: PS2 (Heihachi) -> Xbox (Spawn), and how each converts; Spawn's third pose takes Heihachi's first
HEIHACHI_UI = [
    (85, 86, "profile"),
    (147, 148, "sound"),
    (179, 180, "sound"),
    (285, 286, "paletted"),
    (343, 343, "rgba8"),
    (344, 344, "rgba8"),
    (423, 424, "rgba8"),
    (473, 475, "rgba8"),
    (474, 476, "rgba8"),
    (473, 477, "rgba8"),
]
# Single textures inside banks both discs have, at the same places (the PS2's in brackets where it differs):
# (/2 entry, pack part or None, texture, PS2 texture)
HEIHACHI_TEXTURES = [(203, 0, 19, 19), (395, None, 20, 20), (396, None, 20, 20), (396, None, 35, 35),
                     (396, None, 41, 20),
                     (9, None, 98, 98), (9, None, 99, 99), (10, 0, 100, 100), (10, 0, 101, 101)]   # silhouettes


def heihachi_ui_entries(x2, p2, entries, ui=lambda index: index):
    """Heihachi's UI entries in `entries`, at the US disc's index; `ui` gives the Xbox disc's own, whose entry each
    takes its flags and alignment from (roster.py, Layout)."""
    for pi, xi, kind in HEIHACHI_UI:
        p, x = p2.bytes(pi), x2.bytes(ui(xi))
        if kind == "sound":
            entries[xi] = sound.convert_container(p)
        elif kind == "paletted":
            bank = ps2tex.Bank(p)
            images = [(0, *bank.texture(k).paletted()) for k in range(bank.count)]
            entries[xi] = _keep_flags(textures.paletted_bank(images), x)
        elif kind == "rgba8":
            bank = ps2tex.Bank(p)
            images = [(0, t.width, t.height, t.rows()) for t in (bank.texture(k) for k in range(bank.count))]
            entries[xi] = _keep_flags(textures.rgba8_bank(images), x)
        elif kind == "profile":
            pp, xp = pack_parts(p), pack_parts(x)
            bank = ps2tex.Bank(pp[0])
            images = [(0, *bank.texture(k).paletted()) for k in range(bank.count)]
            picture = _keep_flags(textures.paletted_bank(images), xp[0])
            entries[xi] = build_pack([_pad(picture, 0x800)] + list(pp[1:]), alignment=pack_alignment(x))


def heihachi_ui(x2, p2, entries):
    heihachi_ui_entries(x2, p2, entries)
    # the select grid's icons (/2/5 part 2)
    xp, pp = pack_parts(x2.bytes(5)), pack_parts(p2.bytes(5))
    icons = ps2tex.Bank(pp[2])
    for k in (51, 52):
        xp[2] = textures.replace_p8(xp[2], k, *icons.texture(k).paletted())
    entries[5] = build_pack(xp, alignment=pack_alignment(x2.bytes(5)))
    # single textures in shared banks
    for index in sorted(set(i for i, _, _, _ in HEIHACHI_TEXTURES)):
        xd, pd = entries[index] or x2.bytes(index), p2.bytes(index)
        xparts, pparts = pack_parts(xd), pack_parts(pd)
        banks = {}
        for i, part, k, pk in HEIHACHI_TEXTURES:
            if i != index:
                continue
            source = pparts[part] if part is not None else pd
            bank = banks.setdefault(part, ps2tex.Bank(source))
            image = bank.texture(pk).paletted()
            if part is None:
                xd = textures.replace_p8(xd, k, *image)
            else:
                xparts[part] = textures.replace_p8(xparts[part], k, *image)
        if any(p is not None for i, p, _, _ in HEIHACHI_TEXTURES if i == index):
            entries[index] = build_pack(xparts, alignment=pack_alignment(xd))
        else:
            entries[index] = xd


def build_heihachi(xroot, proot, output):
    top, top_times = xroot.entries()
    x4, p4 = xroot.child(4), proot.child(4)
    entries, times = x4.entries()
    heihachi_block(x4, p4, entries)
    top[4] = build(entries, x4.alignment, x4.time, times)
    x2, p2 = xroot.child(2), proot.child(2)
    entries, times = x2.entries()
    heihachi_ui(x2, p2, entries)
    entries[26] = build_pack([_names_as_link(part, b"Heihachi") for part in pack_parts(x2.bytes(26))],
                             alignment=pack_alignment(x2.bytes(26)))
    for index, data in guest_text(x2, HEIHACHI_TEXT).items():
        entries[index] = data
    top[2] = build(entries, x2.alignment, x2.time, times)
    with open(output, "wb") as f:
        f.write(build(top, xroot.alignment, xroot.time, top_times))


def _is_ps2(root):
    """Whether a root.olk is the PS2 disc's (its models start 08 0c; the GameCube's 'VMG.')."""
    parts = pack_parts(root.child(4).bytes(46))
    return bool(parts) and parts[0][:2] == b"\x08\x0c"


def build_root(xbox_path, gc_path, output):
    xroot, groot = Olnk.open(xbox_path), Olnk.open(gc_path)
    if _is_ps2(groot):
        return build_heihachi(xroot, groot, output)
    top, top_times = xroot.entries()
    x4, g4 = xroot.child(4), groot.child(4)
    entries, times = x4.entries()
    guest_block(x4, g4, entries)
    top[4] = build(entries, x4.alignment, x4.time, times)
    x2, g2 = xroot.child(2), groot.child(2)
    entries, times = x2.entries()
    for index, data in guest_ui(x2, g2).items():
        entries[index] = data
    entries[5] = guest_icons(x2, g2)
    guest_textures(x2, g2, entries)
    entries[26] = build_pack([_names_as_link(part) for part in pack_parts(x2.bytes(26))],
                             alignment=pack_alignment(x2.bytes(26)))
    for index, data in guest_text(x2).items():
        entries[index] = data
    top[2] = build(entries, x2.alignment, x2.time, times)
    with open(output, "wb") as f:
        f.write(build(top, xroot.alignment, xroot.time, top_times))


if __name__ == "__main__":
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    build_root(*sys.argv[1:])
