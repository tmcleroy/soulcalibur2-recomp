/* The guests in the Records (Options > Records): the character usage and win ratio pages.

   The records (0x346F08, thiscall `this` of the routines below) hold, after a dword of total play time (+4), one
   entry of seven dwords for each of the 24 places of the roster (0x19CA20; 0x36A40 place -> character, 0x36A70
   character -> place) at +8 + place x 0x1C: plays, then wins, losses and draws, each twice (+4 for every mode but
   mode 8's). The save keeps +4 to +0x2A4, the 24 entries' end (0xC51F0, 0xC4730); what follows (+0x2A8 on) is the
   fight in progress: the two players' places at +0x2B8, the mode at +0x2B0, a changed flag at +0x2C8.
   The guests' places are 24 and 25 (sc2_hooks.c, roster_place), past the entries, so their plays were counted as
   Spawn's (the guest slot's) and they were never on these pages. They have two entries of their own here (guest
   memory, kept with the save's extra bytes, sc2_guest_items.c), and the routines that read the entries do so for
   the 24 places and the guests loaded:
     0xC4940 counts a play, 0xC4980 a result (win and loss, or a draw for both);
     0xC4C50 gives the usage page's row at a rank, 0xC4A60 a win ratio page's (mode 8's or the others');
     0xC3070 sets the Records screen's list up and 0xC7F20 another records page, each counting its rows: on the
       usage and win ratio pages (kinds 0xE, 0xF, 0x10) the characters of the first 20 places that are unlocked
       (and the list, each bonus fighter allowed), and now the guests loaded;
     0xC46A0 empties the records (a new save). */
#include "sc2_hooks.h"
#include "sc2_guest.h"
#include "kernel_abi.h"

#include <stdbool.h>
#include <string.h>

extern void sub_000C46A0(void);
extern void sub_000C7F20(void);
extern void sub_000C3070(void);
extern uint32_t recomp_kernel_allocate_pool(uint32_t size);

enum {
    RECORDS = 0x00346F08u,
    PLACES = 24u,            /* the roster's */
    GUESTS = 2u,             /* Link 27 (place 24), Heihachi 28 (place 25) */
    ENTRY = 0x1Cu,           /* 7 dwords */
    ALL = PLACES + GUESTS,
    MOST = 99999u,           /* a count's top (0x1869F) */
    TIE_ORDER = 0x00347200u, /* a byte for each place, which goes first between two that are level */
};

static uint32_t guest_entries;   /* GUESTS x ENTRY bytes of guest memory, or 0 before the guests load */

static uint32_t call_guest(uint32_t address, const uint32_t *args, unsigned count)
{
    const RecompRegisters saved = recomp_runtime.registers;
    kernel_call_guest(address, args, count);
    const uint32_t result = recomp_runtime.registers.eax;
    recomp_runtime.registers = saved;
    return result;
}

/* A place's seven dwords, or NULL for a guest's place without its guest */
static uint32_t *entry(uint32_t place)
{
    if (place < PLACES) return recomp_memory_u32(RECORDS + 8u + place * ENTRY);
    if (place < ALL && guest_entries != 0u && sc2_guest_loaded(27u + (place - PLACES)))
        return recomp_memory_u32(guest_entries + (place - PLACES) * ENTRY);
    return NULL;
}

static void count_up(uint32_t *count)
{
    if (*count < MOST) ++*count;
}

static uint32_t place_character(uint32_t place)
{
    const uint32_t args[] = {place};
    return call_guest(0x36A40u, args, 1u);
}

/* allowed in the records' ranking (0x464E0(character, 1, 0)); the guests loaded are */
static bool ranked(uint32_t place)
{
    if (place >= PLACES) return entry(place) != NULL;
    const uint32_t args[] = {place_character(place), 1u, 0u};
    return call_guest(0x464E0u, args, 3u) != 0u;
}

/* unlocked (0x56840(character, 0) of 2 or more), as the usage page's ranking asks; the guests loaded are */
static bool unlocked(uint32_t place)
{
    if (place >= PLACES) return entry(place) != NULL;
    const uint32_t args[] = {place_character(place), 0u};
    return (int32_t)call_guest(0x56840u, args, 2u) >= 2;
}

/* Between two level places the one with the lower order goes first. The regular cast's orders are 0 to 0x13, Inferno's
   and the bonus fighters' (places 20 to 23) 0x14 to 0x17; the pages show as many rows as the first 20 places have
   characters unlocked (0xC7F20), so the last ones ranked are not shown. The guests come after the cast and before
   Inferno and the bonus fighters, so with their rows added both are shown. */
static unsigned tie_order(uint32_t place)
{
    return place < PLACES ? 4u * *recomp_memory(TIE_ORDER + place, 1u) : 4u * 0x13u + 1u + (place - PLACES);
}

/* the original's ret n: the return address and n bytes of arguments */
static void ret(uint32_t argument_bytes)
{
    recomp_runtime.registers.esp += 4u + argument_bytes;
}

