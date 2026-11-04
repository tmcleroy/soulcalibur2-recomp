/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Soulcalibur II game hooks: bindings that wrap a converted game function and
   then run it, unlike the library replacements. tools/build_game.py leaves
   the addresses in recomp_sc2_hooks_lookup_manual out of the lifter's
   --manual-functions, so the original bodies stay converted. */
#include "sc2_hooks.h"
#include "kernel_abi.h"
#include "program_manual.h"
#include "sc2_guest.h"
#include "scene_freeze_adapter.h"
#include "sc2_scene_probe.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint64_t xbox_ReadTimeStampCounter(void);
extern void sub_00023C30(void);
static void skin_snapshot(void)
{
    const uint32_t model = *recomp_memory_u32(recomp_runtime.registers.esp + 4u);
    sub_00023C30();
    sc2_scene_probe_skin(model);
}

extern void sub_00090280(void);
extern void sub_00044360(void);
extern void sub_0003BED0(void);
extern void sub_000502A0(void);
extern void sub_0002ABC0(void);
extern void sub_0006A190(void);
extern void sub_0006B000(void);
extern void sub_000331B0(void);
extern void sub_0002ED80(void);
extern void sub_0002EC90(void);
extern void sub_000569B0(void);
extern void sub_00032630(void);
extern void sub_000566F0(void);
extern void sub_000C4940(void);
extern void sub_00036A70(void);
extern void sub_00036A40(void);
extern void sub_000F1390(void);
extern void sub_000D24F0(void);
extern void sub_000D29A0(void);
extern void sub_000569F0(void);
extern void sub_000DCFA0(void);
extern void sub_000528E0(void);
extern void sub_0001C2B0(void);
extern void sub_00145A40(void);
/* rand (0x145A40). Online, the calls from 0x140730 and 0x141840 (sound and effects, run as often as the host's
   audio and timing have them, so not as often on both machines) draw from a state of their own, so that the
   game's numbers stay alike on both. */
static uint32_t own_rand_state = 1u;
static void rand_hook(void)
{
    const uint32_t from = *recomp_memory_u32(recomp_runtime.registers.esp);
    if (sc2_net_seeded && (from == 0x00141032u || from == 0x00141983u)) {
        own_rand_state = own_rand_state * 0x343fdu + 0x269ec3u;
        recomp_runtime.registers.eax = (own_rand_state >> 16) & 0x7fffu;
        recomp_runtime.registers.esp += 4u;
        return;
    }
    sub_00145A40();
}

void sc2_host_state_get(Sc2HostState *state)
{
    state->net_reads = sc2_net_reads;
    state->own_rand = own_rand_state;
}

void sc2_host_state_set(const Sc2HostState *state)
{
    sc2_net_reads = state->net_reads;
    own_rand_state = state->own_rand;
}

/* 0x44360(player), called by the fighter setup at 0x47A09 just after its
   per-character switch has set the fighter's weapon-bone modes (+0xb54, nine
   bytes; esi is the fighter). The guest's own modes go in before it runs
   (sc2_guest.cpp). */
static void weapon_bones_set(void)
{
    const uint32_t esp = recomp_runtime.registers.esp;
    if (*recomp_memory_u32(esp) == 0x00047a0eu)
        sc2_guest_weapon_bones(*recomp_memory_u32(esp + 4u), recomp_runtime.registers.esi);
    sub_00044360();
}

/* The movie scene's update (0x90280, thiscall). It asks its player for the
   status (vtable +0x20); 3, finished, sets the done flag at +0xfc, and with
   that flag set the update calls 0x90260(1) and the scene ends. --skip-intro
   sets the flag on the first update of the first movie scene after boot, the
   opening, so it ends the way a finished movie does. Later movies (attract,
   endings) play. The opening cannot be skipped with Start on this boot path
   (the game enumerates controllers only once its menus run), so a press seen
   by the host (sc2_menu.cpp) sets the flag too: any movie skips. */
