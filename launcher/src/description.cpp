// SPDX-License-Identifier: GPL-3.0-or-later
// What Soulcalibur II tells the launcher the recomp ports share (recomp-common/launcher/src/launcher.h): the
// words for the game's files, the check of the player's, what the game is started with, the controls that can
// be chosen (runtime/sc2_settings.cpp, which the game reads too).

#include "launcher.h"

#include "guest_page.h"
#include "imgui.h"
#include "layout.h"

#include "../../runtime/sc2_settings.h"
#include "../../runtime/sc2_xbe.h"
#include "../../third_party/doa2u-re/recomp-runtime/disc_image.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <mutex>
#include <system_error>

namespace {

#ifdef RECOMP_SDL_GPU_PRESENTER
void RenderingPage(launcher::PageContext &page) {
    layout::heading("Rendering", "Choose when graphics are prepared. Changes apply the next time you launch the game.", page.width);
    const auto &features = sc2::Features();
    auto values = recomp::FeatureValues(page.settings, features);
    layout::label("STARTUP");
    layout::Card card(page.width);
    ImGui::BeginDisabled(page.playing);
    for (size_t i = 0; i < features.size(); ++i) {
        const auto &feature = features[i];
        if (feature.name != "graphics_prepare") continue;
        layout::row(page.width, feature.title.c_str(), feature.description.c_str(), 190.0f, 36.0f, [&] {
            if (ui::dropdown("##prepare", feature.choices, values[i], 190.0f)) {
                recomp::SetFeature(page.settings, features, i, values[i]);
                page.settings.save();
            }
        });
    }
    ImGui::EndDisabled();
    card.end();
    if (page.playing)
        ui::text("Close the game to change these settings.", ui::Font::Regular, ui::size::kDetail, ui::palette().secondary, page.width);
}

#endif

// SHA-256 (FIPS 180-4) of what `read` gives (a count of bytes read into the buffer, 0 at the end), as lowercase hex.
template <typename Read>
std::string sha256_of(Read read) {
    static const uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
    };
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    auto rotr = [](uint32_t x, int n) { return (x >> n) | (x << (32 - n)); };
    auto block = [&](const uint8_t *p) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) w[i] = uint32_t(p[i * 4]) << 24 | uint32_t(p[i * 4 + 1]) << 16 | uint32_t(p[i * 4 + 2]) << 8 | p[i * 4 + 3];
        for (int i = 16; i < 64; ++i) {
            const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            const uint32_t t1 = hh + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
            const uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    };
    std::array<uint8_t, 64 * 1024> buffer;
    uint64_t length = 0;
    size_t got, carry = 0;
    uint8_t tail[128];
    while ((got = read(buffer.data() + carry, buffer.size() - carry)) > 0) {
        got += carry;
        length += got - carry;
        const size_t whole = got / 64 * 64;
        for (size_t at = 0; at < whole; at += 64) block(buffer.data() + at);
        carry = got - whole;
        std::memmove(buffer.data(), buffer.data() + whole, carry);
    }
    std::memcpy(tail, buffer.data(), carry);
    tail[carry++] = 0x80;
    const size_t padded = carry > 56 ? 128 : 64;
    std::memset(tail + carry, 0, padded - carry);
    for (int i = 0; i < 8; ++i) tail[padded - 1 - i] = uint8_t((length * 8) >> (8 * i));
    for (size_t at = 0; at < padded; at += 64) block(tail + at);
    char hex[65];
    for (int i = 0; i < 8; ++i) std::snprintf(hex + i * 8, 9, "%08x", h[i]);
    return hex;
}

// ... of a file. Read only, always.
std::string sha256_of_file(const std::string &path) {
    std::FILE *file = std::fopen(path.c_str(), "rb");
    if (!file) return {};
    const std::string sum = sha256_of([&](uint8_t *to, size_t size) { return std::fread(to, 1, size, file); });
    std::fclose(file);
    return sum;
}

// The port's media-patched executable or the retail disc's (runtime/sc2_xbe.h).
bool is_supported_xbe(const std::string &sum) {
    return sum == LAUNCHER_XBE_SHA256 || sum == SC2_RETAIL_XBE_SHA256;
}

