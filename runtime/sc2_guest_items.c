/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Link's and Heihachi's weapons as items of their own (issue #77).

   A character's weapons 2-11 are items, 88 + 10 x its place in the roster + (weapon - 2) (0x566F0 gives the item,
   0x56740 the character and weapon of one); the game's 298 items (0x3BB368, a state each: 0 not had, 3 had) are all
   taken, and the guest slot's ten (228-237) are Spawn's. Link and Heihachi took those too: their weapons were had
   when Spawn's were, and Weapon Master's shops (which sell items) and the Museum's weapon gallery showed Spawn's alone.

   Here each has ten items past the game's, 400-409 Link's and 410-419 Heihachi's:
   - had or not, a bit each, in the 20 bits of the game's save that nothing uses: the 76th byte of the packed items
     (0x3BB1F0: 75 bytes hold the 300 states the save keeps), the 16th of the unlock counters (0x3BB34C, 22 bytes: 15
     is the one no call names) and the two states past the 298 (items 298 and 299, a bit of each). So they are saved,
     loaded, started anew and sent to the other player online with the save itself.
   - 0x566F0 gives them for 27 and 28, 0x56740 their character and weapon, 0x56690 their state and 0x57680 sets it;
     0x56BE0 gives their record (a weapon's: kind 8 and its name's text).
   - A shop's list (0xF5760 builds it at the shop's +8 from its table's rows of {item, price}, 12 bytes each, ended
     by -1; room for 40) gets each guest's weapon after the guest slot's own, at its price.
   - The shop's step (0xFAEC0) only buys items below 0x116. Its pick (0xF5D20, from 0xFAF67) is given the guest
     slot's item in a guest's place, and the three calls it then makes with it (0xF3940 the question, 0x57680 the
     purchase, 0x580A0 the announcement) get the guest's back. The line of help under the list (0xF7850) has
     the same limit: its item (from 0xF5D20 at 0xF7A7B, or 0xF5DB0) is stood in for the same way, and the
     character it then asks for (0x56740 from 0xF7BA7, to say whose weapon it is) is the guest.

   And four more, the Collection History's entries their own discs have past Spawn's (the GameCube's items 47-49, the
   PS2's 47): 420 Link's weapon demonstration, 421 Heihachi's (kind 2, which the Exhibition Theater asks for: 0x56980
   through 0x56960, where the Xbox's table gives the guests -1, had from the start, so they were never in the
   Collection History nor on sale), 422 and 423 Link's two outfits (kind 4, his third and fourth costumes: 0x56860).
   - had or not, a bit each in a byte of the guest memory (EXTRA), kept in the save past its last field: the save
     zeroes its 16 KB before writing its fields (0x8C630) and signs all of it, so two bytes after the last field
     (0x346F08, 0x2A4 bytes, written by 0xC5220 and read by 0xC4770) are saved and checked with the rest; a save
     made before has zeros there. 0x565F0 (a new save) clears them with the game's.
   - in a shop, each after the row of Spawn's demonstration (47), at its price; Spawn's stands in for them in the
     shop's step as the guest slot's weapons do for the guests'.
   - in the Collection History (0x580F0: items 0-87 had, 0x574F0 stepping to the next, 0x56630 counting them, the
     counter item + 1 out of 86): after each guest's profile item (48, 49), its own in the order its disc has them;
     their pictures are the guests' file's /2 entries after the title pictures (guest/build.cpp, EXTRA_PICTURES). */
#include "sc2_hooks.h"
#include "sc2_guest.h"
#include "kernel_abi.h"
#include "sc2_version.h"

#include <stdbool.h>
#include <string.h>

enum {
    ITEMS = 0x003BB368u,          /* the states, a byte each */
    ITEMS_PACKED = 0x003BB1F0u,   /* as saved: four to a byte, 75 bytes, and a byte unused */
    COUNTERS = 0x003BB34Cu,       /* 22 unlock counters, a byte each */
    CHANGED = 0x00398ADAu,        /* a word 0x57640 sets when an item changes */
    RECORD = 0x003BB23Cu,         /* the record 0x56BE0 fills for a weapon: its kind (a word), then its name's text */
    FIRST = 400, EACH = 10,       /* the guests' items */
    SLOT_FIRST = 228,             /* the guest slot's (Spawn's) */
    SHOP_ROOM = 40,
    EXTRA_FIRST = 420, EXTRAS = 4, /* Link's demonstration, Heihachi's, Link's two outfits */
    SLOT_DEMO = 47,                /* Spawn's demonstration */
    SAVE_MARK = 0xA5,              /* the first of the two bytes after the save's fields, once they are written */
};