static void movie_scene_update(void)
{
    static bool opening_done;
    const uint32_t scene = recomp_runtime.registers.ecx;
    if (recomp_skip_intro && !opening_done) {
        opening_done = true;
        *recomp_memory_u32(scene + 0xfcu) = 1u;
        fprintf(stderr, "recomp sc2: opening movie skipped\n");
    }
    /* (a press skips any movie, the opening's at boot too: sc2_menu.cpp) */
    if (sc2_movie_skip) {
        sc2_movie_skip = 0;
        *recomp_memory_u32(scene + 0xfcu) = 1u;
        fprintf(stderr, "recomp sc2: movie skipped\n");
    }
    sub_00090280();
}

/* 0x3BED0, the fight setup (cdecl): mode, mode, kind, then per player id,
   costume, flag, then the stage. The attract demo (0x4E840) calls it from
   0x4E8DF with two fighters of its own choosing and the first one's home
   stage. RECOMP_QUICK_MATCH="P1:C1:P2:C2[:STAGE]" (runner --quick-match)
   replaces those, so a test reaches a chosen fight on the title screen's
   first demo: character ids as in the select grid's cells (Link 27,
   Heihachi 28, Spawn 29), costumes from 0. */
static void fight_setup(void)
{
    const uint32_t esp = recomp_runtime.registers.esp;
    const char *quick = getenv("RECOMP_QUICK_MATCH");
    if (quick && *recomp_memory_u32(esp) == 0x0004e8e4u) {
        int v[5] = {-1, -1, -1, -1, -1};
        const int n = sscanf(quick, "%d:%d:%d:%d:%d", &v[0], &v[1], &v[2], &v[3], &v[4]);
        if (n >= 4) {
            *recomp_memory_u32(esp + 0x10u) = (uint32_t)v[0];
            *recomp_memory_u32(esp + 0x14u) = (uint32_t)v[1];
            *recomp_memory_u32(esp + 0x1cu) = (uint32_t)v[2];
            *recomp_memory_u32(esp + 0x20u) = (uint32_t)v[3];
            if (n == 5) *recomp_memory_u32(esp + 0x28u) = (uint32_t)v[4];
            fprintf(stderr, "recomp sc2: quick match %d/%d vs %d/%d on stage %d\n", v[0], v[1], v[2], v[3],
                    (int)*recomp_memory_u32(esp + 0x28u));
        }
    }
    sub_0003BED0();
}

uint32_t sc2_scene;
uint32_t sc2_scene_switches;
int sc2_movie_skip;

/* 0x502A0(scene, fade): asks for the next scene ([0x398704]); the main loop
   switches to it once the fade is done. */
static void scene_switch(void)
{
    const uint32_t esp = recomp_runtime.registers.esp;
    sc2_scene = *recomp_memory_u32(esp + 4u);
    ++sc2_scene_switches;
    fprintf(stderr, "recomp sc2: scene %u\n", sc2_scene);
    sub_000502A0();
}

/* 0x2ABC0(sprite), the menus' sprite draw. A sprite: centre x, y (+0, +4),
   texture coordinates of its four corners (+0x10..+0x2c: top left, top right,
   bottom right, bottom left) and half its size (+0x30, +0x34), in 640x448
   units. The character select's grid panel (centre 320,224, 336x448, a 1:1
   piece of a 512x512 texture) frames four columns; with a guest
   in the fifth column it is drawn as three pieces: up to the middle of the
   fourth column as it is, one column's slice of it again, and the rest one
   column further right. */
static bool select_panel(uint32_t sprite)
{
    const float *f = (const float *)recomp_memory(sprite, 0x38u);
    return sc2_scene == 6u && f[0] == 320.0f && f[1] == 224.0f && f[12] == 168.0f && f[13] == 224.0f &&
           f[4] == 0.0f && f[6] == 0.65625f;
}

