/* SPDX-License-Identifier: GPL-3.0-or-later */
/* PlayStation 2 disc images: ISO 9660 (2048-byte sectors; the primary volume
   descriptor at sector 16 gives the root directory: records of u8 length, ...,
   u32 LE extent at +2, u32 LE size at +10, u8 flags at +25, u8 name length at
   +32, the name at +33, "NAME.EXT;1").
   GameCube disc images: a 0x440-byte header (game ID at 0, the FST's offset
   and size at 0x424), the FST (12-byte entries: u8 directory flag, u24 name
   offset, u32 data offset, u32 size; directories give their parent and end),
   all big-endian. CISO: 'CISO', u32 block size, a map of which blocks are
   present (one byte each, to 0x8000), then the present blocks in order. */
#include "discs.h"

extern "C" {
#include "disc_image.h"
}

#include <algorithm>
#include <cstdio>
#include <map>
#include <mutex>

namespace guest {

namespace {

struct File {
    std::FILE *f = nullptr;
    std::mutex lock;
    explicit File(const std::string &path) : f(std::fopen(path.c_str(), "rb"))
    {
        if (!f) throw Error("cannot open " + path);
    }
    ~File() { if (f) std::fclose(f); }
    Bytes read(uint64_t offset, size_t size)
    {
        std::lock_guard<std::mutex> guard(lock);
        Bytes out(size);
#ifdef _WIN32
        if (_fseeki64(f, int64_t(offset), SEEK_SET) != 0) return {};
#else
        if (fseeko(f, off_t(offset), SEEK_SET) != 0) return {};
#endif
        out.resize(std::fread(out.data(), 1, size, f));
        return out;
    }
};

/* The GameCube disc as a flat image, through a CISO's block map when there is one. */
struct GcImage {
    std::shared_ptr<File> file;
    uint32_t block = 0;
    std::vector<int64_t> blocks;   /* each block's index among the stored ones, or -1 */