static uint32_t extra_at;          /* the byte of the four (guest memory: kept in save states, sent online) */

extern void sub_00056690(void);
extern void sub_00057680(void);
extern void sub_00056740(void);
extern void sub_00056BE0(void);
extern void sub_000F5760(void);
extern void sub_000F5D20(void);
extern void sub_000F3940(void);
extern void sub_000580A0(void);

static bool is_weapon_item(int32_t item) { return item >= FIRST && item < FIRST + 2 * EACH; }
static bool is_extra_item(int32_t item) { return item >= EXTRA_FIRST && item < EXTRA_FIRST + EXTRAS && extra_at != 0u; }
static bool is_guest_item(int32_t item) { return is_weapon_item(item) || is_extra_item(item); }
static uint32_t extra_character(int32_t item) { return item == EXTRA_FIRST + 1 ? 28u : 27u; }

/* The item a guest's stands in for where the game takes only its own: the guest slot's weapon, or Spawn's
   demonstration */
static int32_t stand_in(int32_t item)
{
    return is_extra_item(item) ? SLOT_DEMO : SLOT_FIRST + (item - FIRST) % EACH;
}

int sc2_guest_weapon_item(uint32_t character, uint32_t weapon)
{
    if ((character != 27u && character != 28u) || !sc2_guest_loaded(character) || weapon < 2u || weapon > 11u) return -1;
    return FIRST + (int)(character - 27u) * EACH + (int)(weapon - 2u);
}

/* The bit of a guest's item: where it is and its mask */
static uint8_t *bit_of(int32_t item, uint8_t *mask)
{
    if (is_extra_item(item)) {
        *mask = (uint8_t)(1u << (item - EXTRA_FIRST));
        return recomp_memory(extra_at, 1u);
    }
    const unsigned bit = (unsigned)(item - FIRST);
    if (bit < 8u) {
        *mask = (uint8_t)(1u << bit);
        return recomp_memory(ITEMS_PACKED + 75u, 1u);
    }
    if (bit < 16u) {
        *mask = (uint8_t)(1u << (bit - 8u));
        return recomp_memory(COUNTERS + 15u, 1u);
    }
    *mask = (uint8_t)(1u << ((bit - 16u) & 1u));
    return recomp_memory(ITEMS + 298u + ((bit - 16u) >> 1), 1u);
}

static bool had(int32_t item)
{
    uint8_t mask;
    const uint8_t *at = bit_of(item, &mask);
    return (*at & mask) != 0u;
}

static void guest_call(uint32_t address, const uint32_t *args, unsigned n)
{
    const RecompRegisters saved = recomp_runtime.registers;
    kernel_call_guest(address, args, n);
    recomp_runtime.registers = saved;
}

static void have(int32_t item, bool now)
{
    uint8_t mask;
    uint8_t *at = bit_of(item, &mask);
    if (((*at & mask) != 0u) == now) return;
    *at = now ? (uint8_t)(*at | mask) : (uint8_t)(*at & ~mask);
    *(uint16_t *)(void *)recomp_memory(CHANGED, 2u) = 1u;   /* (as 0x57640) */
    guest_call(0x000564C0u, NULL, 0u);                      /* the states packed again for the save */
}

void sc2_guest_items_all(void)
{
    for (int32_t item = FIRST; item < FIRST + 2 * EACH; ++item) have(item, true);
    for (int32_t item = EXTRA_FIRST; item < EXTRA_FIRST + EXTRAS; ++item)
        if (is_extra_item(item) && sc2_guest_loaded(extra_character(item))) have(item, true);
}

/* 0x56690(item, new too): an item's state */
static void item_state(void)
{
    const int32_t item = (int32_t)*recomp_memory_u32(recomp_runtime.registers.esp + 4u);
    if (!is_guest_item(item)) {
        sub_00056690();
        return;
    }
    recomp_runtime.registers.eax = had(item) ? 3u : 0u;
    recomp_runtime.registers.esp += 4u;
}