static void sprite_piece(uint32_t sprite, float left, float right, float shift)
{
    float *f = (float *)recomp_memory(sprite, 0x38u);
    const float u0 = (left - 152.0f) / 512.0f, u1 = (right - 152.0f) / 512.0f;
    f[0] = (left + right) * 0.5f + shift;
    f[12] = (right - left) * 0.5f;
    f[4] = f[10] = u0;
    f[6] = f[8] = u1;
    kernel_call_guest(0x0002abc0u, &sprite, 1u);
}

static void sprite_draw(void)
{
    static bool inside;
    const uint32_t sprite = *recomp_memory_u32(recomp_runtime.registers.esp + 4u);
    /* (the free camera's clean view leaves out the menus' sprites too: the Museum's profile viewer draws its caption
       strip with them, outside the 2D layer) */
    if (!inside && recomp_scene_freeze_overlay_hidden()) {
        recomp_runtime.registers.esp += 4u;   /* the original's ret */
        return;
    }
    if (inside || !sc2_guest_fifth_column() || !select_panel(sprite)) {
        sub_0002ABC0();
        return;
    }
    /* one grid column on screen (the grid and the panel scale differently),
       cut at the fourth column's middle */
    const float column = 64.0f, cut = 407.84f;
    float saved[14];
    memcpy(saved, recomp_memory(sprite, sizeof saved), sizeof saved);
    inside = true;
    const float left = -column * 0.5f;   /* (the whole grid half a column left, centred again) */
    sprite_piece(sprite, 152.0f, cut, left);
    sprite_piece(sprite, cut - column, cut, left + column);
    sprite_piece(sprite, cut, 488.0f, left + column);
    inside = false;
    memcpy(recomp_memory(sprite, sizeof saved), saved, sizeof saved);
    recomp_runtime.registers.esp += 4u;   /* the original's ret */
}

/* 0x6A190(position, cell): a select cell's place, x = its column x a column's width (0x17774C) plus the grid's
   left; y by its row. With a guest in the fifth column the grid is half a column wider on the right than the
   panel the game centres, so every cell goes half a column left (the panel too, sprite_draw): the grid centred,
   clear of player 2's model. The cursor and the 1P/2P marks follow the cells. */
static void cell_place(void)
{
    const uint32_t position = *recomp_memory_u32(recomp_runtime.registers.esp + 4u);
    const uint32_t cell = *recomp_memory_u32(recomp_runtime.registers.esp + 8u);
    sub_0006A190();
    if (!sc2_guest_fifth_column()) return;
    float *f = (float *)recomp_memory(position, 4u), step;
    memcpy(&step, recomp_memory(0x0017774Cu, 4u), 4u);
    *f -= step * 0.5f;
    /* (with the grid in rows of five, Random, cell 26 in the second column, stands alone under the middle one) */
    if (cell == 26u && sc2_select_grid_balanced()) *f += step;
}

/* 0x6B000(player, pressed), a select cursor's move (thiscall: the cursor's cell at +8, the cells it may take +4 to
   +6, live while +0x14). Up and Down go five cells (a column), Left and Right one (a row), each wrapping and passing
   over empty and locked cells. With the grid in rows of five (sc2_guest.cpp, BalanceGrid), Random (cell 26) stands
   alone under the middle column: Down from the lowest character a column has, or Up from the highest, goes to it,
   and from it Up and Down go into the middle column, as on the screen. The cell changes through 0x6B380 (the cells'
   highlights with it), and the original then runs without the vertical press (Left and Right, the cursor's place,
   and its return, by which the caller sounds the move). */
enum { CELL_RANDOM = 26, PAD_UP = 0x2000u, PAD_DOWN = 0x1000u };

