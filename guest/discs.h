/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The player's discs, read in place (never changed): an Xbox disc image (or
   the root.olk of an extracted one), a GameCube disc image (.iso/.gcm, or
   .ciso) and a PlayStation 2 disc image (.iso), each giving a reader of its
   root.olk. */
#ifndef SC2_GUEST_DISCS_H
#define SC2_GUEST_DISCS_H

#include "olnk.h"

#include <functional>
#include <memory>
#include <string>

namespace guest {

struct DiscFile {
    Olnk::Reader read;   /* offsets from the file's start */
    uint64_t size = 0;
    std::string game_id; /* the GameCube disc's six-letter ID, the PS2's program name, else empty */
    int version = -1;    /* the GameCube disc's version byte (0 for the retail USA disc), else -1 */
    /* The disc's program (the Xbox's Default.xbe, the GameCube's boot.dol, the PS2's SLUS_206.43; for a
       root.olk given itself, the file of that name beside it); throws Error
       when it cannot be read. */
    std::function<Bytes()> program;
    /* A track of the disc's music (bgm.afs, an AFS archive: 'AFS', u32 count, then u32 offset and size of each, from
       the archive's start) for the GameCube disc (and a root.olk given itself, its bgm.afs beside it), else empty;
       throws Error when it cannot be read. */
    std::function<Bytes(uint32_t)> music;
};

/* root.olk from an Xbox disc image (XDVDFS), or a root.olk file itself. */
DiscFile open_xbox_root(const std::string &path);
/* root.olk from a GameCube disc image (plain or CISO). */
DiscFile open_gamecube_root(const std::string &path);
/* ROOT.OLK from a PlayStation 2 disc image (ISO 9660), or the file itself;
   game_id is the program's name from SYSTEM.CNF (SLUS_206.43). */
DiscFile open_ps2_root(const std::string &path);
/* The guest's disc, whichever: ps2 says which it was (PS2: Heihachi; GameCube: Link). */
DiscFile open_other_root(const std::string &path, bool &ps2);

} // namespace guest

#endif
