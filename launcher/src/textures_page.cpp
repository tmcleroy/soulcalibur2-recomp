// SPDX-License-Identifier: GPL-3.0-or-later
// The Textures page: an HD texture pack made for Dolphin and the GameCube disc (GRSEAF), which the player has
// downloaded and unzipped, made into one the game reads (guest/texture_pack.h): its images, by the map built into
// the launcher, written to textures/GRSEAF in the player's folder, where the game looks while HD textures is on
// (the in-game menu's Display tab). The pack is only read.

#include "textures_page.h"

#include "layout.h"
#include "settings.h"
#include "texture_pack.h"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>

#include <SDL3/SDL.h>

namespace {

using layout::Card;
using layout::row;
using ui::Kind;
using ui::px;

struct State {
    std::mutex lock;
    std::string picked;              // a folder the dialog gave, for the page to take
    std::atomic<bool> dialog{false};
    std::atomic<bool> importing{false};
    std::atomic<size_t> done{0}, total{1};
    std::string result;              // the last import's, or ""
    bool failed = false;
    std::thread worker;
    ~State() {
        if (worker.joinable()) worker.join();
    }
};

State &state() {
    static State s;
    return s;
}

std::filesystem::path output_folder(const std::filesystem::path &folder) { return folder / "textures" / "GRSEAF"; }

// How many textures the last import wrote (its list, guest/texture_pack.cpp), or 0
size_t imported(const std::filesystem::path &folder) {
    std::ifstream list(output_folder(folder) / ".imported");
    size_t n = 0;
    for (std::string line; std::getline(list, line);) ++n;
    return n;
}

bool is_folder(const std::string &path) {
    std::error_code error;
    return std::filesystem::is_directory(std::filesystem::u8path(path), error);
}

void take(launcher::PageContext &page, const std::string &path) {
    page.prefs.set("textures", "pack", path);
    page.prefs.save();
}

void start_import(const std::string &pack, const std::filesystem::path &output, const std::string &xbox_disc) {
    State &s = state();
    if (s.importing) return;
    if (s.worker.joinable()) s.worker.join();
    s.importing = true;
    s.done = 0;
    s.total = 1;
    s.worker = std::thread([pack, output, xbox_disc] {
        State &s = state();
        std::string result;
        bool failed = false;
        try {
            // (the game's disc: the menu font's colours the pack does not have, from its palettes)
            const guest::PackImport done = guest::import_dolphin_pack(pack, guest::kDolphinTextureMap,
                output.u8string(), xbox_disc, [&](size_t d, size_t t) {
                    s.done = d;
                    s.total = t;
                });
            result = std::to_string(done.textures) + " textures imported";
            if (done.failed) result += ", " + std::to_string(done.failed) + " of its images could not be read";
            result += ".";
        } catch (const std::exception &e) {
            result = e.what();
            failed = true;
        }
        {
            std::lock_guard<std::mutex> guard(s.lock);
            s.result = result;
            s.failed = failed;
        }
        s.importing = false;
    });
}

void choose_folder(launcher::PageContext &page) {
    State &s = state();
    s.dialog = true;
    SDL_ShowOpenFolderDialog(
        [](void *, const char *const *files, int) {
            State &st = state();
            if (files && files[0]) {
                std::lock_guard<std::mutex> guard(st.lock);
                st.picked = files[0];
            }
            st.dialog = false;
        },
        nullptr, page.window, nullptr, false);
}

}  // namespace

void TexturesPage(launcher::PageContext &page) {
    State &s = state();
    const ui::Palette &p = ui::palette();
    {
        std::lock_guard<std::mutex> guard(s.lock);
        if (!s.picked.empty()) {
            take(page, s.picked);
            s.picked.clear();
        }
    }
    layout::heading("HD textures",
                    "An HD texture pack made for the GameCube version in Dolphin works here too: the Xbox's textures "
                    "are the same pictures, so each of the pack's images is matched to the texture it replaces. "
                    "Download the pack yourself (Soulcalibur 2 Complete HD Textures, for GRSEAF) and unzip it; it is "
                    "only read. Turn on HD textures (the in-game menu's Display tab) to see it.",
                    page.width);

    const std::string pack = page.prefs.get("textures", "pack");
    const size_t count = imported(page.folder);

    layout::label("TEXTURE PACK");
    Card card(page.width);
#ifdef __ANDROID__
    row(page.width, "Not on Android yet",
        "Import the pack on a computer, then copy its textures folder into the app's folder.", 0, 36, [] {});
#else
    const std::string label = pack.empty() ? std::string("No pack chosen")
                                           : std::filesystem::u8path(pack).filename().u8string();
    row(page.width, label.c_str(), "The pack's folder of .dds images (tex1_..., GRSEAF), unzipped. Drop it on this "
        "window or choose it.", 110, 36, [&] {
        if (ui::button("Choose…##pack", Kind::Secondary, 110.0f, !s.dialog && !s.importing)) choose_folder(page);
    });
    layout::inset_separator(page.width);
    std::string status;
    {
        std::lock_guard<std::mutex> guard(s.lock);
        if (s.importing) status = "Importing…";
        else if (!s.result.empty()) status = s.failed ? "It could not be imported: " + s.result : s.result;
        else if (count) status = std::to_string(count) + " textures imported. Import again after changing packs.";
        else status = "Not imported yet.";
    }
    const bool can_import = !pack.empty() && is_folder(pack) && !s.importing && !page.playing;
    row(page.width, "The textures", status.c_str(), 130, 36, [&] {
        if (s.importing) {
            ui::progress_bar(ImGui::GetID("texture_progress"),
                             float(s.done) / float(std::max<size_t>(1, s.total)), px(130), px(8));
        } else if (ui::button(count ? "Import again##textures" : "Import##textures", Kind::Primary, 130.0f, can_import)) {
            start_import(pack, output_folder(page.folder), page.prefs.get("game", "disc"));
        }
    });
#endif
    card.end();
    ui::gap(10);
    ui::text("Your own textures go in the textures folder too: turn on Dump textures (the in-game menu's Display tab), play, and an "
             "image saved under a dumped texture's name (a DDS, any size) takes its place.",
             ui::Font::Regular, ui::size::kDetail, p.secondary, page.width);
}

void TexturesDrop(launcher::PageContext &page, const char *path) {
    if (path && is_folder(path)) take(page, path);
}

bool TexturesBusy() {
    return state().importing || state().dialog;
}