static int32_t pending;   /* the guest's item the shop's pick stands for, or 0 */
static int32_t hinted;    /* ... and the one the help line's item stands for */
extern void sub_000F5DB0(void);

/* 0x57680(item, state): sets an item's state, or with -1 moves it on (not had, got, new, had) */
static void item_set(void)
{
    const uint32_t esp = recomp_runtime.registers.esp;
    int32_t item = (int32_t)*recomp_memory_u32(esp + 4u);
    const int32_t state = (int32_t)*recomp_memory_u32(esp + 8u);
    if (pending && *recomp_memory_u32(esp) == 0x000FB06Fu && item == stand_in(pending)) item = pending;
    if (!is_guest_item(item)) {
        sub_00057680();
        return;
    }
    have(item, state != 0);
    recomp_runtime.registers.eax = had(item) ? 3u : 0u;
    recomp_runtime.registers.esp += 4u;
}

/* 0x56740(item, character out, weapon out) */
static void item_weapon(void)
{
    const uint32_t esp = recomp_runtime.registers.esp;
    int32_t item = (int32_t)*recomp_memory_u32(esp + 4u);
    if (hinted && *recomp_memory_u32(esp) == 0x000F7BACu && item == stand_in(hinted)) item = hinted;
    if (!is_guest_item(item)) {
        sub_00056740();
        return;
    }
    /* (not a weapon: its character, and weapon 0, which the Collection History's viewer takes for no voice line) */
    *recomp_memory_u32(*recomp_memory_u32(esp + 8u)) = is_extra_item(item) ? extra_character(item) : 27u + (uint32_t)(item - FIRST) / EACH;
    *recomp_memory_u32(*recomp_memory_u32(esp + 12u)) = is_extra_item(item) ? 0u : 2u + (uint32_t)(item - FIRST) % EACH;
    recomp_runtime.registers.esp += 4u;
}

/* 0x56BE0(item): its record */
static void item_record(void)
{
    int32_t item = (int32_t)*recomp_memory_u32(recomp_runtime.registers.esp + 4u);
    /* (the shop's help line, given Spawn's demonstration for a guest's) */
    if (hinted && is_extra_item(hinted) && *recomp_memory_u32(recomp_runtime.registers.esp) == 0x000F7B81u &&
        item == SLOT_DEMO)
        item = hinted;
    if (!is_guest_item(item)) {
        sub_00056BE0();
        return;
    }
    if (is_extra_item(item)) {
        /* (as the table's rows: kind, 0, the character's picture 156 + its ID, its text bank) */
        const uint16_t record[4] = {item < EXTRA_FIRST + 2 ? 2u : 4u, 0u, (uint16_t)(156u + extra_character(item)), 0x500u};
        memcpy(recomp_memory(RECORD, sizeof record), record, sizeof record);
        recomp_runtime.registers.eax = RECORD;
        recomp_runtime.registers.esp += 4u;
        return;
    }
    const uint32_t args[3] = {27u + (uint32_t)(item - FIRST) / EACH, 0u, 2u + (uint32_t)(item - FIRST) % EACH};
    const RecompRegisters saved = recomp_runtime.registers;
    kernel_call_guest(0x000F6690u, args, 3u);   /* the weapon's name */
    const uint32_t name = recomp_runtime.registers.eax;
    recomp_runtime.registers = saved;
    *(uint16_t *)(void *)recomp_memory(RECORD, 2u) = 8u;
    *recomp_memory_u32(RECORD + 4u) = name;
    recomp_runtime.registers.eax = RECORD;
    recomp_runtime.registers.esp += 4u;
}

/* 0xF5760(shop in ecx, which): the shop's list. Each guest's weapon goes in after the guest slot's, or, where that
   is not on sale (Spawn not yet had), at the end, at the table's price as the list rounds one (up to a hundred). */
