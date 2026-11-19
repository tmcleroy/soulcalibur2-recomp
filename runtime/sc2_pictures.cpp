/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Export pictures (sc2_pictures.h). Facts from the disc (docs/guests.md has the Art Gallery's table):

   - The Art Gallery's table (sc2_guest.cpp, GalleryPicture) gives each slot of its three galleries (Hi-Res,
     Illustrations, Special) the /2 entry of root.olk that holds its picture. Read from memory, it has the guests'
     pictures too once the guests' file is in (ApplyGallery).
   - Each picture is a VXT bank (tools/guests/textures.py) of one texture: version 2, entry at 0x14 of 0x24 bytes
     (u32 descriptor, u32 flags, u32 0, u16 width, u16 height, u32 data offset, u32 D3D format, u16 stored width,
     u16 stored height, u32 levels). Every one on the US and European discs, and every guest's, is 512 x 512
     A8R8G8B8 (D3D format 6), swizzled: x and y bits interleaved, x first.
   - The Special gallery's pictures are narrower than their texture (388 of its 512 columns; the rest has alpha 0).
     Rows and columns at the edges that are wholly transparent are left out. */
#include "sc2_pictures.h"
#include "sc2_guest.h"
#include "sc2_jpeg.h"

#include "disc_image.h"
extern "C" {
#include "kernel_abi.h"
}

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <vector>

namespace sc2 {

namespace {

std::string root_olk_override;

using Reader = std::function<bool(uint64_t offset, void *buffer, size_t length)>;

/* root.olk as the game reads it: the guests' file, the disc image's, or the disc folder's */
bool OpenRootOlk(Reader &read, std::string &error)
{
    if (!root_olk_override.empty() || !recomp_disc_image_active()) {
        std::string path = root_olk_override;
        if (path.empty()) {
            if (recomp_disc_root_path == nullptr) {
                error = "The game's disc is not known.";
                return false;
            }
            const std::filesystem::path folder = std::filesystem::u8path(recomp_disc_root_path);
            path = (folder / "root.olk").u8string();
            std::error_code fault;
            if (!std::filesystem::is_regular_file(std::filesystem::u8path(path), fault))
                path = (folder / "ROOT.OLK").u8string();
        }
        auto file = std::make_shared<std::ifstream>(std::filesystem::u8path(path), std::ios::binary);
        if (!*file) {
            error = "Cannot read " + path + ".";
            return false;
        }
        read = [file](uint64_t offset, void *buffer, size_t length) {
            file->clear();
            file->seekg(static_cast<std::streamoff>(offset));
            file->read(static_cast<char *>(buffer), static_cast<std::streamsize>(length));
            return static_cast<size_t>(file->gcount()) == length;
        };
        return true;
    }
    RecompDiscImageEntry entry{};
    if (!recomp_disc_image_lookup("root.olk", &entry) || entry.directory) {
        error = "The disc image has no root.olk.";
        return false;
    }
    const uint64_t base = entry.offset;
    read = [base](uint64_t offset, void *buffer, size_t length) {
        return recomp_disc_image_read(base + offset, buffer, length) == length;
    };
    return true;
}

uint32_t Word(const uint8_t *at)
{
    return uint32_t(at[0]) | uint32_t(at[1]) << 8 | uint32_t(at[2]) << 16 | uint32_t(at[3]) << 24;
}

/* An olnk container's entry `index` (tools/guests/olnk.py): where it starts in the file and its size (0: a hole) */
bool Entry(const Reader &read, uint64_t container, uint32_t index, uint64_t &at, uint32_t &size)
{
    uint8_t head[0x20], entry[0x10];
    if (!read(container, head, sizeof head) || std::memcmp(head + 4, "olnk", 4) != 0 || index >= Word(head)) return false;
    if (!read(container + 0x20u + 0x10u * index, entry, sizeof entry)) return false;
    at = container + Word(head + 0x10) + Word(entry);
    size = Word(entry + 4);
    return true;
}

/* The swizzled place of (x, y) in a power-of-two texture (textures.py, _swizzle_rect) */
size_t Swizzled(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
    size_t out = 0;
    int bit = 0;
    for (uint32_t w = width, h = height; w > 1u || h > 1u;) {
        if (w > 1u) {
            out |= size_t(x & 1u) << bit++;
            x >>= 1;
            w >>= 1;
        }
        if (h > 1u) {
            out |= size_t(y & 1u) << bit++;
            y >>= 1;
            h >>= 1;
        }
    }
    return out;
}

/* A picture's VXT as JPEG bytes, or empty when it is not one this can read */
std::vector<uint8_t> PictureJpeg(const std::vector<uint8_t> &vxt)
{
    if (vxt.size() < 0x14 + 0x24 || std::memcmp(vxt.data(), "VXT.", 4) != 0 || vxt[4] != 2 || Word(&vxt[8]) < 1u)
        return {};
    const uint8_t *entry = &vxt[0x14];
    const uint32_t image = Word(entry + 12), offset = Word(entry + 16), format = Word(entry + 20), stored = Word(entry + 24);
    const uint32_t width = image & 0xffffu, height = image >> 16, sw = stored & 0xffffu, sh = stored >> 16;
    constexpr uint32_t kA8R8G8B8 = 6u;
    if (format != kA8R8G8B8 || width == 0u || height == 0u || width > sw || height > sh || (sw & (sw - 1u)) ||
        (sh & (sh - 1u)) || offset + size_t(sw) * sh * 4u > vxt.size())
        return {};
    const auto texel = [&](uint32_t x, uint32_t y) { return &vxt[offset + Swizzled(x, y, sw, sh) * 4u]; };
    /* (the edges with nothing shown: wholly transparent rows and columns) */
    uint32_t left = width, right = 0, top = height, bottom = 0;
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            if (texel(x, y)[3] == 0u) continue;
            left = std::min(left, x);
            right = std::max(right, x + 1u);
            top = std::min(top, y);
            bottom = std::max(bottom, y + 1u);
        }
    }
    if (left >= right || top >= bottom) return {};
    const uint32_t w = right - left, h = bottom - top;
    std::vector<uint8_t> rgb(size_t(w) * h * 3u);
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            const uint8_t *p = texel(left + x, top + y);   /* B, G, R, A */
            uint8_t *q = &rgb[(size_t(y) * w + x) * 3u];
            /* (what is partly transparent, over black, as the gallery shows it) */
            for (int c = 0; c < 3; ++c) q[c] = uint8_t(p[2 - c] * p[3] / 255u);
        }
    }
    return EncodeJpeg(rgb.data(), int(w), int(h), 95);
}

} // namespace

