/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Soulcalibur II game hooks: bindings that wrap a converted game function and
   then run it, unlike the library replacements. tools/build_game.py leaves
   the addresses in recomp_sc2_hooks_lookup_manual out of the lifter's
   --manual-functions, so the original bodies stay converted. */
#include "sc2_hooks.h"
#include "kernel_abi.h"
#include "program_manual.h"
#include "sc2_guest.h"
#include "sc2_version.h"
#include "scene_freeze_adapter.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint64_t xbox_ReadTimeStampCounter(void);

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
extern void sub_000344D0(void);
extern void sub_0002A350(void);
extern void sub_000DE480(void);
extern void sub_00056A00(void);
extern void sub_00055010(void);
extern void sub_00046550(void);
extern void sub_00146610(void);
extern void sub_0006A1F0(void);
/* memmove (0x146610; memcpy calls it too): the C library's, copied natively. Its last bytes go through small jump
   tables, and on the European disc (0x14B810) the lift lost the arm for none left (the table at 0x14BAA8 reads from
   -7 to 0 and slot 0 was not found), so a copy ending on a whole dword stopped the game ("unresolved indirect call
   to 0x0014baef", in Arcade and Survival as fighters attacked). */
static void memory_move(void)
{
    const uint32_t esp = recomp_runtime.registers.esp;
    const uint32_t to = *recomp_memory_u32(esp + 4u), from = *recomp_memory_u32(esp + 8u),
                   count = *recomp_memory_u32(esp + 12u);
    uint8_t *const out = recomp_memory_ram(to, count), *const in = recomp_memory_ram(from, count);
    if (count != 0u && (out == NULL || in == NULL)) {
        sub_00146610();   /* (memory the runtime watches: the game's own copy) */
        return;
    }
    if (count != 0u) memmove(out, in, count);
    recomp_runtime.registers.eax = to;
    recomp_runtime.registers.esp += 4u;   /* the original's ret (cdecl: the caller pops the arguments) */
}
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
    /* The Arcade ending (scene 13, after the last fight) for Lizardman and Assassin, who have none: as on the other
       consoles, it is left out, and the game goes on to the scene the ending ends with (15, 0x4F160). (The
       ending scene's mode 16 plays it over and over; there arcade_ending, below, ends it at once.) */
    if (*recomp_memory_u32(esp + 4u) == 13u && *(const uint16_t *)recomp_memory(0x0039895Eu, 2u) != 16u) {
        const uint32_t record = *recomp_memory_u32(0x00398A08u);
        const int16_t character = record ? *(const int16_t *)recomp_memory(record + 0x58u, 2u) : 1;
        if (character == 30 || character == 31) *recomp_memory_u32(esp + 4u) = 15u;
    }
    sc2_scene = *recomp_memory_u32(esp + 4u);
    ++sc2_scene_switches;
    fprintf(stderr, "recomp sc2: scene %u\n", sc2_scene);
    sub_000502A0();
}