static bool cell_open(int cell)
{
    const uint32_t at = *recomp_memory_u32(0x003CA00Cu) + 36u * (uint32_t)cell;
    const int16_t character = *(const int16_t *)recomp_memory(at, 2u);
    const uint32_t check = *recomp_memory_u32(at + 4u);
    if (character == -1) return false;
    if (!check) return true;
    const RecompRegisters saved = recomp_runtime.registers;
    const uint32_t args[2] = {(uint32_t)(int32_t)character, 0u};
    kernel_call_guest(check, args, 2u);
    const bool open = (recomp_runtime.registers.eax & 0xffu) != 0u;
    recomp_runtime.registers = saved;
    return open;
}

/* The first open cell from `cell` on in steps of `step` while in the five rows, or -1 */
static int open_in_column(int cell, int step)
{
    for (; cell >= 0 && cell < 25; cell += step)
        if (cell_open(cell)) return cell;
    return -1;
}

static void cursor_move(void)
{
    const uint32_t esp = recomp_runtime.registers.esp, cursor = recomp_runtime.registers.ecx;
    const uint32_t player = *recomp_memory_u32(esp + 4u), pressed = *recomp_memory_u32(esp + 8u);
    const int16_t *c = (const int16_t *)recomp_memory(cursor + 4u, 0x12u);   /* +4 first, +6 last, +8 cell, +0x14 live */
    if (!sc2_select_grid_balanced() || !c[8] || c[0] != 0 || c[1] < 29 || !(pressed & (PAD_UP | PAD_DOWN))) {
        sub_0006B000();
        return;
    }
    const int cell = c[2];
    int to = -1;
    if (pressed & PAD_UP)
        to = cell == CELL_RANDOM ? open_in_column(22, -5) : open_in_column(cell - 5, -5) < 0 ? CELL_RANDOM : -1;
    else
        to = cell == CELL_RANDOM ? open_in_column(2, 5) : open_in_column(cell + 5, 5) < 0 ? CELL_RANDOM : -1;
    if (to < 0 || to == cell) {
        sub_0006B000();
        return;
    }
    const RecompRegisters saved = recomp_runtime.registers;
    const uint32_t args[2] = {player, (uint32_t)to};
    kernel_call_guest(0x0006B380u, args, 2u);   /* (thiscall: ECX the cursor, as on entry) */
    recomp_runtime.registers = saved;
    *recomp_memory_u32(esp + 8u) = pressed & ~(uint32_t)(PAD_UP | PAD_DOWN);
    sub_0006B000();
}

uint32_t sc2_net_seed;
int sc2_net_seeded;
int sc2_net_live;
uint64_t sc2_net_reads;

/* QueryPerformanceCounter (0x103A75, stdcall, the timestamp counter at 733 MHz). Online, from the match's start
   (sc2_net_live), the game's clock moves on a 60th of a second for each read of the controls, alike on both
   machines: the game times its updates by it (each object's step, in frames). After a match it goes on from there. */
static void query_performance_counter(void)
{
    static uint64_t base, last;
    static int64_t after; /* (after a match: the clock goes on from where the match's left it) */
    const uint32_t out = *recomp_memory_u32(recomp_runtime.registers.esp + 4u);
    uint64_t now;
    if (sc2_net_live) {
        if (!base) base = xbox_ReadTimeStampCounter();
        now = base + sc2_net_reads * (733333333ull / 60u);
        last = now;
    } else {
        if (last) { after = (int64_t)(last - xbox_ReadTimeStampCounter()); last = 0; }
        now = xbox_ReadTimeStampCounter() + (uint64_t)after;
    }
    *recomp_memory_u32(out) = (uint32_t)now;
    *recomp_memory_u32(out + 4u) = (uint32_t)(now >> 32);
    recomp_runtime.registers.eax = 1u;
    recomp_runtime.registers.esp += 8u;
}

/* 0x1C2B0(seed): seeds rand (0x145A36, with seed >> 4) and draws seed & 0xfff numbers from it; the game calls it
   once, with a clock (0x103A75), and seeds its Mersenne Twister from rand. Online, both games use the host's seed. */