void SetPicturesRootOlk(const std::string &host_path) { root_olk_override = host_path; }

std::string PicturesFolder(const std::string &settings_path)
{
    const std::filesystem::path beside =
        settings_path.empty() ? std::filesystem::current_path() : std::filesystem::u8path(settings_path).parent_path();
    return (beside / "pictures").u8string();
}

int ExportPictures(const std::string &folder, std::string &error)
{
    Reader read;
    if (!OpenRootOlk(read, error)) return -1;
    uint64_t ui = 0;
    uint32_t ui_size = 0;
    if (!Entry(read, 0, 2, ui, ui_size) || ui_size == 0u) {
        error = "root.olk is not the game's (no /2).";
        return -1;
    }
    std::error_code fault;
    std::filesystem::create_directories(std::filesystem::u8path(folder), fault);
    if (fault) {
        error = "Cannot make the folder " + folder + ": " + fault.message();
        return -1;
    }
    static const char *const kGalleries[3] = {"hi-res", "illustrations", "special"};
    int written = 0, failed = 0, unread = 0;
    for (int gallery = 0; gallery < 3; ++gallery) {
        int number = 0;
        for (int slot = 0; slot < 54; ++slot) {   /* (GalleryPicture: -1 past a gallery's grid) */
            const int32_t picture = GalleryPicture(gallery, slot);
            if (picture < 0) continue;
            ++number;
            uint64_t at = 0;
            uint32_t size = 0;
            std::vector<uint8_t> vxt;
            if (Entry(read, ui, uint32_t(picture), at, size) && size != 0u && size < (64u << 20)) {
                vxt.resize(size);
                if (!read(at, vxt.data(), size)) vxt.clear();
            }
            const std::vector<uint8_t> jpeg = PictureJpeg(vxt);
            if (jpeg.empty()) {
                ++unread;
                std::fprintf(stderr, "recomp sc2: pictures: %s %d (/2/%d) could not be read\n", kGalleries[gallery],
                    number, int(picture));
                continue;
            }
            char name[64];
            std::snprintf(name, sizeof name, "%s-%02d.jpg", kGalleries[gallery], number);
            std::ofstream file(std::filesystem::u8path(folder) / name, std::ios::binary);
            file.write(reinterpret_cast<const char *>(jpeg.data()), std::streamsize(jpeg.size()));
            if (file) ++written;
            else ++failed;
        }
    }
    if (failed) {
        error = std::to_string(failed) + " could not be written to " + folder + ".";
        return -1;
    }
    if (written == 0) {
        error = unread ? "The gallery's pictures could not be read from root.olk." : "The Art Gallery has no pictures.";
        return -1;
    }
    return written;
}

void OpenFolder(const std::string &folder)
{
    /* (a file:// address, its bytes outside the plain ones as %XX: satrecomp's Show pictures folder) */
    std::string url = "file://";
#ifdef _WIN32
    url += '/';
#endif
    for (const unsigned char c : folder) {
        const bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '/' ||
            c == '-' || c == '_' || c == '.' || c == '~' || c == ':';
        if (plain) {
            url += static_cast<char>(c);
        } else if (c == '\\') {
            url += '/';
        } else {
            static const char kHex[] = "0123456789ABCDEF";
            url += '%';
            url += kHex[c >> 4];
            url += kHex[c & 15];
        }
    }
    SDL_OpenURL(url.c_str());
}

} // namespace sc2
