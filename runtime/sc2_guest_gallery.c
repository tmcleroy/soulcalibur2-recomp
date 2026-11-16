/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Link's and Heihachi's weapons in the Museum's weapon gallery (issue #77).

   The gallery (0xE01B0; 0xDF110 sets it up, 0xDF9B0 runs its list) lists the characters of a table at 0x34EED0, 19
   rows of {the name in Japanese, the character (a word), its first text (a word)}: a weapon's name is text first +
   weapon of the gallery's texts (/2/399), its description 0xFD further on. 0xDF110 copies the rows of the
   characters had into the gallery's object (+0x90, with their number at +0x84), which has room for the 19 and no
   more; the cursor's row is at +0x80, the first of the six rows shown at +0x82. Two routines read a row's first
   text from the program's table by the object's row (0xDEA20, 0xDEEA0), which is the same row only while every
   character is had.

   The Xbox's texts hold the three guests' (Link's from 217, Heihachi's from 229, Spawn's from 241), and its
   table has Spawn's row alone. Here the list is the game's rows with Link's and Heihachi's after Spawn's. When that
   is more than the object's 19, the object holds 19 of them in a row and moves along the list as the cursor
   passes its ends: 0xDF0D0 (the cursor's move, by Up and Down) is asked with a row either side, so that a move
   past an end shows; the rows then shift by one and the cursor's row and the first row shown are set so that the
   game finds a move to a new row, and makes the six shown again, as for any other. The program's table follows
   the object's rows, so both read the same. */
#include "sc2_hooks.h"
#include "sc2_guest.h"
#include "kernel_abi.h"

#include <stdbool.h>
#include <string.h>

enum {
    TABLE = 0x0034EED0u,
    ROOM = 19,           /* rows in the object, and in the program's table */
    ROWS = 0x90u, ROW = 0x80u, FIRST_SHOWN = 0x82u, COUNT = 0x84u,
    SHOWN = 6,
    SPAWN = 29,
    LINK_TEXT = 217, HEIHACHI_TEXT = 229,
};

extern void sub_000DF110(void);
extern void sub_000DF0D0(void);

static uint8_t program_rows[ROOM][8];   /* the program's table as it came */
static bool kept;
static uint8_t list[ROOM + 2][8];
static unsigned total, offset;

static int16_t *word(uint32_t address) { return (int16_t *)(void *)recomp_memory(address, 2u); }

/* The object's rows and the program's table: the list from `offset` */
static void show(uint32_t object)
{
    const unsigned count = total < ROOM ? total : ROOM;
    memcpy(recomp_memory(object + ROWS, 8u * count), list[offset], 8u * count);
    memset(recomp_memory(TABLE, 8u * ROOM), 0, 8u * ROOM);
    memcpy(recomp_memory(TABLE, 8u * count), list[offset], 8u * count);
    *word(object + COUNT) = (int16_t)count;
}

static void setup(void)
{
    const uint32_t object = *recomp_memory_u32(recomp_runtime.registers.esp + 4u);
    if (!kept) {
        memcpy(program_rows, recomp_memory(TABLE, sizeof program_rows), sizeof program_rows);
        kept = true;
    }
    memcpy(recomp_memory(TABLE, sizeof program_rows), program_rows, sizeof program_rows);
    sub_000DF110();
    total = offset = 0;
    if (!sc2_guest_loaded(27u) && !sc2_guest_loaded(28u)) return;
    const unsigned count = (unsigned)*word(object + COUNT);
    if (count > ROOM) return;
    uint8_t rows[ROOM][8];
    memcpy(rows, recomp_memory(object + ROWS, 8u * count), 8u * count);
    unsigned after = count;   /* the guests' place: after Spawn, or last */
    for (unsigned k = 0; k < count; ++k)
        if (*(int16_t *)(void *)(rows[k] + 4) == SPAWN) after = k + 1u;
    for (unsigned k = 0; k <= count; ++k) {
        if (k == after) {
            for (uint32_t guest = 27u; guest <= 28u; ++guest) {
                if (!sc2_guest_loaded(guest)) continue;
                const int16_t row[2] = {(int16_t)guest, (int16_t)(guest == 27u ? LINK_TEXT : HEIHACHI_TEXT)};
                memcpy(list[total], program_rows[0], 4u);
                memcpy(list[total] + 4, row, 4u);
                ++total;
            }
        }
        if (k < count) memcpy(list[total++], rows[k], 8u);
    }
    show(object);
}

/* 0xDF0D0(object, row): the cursor's row after this frame's Up or Down, kept within the rows */
static void move(void)
{
    const uint32_t esp = recomp_runtime.registers.esp;
    const uint32_t object = *recomp_memory_u32(esp + 4u);
    const int32_t row = (int32_t)*recomp_memory_u32(esp + 8u);
    const int16_t count = *word(object + COUNT);
    if (total <= ROOM || count != ROOM) {
        sub_000DF0D0();
        return;
    }
    *recomp_memory_u32(esp + 8u) = (uint32_t)(row + 1);
    *word(object + COUNT) = (int16_t)(count + 2);
    sub_000DF0D0();
    *recomp_memory_u32(esp + 8u) = (uint32_t)row;
    *word(object + COUNT) = count;
    int32_t to = (int32_t)recomp_runtime.registers.eax - 1;
    if (to < 0) {
        to = 0;
        if (offset > 0u) {
            --offset;
            show(object);
            *word(object + ROW) = 1;
            *word(object + FIRST_SHOWN) = 1;
        }
    } else if (to >= count) {
        to = count - 1;
        if (offset + ROOM < total) {
            ++offset;
            show(object);
            *word(object + ROW) = (int16_t)(count - 2);
            *word(object + FIRST_SHOWN) = (int16_t)(count - 1 - SHOWN);
        }
    }
    recomp_runtime.registers.eax = (uint32_t)to;
}

RecompFunction sc2_guest_gallery_lookup(uint32_t address)
{
    switch (address) {
    case 0x000df110u: return setup;
    case 0x000df0d0u: return move;
    default: return NULL;
    }
}
