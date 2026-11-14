# SPDX-License-Identifier: GPL-3.0-or-later
"""Builds a root.olk with the guests beside Spawn: Link (from the player's GameCube disc) as character 27 and
Heihachi (from the PlayStation 2 disc) as character 28, the IDs the three versions share (Spawn is 29).

  roster.py XBOX_ROOT_OLK [--gamecube GC_ROOT_OLK] [--ps2 PS2_ROOT_OLK] OUTPUT

Each disc's files are read where they were extracted: its root.olk, and beside it its program (boot.dol on
the GameCube, SLUS_206.43 on the PS2). What goes where:
  /4: two blocks appended after Spawn's, 24 (Link) and 25 (Heihachi), built from Spawn's like the guest's
      (build_guest.py); a guest without a disc leaves its block empty. Slot 38 of each holds the guest's
      profile (below), which the runtime puts in the program's tables (runtime/sc2_guest.cpp).
  /2: each guest's select-screen entries in its own places (the versions keep one entry per character, empty
      for the others: Link's at 84, 146, 178, 284, 422, 470-472; Heihachi's at 85, 147, 179, 285, 423,
      473-474); its grid icons appended to /2/5 part 2 and its large icons to /2/9 and /2/10 part 0.

The profile (slot 38): 'GPRF', u32 character, u32 offset of the select-name glyph string (or 0), u32 offset of
the character's select-grid cell (36 bytes in the Xbox's byte order: s16 character, s16 costumes, u32
availability routine, u8 small icons[8], u8 large icons[8], f32, f32, s16, s16; the routine and icons are the
other program's and are set again by the runtime), then
the program's 0xf8-byte profile record in the Xbox's byte order with each string pointer (the words before
+0x78) made an offset in this entry, then the strings. The record's +0x78-0x9f are u16s, +0xa8-0xb7 floats
and the rest bytes, which is how the GameCube's is turned around (the PS2's is already the Xbox's).
"""
import os
import struct
import sys

import build_guest
import ps2tex
import textures
from olnk import Olnk, build, build_pack, pack_alignment, pack_parts

LINK, HEIHACHI = 27, 28
BLOCKS = {LINK: 24, HEIHACHI: 25}
SPAWN_BLOCK = 46 + 39 * 23
FIRST_NEW_ICON = 75          # /2/5 part 2 has 75 icons
FIRST_NEW_LARGE = 148        # /2/10 part 0 has 148 textures, /2/9 two fewer (a large icon k is /2/9's k - 2)

# The programs: (file beside root.olk, profile table, select-name table, select grid, the character's cell)
GAMECUBE = ("boot.dol", 0x80275A60, 0x8028E9D8, 0x80250520, 1)
PS2 = ("SLUS_206.43", 0x003FABB8, None, 0x0048F958, 1)


def block_start(character):
    return 46 + 39 * BLOCKS[character]


class Dol:
    """A GameCube program: 7 text and 11 data sections (offsets, addresses, sizes; big-endian)."""

    def __init__(self, data):
        self.data = data
        self.sections = list(zip(struct.unpack_from(">18I", data, 0), struct.unpack_from(">18I", data, 0x48),
                                 struct.unpack_from(">18I", data, 0x90)))
        self.endian = ">"

    def read(self, address, size):
        for offset, start, length in self.sections:
            if length and start <= address < start + length:
                return self.data[offset + address - start:offset + address - start + size]
        raise ValueError("%#x is not in the program" % address)


class Elf:
    """A PS2 program (ELF, little-endian): its loadable segments."""

    def __init__(self, data):
        self.data = data
        at, count = struct.unpack_from("<I", data, 0x1c)[0], struct.unpack_from("<H", data, 0x2c)[0]
        self.sections = [struct.unpack_from("<IIII", data, at + 32 * i)[1:3] + struct.unpack_from(
            "<I", data, at + 32 * i + 16) for i in range(count)]
        self.endian = "<"

    def read(self, address, size):
        for offset, start, length in self.sections:
            if length and start <= address < start + length:
                return self.data[offset + address - start:offset + address - start + size]
        raise ValueError("%#x is not in the program" % address)


def _string(program, address):
    out = bytearray()
    while True:
        chunk = program.read(address + len(out), 64)
        end = chunk.find(b"\0")
        if end >= 0:
            return bytes(out + chunk[:end])
        out += chunk


