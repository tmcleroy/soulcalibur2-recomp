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

/* The Art Gallery's picture in a gallery's slot (0 Hi-Res, 1 Illustrations, 2 Special; up to 54 slots each): its /2
   entry in root.olk, as the program's table has it now (with the guests' after ApplyGuestRoster), or -1 for none. */
int32_t GalleryPicture(int gallery, int slot);

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
/* The final boss's form against a guest: its own disc's (Link 9, Heihachi 11) when the guests' file has it, or the
   guest slot's (10) */
uint32_t sc2_guest_boss_form(uint32_t character);
/* Nonzero when a guest stands in the select grid's fifth column. */
int sc2_guest_fifth_column(void);
/* Nonzero when the select grid is laid out in rows of five, Random alone in the sixth (sc2_guest.cpp, BalanceGrid). */
int sc2_select_grid_balanced(void);

/* A guest's weapon 2-11 as an item of its own (sc2_guest_items.c), or -1 */
int sc2_guest_weapon_item(uint32_t character, uint32_t weapon);
/* Every guest's weapon had (the menu's Unlock everything) */
void sc2_guest_items_all(void);
/* The guests' demonstrations and Link's outfits, items of their own (sc2_guest_items.c): their state's room, once the
   guests are loaded */
void sc2_guest_items_setup(void);
/* The guests' entries in the Records (sc2_records.c): their room, once the guests are loaded; and in the save, after
   the items' bytes (SC2_RECORDS_SAVE_BYTES there) */
void sc2_records_setup(void);
enum { SC2_RECORDS_SAVE_BYTES = 1 + 2 * 0x1C };
void sc2_records_save(uint8_t *at);
void sc2_records_load(const uint8_t *at);
/* The /2 entry of a guest's Collection History picture past the title pictures (0 Link's demonstration, 1 and 2 his
   outfits, 3 Heihachi's demonstration), or -1 in a guests' file built before them */
int sc2_guest_extra_picture(unsigned which);

/* The Art Gallery's Hi-Res pictures: 20, or more with the guests' (sc2_guest.cpp, ApplyGallery). */
void sc2_guest_gallery_open(uint32_t object, uint32_t gallery);
void sc2_guest_gallery_cells(uint32_t object);

#ifdef __cplusplus
}
#endif

#endif