    Bytes read(uint64_t offset, size_t size) const
    {
        if (!block) return file->read(offset, size);
        Bytes out;
        while (size) {
            const uint64_t b = offset / block, within = offset % block;
            const size_t n = size_t(std::min<uint64_t>(size, block - within));
            if (b >= blocks.size() || blocks[b] < 0) {
                out.insert(out.end(), n, 0);
            } else {
                Bytes part = file->read(0x8000 + uint64_t(blocks[b]) * block + within, n);
                if (part.size() != n) throw Error("GameCube disc image cut short");
                out.insert(out.end(), part.begin(), part.end());
            }
            offset += n;
            size -= n;
        }
        return out;
    }
};

/* A file beside `path` (an extracted root.olk's program). */
Bytes beside(const std::string &path, const std::string &name)
{
    const size_t slash = path.find_last_of("/\\");
    const std::string file = (slash == std::string::npos ? std::string() : path.substr(0, slash + 1)) + name;
    File f(file);
    Bytes out;
    for (uint64_t at = 0;; at += 1 << 20) {
        Bytes part = f.read(at, 1 << 20);
        out.insert(out.end(), part.begin(), part.end());
        if (part.size() < (1u << 20)) return out;
    }
}

/* A DOL's size: its sections' furthest end (7 text and 11 data: offsets at 0, sizes at 0x90). */
uint64_t dol_size(const Bytes &head)
{
    uint64_t end = 0x100;
    for (int i = 0; i < 18; ++i)
        if (be32(head, 0x90 + 4 * i)) end = std::max<uint64_t>(end, uint64_t(be32(head, 4 * i)) + be32(head, 0x90 + 4 * i));
    return end;
}

} // namespace

DiscFile open_xbox_root(const std::string &path)
{
    auto file = std::make_shared<File>(path);
    Bytes head = file->read(0, 8);
    DiscFile out;
    if (head.size() >= 8 && std::memcmp(head.data() + 4, "olnk", 4) == 0) {
#ifdef _WIN32
        _fseeki64(file->f, 0, SEEK_END);
        out.size = uint64_t(_ftelli64(file->f));
#else
        fseeko(file->f, 0, SEEK_END);
        out.size = uint64_t(ftello(file->f));
#endif
        out.read = [file](uint64_t o, size_t n) { return file->read(o, n); };
        out.program = [path]() { return beside(path, "Default.xbe"); };
        return out;
    }
    if (!recomp_disc_image_open(path.c_str())) throw Error("not an Xbox disc image: " + path);
    RecompDiscImageEntry entry;
    if (!recomp_disc_image_lookup("root.olk", &entry) || entry.directory) throw Error("the Xbox disc has no root.olk");
    const uint64_t base = entry.offset;
    out.size = entry.size;
    out.read = [base](uint64_t o, size_t n) {
        Bytes b(n);
        b.resize(recomp_disc_image_read(base + o, b.data(), n));
        return b;
    };
    RecompDiscImageEntry xbe;
    const bool has_xbe = recomp_disc_image_lookup("default.xbe", &xbe) && !xbe.directory;
    out.program = [has_xbe, xbe]() {
        if (!has_xbe) throw Error("the Xbox disc has no default.xbe");
        Bytes b(size_t(xbe.size));
        b.resize(recomp_disc_image_read(xbe.offset, b.data(), b.size()));
        return b;
    };
    return out;
}

DiscFile open_ps2_root(const std::string &path)
{
    auto file = std::make_shared<File>(path);
    const Bytes head = file->read(0, 8);
    DiscFile out;
    if (head.size() >= 8 && std::memcmp(head.data() + 4, "olnk", 4) == 0) {   /* the root.olk itself */
#ifdef _WIN32
        _fseeki64(file->f, 0, SEEK_END);
        out.size = uint64_t(_ftelli64(file->f));
#else
        fseeko(file->f, 0, SEEK_END);
        out.size = uint64_t(ftello(file->f));
#endif
        out.read = [file](uint64_t o, size_t n) { return file->read(o, n); };
        out.program = [path]() { return beside(path, "SLUS_206.43"); };
        return out;
    }
    const Bytes pvd = file->read(16 * 2048, 2048);
    if (pvd.size() < 2048 || pvd[0] != 1 || std::memcmp(pvd.data() + 1, "CD001", 5) != 0)
        throw Error("not a PlayStation 2 disc image: " + path);
    const uint64_t root = uint64_t(le32(pvd, 156 + 2)) * 2048;
    const uint32_t root_size = le32(pvd, 156 + 10);
    const Bytes dir = file->read(root, root_size);
    std::string boot;
    uint64_t olk = 0;
    uint32_t olk_size = 0;
    std::map<std::string, std::pair<uint64_t, uint32_t>> files;
    for (size_t at = 0; at < dir.size();) {
        const uint8_t length = dir[at];
        if (length == 0) {   /* the rest of this sector is padding */
            at = (at / 2048 + 1) * 2048;
            continue;
        }
        if (at + 33 > dir.size() || at + length > dir.size()) break;
        const std::string name(dir.begin() + long(at + 33), dir.begin() + long(at + 33 + dir[at + 32]));
        const uint64_t extent = uint64_t(le32(dir, at + 2)) * 2048;
        const uint32_t size = le32(dir, at + 10);
        files[name] = {extent, size};
        if (name == "ROOT.OLK;1") {
            olk = extent;
            olk_size = size;
        } else if (name == "SYSTEM.CNF;1") {
            const Bytes cnf = file->read(extent, std::min<uint32_t>(size, 1024));
            boot.assign(cnf.begin(), cnf.end());
        }
        at += length;
    }
    if (!olk_size) throw Error("the PlayStation 2 disc has no ROOT.OLK");
    const size_t slus = boot.find("SLUS_");
    if (slus != std::string::npos) out.game_id = boot.substr(slus, 11);
    const auto program = files.find(out.game_id + ";1");
    if (program != files.end()) {
        const auto [extent, size] = program->second;
        out.program = [file, extent = extent, size = size]() { return file->read(extent, size); };
    } else {
        out.program = []() -> Bytes { throw Error("the PlayStation 2 disc's program is missing"); };
    }
    out.size = olk_size;
    out.read = [file, olk](uint64_t o, size_t n) { return file->read(olk + o, n); };
    return out;
}

DiscFile open_other_root(const std::string &path, bool &ps2)
{
    {
        File probe(path);
        const Bytes head = probe.read(0, 8), pvd = probe.read(16 * 2048 + 1, 5);
        if (pvd.size() == 5 && std::memcmp(pvd.data(), "CD001", 5) == 0) {
            ps2 = true;
            return open_ps2_root(path);
        }
        if (head.size() < 8 || std::memcmp(head.data() + 4, "olnk", 4) != 0) {
            ps2 = false;
            return open_gamecube_root(path);
        }
    }
    /* a root.olk itself: the PS2's models start 08 0c, the GameCube's 'VMG.' */
    DiscFile file = open_ps2_root(path);
    const Olnk root(file.read, 0);
    const auto parts = pack_parts(root.child(4).bytes(46), false);
    ps2 = parts && !parts->empty() && parts->at(0).size() > 2 && parts->at(0)[0] == 8 && parts->at(0)[1] == 0x0c;
    if (!ps2) file.program = [path]() { return beside(path, "boot.dol"); };
    return file;
}

DiscFile open_gamecube_root(const std::string &path)
{
    GcImage image;
    image.file = std::make_shared<File>(path);
    Bytes head = image.file->read(0, 0x8000);
    if (head.size() >= 8 && std::memcmp(head.data(), "CISO", 4) == 0) {
        if (head.size() < 0x8000) throw Error("CISO header cut short");
        image.block = le32(head, 4);
        if (!image.block) throw Error("CISO block size 0");
        int64_t n = 0;
        for (size_t i = 8; i < 0x8000; ++i) image.blocks.push_back(head[i] ? n++ : -1);
    }
    const Bytes disc = image.read(0, 0x440);
    if (disc.size() < 0x440 || be32(disc, 0x1c) != 0xc2339f3d) throw Error("not a GameCube disc image: " + path);
    DiscFile out;
    out.game_id.assign(disc.begin(), disc.begin() + 6);
    out.version = disc[7];
    const uint32_t fst_at = be32(disc, 0x424), fst_size = be32(disc, 0x428);
    const Bytes fst = image.read(fst_at, fst_size);
    if (fst.size() < 12) throw Error("GameCube FST missing");
    const uint32_t count = be32(fst, 8);
    const size_t strings = size_t(count) * 12;
    for (uint32_t i = 1; i < count; ++i) {
        const size_t e = size_t(i) * 12;
        if (fst.at(e) != 0) continue;   /* a directory */
        const uint32_t name = be32(fst, e) & 0xffffff;
        std::string s;
        for (size_t p = strings + name; p < fst.size() && fst[p]; ++p) s.push_back(char(fst[p]));
        if (s == "root.olk") {
            const uint64_t dol = be32(disc, 0x420);
            out.program = [image, dol]() {
                const Bytes head = image.read(dol, 0x100);
                if (head.size() < 0x100) throw Error("the GameCube disc's program is missing");
                return image.read(dol, size_t(dol_size(head)));
            };
            const uint64_t base = be32(fst, e + 4);
            out.size = be32(fst, e + 8);
            out.read = [image, base](uint64_t o, size_t n) { return image.read(base + o, n); };
            return out;
        }
    }
    throw Error("the GameCube disc has no root.olk");
}

} // namespace guest
