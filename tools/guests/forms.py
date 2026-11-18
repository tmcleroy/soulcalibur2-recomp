# SPDX-License-Identifier: GPL-3.0-or-later
"""The final boss's forms for the guests.

Inferno takes a fighting style from the character it fights. The program picks a form by that character
(0x528E0 on the Xbox: a table of IDs 1-29) and loads /4 entry 7 + form for the first fight and 23 + form for
the second. Each version has a form for its own guest and none for the other two: 10 for Spawn on the Xbox,
9 for Link on the GameCube (its /4/16 and /4/32), 11 for Heihachi on the PS2 (/4/18 and /4/34; the PS2 disc
holds all three). The guests' file gives the Xbox disc the two it lacks (roster.py), and the game then picks
them (runtime/sc2_hooks.c, boss_form).

A form is a numbered pack of nine parts:
  0  a motion bank of two motions (the scene's);
  1  640 bytes that name them: u32 id, u32 offset of the name table, 8 bytes of 0, then a 16-byte record for each
     motion (u16, u16, u32 date, u32 time, u32 offset of its file's name), the two file names, and the name table
     (u32 offsets from the table's start, then the names);
  2  a numbered pack of one camera track: after the pack's header (0x48 bytes, 32-bit values) four u16, then
     32-bit values (counts and floats);
  3-6  empty;
  7  the sound bank every form of that fight has;
  8  the style's sound bank.
The PS2's parts 0-2 are the Xbox's bytes. The GameCube's are turned around here, checked against the 24 forms
both discs have (motions apart from the padding byte after a motion's bit stream, which neither sets) and
against the PS2's copy of Link's. Part 7 is the Xbox's own, from its guest's form; part 8 is
re-encoded like every other sound bank."""
import struct

import motion
import sound
from olnk import build_pack, pack_alignment, pack_parts

FIRST, SECOND = 7, 23          # /4: the first fight's forms, the second's
LINK_FORM, SPAWN_FORM, HEIHACHI_FORM = 9, 10, 11


def _swap16(out, at, count):
    for i in range(at, at + 2 * count, 2):
        out[i], out[i + 1] = out[i + 1], out[i]


def _swap32(out, at, count):
    for i in range(at, at + 4 * count, 4):
        out[i:i + 4] = out[i:i + 4][::-1]


def convert_names(gc):
    """Part 1, GameCube byte order to the Xbox's."""
    out = bytearray(gc)
    if len(gc) < 0x20:
        return bytes(out)
    table, = struct.unpack_from(">I", gc, 4)
    _swap32(out, 0, 4)
    first_name, = struct.unpack_from(">I", gc, 0x1c)
    for record in range(0x10, min(first_name, len(gc)) - 15, 16):
        _swap16(out, record, 2)
        _swap32(out, record + 4, 3)
    if table + 4 <= len(gc):
        names, = struct.unpack_from(">I", gc, table)
        _swap32(out, table, min(names, len(gc) - table) // 4)
    return bytes(out)


def convert_camera(gc):
    """Part 2, GameCube byte order to the Xbox's."""
    out = bytearray(gc)
    _swap32(out, 0, len(gc) // 4)
    if len(gc) >= 0x50:
        out[0x48:0x50] = gc[0x48:0x50]
        _swap16(out, 0x48, 4)
    return bytes(out)


def _build(parts, xbox_form):
    return build_pack(parts, alignment=pack_alignment(xbox_form))


def link_form(xbox_form, gc_form):
    """Link's form (the GameCube's /4/16 or /4/32) for the Xbox, beside the Xbox's own guest's form."""
    parts = pack_parts(gc_form, True)
    parts[0] = motion.convert_bank(parts[0])
    parts[1] = convert_names(parts[1])
    parts[2] = convert_camera(parts[2])
    parts[7] = pack_parts(xbox_form)[7]
    parts[8] = sound.convert_container(parts[8])
    return _build(parts, xbox_form)


def heihachi_form(xbox_form, ps2_form):
    """Heihachi's form (the PS2's /4/18 or /4/34) for the Xbox."""
    parts = pack_parts(ps2_form)
    parts[7] = pack_parts(xbox_form)[7]
    parts[8] = sound.convert_container(parts[8])
    return _build(parts, xbox_form)
