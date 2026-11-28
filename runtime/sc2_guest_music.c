/* The Legend of Zelda's theme for Link, as the GameCube plays it.

   0xF0360(request) starts the music: the request's low byte (1 to 16: a stage's music, else drawn), 4 the destined
   battle's (its track by the final boss's form for the pair, 0x528E0: 0x37-0x39, or 0x1E), 0x8026 and others their
   own. It plays the track it picks from bgm.afs with ADXT_StartAfs (0x10C860, on the handle at 0x604630).
   The GameCube's (0x8007E660) is the same, then, before it plays, puts its track 66 (the theme) in the track's place
   when Link fights, by the mode ([0x398940 + 0x1E], or 3 when [+0x12] is set; the players at [+0xC4] and [+0xC8],
   the second the opponent in the one-player modes, their characters at +0x62):
     Arcade (1): the opponent is Link in a destined battle (0x52E70 here, 0x800B5CC8 there);
     modes 2 (Time Attack), 6 (Survival), 7 and 14: the opponent is Link and the request's low byte is 1;
     modes 3, 8, 10 and 12: either player is Link and the request's low byte is 1;
     the Exhibition Theater (9) and Character Profiles (15): either fighter is Link, whatever the request.
   The Xbox's bgm.afs has no such track (its tracks from 65 on are the GameCube's from 67), and its destined battle
   for Link's form (9) takes the default, 0x1E. The guests' file has the GameCube's track (guest/build.h,
   LINK_THEME_TRACK); here the same choice plays it from guest memory (ADXT_StartMem2, 0x10C930) where the
   GameCube's plays its track, the rest as the original does. */
#include "sc2_hooks.h"
#include "sc2_guest.h"
#include "kernel_abi.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

extern void sub_000F0360(void);
extern uint32_t recomp_kernel_allocate_pool(uint32_t size);

enum {
    STATE = 0x00398940u,     /* the mode at +0x1E, the players at +0xC4 and +0xC8 */
    ADXT_HANDLE = 0x00604630u,
    LINK = 27u,
};

static uint32_t theme_at, theme_size;   /* the theme's copy in guest memory, once it has played */

static uint32_t call_guest(uint32_t address, const uint32_t *args, unsigned count)
{
    const RecompRegisters saved = recomp_runtime.registers;
    kernel_call_guest(address, args, count);
    const uint32_t result = recomp_runtime.registers.eax;
    recomp_runtime.registers = saved;
    return result;
}

static bool is_link(uint32_t player_slot)
{
    const uint32_t player = *recomp_memory_u32(STATE + player_slot);
    return player != 0u && *(const int16_t *)(const void *)recomp_memory(player + 0x62u, 2u) == (int16_t)LINK;
}

static bool theme_wanted(uint32_t request)
{
    if (!sc2_guest_loaded(LINK)) return false;
    uint32_t mode = (uint32_t)(int32_t) * (const int16_t *)(const void *)recomp_memory(STATE + 0x1Eu, 2u);
    if (*recomp_memory(STATE + 0x12u, 1u) != 0u) mode = 3u;
    const bool first = (request & 0xFFu) == 1u;
    const bool opponent = is_link(0xC8u), either = opponent || is_link(0xC4u);
    switch (mode) {
    case 1: return opponent && call_guest(0x52E70u, NULL, 0u) != 0u;
    case 2: case 6: case 7: case 14: return opponent && first;
    case 3: case 8: case 10: case 12: return either && first;
    case 9: case 15: return either;
    default: return false;
    }
}

static void music_start(void)
{
    const uint32_t request = *recomp_memory_u32(recomp_runtime.registers.esp + 4u);
    uint32_t size = 0u;
    const uint8_t *theme = theme_wanted(request) ? sc2_guest_link_theme(&size) : NULL;
    if (theme == NULL) {
        sub_000F0360();
        return;
    }
    if (theme_at == 0u) {
        theme_at = recomp_kernel_allocate_pool(size);
        if (theme_at == 0u) {
            fprintf(stderr, "recomp sc2: no room for Link's theme (%u bytes)\n", size);
            sub_000F0360();
            return;
        }
        memcpy(recomp_memory(theme_at, size), theme, size);
        theme_size = size;
    }
    fprintf(stderr, "recomp sc2: music request %#x: Link's theme\n", request);
    const uint32_t handle = *recomp_memory_u32(ADXT_HANDLE);
    const uint32_t stop[] = {handle, 1u}, start[] = {handle, theme_at, theme_size}, fade[] = {2u, 0x3F800000u};
    call_guest(0x10AF90u, stop, 2u);
    call_guest(0x10C930u, start, 3u);
    *recomp_memory_u32(0x0055C1DCu) = 2u;
    call_guest(0xEE510u, fade, 2u);
    recomp_runtime.registers.esp += 4u;   /* (the original's ret) */
}

RecompFunction sc2_guest_music_lookup(uint32_t address)
{
    return address == 0x000f0360u ? music_start : NULL;
}
