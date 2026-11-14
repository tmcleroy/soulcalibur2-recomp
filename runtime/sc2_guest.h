/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The guest character: Spawn (the Xbox's), Link (rebuilt from the player's
   GameCube disc) or Heihachi (from the PlayStation 2 disc), by
   tools/guests/build_guest.py into a root.olk the game reads in place of the
   disc's. */
#ifndef SC2_GUEST_H
#define SC2_GUEST_H

#include <stdint.h>

#ifdef __cplusplus
namespace sc2 {

/* Link's names in place of Spawn's in the loaded program (profile, select
   screens); call once the XBE is in memory. */
void ApplyGuestLink();

/* Heihachi's, the same way. */
void ApplyGuestHeihachi();

/* The guests beside Spawn: `path` is a root.olk built by tools/guests/roster.py
   (Link as character 27, Heihachi as 28, each when its disc was given). Their
   blocks, profiles, availability and select-grid cells go into the program's
   tables; returns false (and changes nothing) when the file is not one. Call
   once the XBE is in memory. */
bool ApplyGuestRoster(const char *path);

/* Whether the guests' file at `path` was built from this program's release's disc (US or European: their menus are
   numbered otherwise, guest/build.cpp's Layout). Says why on stderr when not; the game then reads the disc's own
   root.olk, without the guests. */
bool GuestFileMatches(const char *path);

} // namespace sc2

extern "C" {
#endif

/* The guest's weapon-bone modes on the fighter (player at `player`, fighter
   object at `fighter`), where its own program sets them differently from the
   guest slot's (Spawn's): called by the hook in sc2_hooks.c. */
void sc2_guest_weapon_bones(uint32_t player, uint32_t fighter);

/* Nonzero when the guests' file has this guest (Link 27, Heihachi 28). */
int sc2_guest_loaded(uint32_t character);
/* A guest's profile item (48 Link, 49 Heihachi), or -1 (no guest, or a guests' file without its unlock's picture) */
int sc2_guest_profile_item(uint32_t character);
/* Nonzero when a guest stands in the select grid's fifth column. */
int sc2_guest_fifth_column(void);
/* Nonzero when the select grid is laid out in rows of five, Random alone in the sixth (sc2_guest.cpp, BalanceGrid). */
int sc2_select_grid_balanced(void);

/* The Art Gallery's Hi-Res pictures: 20, or more with the guests' (sc2_guest.cpp, ApplyGallery). */
int sc2_guest_hires_pictures(void);

#ifdef __cplusplus
}
#endif

#endif
