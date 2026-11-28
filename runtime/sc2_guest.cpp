/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Link in the guest's place. The data (models, moves, sounds, the select
   screens' art) comes from the rebuilt root.olk; the names are the
   program's own strings, rewritten here. Facts from Default.xbe (docs/guests.md):

   - Spawn's profile record, 0x19f050: pointers to his short name (0x177984
     "Spawn"), Japanese name (0x177978), select name (0x177970 "SPAWN"),
     weapon (0x177968 "Agony", pointer at 0x19f080), birthplace (0x177954
     "Detroit/Michigan") and age (0x177950 "31").
   - The name records in .data: a character ID byte then the name, 0x1d
     SPAWN; several copies.
   Link's own values are the GameCube program's: Link, リンク, LINK,
   Master Sword, Hyrule, "--". Heihachi's are the PS2 program's (SLUS_206.43):
   Heihachi, 平八, HEIHACHI, Iron Fist, Japan?, 75.

   The fighter setup (0x47880 here, 0x800a0e00 on the GameCube) sets nine
   weapon-bone modes a character, one a weapon bone 21-29, at the fighter's
   +0xb54, by character ID (the player's +0x5a, or +0x5c when +0x58 is 0x19).
   The guest's ID here is 0x1d, which takes the default (9, 0xe, then 0): Link
   then wore his shield at a fixed point in the stage and his quiver and arrow
   at his feet. The GameCube's case for Link (ID 27) sets 0xd, 9, 3, 9, 9, 9,
   0xd, 0xd (he is left-handed); it also loads an item model for three of his
   weapons (0x800a1cc4), which is not done here. The PS2 has no case for
   Heihachi: the default is his. */
#include "sc2_guest.h"
#include "sc2_version.h"

extern "C" {
#include "runtime.h"
}

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

extern "C" uint32_t recomp_kernel_allocate_pool(uint32_t size);
extern "C" uint64_t (*recomp_kernel_read_source_hook)(const char *host_path, uint64_t offset, uint32_t length);

namespace sc2 {

namespace {

enum class Guest { Spawn, Link, Heihachi };
Guest g_guest = Guest::Spawn;
bool g_roster_link = false;   /* Link as character 27 (ApplyGuestRoster) */
bool g_roster_column = false; /* a guest in the grid's fifth column */
bool g_grid_balanced = false; /* the grid laid out in rows of five (BalanceGrid) */
bool g_guest_loaded[2];       /* Link, Heihachi in the guests' file (sc2_guest_loaded) */
bool g_profile_item[2];       /* ... with their profile items, 48 and 49 (sc2_guest_profile_item) */
bool g_boss_form[2];          /* ... with the final boss's form for each (sc2_guest_boss_form) */

void Write(uint32_t address, const char *bytes, size_t length)
{
    std::memcpy(recomp_memory(address, length), bytes, length);
}

void WritePointer(uint32_t address, uint32_t value)
{
    const char bytes[4] = {static_cast<char>(value), static_cast<char>(value >> 8),
                           static_cast<char>(value >> 16), static_cast<char>(value >> 24)};
    Write(address, bytes, 4);
}

/* The name records: ID 0x1d then "SPAWN", in the program's data (slots of
   0x30 bytes, room for any of the names). */
void RenameRecords(const char *name, size_t length)
{
    static const char record[] = "\x1dSPAWN";
    for (uint32_t address = 0x00340000u; address < 0x00360000u; ++address) {
        if (std::memcmp(recomp_memory(address, sizeof record), record, sizeof record) == 0)
            Write(address + 1u, name, length);
    }
}

} // namespace

void ApplyGuestLink()
{
    g_guest = Guest::Link;

    Write(0x00177970u, "LINK\0", 6);
    Write(0x00177984u, "Link\0", 6);
    Write(0x00177978u, "\x83\x8a\x83\x93\x83\x4e\0", 8);   /* リンク (Shift-JIS) */
    Write(0x00177950u, "--\0", 3);
    /* "Detroit/Michigan" and its padding (20 bytes) hold both of Link's longer
       strings; the weapon pointer moves to the second. */
    Write(0x00177954u, "Hyrule\0Master Sword", 20);
    WritePointer(0x0019f080u, 0x00177954u + 7u);
    RenameRecords("LINK\0", 6);
}

void ApplyGuestHeihachi()
{
    g_guest = Guest::Heihachi;
    /* Spawn's strings sit together, 0x177950-0x17798b: age (4 bytes),
       birthplace (20), weapon (8), select name (8), Japanese name (12), name
       (8). Heihachi's longer names take the room of the ones before them; the
       record's pointers follow (the weapon's first, as the others then take
       its old place). */
    Write(0x00177950u, "75\0", 3);
    Write(0x00177954u, "Japan?\0Iron Fist\0", 17);
    WritePointer(0x0019f080u, 0x00177954u + 7u);
    Write(0x00177968u, "\x95\xbd\x94\xaa\0", 5);   /* 平八 (Shift-JIS) */
    Write(0x00177970u, "HEIHACHI\0", 9);
    Write(0x0017797cu, "Heihachi\0", 9);
    for (uint32_t at = 0x0019f050u; at < 0x0019f0a0u; at += 4u) {
        if (at == 0x0019f080u) continue;
        uint32_t value;
        std::memcpy(&value, recomp_memory(at, 4), 4);
        if (value == 0x00177978u) WritePointer(at, 0x00177968u);        /* the Japanese name */
        else if (value == 0x00177984u) WritePointer(at, 0x0017797cu);   /* the name */
    }
    RenameRecords("HEIHACHI\0", 9);
}

namespace {

/* The program's tables, by character (docs/guests.md, "Characters beside
   Spawn"): each character's first /4 entry (s16), its second-costume flag
   (u8), its 0xf8-byte profile, its select-name glyph string (pointer), what
   each mode allows it (u8[6]), its select-grid cell (u8); and the grid itself,
   5 columns of 6 cells of 36 bytes, the fifth column empty. */
/* The items below 0x58: a record of 8 bytes each (kind, 0, picture, flags) */
constexpr uint32_t kItems = 0x001a1ff8u;
constexpr uint32_t kBlockStart = 0x0019f430u, kSecondCostume = 0x0019f474u, kProfiles = 0x0019d438u,
                   kGlyphNames = 0x001a2450u, kAllowed = 0x001a07f0u, kCellOf = 0x0017a7c8u, kEndings = 0x0034ed88u,
                   kGrid = 0x0017a390u, kAvailable = 0x00069890u, kGallery = 0x0034e578u;
constexpr int kSpawn = 29;

struct RosterGuest {
    int character, block, cell, costumes;
};
/* The guests' file's /2, as this program's disc numbers it (guest/build.cpp, Layout): the European disc's entries from
   the US disc's 95 on are one lower, and its large select-grid icons (/2/10 part 0) 6 more. */
constexpr uint32_t kUiEntries = SC2_PAL ? 585u : 516u;
constexpr uint32_t Ui(uint32_t us) { return SC2_PAL && us >= 88u ? us - 1u : us; }
constexpr uint32_t kFirstNewLarge = SC2_PAL ? 154u : 148u;   /* the large icons roster.py appends */

/* A weapon's parts as the Extra modes' weapon choice shows it (0x84F40, from 0x47DB0, the shown weapon's drawing): by
   character, {count, pointer} at 0x17D388, each part a joint (u32) then its offset and rotation (3 floats each). The
   Xbox's table has none for 27 and 28, so the guests' weapons were never placed there and did not show. Their own
   are their programs' (each program's table has its guest's row): Link's two (sword, shield) at 0x802518CC on the
   GameCube (big-endian there), Heihachi's two (his gauntlets) at 0x490960 on the PS2. */
constexpr uint32_t kWeaponParts = 0x0017d388u;
struct WeaponPart {
    uint32_t joint;
    float place[6];
};
static_assert(sizeof(WeaponPart) == 0x1c, "a weapon part is 0x1c bytes");
const WeaponPart kLinkWeaponParts[2] = {
    {0u, {-0.249056771f, -0.458585054f, -0.0500000007f, 0.249999955f, 0.0f, 0.249999955f}},
    {0u, {0.15094322f, 0.00999999978f, 0.00999999978f, 0.0f, -0.249999955f, 0.0f}},
};
const WeaponPart kHeihachiWeaponParts[2] = {
    {0u, {0.0934636369f, 0.0199999996f, -0.100000001f, -0.250206202f, -0.078572087f, -0.252249748f}},
    {0u, {-0.0664619505f, -0.0299999993f, 0.100000001f, 0.339396f, -0.136986092f, -0.299951851f}},
};

/* Link and Heihachi: their blocks (roster.py) and the fifth column's top two cells. */
const RosterGuest kRoster[2] = {{27, 24, 4, 4}, {28, 25, 9, 2}};
/* ... and the final boss's form for each on its own disc (the Xbox's guest slot has 10) */
constexpr uint32_t kBossForm[2] = {9u, 11u}, kSlotBossForm = 10u;

/* An olnk container's entry (offset in the file, size), or size 0 for a hole. */
bool Entry(std::FILE *f, uint64_t container, uint32_t index, uint64_t &at, uint32_t &size)
{
    uint32_t head[8], entry[4];
    if (std::fseek(f, long(container), SEEK_SET) || std::fread(head, 4, 8, f) != 8 ||
        std::memcmp(&head[1], "olnk", 4) != 0 || index >= head[0])
        return false;
    if (std::fseek(f, long(container + 0x20u + 16u * index), SEEK_SET) || std::fread(entry, 4, 4, f) != 4)
        return false;
    at = container + head[4] + entry[0];
    size = entry[1];
    return true;
}

template <typename T> void Put(uint32_t address, T value) { std::memcpy(recomp_memory(address, sizeof value), &value, sizeof value); }
template <typename T> T Get(uint32_t address)
{
    T value;
    std::memcpy(&value, recomp_memory(address, sizeof value), sizeof value);
    return value;
}

} // namespace

/* The Art Gallery (0x34E578: 45 slots for each of its galleries, each a thumbnail in its bank and the /2 entry
   of the picture; -1 none; the program indexes it gallery x 45 + slot, and lays each gallery out in a grid set by
   0xDCFA0: Hi-Res 5 x 4, Illustrations 9 x 5, Special 5 x 2). The guests' pictures, as their own discs have them
   (tools/guests/roster.py appends their thumbnails to each gallery's bank, Link's first):
   - Hi-Res (/2/395): their portraits (/2/422 Link, 423 Heihachi);
   - Illustrations (/2/396): Link's three (470-472), then Heihachi's two (473, 474);
   - Special (/2/397): their own versions of the two black-and-white pictures with Soul Edge and Soul Calibur, where
     the Xbox has Spawn's (/2/493 and 495): Link's (the file's 479, 480), Heihachi's (481, 482).
   A guests' file built before that (GUESTS_VERSION 4) has Heihachi's illustrations after the portraits in Hi-Res and
   nothing for Special. Illustrations then holds 47 pictures, past its 45 slots: the galleries are kept here, and the
   one opened is written to the table (Illustrations over Special's first slots; sc2_guest_gallery_open). */
constexpr int kGallerySlots = 54, kColumns[3] = {5, 9, 5}, kRows[3] = {4, 5, 2};
int g_gallery_columns[3] = {5, 9, 5};
int32_t g_gallery[3][kGallerySlots][2];
int g_gallery_rows[3] = {4, 5, 2};
bool g_gallery_kept = false;   /* (no guests' file: the program's table as it is) */

static void WriteGallery(int gallery, int slots)
{
    std::memcpy(recomp_memory(kGallery + 8u * uint32_t(gallery * 45), 8u * uint32_t(slots)), g_gallery[gallery],
                8u * uint32_t(slots));
}

static void ApplyGallery(bool link, bool heihachi, bool special)
{
    for (int g = 0; g < 3; ++g) {
        std::memcpy(g_gallery[g], recomp_memory(kGallery + 8u * uint32_t(g * 45), 8u * 45u), 8u * 45u);
        for (int at = 45; at < kGallerySlots; ++at) g_gallery[g][at][0] = g_gallery[g][at][1] = -1;
    }
    std::vector<std::pair<int32_t, int32_t>> add[3];   /* (thumbnail, picture) after each gallery's own */
    int32_t thumbnail[3] = {21, 43, 11};               /* each bank's first appended */
    auto put = [&](int g, uint32_t picture) { add[g].emplace_back(thumbnail[g]++, int32_t(Ui(picture))); };
    if (link) put(0, 422);
    if (heihachi) put(0, 423);
    if (heihachi && !special) put(0, 473), put(0, 474);
    if (link) put(1, 470), put(1, 471), put(1, 472);
    if (heihachi && special) put(1, 473), put(1, 474);
    if (link && special) put(2, 479), put(2, 480);
    if (heihachi && special) put(2, 481), put(2, 482);
    const int first[3] = {20, 42, 10};
    for (int g = 0; g < 3; ++g) {
        int at = first[g];
        for (const auto &[t, picture] : add[g]) g_gallery[g][at][0] = t, g_gallery[g][at][1] = picture, ++at;
        /* (a row more, or for Illustrations, whose five rows of nine fill the screen's height, a tenth column) */
        g_gallery_columns[g] = g == 1 && at > kColumns[g] * kRows[g] ? kColumns[g] + 1 : kColumns[g];
        g_gallery_rows[g] = std::max(kRows[g], (at + g_gallery_columns[g] - 1) / g_gallery_columns[g]);
        for (; at < g_gallery_columns[g] * g_gallery_rows[g]; ++at) g_gallery[g][at][0] = g_gallery[g][at][1] = -1;
        WriteGallery(g, 45);
    }
    g_gallery_kept = true;
}

/* The grid in rows of five (issue #6). The guests are first put in the fifth column (cells 4 and 9); here every
   character is laid out again in reading order, five to a row, the guests after Spawn: 25 characters fill five
   rows. Random keeps cell 26 (the program names it there in several places), alone in the sixth row; it is drawn
   under the middle column and the cursor reaches it from the bottom of any column (sc2_hooks.c, cell_place and
   cursor_move). Each character's cell (0x17A7C8) follows; a character the grid does not show (the IDs that pointed
   at the empty cell 19) points at cell 29, empty in every layout. */
static void BalanceGrid()
{
    constexpr int kCells = 30, kRandom = 26, kNowhere = 29;
    uint8_t cells[kCells][36];
    std::memcpy(cells, recomp_memory(kGrid, sizeof cells), sizeof cells);
    auto character = [&](int cell) { int16_t c; std::memcpy(&c, cells[cell], 2); return int(c); };
    std::vector<int> order, guests;
    for (int cell = 0; cell < kCells; ++cell) {
        const int c = character(cell);
        if (cell == kRandom || c < 0) continue;
        (c == 27 || c == 28 ? guests : order).push_back(cell);
    }
    for (size_t at = 0; at < order.size(); ++at) {
        if (character(order[at]) != kSpawn) continue;
        order.insert(order.begin() + long(at + 1), guests.begin(), guests.end());
        guests.clear();
        break;
    }
    order.insert(order.end(), guests.begin(), guests.end());
    if (order.size() > size_t(kRandom - 1)) return;   /* (more than five rows: as it was) */

    uint8_t laid[kCells][36] = {};
    for (int cell = 0; cell < kCells; ++cell) {
        const int16_t none = -1;
        std::memcpy(laid[cell], &none, 2);
    }
    std::memcpy(laid[kRandom], cells[kRandom], 36);
    for (int id = 0; id <= 32; ++id) Put<uint8_t>(kCellOf + uint32_t(id), uint8_t(kNowhere));   /* (IDs 0-32) */
    for (size_t at = 0; at < order.size(); ++at) {
        std::memcpy(laid[at], cells[order[at]], 36);
        Put<uint8_t>(kCellOf + uint32_t(character(order[at])), uint8_t(at));
    }
    std::memcpy(recomp_memory(kGrid, sizeof laid), laid, sizeof laid);
    g_grid_balanced = true;
}

int32_t GalleryPicture(int gallery, int slot)
{
    if (gallery < 0 || gallery > 2 || slot < 0 || slot >= g_gallery_columns[gallery] * g_gallery_rows[gallery]) return -1;
    const int32_t picture = g_gallery_kept ? g_gallery[gallery][slot][1]
                                           : Get<int32_t>(kGallery + 8u * uint32_t(gallery * 45 + slot) + 4u);
    return picture > 0 ? picture : -1;
}

bool GuestFileMatches(const char *path)
{
    std::FILE *f = std::fopen(path, "rb");
    if (!f) return false;
    uint64_t ui;
    uint32_t size, count = 0;
    const bool read = Entry(f, 0, 2, ui, size) && std::fseek(f, long(ui), SEEK_SET) == 0 && std::fread(&count, 4, 1, f) == 1;
    std::fclose(f);
    /* (the disc's /2 entries, then, in a file built since, the GameCube's and the PS2's title pictures: SetUpTitles) */
    if (read && (count == kUiEntries || count == kUiEntries + 6u || count == kUiEntries + 10u)) return true;
    std::fprintf(stderr, "recomp sc2: the guests' file %s was built from %s, not this one: playing without the guests\n",
                 path, !read ? "something else than a disc" : count == 585u || count == 591u || count == 595u ? "the European disc"
                                                          : count == 516u || count == 522u || count == 526u ? "the US disc" : "another disc");
    return false;
}

/* The title screen (scene 5) shows one of three pictures, /2/12-14, by the save's progress (a new save the first).
   The guests' file has the GameCube's and the PS2's three after the disc's /2 entries (guest/build.cpp, titles), the
   same size as the Xbox's: as the game reads one of its own, the read takes one of the consoles' at random, from
   those the file has (the Xbox's own among them). */
struct Title {
    uint64_t at = 0;
    uint32_t size = 0;
    uint64_t others[2] = {};
    const char *names[2] = {};
    uint32_t other_count = 0;
};
Title g_titles[3];
std::string g_titles_path;

uint64_t TitleSource(const char *host_path, uint64_t offset, uint32_t length)
{
    if (host_path == nullptr) return offset;
    const size_t path_length = std::strlen(host_path);   /* (the guests' file, by its name: the path may be written another way) */
    if (path_length < g_titles_path.size() || std::strcmp(host_path + path_length - g_titles_path.size(), g_titles_path.c_str()) != 0)
        return offset;
    for (const Title &t : g_titles) {
        if (t.other_count == 0u || offset != t.at || length < t.size) continue;
        static std::mt19937 random{std::random_device{}()};
        const uint32_t pick = uint32_t(random() % (t.other_count + 1u));
        std::fprintf(stderr, "recomp sc2: the title screen's picture %u, the %s's\n", uint32_t(&t - g_titles),
                     pick == 0u ? "Xbox" : t.names[pick - 1u]);
        return pick == 0u ? offset : t.others[pick - 1u];
    }
    return offset;
}

/* The Collection History's pictures of the guests' own items, after the title pictures (guest/build.cpp,
   EXTRA_PICTURES): their /2 entries, or 0 for none (a guests' file built before them, or a hole for a disc not given) */
uint32_t g_extra_picture[4];

void SetUpTitles(std::FILE *f, uint64_t ui, const char *path)
{
    constexpr uint32_t kTitleFirst = 12u, kTitlePictures = 3u;   /* (guest/build.h, TITLE_FIRST, TITLE_PICTURES) */
    uint32_t count = 0;
    if (std::fseek(f, long(ui), SEEK_SET) || std::fread(&count, 4, 1, f) != 1 || count < kUiEntries + 2u * kTitlePictures)
        return;   /* (a guests' file built before these) */
    for (uint32_t k = 0; k < 4u && count >= kUiEntries + 2u * kTitlePictures + 4u; ++k) {
        uint64_t at;
        uint32_t size;
        const uint32_t entry = kUiEntries + 2u * kTitlePictures + k;
        g_extra_picture[k] = Entry(f, ui, entry, at, size) && size != 0u ? entry : 0u;
    }
    for (uint32_t k = 0; k < kTitlePictures; ++k) {
        Title &t = g_titles[k];
        if (!Entry(f, ui, Ui(kTitleFirst + k), t.at, t.size) || t.size == 0u) continue;
        for (uint32_t console = 0; console < 2u; ++console) {
            uint64_t at;
            uint32_t size;
            if (Entry(f, ui, kUiEntries + console * kTitlePictures + k, at, size) && size == t.size) {
                t.names[t.other_count] = console == 0u ? "GameCube" : "PlayStation 2";
                t.others[t.other_count++] = at;
            }
        }
    }
    const char *name = std::strrchr(path, '/');
#ifdef _WIN32
    if (const char *back = std::strrchr(path, '\\'); back != nullptr && (name == nullptr || back > name)) name = back;
#endif
    g_titles_path = name != nullptr ? name : path;
    recomp_kernel_read_source_hook = TitleSource;
}

bool ApplyGuestRoster(const char *path)
{
    std::FILE *f = std::fopen(path, "rb");
    if (!f) return false;
    uint64_t characters;
    uint32_t size;
    std::vector<uint8_t> profiles[2];
    bool ok = Entry(f, 0, 4, characters, size);
    /* (the picture each guest's profile unlock shows, /2/345 and /2/346: a guests' file built before these has none,
       and the game would wait for it for ever, so the guest keeps the Xbox's none) */
    bool picture[2] = {false, false};
    uint64_t ui = 0;
    uint32_t ui_size;
    const bool have_ui = ok && Entry(f, 0, 2, ui, ui_size);
    if (have_ui) {
        for (int g = 0; g < 2; ++g) {
            uint64_t at;
            uint32_t length;
            picture[g] = Entry(f, ui, Ui(345u + uint32_t(g)), at, length) && length != 0u;
        }
    }
    /* (the guests' Special pictures, /2/479 and 481: a guests' file built before these has none, and Heihachi's
       illustrations in Hi-Res; ApplyGallery) */
    bool special = false;
    for (uint32_t us : {479u, 481u}) {
        uint64_t at;
        uint32_t length;
        special = special || (have_ui && Entry(f, ui, Ui(us), at, length) && length != 0u);
    }
    /* (the final boss's form for each guest, /4 entries 7 and 23 + the form: Link's 9, Heihachi's 11, as on their
       own discs; tools/guests/forms.py. A guests' file built before these has none.) */
    bool form[2] = {false, false};
    for (int g = 0; ok && g < 2; ++g) {
        uint64_t at;
        uint32_t first = 0, second = 0;
        form[g] = Entry(f, characters, 7u + kBossForm[g], at, first) && Entry(f, characters, 23u + kBossForm[g], at, second) &&
                  first != 0u && second != 0u;
    }
    for (int g = 0; ok && g < 2; ++g) {
        uint64_t at;
        if (!Entry(f, characters, uint32_t(46 + 39 * kRoster[g].block + 38), at, size)) { ok = false; break; }
        if (size < 0x10u + 0xf8u) continue;   /* (no disc for this guest) */
        profiles[g].resize(size);
        if (std::fseek(f, long(at), SEEK_SET) || std::fread(profiles[g].data(), 1, size, f) != size ||
            std::memcmp(profiles[g].data(), "GPRF", 4) != 0)
            ok = false;
    }
    if (ok && have_ui) SetUpTitles(f, ui, path);
    std::fclose(f);
    if (!ok) return false;

    uint32_t small = 75u, large = kFirstNewLarge;   /* the icons roster.py appends, Link's first */
    for (int g = 0; g < 2; ++g) {
        const RosterGuest &guest = kRoster[g];
        const std::vector<uint8_t> &p = profiles[g];
        if (p.empty()) continue;
        uint32_t words[4];
        std::memcpy(words, p.data(), sizeof words);
        if (int(words[1]) != guest.character) return false;
        /* The profile's strings stay where the entry has them, in guest memory. */
        const uint32_t base = recomp_kernel_allocate_pool(uint32_t(p.size()));
        if (!base) return false;
        std::memcpy(recomp_memory(base, p.size()), p.data(), p.size());
        const uint32_t record = kProfiles + 0xf8u * uint32_t(guest.character);
        std::memcpy(recomp_memory(record, 0xf8), p.data() + 0x10, 0xf8);
        for (uint32_t at = 0; at < 0x78u; at += 4u) {
            const uint32_t offset = Get<uint32_t>(record + at);
            if (offset) Put<uint32_t>(record + at, base + offset);
        }
        if (words[2]) Put<uint32_t>(kGlyphNames + 4u * uint32_t(guest.character), base + words[2]);
        Put<int16_t>(kBlockStart + 2u * uint32_t(guest.character), int16_t(46 + 39 * guest.block));
        Put<uint8_t>(kSecondCostume + uint32_t(guest.character),
                     guest.costumes == 4 ? uint8_t(0) : Get<uint8_t>(kSecondCostume + kSpawn));
        std::memcpy(recomp_memory(kAllowed + 6u * uint32_t(guest.character), 6),
                    recomp_memory(kAllowed + 6u * kSpawn, 6), 6);
        /* The cell: the guest's own (its costumes and where its model stands) with this program's routine
           and the icons appended for it. */
        uint8_t cell[36];
        std::memcpy(cell, p.data() + words[3], sizeof cell);
        std::memcpy(cell + 4, &kAvailable, 4);
        std::memset(cell + 8, 0, 16);
        for (int k = 0; k < guest.costumes; ++k) {
            cell[8 + k] = uint8_t(small + uint32_t(k));
            cell[16 + k] = uint8_t(large + uint32_t(k));
        }
        std::memcpy(recomp_memory(kGrid + 36u * uint32_t(guest.cell), sizeof cell), cell, sizeof cell);
        Put<uint8_t>(kCellOf + uint32_t(guest.character), uint8_t(guest.cell));
        small += uint32_t(guest.costumes);
        large += uint32_t(guest.costumes);
        {
            const WeaponPart *parts = guest.character == 27 ? kLinkWeaponParts : kHeihachiWeaponParts;
            const uint32_t at = recomp_kernel_allocate_pool(uint32_t(2u * sizeof(WeaponPart)));
            if (at) {
                std::memcpy(recomp_memory(at, 2u * sizeof(WeaponPart)), parts, 2u * sizeof(WeaponPart));
                Put<int32_t>(kWeaponParts + 8u * uint32_t(guest.character), 2);
                Put<uint32_t>(kWeaponParts + 8u * uint32_t(guest.character) + 4u, at);
            }
        }
        if (guest.character == 27) g_roster_link = true;
        if (guest.cell % 5 == 4) g_roster_column = true;
        /* The Arcade ending (0xDE480): its row in the table by character (0x34ED88: the /2 entry of its picture,
           the lines of its text plus one, its first text in /2/87, 0). The Xbox's has none for 27 and 28 (the
           ending then loads entry -1), but /2/87 has their texts, at the places the PS2's table gives Heihachi
           (81-84) and before them Link (77-80); their pictures are the guests' file's /2/84 and /2/85. */
        if (guest.character == 27 || guest.character == 28) {
            const bool link = guest.character == 27;
            const int16_t row[4] = {int16_t(link ? 84 : 85), int16_t(link ? 10 : 12), int16_t(link ? 77 : 81), 0};
            std::memcpy(recomp_memory(kEndings + 8u * uint32_t(guest.character), sizeof row), row, sizeof row);
        }
        std::fprintf(stderr, "recomp sc2: character %d from the guests' file (block %d, cell %d)\n",
                     guest.character, guest.block, guest.cell);
    }
    if (g_roster_column) BalanceGrid();
    ApplyGallery(!profiles[0].empty(), !profiles[1].empty(), special);   /* (Link, Heihachi) */
    /* Their profiles, items of their own: the two the game leaves empty (48, 49), as every character's profile item
       is (kind 3, the character's picture 156 + its ID; the unlock's picture at /2/297 + the item, built from the
       guest's disc, tools/guests/roster.py); sc2_hooks.c, profile_item, gives them to Link and Heihachi, so that
       clearing Arcade unlocks them and the Museum's profile viewer asks for them. */
    for (int g = 0; g < 2; ++g) {
        g_profile_item[g] = !profiles[g].empty() && picture[g];
        if (!g_profile_item[g]) continue;
        const uint16_t record[4] = {3u, 0u, uint16_t(156u + 27u + uint32_t(g)), 0x500u};
        std::memcpy(recomp_memory(kItems + 8u * (48u + uint32_t(g)), sizeof record), record, sizeof record);
    }
    g_guest_loaded[0] = !profiles[0].empty();
    g_guest_loaded[1] = !profiles[1].empty();
    if (g_guest_loaded[0] || g_guest_loaded[1]) {
        sc2_guest_items_setup();
        sc2_records_setup();
    }
    g_boss_form[0] = g_guest_loaded[0] && form[0];
    g_boss_form[1] = g_guest_loaded[1] && form[1];
    return true;
}

} // namespace sc2

/* 0xDCFA0(gallery object, gallery) has set the gallery's grid (sc2_hooks.c, gallery_grid): with the guests' pictures
   a gallery may have more rows, and the table gets the gallery opened, in full (Illustrations' cells past the 45 over
   Special's first slots, which are written back when Special opens). */
extern "C" void sc2_guest_gallery_open(uint32_t object, uint32_t gallery)
{
    using namespace sc2;
    if (gallery > 2u || !g_gallery_kept) return;
    const int g = int(gallery), cells = g_gallery_columns[g] * g_gallery_rows[g];
    for (int other = 0; other < 3; ++other) WriteGallery(other, 45);
    WriteGallery(g, cells);
    *recomp_memory(object + 0x7eu, 1u) = uint8_t(g_gallery_columns[g]);
    *recomp_memory(object + 0x7du, 1u) = uint8_t(g_gallery_rows[g]);
    *recomp_memory(object + 0x7cu, 1u) = uint8_t(cells);
}

/* 0xDD5B0(gallery object) has loaded the opened gallery's thumbnails and cleared the object's list of its cells'
   sprites (+0x74, after the pictures' room in the work area) for 45 cells; the game goes over every cell of the
   grid each frame, and skips a cell whose entry is 0. With more cells the entries past the 45 are cleared too. */
extern "C" void sc2_guest_gallery_cells(uint32_t object)
{
    using namespace sc2;
    int cells = 45;
    for (int g = 0; g_gallery_kept && g < 3; ++g) cells = std::max(cells, g_gallery_columns[g] * g_gallery_rows[g]);
    const uint32_t list = Get<uint32_t>(object + 0x74u);
    for (int cell = 45; list && cell < cells; ++cell) Put<uint32_t>(list + 4u * uint32_t(cell), 0u);
}

extern "C" int sc2_guest_extra_picture(unsigned which)
{
    return which < 4u && sc2::g_extra_picture[which] ? int(sc2::g_extra_picture[which]) : -1;
}

extern "C" int sc2_guest_loaded(uint32_t character)
{
    return (character == 27u || character == 28u) && sc2::g_guest_loaded[character - 27u];
}

extern "C" uint32_t sc2_guest_boss_form(uint32_t character)
{
    if ((character == 27u || character == 28u) && sc2::g_boss_form[character - 27u]) return sc2::kBossForm[character - 27u];
    return sc2::kSlotBossForm;
}

extern "C" int sc2_guest_profile_item(uint32_t character)
{
    return (character == 27u || character == 28u) && sc2::g_profile_item[character - 27u] ? int(48u + character - 27u) : -1;
}

extern "C" int sc2_select_grid_balanced(void)
{
    return sc2::g_grid_balanced;
}

extern "C" int sc2_guest_fifth_column(void)
{
    return sc2::g_roster_column;
}

extern "C" void sc2_guest_weapon_bones(uint32_t player, uint32_t fighter)
{
    using sc2::g_guest;
    using sc2::Guest;
    int16_t id;
    std::memcpy(&id, recomp_memory(player + 0x58u, 2), 2);
    std::memcpy(&id, recomp_memory(player + (id == 0x19 ? 0x5cu : 0x5au), 2), 2);
    /* Link as himself (27), or in the guest's place (Spawn's, 0x1d). */
    if (!(sc2::g_roster_link && id == 27) && !(g_guest == Guest::Link && id == 0x1d)) return;
    /* The pair of ID 22's case (left-handed: 0xd, 9), the rest 0. The GameCube's
       own (0xd, 9, 3, 9, 9, 9, 0xd, 0xd) stands the scabbard straight out of his
       back here, its item code (0x800a1cc4, the player's +0x3af0) not being in
       this program. shortcut: his shield is still not on his arm. */
    static const uint8_t link[9] = {0x0d, 0x09, 0, 0, 0, 0, 0, 0, 0};
    /* As himself (27, his own profile), the GameCube's own case. */
    static const uint8_t own[9] = {0x0d, 0x09, 0x03, 0x09, 0x09, 0x09, 0x0d, 0x0d, 0};
    std::memcpy(recomp_memory(fighter + 0xb54u, 9), id == 27 ? own : link, 9);
}