def profile(program, table, names, grid, cell, character):
    """The profile entry (slot 38) for a character, from its own program."""
    e = program.endian
    record = bytearray(program.read(table + 0xf8 * character, 0xf8))
    out = bytearray(0x10 + 0xf8)
    strings = {}

    def place(text):
        if text not in strings:
            strings[text] = len(out)
            out.extend(text + b"\0")
        return strings[text]

    for at in range(0, 0x78, 4):
        pointer, = struct.unpack_from(e + "I", record, at)
        struct.pack_into("<I", record, at, place(_string(program, pointer)) if pointer else 0)
    if e == ">":
        for at in range(0x78, 0xa8, 2):
            record[at:at + 2] = record[at:at + 2][::-1]
        for at in range(0xa8, 0xb8, 4):
            record[at:at + 4] = record[at:at + 4][::-1]
    name = 0
    if names is not None:
        pointer, = struct.unpack_from(e + "I", program.read(names + 4 * character, 4))
        name = place(_string(program, pointer))
    raw = program.read(grid + 36 * cell, 36)
    if struct.unpack_from(e + "h", raw, 0)[0] != character:
        raise ValueError("the select grid's cell %d is not character %d" % (cell, character))
    fields = struct.unpack_from(e + "hhI", raw, 0) + struct.unpack_from(e + "ffhh", raw, 24)
    cell_at = len(out)
    out.extend(struct.pack("<hhI", *fields[:3]) + raw[8:24] + struct.pack("<ffhh", *fields[3:]))
    out[0:0x10] = b"GPRF" + struct.pack("<III", character, name, cell_at)
    out[0x10:0x10 + 0xf8] = record
    return bytes(out)


def _program(root_path, name):
    with open(os.path.join(os.path.dirname(root_path), name), "rb") as f:
        return f.read()


def _new_block(x4, entries, make):
    """A guest's 39 entries: Spawn's block with what `make` converts over it (the guest builders work on the
    guest's place, so they work on a copy of the container's entries here)."""
    work = list(entries[:982])
    make(work)
    return work[SPAWN_BLOCK:SPAWN_BLOCK + 39]


# Each guest's /2 entries: (converted at Spawn's entry, own entry)
# (/2 index the conversion gives, the guest's own): its profile, voices, select art, portrait, poses, and the picture
# its profile's unlock shows (/2/297 + its profile item: 48 Link, 49 Heihachi, runtime/sc2_guest.cpp; the guest
# slot's costume picture on its disc)
LINK_UI = [(86, 84), (148, 146), (180, 178), (286, 284), (424, 422), (475, 470), (476, 471), (477, 472), (343, 345)]
HEIHACHI_UI = [(86, 85), (148, 147), (180, 179), (286, 285), (424, 423), (475, 473), (476, 474), (343, 346)]


class Layout:
    """The Xbox disc's /2, as the US disc numbers it or the European one (Europe, Australia: 585 entries to the US
    disc's 516). The European disc has the same entries up to 87, then the Arcade endings' text in its four other
    languages where the US disc has empty entries, so every entry from the US disc's 95 on is one lower; its other
    languages' text and pictures follow the US disc's last entry. Its large select-grid icons (/2/10 part 0) are 6
    more, before the characters'; /2/9 is as the US disc's. (guest/build.cpp, Layout, the same)"""
    def __init__(self, x2):
        self.european = x2.count == 585
        if not self.european and x2.count != 516:
            sys.exit("the Xbox disc's menus are not Soulcalibur II's (USA or Europe)")

    def ui(self, us):
        return us - 1 if self.european and us >= 88 else us

    def large(self, us):
        return us + 6 if self.european else us


def _flags(vxt, k):
    """Texture k's entry flags (the second word of its table entry)."""
    stride = 0x24 if vxt[4] == 2 else 0x20
    return struct.unpack_from("<I", vxt, 0x14 + stride * k + 4)[0]


