/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Weapon Master's character select (scene 26) with the guests in a sixth column.

   The game's select is a 5 x 4 grid of the table at 0x36C490 (24 entries: character ID, outfit), the cursor's
   cell at 0x6054D4. 0xFA030 builds it: for each of 20 cells, its icon (0x6E030 of the character and outfit, or
   0x4A while locked), a frame and a shade, three sprites from 0xF8740 kept in the select's pool of 64 slots
   (0xF2470: their pointers, the icons in slots 0-19, the frames 20-39, the shades 40-59, the cursor 60;
   0xF2480: a byte each, 1 growing, 0xFF shrinking; 0xF2490: a float each, the scale). 0xFA430 runs a frame:
   the moves (held 0x2000 up, 0x1000 down, 0x800 left, 0x400 right, each to an unlocked cell), the outfit (pressed
   0x100), the choice (0x8040: the character and outfit to the Weapon Master state's +0x20C) and Back (0x4020),
   then each cell's scale (0xF3760, 0xF2830) and the cursor cell's pulse (0x605540).

   The pool and the code count 20 cells, so the guests' column (Link beside the first row, Heihachi the second)
   is kept here: its sprites made after 0xFA030 as its own loop makes them, two of the pool's unused slots (61,
   62) for their scale. Right from the grid's last column comes into it. While the cursor is there, the moves,
   the outfit and the choice are taken from 0xFA430 (its reads of the pad, 0xF1C70 held and 0xF1C40 pressed, have
   those bits left out) and done here as it does them; Back stays its.

   The grid's white box (0xB81B0 from 0xFA408: left, top 88, depth, width 376 = five cells of 72 and a margin of 8
   each side, height 288, the width as the wide screen has it, 0x20270) is the game's five columns wide, and the
   guests stood outside it (issue #77). With the column the box is a cell wider, and the cells and the cursor
   (their offsets from the grid's corner, 0x188F24 and 0x188F28, read by this screen alone) half a cell to the
   left: the six columns centred as the five were. */
#include "sc2_hooks.h"
#include "sc2_guest.h"
#include "kernel_abi.h"

#include <stdbool.h>
#include <string.h>

enum {
    TABLE = 0x0036C490u,      /* (character, outfit) per cell */
    CURSOR = 0x006054D4u,     /* the cursor's cell */
    STATE = 0x00605470u,      /* the Weapon Master state: +0x20C the character, +0x20D the outfit */
    LEAVING = 0x006054A8u,    /* set once chosen or left */
    PULSE = 0x00605540u,      /* the cursor cell's colour this frame */
    POOL = 0x003982A0u,       /* + 0x3BF0F8: the select's slots */
    CONTEXT = 0x00398AECu,    /* the sprites' (edi for 0xF8740) */
    STEP_X = 0x0017F318u, STEP_Y = 0x0017774Cu,   /* a cell's width and height */
    CELL_X = 0x00188F24u, CELL_Y = 0x00188DC4u,   /* the cell sprites' offset from the grid's corner */
    CURSOR_X = 0x00188F28u, CURSOR_Y = 0x00188F44u,
    SMALL = 0x00188F20u,      /* a cell's scale when the cursor is elsewhere */
    COLUMNS = 5, ROWS = 4,
    COLUMN = 5,               /* the guests' */
    FIRST_SLOT = 61,
    HELD_MOVES = 0x3C00u, PRESSED_OWN = 0x8140u,
    UP = 0x2000u, DOWN = 0x1000u, LEFT = 0x800u, RIGHT = 0x400u, OUTFIT = 0x100u, CHOOSE = 0x8040u,
};
static const float GRID_X = 148.0f, GRID_Y = 112.0f;   /* 0xFA030's first cell */
static const float BOX_WIDTH = 376.0f;                 /* the box's, five columns */
static float cell_x, cursor_x;                         /* the offsets as the program has them (0 before the first build) */
static bool wide;                                      /* the box takes the column in */

static uint32_t guests[2];       /* the column: the guests the file has */
static unsigned count;
static uint32_t icon[2], frame[2], shade[2];
static uint8_t outfit[2];
static bool in_column, masking, opening_on_guest;
static unsigned row;             /* the cursor's, in the column */
static uint32_t scratch;         /* 16 bytes of guest memory for a colour */

extern void sub_000FA030(void);
extern void sub_000FA430(void);
extern void sub_000F3830(void);
extern void sub_000F2510(void);
extern void sub_000F1C40(void);
extern void sub_000F1C70(void);
extern void sub_000B81B0(void);

static uint32_t bits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static float constant(uint32_t address) { float f; memcpy(&f, recomp_memory(address, 4u), 4); return f; }
static uint32_t pool(void) { return *recomp_memory_u32(POOL) + 0x3BF0F8u; }
static uint8_t *slot_flag(unsigned slot) { return recomp_memory(pool() + 0x100u + slot, 1u); }
static float *slot_scale(unsigned slot) { return (float *)(void *)recomp_memory(pool() + 0x140u + slot * 4u, 4u); }
static uint32_t slot_sprite(unsigned slot) { return *recomp_memory_u32(pool() + slot * 4u); }
static uint32_t cell_character(unsigned cell) { return *recomp_memory(TABLE + cell * 2u, 1u); }

/* A guest routine with these stack arguments and registers; the caller's registers are kept. */
static uint32_t call(uint32_t address, const uint32_t *args, unsigned n, uint32_t eax, uint32_t ecx, uint32_t esi,
    uint32_t edi)
{
    const RecompRegisters saved = recomp_runtime.registers;
    recomp_runtime.registers.eax = eax;
    recomp_runtime.registers.ecx = ecx;
    recomp_runtime.registers.esi = esi;
    recomp_runtime.registers.edi = edi;
    kernel_call_guest(address, args, n);
    const uint32_t result = recomp_runtime.registers.eax;
    recomp_runtime.registers = saved;
    return result;
}

static void sound(uint32_t id) { const uint32_t a[] = {id, 0u}; call(0xEDB70u, a, 2u, 0, 0, 0, 0); }
static bool unlocked(uint32_t character) { const uint32_t a[] = {character}; return call(0x56840u, a, 1u, 0, 0, 0, 0) != 0u; }
static uint32_t icon_of(uint32_t character, uint32_t look)
{
    const uint32_t a[] = {character, look};
    return call(0x6E030u, a, 2u, 0, 0, 0, 0);
}
static uint32_t read_pad(uint32_t address) { return call(address, NULL, 0u, 0, 0, 0, 0); }

static uint32_t sprite(float x, float y, uint32_t picture, uint32_t layer, uint32_t depth, uint32_t bank)
{
    const uint32_t a[] = {bits(x), bits(y), picture, 0u, layer, bits(1.0f), 8u, depth, 0x604C00u, bank, 0u, 0u};
    const uint32_t made = call(0xF8740u, a, 12u, 0, 0, 0, *recomp_memory_u32(CONTEXT));
    call(0xF26E0u, NULL, 0u, made, 0, 0, 0);
    return made;
}

/* The cursor sprite (slot 60) to a cell, as 0xFA430 moves it */
static void cursor_to(unsigned col, unsigned at_row)
{
    const uint32_t a[] = {bits((float)col * constant(STEP_X) + constant(CURSOR_X)),
        bits((float)at_row * constant(STEP_Y) + constant(CURSOR_Y)), bits(1.0f)};
    call(0xF2640u, a, 3u, 0, 0, slot_sprite(60u), 0);
}

/* A cell no longer the cursor's: shrinking, its colour plain */
static void leave(unsigned slot, uint32_t made)
{
    *slot_flag(slot) = 0xFFu;
    call(0xF28E0u, NULL, 0u, made, 0xFFFFFFFFu, 0, 0);
}

static void build_column(void)
{
    count = 0;
    for (uint32_t c = 27u; c <= 28u; ++c)
        if (sc2_guest_loaded(c)) guests[count++] = c;
    in_column = false;
    if (!count) return;
    if (!scratch) scratch = recomp_kernel_allocate_pool(16u);
    const float shadow[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    memcpy(recomp_memory(scratch, 16u), shadow, 16u);
    for (unsigned k = 0; k < count; ++k) {
        const float x = GRID_X + (float)COLUMN * constant(STEP_X) + constant(CELL_X);
        const float y = GRID_Y + (float)k * constant(STEP_Y) + constant(CELL_Y);
        const uint32_t picture = unlocked(guests[k]) ? icon_of(guests[k], outfit[k]) : 0x4Au;
        icon[k] = sprite(x, y, picture, 7u, 0x3DF5C28Fu, 0x188EC0u);
        frame[k] = sprite(x, y, 0x47u, 1u, 0x3DF3B646u, 0x188EB8u);
        shade[k] = sprite(x, y, 0x48u, 2u, 0x3DF7CED9u, 0x188EB0u);
        call(0xF28A0u, NULL, 0u, scratch, shade[k], 0, 0);
        *slot_flag(FIRST_SLOT + k) = 0u;
        *slot_scale(FIRST_SLOT + k) = constant(SMALL);
    }
}

static void enter_column(unsigned at_row)
{
    const uint32_t from = *recomp_memory_u32(CURSOR);
    leave(from, slot_sprite(from));
    in_column = true;
    row = at_row < count ? at_row : count - 1u;
    *slot_flag(FIRST_SLOT + row) = 1u;
    cursor_to(COLUMN, row);
}

/* 0xF3830(character): the cell the cursor opens on. A guest's is in the column, which the game has not: it opens
   on the first cell (its outfit given back after the build), and the column takes it. */
static void open_cell(void)
{
    const uint32_t character = *recomp_memory_u32(recomp_runtime.registers.esp + 4u);
    if (sc2_guest_loaded(character)) {
        opening_on_guest = true;
        recomp_runtime.registers.eax = 0u;
        recomp_runtime.registers.esp += 4u;
        return;
    }
    sub_000F3830();
}

static void build(void)
{
    const uint32_t state = *recomp_memory_u32(STATE);
    const uint32_t chosen = *recomp_memory(state + 0x20Cu, 1u), look = *recomp_memory(state + 0x20Du, 1u);
    uint8_t kept[2];
    memcpy(kept, recomp_memory(TABLE, 2u), 2u);
    opening_on_guest = false;
    for (unsigned k = 0; k < 2u; ++k)
        if (27u + k == chosen) outfit[k] = (uint8_t)look;
    /* (the six columns centred: every cell and the cursor half a cell left, the box a cell wider, box()) */
    if (cell_x == 0.0f) {
        cell_x = constant(CELL_X);
        cursor_x = constant(CURSOR_X);
    }
    wide = sc2_guest_loaded(27u) || sc2_guest_loaded(28u);
    const float shift = wide ? constant(STEP_X) * 0.5f : 0.0f;
    *recomp_memory_u32(CELL_X) = bits(cell_x - shift);
    *recomp_memory_u32(CURSOR_X) = bits(cursor_x - shift);
    sub_000FA030();
    build_column();
    if (opening_on_guest) {
        memcpy(recomp_memory(TABLE, 2u), kept, 2u);
        for (unsigned k = 0; k < count; ++k)
            if (guests[k] == chosen) enter_column(k);
    }
}

static void pad(uint32_t mask, void (*original)(void))
{
    original();
    if (masking) recomp_runtime.registers.eax &= ~mask;
}
static void held(void) { pad(HELD_MOVES, sub_000F1C70); }
static void pressed(void) { pad(PRESSED_OWN, sub_000F1C40); }

/* Left out of the column: the grid's unlocked cell nearest the end of this row (or, failing that, of the rows
   above and below) */
static bool back_to_grid(void)
{
    for (unsigned d = 0; d < ROWS; ++d) {
        const int rows[2] = {(int)row - (int)d, (int)row + (int)d};
        for (unsigned i = 0; i < 2u; ++i) {
            if (rows[i] < 0 || rows[i] >= ROWS) continue;
            for (int col = COLUMNS - 1; col >= 0; --col) {
                const unsigned cell = (unsigned)rows[i] * COLUMNS + (unsigned)col;
                if (!unlocked(cell_character(cell))) continue;
                leave(FIRST_SLOT + row, icon[row]);
                in_column = false;
                *recomp_memory_u32(CURSOR) = cell;
                *slot_flag(cell) = 1u;
                cursor_to((unsigned)col, (unsigned)rows[i]);
                sound(0x19u);
                return true;
            }
        }
    }
    return false;
}

static void column_input(void)
{
    if (*recomp_memory_u32(LEAVING)) return;
    const uint32_t hold = read_pad(0xF1C70u), press = read_pad(0xF1C40u);
    if (hold & LEFT) {
        back_to_grid();
        return;
    }
    unsigned next = row;
    if ((hold & UP) && row > 0u) next = row - 1u;
    if ((hold & DOWN) && row + 1u < count) next = row + 1u;
    if (next != row) {
        leave(FIRST_SLOT + row, icon[row]);
        row = next;
        *slot_flag(FIRST_SLOT + row) = 1u;
        cursor_to(COLUMN, row);
        sound(0x19u);
        return;
    }
    const uint32_t character = guests[row];
    if (!unlocked(character)) return;
    if (press & OUTFIT) {
        /* the next outfit, or the first when there is none */
        sound(0x0Du);
        uint32_t look = outfit[row] + 1u, picture = icon_of(character, look);
        const uint32_t a[] = {character, look};
        if (picture == 0xFFFFFFFFu || call(0x56920u, a, 2u, 0, 0, 0, 0) == 0u) {
            look = 0u;
            picture = icon_of(character, 0u);
        }
        const uint32_t p[] = {picture};
        call(0xF2890u, p, 1u, icon[row], 0, 0, 0);
        outfit[row] = (uint8_t)look;
    } else if (press & CHOOSE) {
        /* chosen: the character and outfit to the state, as 0xFA430 does */
        sound(0x1Au);
        const uint32_t state = *recomp_memory_u32(STATE);
        *recomp_memory(state + 0x20Cu, 1u) = (uint8_t)character;
        *recomp_memory(state + 0x20Du, 1u) = outfit[row];
        *recomp_memory_u32(LEAVING) = 1u;
        const uint32_t a[] = {2u, 1u};
        call(0xF1310u, a, 2u, 0, 0, 0, 0);
    }
}

static void step(void)
{
    if (!in_column && count && !*recomp_memory_u32(LEAVING)) {
        const uint32_t cell = *recomp_memory_u32(CURSOR);
        if (cell % COLUMNS == COLUMNS - 1u && (read_pad(0xF1C70u) & RIGHT)) {
            enter_column(cell / COLUMNS);
            sound(0x19u);
        }
    }
    masking = in_column;
    sub_000FA430();
    masking = false;
    if (!count) return;
    if (in_column) {
        /* the game pulsed its cursor cell, which is not where the cursor is */
        call(0xF28E0u, NULL, 0u, slot_sprite(*recomp_memory_u32(CURSOR)), 0xFFFFFFFFu, 0, 0);
        column_input();
    }
    for (unsigned k = 0; k < count; ++k) {
        const uint32_t a[] = {0x3F5C0000u, bits(1.0f), 0x3C75C28Fu};
        call(0xF3760u, a, 3u, 1u, 0, FIRST_SLOT + k, 0);
        const uint32_t s[] = {bits(*slot_scale(FIRST_SLOT + k))};
        call(0xF2830u, s, 1u, 0, icon[k], 0, 0);
        call(0xF2830u, s, 1u, 0, frame[k], 0, 0);
        call(0xF2830u, s, 1u, 0, shade[k], 0, 0);
    }
    if (in_column) call(0xF28A0u, NULL, 0u, PULSE, icon[row], 0, 0);
}

/* 0xF2510 (edx: the sprites ended too): the pool emptied as the select closes; the column's sprites with it */
static void cleared(void)
{
    const bool ending = recomp_runtime.registers.edx != 0u;
    sub_000F2510();
    for (unsigned k = 0; k < count; ++k) {
        if (ending) {
            if (icon[k]) *recomp_memory(icon[k] + 0x24u, 1u) = 0x0Cu;
            if (frame[k]) *recomp_memory(frame[k] + 0x24u, 1u) = 0x0Cu;
            if (shade[k]) *recomp_memory(shade[k] + 0x24u, 1u) = 0x0Cu;
        }
        icon[k] = frame[k] = shade[k] = 0u;
    }
    count = 0;
    in_column = false;
}

/* 0xB81B0(context, left, top, depth, width, height, ...): a menu box. The select's (from 0xFA408) a column wider
   about the same centre, in the width's own scale (the wide screen's is narrower). */
static void box(void)
{
    const uint32_t esp = recomp_runtime.registers.esp;
    if (wide && *recomp_memory_u32(esp) == 0x000FA408u) {
        float left, width;
        memcpy(&left, recomp_memory(esp + 8u, 4u), 4u);
        memcpy(&width, recomp_memory(esp + 0x14u, 4u), 4u);
        const float more = width * constant(STEP_X) / BOX_WIDTH;
        *recomp_memory_u32(esp + 8u) = bits(left - more * 0.5f);
        *recomp_memory_u32(esp + 0x14u) = bits(width + more);
    }
    sub_000B81B0();
}

RecompFunction sc2_wm_select_lookup(uint32_t address)
{
    switch (address) {
    case 0x000fa030u: return build;
    case 0x000fa430u: return step;
    case 0x000f3830u: return open_cell;
    case 0x000f2510u: return cleared;
    case 0x000f1c70u: return held;
    case 0x000f1c40u: return pressed;
    case 0x000b81b0u: return box;
    default: return NULL;
    }
}