static void shop_list(void)
{
    const uint32_t shop = recomp_runtime.registers.ecx;
    const uint32_t which = *recomp_memory_u32(recomp_runtime.registers.esp + 4u);
    sub_000F5760();
    if (!sc2_guest_loaded(27u) && !sc2_guest_loaded(28u)) return;
    const uint32_t table = *recomp_memory_u32(shop + 4u) + 12u * *recomp_memory_u32(shop + 0x1F0u + 4u * which);
    uint8_t *list = recomp_memory(shop + 8u, 12u * SHOP_ROOM);
    unsigned count = 0;
    while (count < SHOP_ROOM - 1u && *(int16_t *)(void *)(list + 12u * count) != -1) ++count;
    for (uint32_t row = table;; row += 12u) {
        const int32_t item = *(int16_t *)(void *)recomp_memory(row, 2u);
        if (item == -1) break;
        if (item == SLOT_DEMO && extra_at != 0u) {
            unsigned at = count;
            for (unsigned k = 0; k < count; ++k)
                if (*(int16_t *)(void *)(list + 12u * k) == item) at = k + 1u;
            static const int16_t order[EXTRAS] = {EXTRA_FIRST, EXTRA_FIRST + 2, EXTRA_FIRST + 3, EXTRA_FIRST + 1};
            for (unsigned k = 0; k < EXTRAS; ++k) {
                if (!sc2_guest_loaded(extra_character(order[k])) || count >= SHOP_ROOM - 1u) continue;
                uint8_t entry[12];
                memcpy(entry, recomp_memory(row, 12u), 12u);
                memcpy(entry, &order[k], 2);
                memmove(list + 12u * (at + 1u), list + 12u * at, 12u * (count - at));
                memcpy(list + 12u * at, entry, 12u);
                ++at;
                ++count;
            }
            continue;
        }
        if (item < SLOT_FIRST || item >= SLOT_FIRST + EACH) continue;
        unsigned at = count;
        for (unsigned k = 0; k < count; ++k)
            if (*(int16_t *)(void *)(list + 12u * k) == item) at = k + 1u;
        for (uint32_t guest = 27u; guest <= 28u; ++guest) {
            if (!sc2_guest_loaded(guest) || count >= SHOP_ROOM - 1u) continue;
            uint8_t entry[12];
            memcpy(entry, recomp_memory(row, 12u), 12u);
            const int16_t own = (int16_t)(FIRST + (int)(guest - 27u) * EACH + (item - SLOT_FIRST));
            int32_t price;
            memcpy(&price, entry + 4, 4);
            price = (price + 99) / 100 * 100;
            memcpy(entry, &own, 2);
            memcpy(entry + 4, &price, 4);
            memmove(list + 12u * (at + 1u), list + 12u * at, 12u * (count - at));
            memcpy(list + 12u * at, entry, 12u);
            ++at;
            ++count;
        }
    }
    const int16_t end = -1;
    memcpy(list + 12u * count, &end, 2);
}

/* 0xF5D20(shop in ecx, which, gold, item out, price out): the item under the shop's cursor, when it can be bought */
static void shop_pick(void)
{
    const uint32_t esp = recomp_runtime.registers.esp;
    const uint32_t caller = *recomp_memory_u32(esp);
    const uint32_t out = *recomp_memory_u32(esp + 12u);
    sub_000F5D20();
    if (caller != 0x000FAF6Cu && caller != 0x000F7A80u) return;
    const int32_t item = (int32_t)*recomp_memory_u32(out);
    const int32_t guest = is_guest_item(item) ? item : 0;
    if (caller == 0x000FAF6Cu) pending = guest;
    else if (guest) hinted = guest;
    if (guest) *recomp_memory_u32(out) = (uint32_t)stand_in(item);
}

/* 0xF5DB0(shop in ecx, which): the item under the shop's cursor, for the help line */
static void shop_hint(void)
{
    sub_000F5DB0();
    const int32_t item = (int32_t)recomp_runtime.registers.eax;
    hinted = is_guest_item(item) ? item : 0;
    if (hinted) recomp_runtime.registers.eax = (uint32_t)stand_in(item);
}

/* An argument that is the pick's stand-in, in a call from the shop's step: the guest's item again */
static void pick_back(uint32_t argument, uint32_t from, uint32_t or_from)
{
    const uint32_t esp = recomp_runtime.registers.esp;
    const uint32_t caller = *recomp_memory_u32(esp);
    uint32_t *item = recomp_memory_u32(esp + 4u * argument);
    if (pending && (caller == from || caller == or_from) && (int32_t)*item == stand_in(pending))
        *item = (uint32_t)pending;
}