def build_roster(xbox_path, gc_path, ps2_path, output):
    xroot = Olnk.open(xbox_path)
    top, top_times = xroot.entries()
    x4, x2 = xroot.child(4), xroot.child(2)
    layout = Layout(x2)
    e4, t4 = x4.entries()
    e2, t2 = x2.entries()
    e4 = e4 + [None] * 78
    t4 = t4 + t4[SPAWN_BLOCK:SPAWN_BLOCK + 39] * 2
    icons_x = pack_parts(x2.bytes(5))
    # The Art Gallery's thumbnails (runtime/sc2_guest.cpp, the gallery's slots): Hi-Res's (/2/395) gets Link's
    # portrait, then Heihachi's and his two illustrations (Illustrations has room for three only); Illustrations'
    # (/2/396) gets Link's three. Each from the guest slot's textures on its disc.
    hires_entry, illus_entry = layout.ui(395), layout.ui(396)
    hires, illus = x2.bytes(hires_entry), x2.bytes(illus_entry)
    hires_new, illus_new = [], []
    small, large9, large10 = icons_x[2], x2.bytes(9), pack_parts(x2.bytes(10))
    small_new, large9_new, large10_new = [], [], []

    if gc_path:
        groot = Olnk.open(gc_path)
        g4, g2 = groot.child(4), groot.child(2)
        block = _new_block(x4, e4, lambda work: build_guest.link_block(x4, g4, work))
        program = Dol(_program(gc_path, GAMECUBE[0]))
        block[38] = profile(program, *GAMECUBE[1:], LINK)
        e4[block_start(LINK):block_start(LINK) + 39] = block
        ui = build_guest.guest_ui(x2, g2, layout.ui)
        for spawn, own in LINK_UI:
            e2[layout.ui(own)] = ui[spawn]
        gicons, g9, g10 = pack_parts(g2.bytes(5), True)[2], g2.bytes(9), pack_parts(g2.bytes(10), True)[0]
        for k in range(4):
            small_new.append((_flags(small, 51),) + textures.c8_texture(gicons, 51 + k))
            large9_new.append((_flags(large9, 98),) + textures.c8_texture(g9, 98 + k))
            large10_new.append((_flags(large10[0], layout.large(100)),) + textures.c8_texture(g10, 100 + k))
        hires_new.append((_flags(hires, 20),) + textures.c8_texture(g2.bytes(395), 20))
        for k in (20, 35, 41):
            illus_new.append((_flags(illus, k),) + textures.c8_texture(g2.bytes(396), k))

    if ps2_path:
        proot = Olnk.open(ps2_path)
        p4, p2 = proot.child(4), proot.child(2)
        block = _new_block(x4, e4, lambda work: build_guest.heihachi_block(x4, p4, work))
        program = Elf(_program(ps2_path, PS2[0]))
        block[38] = profile(program, *PS2[1:], HEIHACHI)
        e4[block_start(HEIHACHI):block_start(HEIHACHI) + 39] = block
        work = list(e2)   # (scratch: Heihachi's entries at the US disc's indexes)
        build_guest.heihachi_ui_entries(x2, p2, work, layout.ui)
        for spawn, own in HEIHACHI_UI:
            e2[layout.ui(own)] = work[spawn]
        picons, p9, p10 = (ps2tex.Bank(pack_parts(p2.bytes(5))[2]), ps2tex.Bank(p2.bytes(9)),
                           ps2tex.Bank(pack_parts(p2.bytes(10))[0]))
        for k in range(2):
            small_new.append((_flags(small, 51),) + picons.texture(51 + k).paletted())
            large9_new.append((_flags(large9, 98),) + p9.texture(98 + k).paletted())
            large10_new.append((_flags(large10[0], layout.large(100)),) + p10.texture(100 + k).paletted())
        p395, p396 = ps2tex.Bank(p2.bytes(395)), ps2tex.Bank(p2.bytes(396))
        hires_new.append((_flags(hires, 20),) + p395.texture(20).paletted())
        for k in (20, 35):
            hires_new.append((_flags(hires, 20),) + p396.texture(k).paletted())

    if hires_new:
        e2[hires_entry] = textures.append_p8(hires, hires_new)
    if illus_new:
        e2[illus_entry] = textures.append_p8(illus, illus_new)
    icons_x[2] = textures.append_p8(small, small_new)
    e2[5] = build_pack(icons_x, alignment=pack_alignment(x2.bytes(5)))
    e2[9] = textures.append_p8(large9, large9_new)
    large10[0] = textures.append_p8(large10[0], large10_new)
    e2[10] = build_pack(large10, alignment=pack_alignment(x2.bytes(10)))

    top[4] = build(e4, x4.alignment, x4.time, t4)
    top[2] = build(e2, x2.alignment, x2.time, t2)
    with open(output, "wb") as f:
        f.write(build(top, xroot.alignment, xroot.time, top_times))


if __name__ == "__main__":
    args = sys.argv[1:]
    options = {"--gamecube": None, "--ps2": None}
    for key in options:
        if key in args:
            at = args.index(key)
            options[key] = args[at + 1]
            del args[at:at + 2]
    if len(args) != 2:
        sys.exit(__doc__)
    build_roster(args[0], options["--gamecube"], options["--ps2"], args[1])