/* 0xC4940(slot, character), thiscall: a play of the character by the player in the slot (0 or 1) */
static void play_counted(void)
{
    const uint32_t esp = recomp_runtime.registers.esp, records = recomp_runtime.registers.ecx;
    const uint32_t slot = *recomp_memory_u32(esp + 4u), character = *recomp_memory_u32(esp + 8u);
    uint32_t place = (character == 27u || character == 28u) ? PLACES + (character - 27u) : 0u;
    if (place < PLACES || entry(place) == NULL) {
        const uint32_t args[] = {(character == 27u || character == 28u) ? 29u : character};
        place = call_guest(0x36A70u, args, 1u);
    }
    *recomp_memory_u32(records + 0x2B8u + slot * 4u) = place;
    uint32_t *e = entry(place);
    if (e != NULL) count_up(&e[0]);
    *recomp_memory_u32(records + 0x2C8u) |= 1u;
    ret(8u);
}

/* 0xC4980(winner's slot, or -1 for a draw), thiscall */
static void result_counted(void)
{
    const uint32_t esp = recomp_runtime.registers.esp, records = recomp_runtime.registers.ecx;
    const uint32_t winner = *recomp_memory_u32(esp + 4u);
    const uint32_t other_modes = *recomp_memory_u32(records + 0x2B0u) != 8u;
    uint32_t *const places = recomp_memory_u32(records + 0x2B8u);
    if (winner != 0xFFFFFFFFu) {
        uint32_t *w = entry(places[winner & 1u]), *l = entry(places[(winner & 1u) ^ 1u]);
        if (w != NULL) count_up(&w[1u + other_modes]);
        if (l != NULL) count_up(&l[3u + other_modes]);
    } else {
        for (unsigned slot = 0; slot < 2u; ++slot) {
            uint32_t *e = entry(places[slot]);
            if (e != NULL) count_up(&e[5u + other_modes]);
        }
    }
    *recomp_memory_u32(records + 0x2C8u) |= 1u;
    ret(4u);
}

/* the place at a rank (0 the first), or 23 (the original read past its 24) */
static uint32_t at_rank(const int rank_of[ALL], int rank)
{
    for (uint32_t place = 0; place < ALL; ++place)
        if (rank_of[place] == rank) return place;
    return PLACES - 1u;
}

/* 0xC4C50(row out, rank), thiscall: the usage page's row at the rank. A row: the place (byte +0), 0 (byte +2), the
   plays (+0x20) and the share of all plays in percent (float +0x24). Places not unlocked are not ranked. */
static void usage_row(void)
{
    const uint32_t esp = recomp_runtime.registers.esp;
    const uint32_t out = *recomp_memory_u32(esp + 4u);
    const int rank = (int)*recomp_memory_u32(esp + 8u);
    uint32_t plays[ALL] = {0}, total = 0u;
    float share[ALL] = {0};
    bool present[ALL];
    for (uint32_t p = 0; p < ALL; ++p) {
        const uint32_t *e = entry(p);
        present[p] = e != NULL;
        if (e) plays[p] = e[0];
        total += plays[p];
    }
    if (total != 0u)
        for (uint32_t p = 0; p < ALL; ++p)
            if (present[p]) share[p] = ranked(p) ? (float)((double)plays[p] * 100.0 / (double)total) : -1.0f;
    bool open[ALL];
    for (uint32_t p = 0; p < ALL; ++p) open[p] = present[p] && unlocked(p);
    int rank_of[ALL];
    for (uint32_t d = 0; d < ALL; ++d) {
        if (!open[d]) { rank_of[d] = -1; continue; }
        int above = 0;
        for (uint32_t e = 0; e < ALL; ++e) {
            if (e == d || !open[e]) continue;
            if (share[e] > share[d] || (share[e] == share[d] && tie_order(e) < tie_order(d))) ++above;
        }
        rank_of[d] = above;
    }
    const uint32_t place = at_rank(rank_of, rank);
    *recomp_memory(out, 1u) = (uint8_t)place;
    *recomp_memory(out + 2u, 1u) = 0u;
    *recomp_memory_u32(out + 0x20u) = plays[place];
    memcpy(recomp_memory(out + 0x24u, 4u), &share[place], 4u);
    ret(8u);
}

/* 0xC4A60(row out, rank, which: 0 mode 8's, 1 the others'), thiscall: a win ratio page's row at the rank. A row: the
   place (byte +0), 0 (byte +2), wins (+0x10), losses (+0x14), draws (+0x18), the wins' share in percent (float
   +0x24). Level ratios go by more wins, then fewer losses, fewer draws, then the tie order. */
