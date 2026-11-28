/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Builds the root.olk with Link (from the player's GameCube disc) in the
   guest's place: the port of tools/guests/build_guest.py, which says what goes
   where. Reads the two discs only; writes the one output file. */
#ifndef SC2_GUEST_BUILD_H
#define SC2_GUEST_BUILD_H

#include "discs.h"

#include <cstdint>
#include <functional>
#include <string>

namespace guest {

/* progress(done, total) as the steps finish */
using Progress = std::function<void(int done, int total)>;

/* Throws guest::Error (with a reason for the player) on a disc it cannot use. */
void build_guest_root(const DiscFile &xbox, const DiscFile &gamecube, const std::string &output,
                      const Progress &progress = {});

/* The game ID the GameCube disc must have (Soulcalibur II, USA). */
constexpr const char *GAMECUBE_ID = "GRSEAF";
/* The retail disc's version byte and root.olk size: checked before building (root.olk), and named when a GameCube
   image fails to convert. A disc with that ID and another root.olk is another version, or an image a tool changed. */
constexpr int GAMECUBE_VERSION = 0;
constexpr uint64_t GAMECUBE_ROOT_SIZE = 587427840u;

/* The same with Heihachi, from the player's PlayStation 2 disc (the port of
   build_guest.py's build_heihachi). */
void build_heihachi_root(const DiscFile &xbox, const DiscFile &ps2, const std::string &output,
                         const Progress &progress = {});

/* Link (character 27) and Heihachi (28) beside Spawn, from whichever of the
   two discs are given (null for one not given): the port of
   tools/guests/roster.py. Its discs' programs are read too (DiscFile::program). */
void build_guests_root(const DiscFile &xbox, const DiscFile *gamecube, const DiscFile *ps2, const std::string &output,
                       const Progress &progress = {});

/* What the builders write, as a number: raise it whenever their output changes
   (anything in the guests' file), so a file built by an earlier release is
   noticed: the launcher asks for a rebuild, and online play compares it (two
   players need the same file). build_guests_root writes it beside the file, as
   <output>.version ("sc2-guests N"). */
constexpr int GUESTS_VERSION = 8;

/* The version a guests' file was built with: 0 when it has no stamp (built
   before stamps, or a build that did not finish). */
int guests_version(const std::string &output);

/* The title screen's pictures, /2/12 on (the same on every disc): the guests' file has the GameCube's and the PS2's
   after the disc's own /2 entries (TITLE_PICTURES each, the GameCube's first). */
constexpr uint32_t TITLE_FIRST = 12, TITLE_PICTURES = 3;
/* ... and after those, the Collection History's pictures of the guests' own items: Link's weapon demonstration and his
   two outfits (the GameCube's /2/344-346), Heihachi's demonstration (the PS2's /2/344). */
constexpr uint32_t EXTRA_PICTURES = 4;

/* The PS2 disc's program (Soulcalibur II, USA). */
constexpr const char *PS2_ID = "SLUS_206.43";
constexpr uint64_t PS2_ROOT_SIZE = 705277952;

} // namespace guest

#endif