static void random_seed(void)
{
    if (sc2_net_seeded) {
        *recomp_memory_u32(recomp_runtime.registers.esp + 4u) = sc2_net_seed;
        fprintf(stderr, "recomp sc2: the match's seed %08x\n", sc2_net_seed);
    }
    sub_0001C2B0();
}

/* 0x528E0(id, opponent): which of the final boss's forms (Inferno takes a fighting style from the one it fights:
   its /4 entry 7 + the form, or 23 + it) goes with the opponent's character, by a table of IDs 1-29 (0x52978).
   The guests beside Spawn (27, 28) are not in it and get form 0, whose entries are empty on the disc: loading one
   is the game's own endless loop (0x20ED0), before the final stage of Arcade. They take the guest slot's (Spawn's,
   form 10), as Spawn in their place would. */
static void boss_form(void)
{
    const int32_t opponent = (int32_t)*recomp_memory_u32(recomp_runtime.registers.esp + 8u);
    if (opponent == 27 || opponent == 28) {
        recomp_runtime.registers.eax = 10u;
        recomp_runtime.registers.esp += 4u;
        return;
    }
    sub_000528E0();
}

/* 0x566F0 (character in esi, weapon in edi): the item that unlocks a character's weapon 2-11 (86 + 10 x its
   place in the 24-character roster, 0x36A70, + the weapon). The guests beside Spawn have no place there, so
   theirs came out as items 78-87, which are other things. They take the guest slot's, Spawn's: Link's and
   Heihachi's weapons are had as Spawn's are, as each had them on their own console. */
static void weapon_item(void)
{
    const uint32_t character = recomp_runtime.registers.esi;
    if (character == 27u || character == 28u) recomp_runtime.registers.esi = 29u;
    sub_000566F0();
    recomp_runtime.registers.esi = character;
}

/* 0xC4940(records, slot, character), thiscall: counts a play of the character in the records (the count at +8 + its
   roster place x 0x1C, the place also kept at +0x2B8 + slot x 4). The guests have no place (-1), so the count it
   raised was a dword 0x14 bytes before the records. Their plays count as Spawn's, the guest slot's. */
static void play_counted(void)
{
    uint32_t *character = recomp_memory_u32(recomp_runtime.registers.esp + 8u);
    if (*character == 27u || *character == 28u) *character = 29u;
    sub_000C4940();
}

/* 0x569F0 (character in eax): the character's own item (its roster place + 0x116), which the game checks
   (0x56690) and sets (0x57680). With no place the guests had item 277, another's; they take Spawn's. */
static void character_item(void)
{
    if (recomp_runtime.registers.eax == 27u || recomp_runtime.registers.eax == 28u) recomp_runtime.registers.eax = 29u;
    sub_000569F0();
}

/* The guests' places beside the 24-character roster (0x19CA20): Link 24, Heihachi 25. 0x36A70(character) gives a
   character's place, -1 for the guests before; 0x36A40(place) the character at a place (above 23: the 24th's). The
   records keep a character as its place in a byte (0xC53A0, 0xC5A20, 0xC5FF0: Arcade, Survival and the others, 0xFF
   for none), so with places of their own a guest's records name them. Where a place indexes a 24-entry array the
   guests keep Spawn's (play counts 0xC4940, own item 0x569F0, weapons 0x566F0, above) or none (0xF1390, below). */
enum { GUEST_PLACE = 24u };
static void roster_place(void)
{
    const uint32_t character = *recomp_memory_u32(recomp_runtime.registers.esp + 4u);
    if ((character == 27u || character == 28u) && sc2_guest_loaded(character)) {
        recomp_runtime.registers.eax = GUEST_PLACE + (character - 27u);
        recomp_runtime.registers.esp += 4u;
        return;
    }
    sub_00036A70();
}