static void win_ratio_row(void)
{
    const uint32_t esp = recomp_runtime.registers.esp;
    const uint32_t out = *recomp_memory_u32(esp + 4u);
    const int rank = (int)*recomp_memory_u32(esp + 8u);
    const uint32_t which = *recomp_memory_u32(esp + 12u) != 0u;
    uint32_t wins[ALL] = {0}, losses[ALL] = {0}, draws[ALL] = {0};
    float ratio[ALL] = {0};
    bool present[ALL];
    for (uint32_t p = 0; p < ALL; ++p) {
        const uint32_t *e = entry(p);
        present[p] = e != NULL;
        if (!e) continue;
        wins[p] = e[1u + which];
        losses[p] = e[3u + which];
        draws[p] = e[5u + which];
        if (!ranked(p)) {
            ratio[p] = -1.0f;
            continue;
        }
        const uint32_t games = wins[p] + losses[p] + draws[p];
        if (games != 0u) ratio[p] = (float)((double)wins[p] / (double)games);
    }
    int rank_of[ALL];
    for (uint32_t d = 0; d < ALL; ++d) {
        if (!present[d]) { rank_of[d] = -1; continue; }
        int above = 0;
        for (uint32_t e = 0; e < ALL; ++e) {
            if (e == d || !present[e]) continue;
            if (ratio[e] > ratio[d]) { ++above; continue; }
            if (ratio[e] != ratio[d]) continue;
            if (wins[e] != wins[d]) { above += wins[e] > wins[d]; continue; }
            if (losses[e] != losses[d]) { above += losses[e] < losses[d]; continue; }
            if (draws[e] != draws[d]) { above += draws[e] < draws[d]; continue; }
            above += tie_order(e) < tie_order(d);
        }
        rank_of[d] = above;
    }
    const uint32_t place = at_rank(rank_of, rank);
    const float percent = ratio[place] * 100.0f;
    *recomp_memory(out, 1u) = (uint8_t)place;
    *recomp_memory(out + 2u, 1u) = 0u;
    *recomp_memory_u32(out + 0x10u) = wins[place];
    *recomp_memory_u32(out + 0x14u) = losses[place];
    *recomp_memory_u32(out + 0x18u) = draws[place];
    memcpy(recomp_memory(out + 0x24u, 4u), &percent, 4u);
    ret(12u);
}

/* 0xC7F20(..., kind, ...), thiscall: a records page (its kind at +0xD1, its rows at +0xD3) */
static void page_set_up(void)
{
    const uint32_t page = recomp_runtime.registers.ecx;
    sub_000C7F20();
    const uint8_t kind = *recomp_memory(page + 0xD1u, 1u);
    if (kind != 0x0Eu && kind != 0x0Fu && kind != 0x10u) return;
    for (uint32_t p = PLACES; p < ALL; ++p)
        if (entry(p) != NULL) ++*recomp_memory(page + 0xD3u, 1u);
}

/* 0xC3070(kind, ...), thiscall: the Records screen's list (its kind at +0x60). It counts its rows as 0xC7F20 does,
   adding each bonus fighter allowed (0x464E0), and keeps the rows past the 10 shown at +0xE8, how far it scrolls. */
static void list_set_up(void)
{
    const uint32_t list = recomp_runtime.registers.ecx;
    sub_000C3070();
    const uint32_t kind = *recomp_memory_u32(list + 0x60u);
    if (kind != 0x0Eu && kind != 0x0Fu && kind != 0x10u) return;
    for (uint32_t p = PLACES; p < ALL; ++p)
        if (entry(p) != NULL) ++*recomp_memory_u32(list + 0xE8u);
}

/* 0xC46A0: the records emptied (a new save) */
static void records_emptied(void)
{
    sub_000C46A0();
    if (guest_entries != 0u) memset(recomp_memory(guest_entries, GUESTS * ENTRY), 0, GUESTS * ENTRY);
}

void sc2_records_setup(void)
{
    if (guest_entries == 0u) guest_entries = recomp_kernel_allocate_pool(GUESTS * ENTRY);
    if (guest_entries != 0u) memset(recomp_memory(guest_entries, GUESTS * ENTRY), 0, GUESTS * ENTRY);
}

/* The guests' entries in the save, after its fields (sc2_guest_items.c): SC2_RECORDS_SAVE_BYTES bytes, a mark then
   the entries; a save without the mark (before this, or without the guests) has none. */
enum { RECORDS_MARK = 0x5Cu };

void sc2_records_save(uint8_t *at)
{
    if (guest_entries == 0u) return;
    at[0] = RECORDS_MARK;
    memcpy(at + 1, recomp_memory(guest_entries, GUESTS * ENTRY), GUESTS * ENTRY);
}

void sc2_records_load(const uint8_t *at)
{
    if (guest_entries == 0u) return;
    if (at[0] == RECORDS_MARK) memcpy(recomp_memory(guest_entries, GUESTS * ENTRY), at + 1, GUESTS * ENTRY);
    else memset(recomp_memory(guest_entries, GUESTS * ENTRY), 0, GUESTS * ENTRY);
}

RecompFunction sc2_records_lookup(uint32_t address)
{
    switch (address) {
    case 0x000c4940u: return play_counted;
    case 0x000c4980u: return result_counted;
    case 0x000c4c50u: return usage_row;
    case 0x000c4a60u: return win_ratio_row;
    case 0x000c7f20u: return page_set_up;
    case 0x000c3070u: return list_set_up;
    case 0x000c46a0u: return records_emptied;
    default: return NULL;
    }
}