/* 0x2ABC0(sprite), the menus' sprite draw. */
static void sprite_draw(void)
{
    const uint32_t sprite = *recomp_memory_u32(recomp_runtime.registers.esp + 4u);
    /* (the free camera's clean view leaves out the menus' sprites too: the Museum's profile viewer draws its caption
       strip with them, outside the 2D layer) */
    if (recomp_scene_freeze_overlay_hidden()) {
        recomp_runtime.registers.esp += 4u;   /* the original's ret */
        return;
    }
    sub_0002ABC0();
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

/* 0x6A1F0, a select cell's setup (thiscall: ECX the cell, +0x90 its row in the grid [0x3CA00C], 36 bytes a cell,
   the character at +0). A character of -1 is an empty cell: not drawn, and the cursors pass over it. The Museum's
   Character Profiles (mode 15) and Character Ending (mode 16) have nothing for the bonus fighters (Lizardman,
   Assassin, Berserker: 30-32), and showed them greyed, as if still to be unlocked; there their cells are empty, as
   on the other consoles, and are given back for every other select (museum_grid, from here and each frame). */
static struct { int cell; int16_t character; } museum_hidden[3];
static int museum_hidden_count;

static void museum_grid(bool hide)
{
    const uint32_t grid = *recomp_memory_u32(0x003CA00Cu);
    if (!grid) return;
    if (!hide) {
        for (int k = 0; k < museum_hidden_count; ++k)
            *(int16_t *)recomp_memory(grid + 36u * (uint32_t)museum_hidden[k].cell, 2u) = museum_hidden[k].character;
        museum_hidden_count = 0;
        return;
    }
    for (int cell = 0; cell < 30 && museum_hidden_count < 3; ++cell) {
        int16_t *character = (int16_t *)recomp_memory(grid + 36u * (uint32_t)cell, 2u);
        if (*character < 30 || *character > 32) continue;
        museum_hidden[museum_hidden_count].cell = cell;
        museum_hidden[museum_hidden_count++].character = *character;
        *character = -1;
    }
}

static bool museum_select_mode(void)
{
    const uint16_t mode = *(const uint16_t *)recomp_memory(0x0039895Eu, 2u);
    return mode == 15u || mode == 16u;
}

static void cell_setup(void)
{
    museum_grid(museum_select_mode());
    sub_0006A1F0();
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
   The guests beside Spawn (27, 28) and bonus fighters (30..32) are not in it and get form 0, whose entries are empty
   on the disc: loading one is the game's own endless loop (0x20ED0), before the final stage of Arcade. Each
   version's program has a form for its own guest alone: 10 for Spawn here, 9 for Link on the GameCube, 11 for
   Heihachi on the PS2. The guests' file brings those two from their discs (tools/guests/forms.py) and Link and
   Heihachi take them (issue #90); with a file built before that, and for the bonus fighters, the guest slot's
   (Spawn's, form 10), as Spawn in their place would. */
static void boss_form(void)
{
    const int32_t opponent = (int32_t)*recomp_memory_u32(recomp_runtime.registers.esp + 8u);
    if (opponent == 27 || opponent == 28 || (opponent >= 30 && opponent <= 32)) {
        recomp_runtime.registers.eax = sc2_guest_boss_form((uint32_t)opponent);
        recomp_runtime.registers.esp += 4u;
        return;
    }
    sub_000528E0();
}

/* 0x55010(...): sets up a fight's second fighter, among them the final boss's fighting style. Against Inferno
   (character 19) the style is its opponent's, except for the characters that came from elsewhere, whose moves
   Inferno does not take: those it fights with Nightmare's (17). The test (0x55180) names Spawn and Necrid, with
   Inferno and Charade (0x46600) and the bonus fighters (0x46550, IDs 30 and over). Each version names its own guest
   there: the GameCube's program Link (27, 0x800B4A60), the PS2's Heihachi (28, 0x15F908), so on their consoles
   Inferno meets them with Nightmare's moves the first time (issue #90). Here, while 0x55010 runs, 0x46550 answers
   yes for both: it is called once in it, for that test. */
static int inferno_style_test;
static void second_fighter(void)
{
    ++inferno_style_test;
    sub_00055010();
    --inferno_style_test;
}

/* 0x46550(character): a bonus fighter (30 and over) */
static void bonus_fighter(void)
{
    const uint32_t character = *recomp_memory_u32(recomp_runtime.registers.esp + 4u);
    if (inferno_style_test && (character == 27u || character == 28u)) {
        recomp_runtime.registers.eax = 1u;
        recomp_runtime.registers.esp += 4u;   /* the original's ret */
        return;
    }
    sub_00046550();
}

/* 0xDE480(ending), the Arcade ending's first step: loads its picture and text by the character's row in the table
   at 0x34ED88 (the /2 entry of its picture, the lines of its text plus one, its first text in /2/87). The bonus
   fighters (Lizardman 30, Assassin 31, Berserker 32) have no rows (-1: /2/-1 is loaded and the game stops). As on
   the other consoles, Berserker has Astaroth's (18), and Lizardman and Assassin none: their ending scene is left
   out (scene_switch); where it still starts (mode 16), it goes straight to its last step (3, done). */
static void arcade_ending(void)
{
    const uint32_t ending = *recomp_memory_u32(recomp_runtime.registers.esp + 4u);
    const uint32_t character = *recomp_memory_u32(ending + 0x74u);
    if (character == 30u || character == 31u) {
        *recomp_memory_u32(ending + 0x7cu) = 3u;
        recomp_runtime.registers.esp += 4u;   /* the original's ret */
        return;
    }
    if (character == 32u) {
        memcpy(recomp_memory(0x0034ED88u + 8u * 32u, 8u), recomp_memory(0x0034ED88u + 8u * 18u, 8u), 8u);
    }
    sub_000DE480();
}

/* 0x698D0(character, costume), availability callback for bonus characters
   (Lizardman 30, Assassin 31, Berserker 32). In the original it only allowed
   them in VS, Team Battle and Practice (returning 0 for Arcade, Survival, and
   Time Attack). Unlocked everywhere: available in all modes once unlocked. */
static void character_available_bonus(void)
{
    const uint32_t esp = recomp_runtime.registers.esp;
    const uint32_t character = *recomp_memory_u32(esp + 4u);
    const uint32_t costume = *recomp_memory_u32(esp + 8u);
    /* In the Museum profile viewer (mode 9), they have no profiles, so leave locked. */
    if (*(const uint16_t *)recomp_memory(0x0039895Eu, 2u) == 9u) {
        recomp_runtime.registers.eax = 0u;
        recomp_runtime.registers.esp += 4u;
        return;
    }
    const uint32_t args[3] = {character, 1u, 0u};
    kernel_call_guest(0x000464e0u, args, 3u);
    if ((recomp_runtime.registers.eax & 0xffu) == 0u) {
        recomp_runtime.registers.eax = 0u;
        recomp_runtime.registers.esp += 4u;
        return;
    }
    const uint32_t c_args[2] = {character, costume};
    kernel_call_guest(0x00056920u, c_args, 2u);
    recomp_runtime.registers.eax = (recomp_runtime.registers.eax != 0u) ? 1u : 0u;
    recomp_runtime.registers.esp += 4u;
}

/* 0x566F0 (character in esi, weapon in edi): the item that unlocks a character's weapon 2-11 (86 + 10 x its
   place in the 24-character roster, 0x36A70, + the weapon), or -1 (the weapon it starts with, or a mode that gives
   every weapon). The guests beside Spawn have no place there, so theirs came out as items 78-87, which are other
   things. They have items of their own past the game's (sc2_guest_items.c), where the guest slot's has one. */
static void weapon_item(void)
{
    const uint32_t character = recomp_runtime.registers.esi;
    if (character != 27u && character != 28u) {
        sub_000566F0();
        return;
    }
    recomp_runtime.registers.esi = 29u;
    sub_000566F0();
    recomp_runtime.registers.esi = character;
    if (recomp_runtime.registers.eax != 0xFFFFFFFFu) {
        const int own = sc2_guest_weapon_item(character, recomp_runtime.registers.edi);
        if (own >= 0) recomp_runtime.registers.eax = (uint32_t)own;
    }
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
   (0x56690) and sets (0x57680): clearing Arcade gets it, and with it had the Museum's Character Ending shows the
   character's ending. With no place the guests had item 277, another's; they take Spawn's. The bonus fighters
   (30-32) have the roster's last three places, so items 299-301, though no endings of their own: 299 is one of the
   save's spare states, which hold Heihachi's last weapons (sc2_guest_items.c), and 300 and 301 are past the states
   altogether. Clearing Arcade with Lizardman so gave Heihachi a weapon, and with those weapons had the Museum
   offered Lizardman's ending, which does not exist (issue #91). They have no item (-1, the game's "none": nothing
   sets it), and for the Museum it is not had (character_ending, below). */
static void character_item(void)
{
    if (recomp_runtime.registers.eax >= 30u && recomp_runtime.registers.eax <= 32u) {
        recomp_runtime.registers.eax = 0xffffffffu;
        recomp_runtime.registers.esp += 4u;   /* the original's ret */
        return;
    }
    if (recomp_runtime.registers.eax == 27u || recomp_runtime.registers.eax == 28u) recomp_runtime.registers.eax = 29u;
    sub_000569F0();
}

/* 0x56A00(character): the state of the character's own item (0 not had, 3 had), by which the Museum lists its
   Character Ending entry (0x775E0) and opens each cell of that entry's character select (0x6A28A). The bonus
   fighters have none (character_item): not had, where the game's "none" counts as had. */
static void character_ending(void)
{
    const uint32_t character = *recomp_memory_u32(recomp_runtime.registers.esp + 4u);
    if (character >= 30u && character <= 32u) {
        recomp_runtime.registers.eax = 0u;
        recomp_runtime.registers.esp += 4u;   /* the original's ret */
        return;
    }
    sub_00056A00();
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

/* 0x77520, one of the states Mode Select's Museum entry is shown by (0x77760): the highest of the characters'
   (1 to 32) unlock states, 0x56980, 3 at the first that is 3. That reads the character's unlock item from a table
   (0x1A22B8) that has none (-1) for 27 and 28, and an item -1 is had (0x56690), so with a guest selectable the
   Museum showed on a fresh save with none of its own entries, and Down in it looked for one forever (0x77860).
   The guests count for nothing here; character select and the others still take them as had. */
static void museum_characters(void)
{
    const RecompRegisters saved = recomp_runtime.registers;
    uint32_t state = 0u;
    for (uint32_t character = 1u; character < 0x21u && state != 3u; ++character) {
        if (character == 27u || character == 28u) continue;
        kernel_call_guest(0x00056980u, &character, 1u);
        const uint32_t own = recomp_runtime.registers.eax;
        recomp_runtime.registers = saved;
        if (own == 3u || own == 2u) state = own;
    }
    recomp_runtime.registers.eax = state;
    recomp_runtime.registers.esp += 4u;   /* the original's ret */
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
    if (museum_hidden_count && !museum_select_mode()) museum_grid(false);   /* (the Museum's select left) */
    sc2_frame_begins();
    sub_00032630();
}

/* 0x344D0(region, name, base, size): one of the regions the game splits its data memory into (0x34520 makes them,
   each a fixed size at a fixed place). The first, "SYSTEM_DATA", holds /2/5 alone (the select screens' small icons
   and, on the European disc, the weapons' effect icons after them); "COMMON MOTION" follows it. The US disc's /2/5
   is 0x123A00 bytes in a region of 0x140000. The European disc's is 0x14B000 in one of 0x14E000, and the guests'
   six icons make it 0x152900: its end, the last effect icons' pictures, lay in the next region and was written
   over, so one effect icon was noise for every character (issue #77). With the guests the European program's first
   region is 0x8000 longer and the second, which has 0x49000 to spare, starts and ends that much later. */
static void data_region(void)
{
#if SC2_PAL
    enum { MORE = 0x8000u };
    const uint32_t esp = recomp_runtime.registers.esp;
    if (sc2_guest_loaded(27u) || sc2_guest_loaded(28u)) {
        const char *name = (const char *)recomp_memory(*recomp_memory_u32(esp + 8u), 16u);
        if (strncmp(name, "SYSTEM_DATA", 12u) == 0) {
            *recomp_memory_u32(esp + 0x10u) += MORE;
        } else if (strncmp(name, "COMMON MOTION", 14u) == 0) {
            *recomp_memory_u32(esp + 0xCu) += MORE;
            *recomp_memory_u32(esp + 0x10u) -= MORE;
        }
    }
#endif
    sub_000344D0();
}

/* 0x2A350(request): the game leaves for the Xbox Dashboard (request: what for; 3 is its memory screen, with the
   drive and the blocks wanted). It fades the sound out, stops its threads and calls XLaunchNewImage with no program.
   The one way there in play is "SOULCALIBUR II game data is unusable. Press B to delete unusable data": a save it
   cannot read (the other release's: the European program finds the US disc's save and the other way round), which
   the player was to delete in the Dashboard. There is no Dashboard, and the game's own way out hung with a black
   screen (issue #77). The save is set aside and the game started over, as the player would have it after
   (runner.cpp, sc2_dashboard: it comes back only when it could not, and the game then goes on as before). */
void sc2_dashboard(void);
static void leave_for_dashboard(void)
{
    const uint32_t request = *recomp_memory_u32(recomp_runtime.registers.esp + 4u);
    if (request != 0u && *recomp_memory_u32(request) == 3u) sc2_dashboard();
    sub_0002A350();
}

/* Weapon Master's character select with the guests' row (sc2_wm_select.c) */
RecompFunction sc2_wm_select_lookup(uint32_t address);
/* The guests' weapons as items of their own (sc2_guest_items.c) */
RecompFunction sc2_guest_items_lookup(uint32_t address);
/* ... and their rows in the Museum's weapon gallery (sc2_guest_gallery.c) */
RecompFunction sc2_guest_gallery_lookup(uint32_t address);

RecompFunction recomp_sc2_hooks_lookup_manual(uint32_t address)
{
    switch (address) {
    case 0x00090280u: return movie_scene_update;
    case 0x00044360u: return weapon_bones_set;
    case 0x0003bed0u: return fight_setup;
    case 0x000502a0u: return scene_switch;
    case 0x0002abc0u: return sprite_draw;
    case 0x0006a190u: return cell_place;
    case 0x0006a1f0u: return cell_setup;
    case 0x0006b000u: return cursor_move;
    case 0x000698d0u: return character_available_bonus;
    case 0x000566f0u: return weapon_item;
    case 0x000c4940u: return play_counted;
    case 0x00036a70u: return roster_place;
    case 0x00036a40u: return roster_character;
    case 0x000f1390u: return character_float;
    case 0x000d24f0u: return result_name;
    case 0x000d29a0u: return beaten_list;
    case 0x000569f0u: return character_item;
    case 0x00056a00u: return character_ending;
    case 0x00055010u: return second_fighter;
    case 0x00046550u: return bonus_fighter;
    case 0x000dcfa0u: return gallery_grid;
    case 0x000528e0u: return boss_form;
    case 0x0001c2b0u: return random_seed;
    case 0x00103a75u: return query_performance_counter;
    case 0x00145a40u: return rand_hook;
    case 0x00146610u: return memory_move;
    case 0x000331b0u: return layer_2d;
    case 0x0002ed80u: return box_fill;
    case 0x000569b0u: return profile_item;
    case 0x00077520u: return museum_characters;
    case 0x0002ec90u: return box_frame;
    case 0x00032630u: return frame_begins;
    case 0x000344d0u: return data_region;
    case 0x0002a350u: return leave_for_dashboard;
    case 0x000de480u: return arcade_ending;
    default: break;
    }
    const RecompFunction items = sc2_guest_items_lookup(address);
    if (items) return items;
    const RecompFunction gallery = sc2_guest_gallery_lookup(address);
    return gallery ? gallery : sc2_wm_select_lookup(address);
}