// What a refusal says about the file found, so a report can name it.
std::string found(const std::string &sum) {
    return sum.empty() ? std::string() : " (its Default.xbe's SHA-256 begins " + sum.substr(0, 12) + ")";
}

bool is_iso(const std::string &path) {
    std::string extension = std::filesystem::u8path(path).extension().string();
    for (char &c : extension) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return extension == ".iso" || extension == ".xiso";
}

// A disc image (the launcher's check runs on its own thread, the image reader's state is the launcher's alone)
launcher::FileCheck check_image(const std::string &path) {
    using Result = launcher::FileCheck::Result;
    launcher::FileCheck check;
    std::lock_guard<std::mutex> disc(g_disc_image_lock);   // (the Guest page's build reads images too)
    if (!recomp_disc_image_open(path.c_str())) {
        check.message = "This isn't an Xbox disc image.";
        return check;
    }
    RecompDiscImageEntry xbe;
    std::string sum;
    if (recomp_disc_image_lookup("default.xbe", &xbe) && !xbe.directory) {
        uint64_t at = xbe.offset, left = xbe.size;
        sum = sha256_of([&](uint8_t *to, size_t size) {
            const size_t count = static_cast<size_t>(std::min<uint64_t>(left, size));
            const size_t got = recomp_disc_image_read(at, to, count);
            at += got;
            left -= got;
            return got;
        });
    }
    bool complete = true;
    for (const char *needed : {"root.olk", "movie.afs", "bgm.afs"}) {
        RecompDiscImageEntry entry;
        complete = complete && recomp_disc_image_lookup(needed, &entry) && !entry.directory;
    }
    recomp_disc_image_close();
    if (sum.empty()) {
        check.message = "This Xbox disc image has no Default.xbe.";
    } else if (!is_supported_xbe(sum)) {
        check.message = "This Xbox disc image isn't Soulcalibur II (USA), the release this port is made from" +
            found(sum) + ".";
    } else if (!complete) {
        check.message = "Soulcalibur II (USA), but the image is missing some of the disc's files.";
    } else {
        check.result = Result::Good;
        check.message = "Soulcalibur II (USA) disc image, verified.";
    }
    return check;
}

// The game's files: an Xbox disc image, or the disc's Default.xbe with the rest of the disc's files beside it. Only
// ever read.
launcher::FileCheck check_game(const std::string &path) {
    if (is_iso(path)) return check_image(path);
    using Result = launcher::FileCheck::Result;
    launcher::FileCheck check;
    std::error_code error;
    const std::filesystem::path file = std::filesystem::u8path(path);
    if (!std::filesystem::is_regular_file(file, error)) {
        check.message = "The file can't be opened.";
        return check;
    }
    std::string name = file.filename().string();
    for (char &c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (name != "default.xbe") {
        check.message = "Choose your Soulcalibur II disc image (.iso), or the Default.xbe among the disc's files.";
        return check;
    }
    const std::string sum = sha256_of_file(path);
    if (!is_supported_xbe(sum)) {
        check.message = "This Default.xbe isn't Soulcalibur II (USA) for the Xbox, the release this port is made from" +
            found(sum) + ".";
        return check;
    }
    for (const char *needed : {"root.olk", "movie.afs", "bgm.afs"}) {
        if (!std::filesystem::is_regular_file(file.parent_path() / needed, error)) {
            check.message = std::string("Soulcalibur II (USA), but ") + needed +
                " isn't beside it. Keep the disc's files together in one folder.";
            return check;
        }
    }
    check.result = Result::Good;
    check.message = "Soulcalibur II (USA), verified.";
    return check;
}

// What the game is started with: the player's game files (read only), their settings and their saves.
std::vector<std::string> arguments(const launcher::Launch &launch) {
    const std::filesystem::path xbe = std::filesystem::u8path(launch.file);
    std::vector<std::string> args;
    if (is_iso(launch.file)) {
        args = {"--disc", launch.file};
    } else {
        args = {"--xbe", launch.file, "--disc-root", xbe.parent_path().string()};
    }
    for (const std::string &arg : {std::string("--settings"), (launch.folder / "settings.ini").string(),
             std::string("--storage"), (launch.folder / "saves").string()})
        args.push_back(arg);
    if (launch.on("fullscreen")) args.push_back("--fullscreen");
    if (launch.on("widescreen")) args.push_back("--widescreen");
    if (launch.on("free_camera")) args.push_back("--free-camera");
    for (const std::string &arg : GuestArguments(launch.folder)) args.push_back(arg);
    return args;
}

// A console-like session, where a window is no use: SteamOS's Game Mode (Steam Deck, Steam Machine). There the
// game fills the screen unless the player says not.
bool console_session() {
    auto is = [](const char *name, const char *value) {
        const char *found = std::getenv(name);
        return found && std::strcmp(found, value) == 0;
    };
    return is("XDG_CURRENT_DESKTOP", "gamescope") || is("SteamGamepadUI", "1") || is("SteamDeck", "1");
}

} // namespace