static void shop_question(void)
{
    pick_back(2u, 0x000FB001u, 0x000FB025u);
    sub_000F3940();
}

static void shop_announce(void)
{
    pick_back(1u, 0x000FB088u, 0x000FB088u);
    sub_000580A0();
}

/* ---- the four past the weapons: the demonstrations and Link's outfits ---- */

extern void sub_00056960(void);
extern void sub_00056860(void);
extern void sub_00056630(void);
extern void sub_00028900(void);
extern void sub_00014A10(void);
extern void sub_000C5220(void);
extern void sub_000C4770(void);
extern void sub_000565F0(void);
extern uint32_t recomp_kernel_allocate_pool(uint32_t size);

void sc2_guest_items_setup(void)
{
    if (extra_at == 0u) extra_at = recomp_kernel_allocate_pool(4u);
    if (extra_at != 0u) memset(recomp_memory(extra_at, 4u), 0, 4u);
}

/* 0x56960 (character in eax): the character's weapon demonstration, from the table at 0x1A22B8 */
static void demo_item(void)
{
    const uint32_t character = recomp_runtime.registers.eax;
    if (extra_at == 0u || (character != 27u && character != 28u) || !sc2_guest_loaded(character)) {
        sub_00056960();
        return;
    }
    recomp_runtime.registers.eax = character == 27u ? EXTRA_FIRST : EXTRA_FIRST + 1u;
    recomp_runtime.registers.esp += 4u;
}

/* 0x56860 (costume in eax, character): the item a costume needs, -1 for none (the game's: the third costume only) */
static void costume_item(void)
{
    const uint32_t costume = recomp_runtime.registers.eax;
    const uint32_t character = *recomp_memory_u32(recomp_runtime.registers.esp + 4u);
    if (extra_at == 0u || character != 27u || !sc2_guest_loaded(27u) || (costume != 2u && costume != 3u)) {
        sub_00056860();
        return;
    }
    recomp_runtime.registers.eax = EXTRA_FIRST + costume;   /* (422, 423) */
    recomp_runtime.registers.esp += 4u;
}

/* The Collection History's entries in order: the game's 0-87 (48 and 49 the guests' profiles when they have one),
   each guest's own after its profile. */
static unsigned collection(int32_t *out)
{
    unsigned n = 0;
    for (int32_t item = 0; item < 88; ++item) {
        if ((item == 48 || item == 49) && sc2_guest_profile_item(27u + (uint32_t)(item - 48)) < 0) continue;
        out[n++] = item;
        if (item == 48 && extra_at && sc2_guest_loaded(27u))
            out[n++] = EXTRA_FIRST, out[n++] = EXTRA_FIRST + 2, out[n++] = EXTRA_FIRST + 3;
        if (item == 49 && extra_at && sc2_guest_loaded(28u)) out[n++] = EXTRA_FIRST + 1;
    }
    return n;
}

static bool collected(int32_t item)
{
    if (is_extra_item(item)) return had(item);
    return *(const int8_t *)recomp_memory(ITEMS + (uint32_t)item, 1u) >= 2;
}

/* 0x56630: how many of the Collection History's items are had (0-87, state 2 or more) */
static void collection_count(void)
{
    sub_00056630();
    for (int32_t item = EXTRA_FIRST; item < EXTRA_FIRST + EXTRAS; ++item)
        if (is_extra_item(item) && sc2_guest_loaded(extra_character(item)) && had(item)) ++recomp_runtime.registers.eax;
}

/* 0x574F0(item, step): the next had item that way, round the list */
static void collection_step(void)
{
    const uint32_t esp = recomp_runtime.registers.esp;
    const int32_t from = (int32_t)*recomp_memory_u32(esp + 4u), step = (int32_t)*recomp_memory_u32(esp + 8u);
    int32_t list[100];
    const int n = (int)collection(list);
    int at = -1;
    for (int k = 0; k < n; ++k)
        if (list[k] == from) at = k;
    if (at < 0 && step < 0) at = n;
    int32_t found = from;
    for (int tries = 0; tries < n; ++tries) {
        at += step;
        if (at >= n) at = 0;
        if (at < 0) at = n - 1;
        if (collected(list[at])) {
            found = list[at];
            break;
        }
    }
    recomp_runtime.registers.eax = (uint32_t)found;
    recomp_runtime.registers.esp += 4u;
}