static void roster_character(void)
{
    const uint32_t place = *recomp_memory_u32(recomp_runtime.registers.esp + 4u);
    if (place == GUEST_PLACE || place == GUEST_PLACE + 1u) {
        recomp_runtime.registers.eax = 27u + (place - GUEST_PLACE);
        recomp_runtime.registers.esp += 4u;
        return;
    }
    sub_00036A40();
}

/* 0xF1390(character): a float of the character's from a 24-entry table (0x604B70) by its place, or 0 with none.
   The guests' places are past it: they have none, as before. */
static void character_float(void)
{
    uint32_t *character = recomp_memory_u32(recomp_runtime.registers.esp + 4u);
    const uint32_t was = *character;
    if (was == 27u || was == 28u) *character = 0u;
    sub_000F1390();
    *character = was;
}

/* 0xD24F0(character, ...): the result screens' (Survival, Time Attack and the others) caption, the character's
   name from a table of 24 (0x34A9E0: an ID byte, then the name, 0x22 bytes each). The guests are not in it, so their
   name was whatever the buffer held before (RAPHAEL). For a guest, Spawn's row (14) holds the guest's while it runs. */
static void result_name(void)
{
    const uint32_t character = *recomp_memory_u32(recomp_runtime.registers.esp + 4u);
    if ((character != 27u && character != 28u) || !sc2_guest_loaded(character)) {
        sub_000D24F0();
        return;
    }
    uint8_t *row = recomp_memory(0x34A9E0u + 14u * 0x22u, 0x22u), kept[0x22];
    memcpy(kept, row, sizeof kept);
    memset(row, 0, sizeof kept);
    row[0] = (uint8_t)character;
    memcpy(row + 1, character == 27u ? "LINK" : "HEIHACHI", character == 27u ? 4u : 8u);
    sub_000D24F0();
    memcpy(recomp_memory(0x34A9E0u + 14u * 0x22u, 0x22u), kept, sizeof kept);
}

/* 0xD29A0: a result screen's characters beaten in the run, those whose count (0x398B8C, a word by ID, raised as
   each falls) is 1 or more, in the order of the name table's second copy (0x34AD10, 24 rows of 0x22 bytes). The
   guests have counts but no rows: each beaten guest takes, while it runs, the row of a character beaten 0 times
   (shown by nobody), the table given back after. */
static void beaten_list(void)
{
    enum { ROWS = 0x34AD10u, ROW = 0x22u, COUNTS = 0x398B8Cu };
    uint32_t lent[2] = {0u, 0u};
    uint8_t kept[2] = {0u, 0u};
    unsigned lending = 0;
    for (uint32_t guest = 27u; guest <= 28u; ++guest) {
        if (!sc2_guest_loaded(guest) || *(uint16_t *)(void *)recomp_memory(COUNTS + guest * 2u, 2u) == 0u) continue;
        for (int row = 23; row >= 0; --row) {
            const uint32_t at = ROWS + (uint32_t)row * ROW;
            const uint8_t id = *recomp_memory(at, 1u);
            if (id == 27u || id == 28u || *(uint16_t *)(void *)recomp_memory(COUNTS + id * 2u, 2u) != 0u) continue;
            lent[lending] = at;
            kept[lending] = id;
            *recomp_memory(at, 1u) = (uint8_t)guest;
            ++lending;
            break;
        }
    }
    sub_000D29A0();
    for (unsigned i = 0; i < lending; ++i) *recomp_memory(lent[i], 1u) = kept[i];
}

/* 0xDCFA0(gallery object, gallery): sets the Art Gallery's grid, columns at +0x7e and rows at +0x7d (Hi-Res 5 x 4,
   Illustrations 9 x 5, Special 5 x 2) and their product at +0x7c. With the guests' pictures Hi-Res has more than
   20 (sc2_guest.cpp, ApplyGallery), and a fifth row. */