const launcher::Description &launcher::describe() {
    static const Description about = [] {
        Description d;
        d.original = "on the Xbox";
        d.pages = {{"Guest", GuestPage, GuestDrop, GuestBusy}};
#ifdef RECOMP_SDL_GPU_PRESENTER
        d.pages.push_back({"Rendering", RenderingPage});
#endif
        d.open_on_game_page = true;
        d.file_heading = "GAME FILES";
#ifdef __ANDROID__
        d.file_intro = "Choose your own copy of the game: an image of your Soulcalibur II (USA) Xbox disc (.iso). "
                       "It is copied into the app's folder, where the game reads it.";
#else
        d.file_intro = "Choose your own copy of the game: an image of your Soulcalibur II (USA) Xbox disc (.iso), "
                       "or the Default.xbe among the disc's files. It stays where it is: the launcher and the game "
                       "only read it.";
#endif
        d.file_empty = "No game chosen. Drop your disc image (or the disc's Default.xbe) on this window or choose it.";
        d.file_not_set_up = "The game isn't set up yet. Open the launcher (without --play) once to choose your game files.";
        d.file_filter = {"Xbox disc image or Default.xbe", "iso;xiso;xbe"};
        d.file_key = "disc";
        d.check = check_game;
        d.check_on_thread = true;
        d.arguments = arguments;
        d.display = {
            {"fullscreen", "Full screen", "F11 switches while playing.", console_session(), nullptr},
            {"widescreen", "Widescreen", "The game's own 16:9 mode: menus, fights and the HUD laid out wide.", true, nullptr},
            {"free_camera", "Free camera", "` (backquote) or clicking the right stick takes the camera and flies it; "
                                          "the same again gives it back.", false, nullptr},
        };
        // (online play: runtime/sc2_netplay.h; the introduction service: online/)
        d.online = {"--host", "--join", 7845, true,
                    "Two players, each on their own machine with the game: one hosts and gives the other the code "
                    "their game shows; the other joins with it. Both games go straight to VS Battle's character "
                    "select, with the host's save (options and unlocks). Guest characters must be built alike on both, "
                    "or on neither."};
        d.online.lobby = "https://soulcalibur2-online.tmcleroy.workers.dev/v1/lobby/soulcalibur2?protocol=1001";
        d.online.list_argument = "--net-list";
        d.online.name_argument = "--net-name";
        d.online.invite_argument = "--net-invite";
        d.online.invite_name_argument = "--net-invite-name";
        d.online.room = "https://soulcalibur2-online.tmcleroy.workers.dev/v1/room";
        d.online.room_protocol = 1001;
        d.online.game = "soulcalibur2";
        d.online_page = true;            // (online play on its own page, after Game)
        d.own_pages_after_game = true;   // (Game, Online, then Guest)
        d.bindings = sc2::BindingTable();
        d.controls_heading = "WHILE PLAYING";
        d.controls = {{"Keys", "Esc or F1: menu (sound, quit)   F11: full screen. On a gamepad: Back opens and "
                               "closes the menu (unless you give Back to the game above). The left stick always moves; "
                               "the game's own Options set what each button does in a fight."}};
        return d;
    }();
    return about;
}