/* 0x28900(format, x, y, ...) from 0x58353, the Collection History's "COLLECTION DATA %d/%d": the entry's place and
   the list's length (the game's item + 1, less 2 past 49, out of 86) */
static void collection_counter(void)
{
    const uint32_t esp = recomp_runtime.registers.esp;
    if (*recomp_memory_u32(esp) == 0x00058358u) {
        int32_t list[100];
        const unsigned n = collection(list);
        const int32_t item = (int32_t)*recomp_memory_u32(recomp_runtime.registers.esi + 0x50u);
        /* (the European program passes its format's text first: one argument more) */
        const uint32_t place = SC2_PAL ? 0x1Cu : 0x18u;
        for (unsigned k = 0; k < n; ++k)
            if (list[k] == item) *recomp_memory_u32(esp + place) = k + 1u;
        *recomp_memory_u32(esp + place + 4u) = n;
    }
    sub_00028900();
}

/* 0x14A10 from 0x573DD: loads the viewed item's picture, /2/(297 + item); a guest's own is in the guests' file after
   the title pictures (Link's demonstration and outfits, then Heihachi's demonstration), or, in a file built before,
   its profile's */
static void collection_picture(void)
{
    const uint32_t esp = recomp_runtime.registers.esp;
    if (*recomp_memory_u32(esp) == 0x000573E2u) {
        const uint32_t first = SC2_PAL ? 296u : 297u;
        const int32_t item = (int32_t)(*recomp_memory_u32(esp + 0xCu) - first);
        if (is_extra_item(item)) {
            static const uint32_t place[EXTRAS] = {0u, 3u, 1u, 2u};
            const int picture = sc2_guest_extra_picture(place[item - EXTRA_FIRST]);
            *recomp_memory_u32(esp + 0xCu) =
                picture >= 0 ? (uint32_t)picture : first + 48u + (extra_character(item) - 27u);
        }
    }
    sub_00014A10();
}

/* 0xC5220(at) writes the save's last field there (0x2A4 bytes); 0xC4770(at) reads it */
static void save_last_field(void)
{
    const uint32_t at = *recomp_memory_u32(recomp_runtime.registers.esp + 4u) + 0x2A4u;
    sub_000C5220();
    if (extra_at == 0u) return;
    uint8_t *after = recomp_memory(at, 2u + SC2_RECORDS_SAVE_BYTES);
    after[0] = SAVE_MARK;
    after[1] = *recomp_memory(extra_at, 1u);
    sc2_records_save(after + 2);
}

static void load_last_field(void)
{
    const uint32_t at = *recomp_memory_u32(recomp_runtime.registers.esp + 4u) + 0x2A4u;
    sub_000C4770();
    if (extra_at == 0u) return;
    const uint8_t *after = recomp_memory(at, 2u + SC2_RECORDS_SAVE_BYTES);
    *recomp_memory(extra_at, 1u) = after[0] == SAVE_MARK ? (uint8_t)(after[1] & 0x0Fu) : 0u;
    sc2_records_load(after + 2);
}

/* 0x565F0: a new save's items, none had */
static void items_reset(void)
{
    sub_000565F0();
    if (extra_at != 0u) *recomp_memory(extra_at, 1u) = 0u;
}

RecompFunction sc2_guest_items_lookup(uint32_t address)
{
    switch (address) {
    case 0x00056690u: return item_state;
    case 0x00057680u: return item_set;
    case 0x00056740u: return item_weapon;
    case 0x00056be0u: return item_record;
    case 0x000f5760u: return shop_list;
    case 0x000f5d20u: return shop_pick;
    case 0x000f5db0u: return shop_hint;
    case 0x000f3940u: return shop_question;
    case 0x000580a0u: return shop_announce;
    case 0x00056960u: return demo_item;
    case 0x00056860u: return costume_item;
    case 0x00056630u: return collection_count;
    case 0x000574f0u: return collection_step;
    case 0x00028900u: return collection_counter;
    case 0x00014a10u: return collection_picture;
    case 0x000c5220u: return save_last_field;
    case 0x000c4770u: return load_last_field;
    case 0x000565f0u: return items_reset;
    default: return NULL;
    }
}