static void gallery_grid(void)
{
    const uint32_t object = *recomp_memory_u32(recomp_runtime.registers.esp + 4u);
    const uint32_t gallery = *recomp_memory_u32(recomp_runtime.registers.esp + 8u);
    sub_000DCFA0();
    if (gallery == 0u && sc2_guest_hires_pictures() > 20) {
        *recomp_memory(object + 0x7du, 1u) = 5u;
        *recomp_memory(object + 0x7cu, 1u) = 25u;
    }
}

/* 0x331B0 draws the frame's 2D layer (the life bars, timer, names, round and stage text, menus) from a list, each
   item through its table entry (0x19BDC0); the free camera's clean view (H or the black button) leaves it out. */
static void layer_2d(void)
{
    sc2_training_hit_boxes(); /* (the 3D scene drawn: training's hit boxes as it shows them) */
    if (recomp_scene_freeze_overlay_hidden()) {
        recomp_runtime.registers.esp += 4u;   /* the original's ret */
        return;
    }
    sub_000331B0();
}

/* 0x569B0, a character's profile item (EAX the character, in a table at 0x1A2300 that has none for 27 and 28):
   the guests' are 48 and 49 (sc2_guest.cpp), so that clearing Arcade with one unlocks their profile. */
static void profile_item(void)
{
    const int item = sc2_guest_profile_item(recomp_runtime.registers.eax);
    if (item >= 0) {
        recomp_runtime.registers.eax = (uint32_t)item;
        recomp_runtime.registers.esp += 4u;   /* the original's ret */
        return;
    }
    sub_000569B0();
}

/* The menus' boxes, drawn outside the 2D layer: 0x2ED80 their fill and 0x2EC90 their frame (the caption strip of
   the Museum's profile viewer, the character select's name box). The free camera's clean view leaves them out too. */
static void box_fill(void)
{
    if (recomp_scene_freeze_overlay_hidden()) {
        recomp_runtime.registers.esp += 4u;   /* the original's ret */
        return;
    }
    sub_0002ED80();
}

static void box_frame(void)
{
    if (recomp_scene_freeze_overlay_hidden()) {
        recomp_runtime.registers.esp += 4u;   /* the original's ret */
        return;
    }
    sub_0002EC90();
}

/* 0x32630, the frame routine's (0x2DD40) first call: the start of a frame (sc2_frame.cpp) */
static void frame_begins(void)
{
    sc2_frame_begins();
    sub_00032630();
}

/* Weapon Master's character select with the guests' row (sc2_wm_select.c) */
RecompFunction sc2_wm_select_lookup(uint32_t address);

RecompFunction recomp_sc2_hooks_lookup_manual(uint32_t address)
{
    if (address == 0x00023c30u) return skin_snapshot;
    switch (address) {
    case 0x00090280u: return movie_scene_update;
    case 0x00044360u: return weapon_bones_set;
    case 0x0003bed0u: return fight_setup;
    case 0x000502a0u: return scene_switch;
    case 0x0002abc0u: return sprite_draw;
    case 0x0006a190u: return cell_place;
    case 0x0006b000u: return cursor_move;
    case 0x000566f0u: return weapon_item;
    case 0x000c4940u: return play_counted;
    case 0x00036a70u: return roster_place;
    case 0x00036a40u: return roster_character;
    case 0x000f1390u: return character_float;
    case 0x000d24f0u: return result_name;
    case 0x000d29a0u: return beaten_list;
    case 0x000569f0u: return character_item;
    case 0x000dcfa0u: return gallery_grid;
    case 0x000528e0u: return boss_form;
    case 0x0001c2b0u: return random_seed;
    case 0x00103a75u: return query_performance_counter;
    case 0x00145a40u: return rand_hook;
    case 0x000331b0u: return layer_2d;
    case 0x0002ed80u: return box_fill;
    case 0x000569b0u: return profile_item;
    case 0x0002ec90u: return box_frame;
    case 0x00032630u: return frame_begins;
    default: return sc2_wm_select_lookup(address);
    }
}
