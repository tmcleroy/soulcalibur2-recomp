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
     character it then asks for (0x56740 from 0xF7BA7, to say whose weapon it is) is the guest. */
#include "sc2_hooks.h"
#include "sc2_guest.h"
#include "kernel_abi.h"

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
};

extern void sub_00056690(void);
extern void sub_00057680(void);
extern void sub_00056740(void);
extern void sub_00056BE0(void);
extern void sub_000F5760(void);
extern void sub_000F5D20(void);
extern void sub_000F3940(void);
extern void sub_000580A0(void);

static bool is_guest_item(int32_t item) { return item >= FIRST && item < FIRST + 2 * EACH; }

int sc2_guest_weapon_item(uint32_t character, uint32_t weapon)
{
    if ((character != 27u && character != 28u) || !sc2_guest_loaded(character) || weapon < 2u || weapon > 11u) return -1;
    return FIRST + (int)(character - 27u) * EACH + (int)(weapon - 2u);
}

/* The bit of a guest's item: where it is and its mask */
static uint8_t *bit_of(int32_t item, uint8_t *mask)
{
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
    if (pending && *recomp_memory_u32(esp) == 0x000FB06Fu && item == SLOT_FIRST + (pending - FIRST) % EACH) item = pending;
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
    if (hinted && *recomp_memory_u32(esp) == 0x000F7BACu && item == SLOT_FIRST + (hinted - FIRST) % EACH) item = hinted;
    if (!is_guest_item(item)) {
        sub_00056740();
        return;
    }
    *recomp_memory_u32(*recomp_memory_u32(esp + 8u)) = 27u + (uint32_t)(item - FIRST) / EACH;
    *recomp_memory_u32(*recomp_memory_u32(esp + 12u)) = 2u + (uint32_t)(item - FIRST) % EACH;
    recomp_runtime.registers.esp += 4u;
}

/* 0x56BE0(item): its record */
static void item_record(void)
{
    const int32_t item = (int32_t)*recomp_memory_u32(recomp_runtime.registers.esp + 4u);
    if (!is_guest_item(item)) {
        sub_00056BE0();
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
    if (guest) *recomp_memory_u32(out) = (uint32_t)(SLOT_FIRST + (item - FIRST) % EACH);
}

/* 0xF5DB0(shop in ecx, which): the item under the shop's cursor, for the help line */
static void shop_hint(void)
{
    sub_000F5DB0();
    const int32_t item = (int32_t)recomp_runtime.registers.eax;
    hinted = is_guest_item(item) ? item : 0;
    if (hinted) recomp_runtime.registers.eax = (uint32_t)(SLOT_FIRST + (item - FIRST) % EACH);
}

/* An argument that is the pick's stand-in, in a call from the shop's step: the guest's item again */
static void pick_back(uint32_t argument, uint32_t from, uint32_t or_from)
{
    const uint32_t esp = recomp_runtime.registers.esp;
    const uint32_t caller = *recomp_memory_u32(esp);
    uint32_t *item = recomp_memory_u32(esp + 4u * argument);
    if (pending && (caller == from || caller == or_from) && (int32_t)*item == SLOT_FIRST + (pending - FIRST) % EACH)
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
    default: return NULL;
    }
}
