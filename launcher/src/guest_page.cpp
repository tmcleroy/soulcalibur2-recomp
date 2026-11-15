// SPDX-License-Identifier: GPL-3.0-or-later
// The Guest page: the other versions' guests beside Spawn. Link is built from the player's GameCube disc and
// Heihachi from their PlayStation 2 disc (guest/build.h, build_guests_root), into one file in the player's folder
// (guest/guests-root.olk, or guest/guests-root-europe.olk from the European Xbox disc, whose menus are numbered
// otherwise) that the game reads in place of the Xbox disc's root.olk (--guests): they then stand in the select
// grid's fifth column. The discs are only read (on Android, copied into the app's folder first: the
// system's picker gives documents, not files).

#include "guest_page.h"

#include "build.h"
#include "layout.h"
#include "settings.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>

#ifdef __ANDROID__
#include <SDL3/SDL.h>
#include <jni.h>

#include <sstream>
#endif

std::mutex g_disc_image_lock;

namespace {

using layout::Card;
using layout::row;
using ui::Kind;
using ui::px;

// The two guests the player's other discs give
struct Guest {
    const char *name;
    const char *disc;       // what the disc is, for the page
};
const std::array<Guest, 2> kGuests = {{{"Link", "GameCube"}, {"Heihachi", "PlayStation 2"}}};
// what is built, in the player's folder: one for each release's Xbox disc (guest/build.cpp, Layout)
constexpr const char *kBuilt = "guests-root.olk", *kBuiltEurope = "guests-root-europe.olk";
const char *disc_key(int guest) { return guest == 0 ? "gamecube" : "ps2"; }

struct State {
    std::mutex lock;
    std::string picked;              // a disc the dialog gave, for the page to take
    int picked_for = 0;
    std::atomic<bool> dialog{false};
    std::atomic<bool> copying{false};   // (Android: the chosen disc being copied into the app's folder)
    std::atomic<long long> copy_done{0}, copy_total{0};
    std::atomic<bool> building{false};
    std::atomic<int> done{0}, total{1};
    std::string error;               // the last build's or copy's, or ""
    std::thread worker;
    ~State() {
        if (worker.joinable()) worker.join();   // (a build under way finishes before the launcher ends)
    }
};

State &state() {
    static State s;
    return s;
}

std::filesystem::path built_file(const std::filesystem::path &folder, bool european) {
    return folder / "guest" / (european ? kBuiltEurope : kBuilt);
}

bool is_file(const std::filesystem::path &path) {
    std::error_code error;
    return std::filesystem::is_regular_file(path, error);
}

// The Xbox disc's root.olk: in the image, or beside the Default.xbe
std::string xbox_source(const std::string &game_file) {
    std::filesystem::path file = std::filesystem::u8path(game_file);
    std::string extension = file.extension().string();
    for (char &c : extension) c = char(std::tolower(static_cast<unsigned char>(c)));
    if (extension == ".iso" || extension == ".xiso") return game_file;
    return (file.parent_path() / "root.olk").string();
}

// Which guest a dropped disc is for: a PS2 disc image is ISO 9660 ("CD001" in sector 16), a GameCube one is not
int guest_of_disc(const std::string &path) {
    std::FILE *f = std::fopen(path.c_str(), "rb");
    if (!f) return 0;
    char magic[5] = {};
    const bool iso = std::fseek(f, 16 * 2048 + 1, SEEK_SET) == 0 && std::fread(magic, 1, 5, f) == 5 &&
                     std::memcmp(magic, "CD001", 5) == 0;
    std::fclose(f);
    return iso ? 1 : 0;
}

// Builds the guests' file from whichever of the two discs the player chose.
void start_build(const std::string &xbox, const std::string discs[2], const std::filesystem::path &output) {
    State &s = state();
    if (s.building) return;
    if (s.worker.joinable()) s.worker.join();
    s.building = true;
    s.done = 0;
    s.total = 1;
    {
        std::lock_guard<std::mutex> guard(s.lock);
        s.error.clear();
    }
    s.worker = std::thread([xbox, gamecube = discs[0], ps2 = discs[1], output] {
        State &s = state();
        std::string error;
        try {
            std::error_code ignored;
            std::filesystem::create_directories(output.parent_path(), ignored);
            std::lock_guard<std::mutex> lock(g_disc_image_lock);   // (the disc image reader is one for all)
            const guest::DiscFile x = guest::open_xbox_root(xbox);
            auto progress = [&](int done, int total) {
                s.done = done;
                s.total = total;
            };
            std::unique_ptr<guest::DiscFile> gc, p2;
            if (!gamecube.empty()) gc = std::make_unique<guest::DiscFile>(guest::open_gamecube_root(gamecube));
            if (!ps2.empty()) p2 = std::make_unique<guest::DiscFile>(guest::open_ps2_root(ps2));
            guest::build_guests_root(x, gc.get(), p2.get(), output.u8string(), progress);
        } catch (const std::exception &e) {
            error = std::string("It could not be built: ") + e.what();
        }
        {
            std::lock_guard<std::mutex> guard(s.lock);
            s.error = error;
        }
        s.building = false;
    });
}

void take(launcher::PageContext &page, int guest, const std::string &path) {
    page.prefs.set("guest", disc_key(guest), path);
    page.prefs.save();
}

#ifdef __ANDROID__
// The system's picker, and the disc chosen copied into `folder` by the app's activity (LauncherActivity.importFiles,
// as the Game page has its disc copied): the copy's path, or "" (on a thread of the caller's: it waits for the player
// and the copy)
std::string import_disc(const std::filesystem::path &folder, std::string &error_out) {
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    JNIEnv *env = static_cast<JNIEnv *>(SDL_GetAndroidJNIEnv());
    jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (!env || !activity) return {};
    std::string path;
    jclass activity_class = env->GetObjectClass(activity);
    jmethodID import = env->GetMethodID(activity_class, "importFiles", "(Ljava/lang/String;Z)V");
    jmethodID result = env->GetMethodID(activity_class, "importResult", "()Ljava/lang/String;");
    jmethodID copied = env->GetMethodID(activity_class, "importCopied", "()J");
    jmethodID total = env->GetMethodID(activity_class, "importTotal", "()J");
    jmethodID import_error = env->GetMethodID(activity_class, "importError", "()Ljava/lang/String;");
    if (env->ExceptionCheck()) env->ExceptionClear();
    if (import && result) {
        jstring folder_text = env->NewStringUTF(folder.string().c_str());
        env->CallVoidMethod(activity, import, folder_text, static_cast<jboolean>(false));
        env->DeleteLocalRef(folder_text);
        State &st = state();
        for (;;) {
            SDL_Delay(100);
            if (copied && total) {
                st.copy_done = static_cast<long long>(env->CallLongMethod(activity, copied));
                st.copy_total = static_cast<long long>(env->CallLongMethod(activity, total));
                if (env->ExceptionCheck()) env->ExceptionClear();
            }
            jstring text = static_cast<jstring>(env->CallObjectMethod(activity, result));
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                break;
            }
            if (!text) continue;
            const char *chars = env->GetStringUTFChars(text, nullptr);
            std::istringstream lines(chars ? chars : "");
            std::getline(lines, path);
            env->ReleaseStringUTFChars(text, chars);
            env->DeleteLocalRef(text);
            if (path.empty() && import_error) {
                jstring err = static_cast<jstring>(env->CallObjectMethod(activity, import_error));
                if (env->ExceptionCheck()) {
                    env->ExceptionClear();
                } else if (err) {
                    const char *err_chars = env->GetStringUTFChars(err, nullptr);
                    if (err_chars && err_chars[0]) error_out = err_chars;
                    env->ReleaseStringUTFChars(err, err_chars);
                    env->DeleteLocalRef(err);
                }
            }
            break;
        }
    }
    if (env->ExceptionCheck()) env->ExceptionClear();
    env->DeleteLocalRef(activity_class);
    env->DeleteLocalRef(activity);
    return path;
}
#endif

void choose_disc(launcher::PageContext &page, int guest) {
    State &s = state();
    s.dialog = true;
    s.picked_for = guest;
    {
        std::lock_guard<std::mutex> guard(s.lock);
        s.error.clear();
    }
#ifdef __ANDROID__
    s.copy_done = 0;
    s.copy_total = 0;
    s.copying = true;
    std::thread([folder = page.folder / "guest" / "discs"] {
        std::string copy_error;
        const std::string path = import_disc(folder, copy_error);
        State &st = state();
        {
            std::lock_guard<std::mutex> guard(st.lock);
            if (!path.empty()) st.picked = path;
            else if (!copy_error.empty()) st.error = "Could not copy the disc: " + copy_error;
        }
        st.copying = false;
        st.dialog = false;
        SDL_Event event{};
        event.type = SDL_EVENT_USER;
        SDL_PushEvent(&event);
    }).detach();
    return;
#endif
    static const SDL_DialogFileFilter gamecube = {"GameCube disc image", "iso;gcm;ciso"};
    static const SDL_DialogFileFilter ps2 = {"PlayStation 2 disc image", "iso"};
    SDL_ShowOpenFileDialog(
        [](void *, const char *const *files, int) {
            State &st = state();
            if (files && files[0]) {
                std::lock_guard<std::mutex> guard(st.lock);
                st.picked = files[0];
            }
            st.dialog = false;
        },
        nullptr, page.window, guest == 0 ? &gamecube : &ps2, 1, nullptr, false);
}

}  // namespace

void GuestPage(launcher::PageContext &page) {
    State &s = state();
    const ui::Palette &p = ui::palette();
    {
        std::lock_guard<std::mutex> guard(s.lock);
        if (!s.picked.empty()) {
            take(page, s.picked_for, s.picked);
            s.picked.clear();
        }
    }
    layout::heading("Guest characters",
                    "Soulcalibur II's guest was a different fighter on each console: Spawn on the Xbox, Link on the "
                    "GameCube, Heihachi on the PlayStation 2. With your own GameCube or PlayStation 2 disc, Link and "
                    "Heihachi join Spawn on the select screen: models, moves, weapons, voices and select screen art "
                    "are converted from your discs (they are only read).",
                    page.width);

    const std::string game_file = page.prefs.get("game", "disc");
    const bool european = IsEuropeanDisc(game_file);
    const std::filesystem::path output = built_file(page.folder, european);
    const bool built = is_file(output);
    std::string discs[2];
    for (int g = 0; g < 2; ++g) discs[g] = page.prefs.get("guest", disc_key(g));

    layout::label("YOUR DISCS");
    Card card(page.width);
    for (int g = 0; g < 2; ++g) {
        const Guest &guest = kGuests[g];
        if (g) layout::inset_separator(page.width);
        const std::string label = discs[g].empty() ? std::string("No ") + guest.disc + " disc chosen"
                                                   : std::filesystem::u8path(discs[g]).filename().u8string();
        const std::string detail = std::string(guest.name) + ": Soulcalibur II (USA) for the " + guest.disc +
            (g == 0 ? ", as a disc image (.iso, .gcm or .ciso)." : ", as a disc image (.iso).") +
#ifdef __ANDROID__
            " Choose it: it is copied into the app's folder.";
#else
            " Drop it on this window or choose it.";
#endif
        row(page.width, label.c_str(), detail.c_str(), 110, 36, [&] {
            if (ui::button(g == 0 ? "Choose…##gc" : "Choose…##ps2", Kind::Secondary, 110.0f, !s.dialog))
                choose_disc(page, g);
        });
    }
    layout::inset_separator(page.width);
    std::string status;
    {
        std::lock_guard<std::mutex> guard(s.lock);
        if (s.copying) {
            const long long done = s.copy_done, total = s.copy_total;
            if (total > 0) {
                status = "Copying the disc into the app's folder… (" +
                         std::to_string(done / (1024 * 1024)) + " / " +
                         std::to_string(total / (1024 * 1024)) + " MB)";
            } else if (done > 0) {
                status = "Copying the disc into the app's folder… (" +
                         std::to_string(done / (1024 * 1024)) + " MB)";
            } else {
                status = "Waiting for the file picker…";
            }
        } else if (s.building) status = "Converting…";
        else if (!s.error.empty()) status = s.error;
        else if (built && guest::guests_version(output.u8string()) != guest::GUESTS_VERSION)
            status = "Built by an earlier release: press Rebuild. Until then the guests may lack what this release "
                     "adds, and online play needs both players' guests built by the same release.";
        else if (built) status = "Built from your discs: the guests stand in the select grid's right-hand column. "
                                 "Rebuild after changing discs.";
        else status = "Not built yet.";
    }
    const bool can_build = (!discs[0].empty() || !discs[1].empty()) && !game_file.empty() && !s.building &&
                           !page.playing;
    row(page.width, "The guests", status.c_str(), 130, 36, [&] {
        if (s.building) {
            ui::progress_bar(ImGui::GetID("guest_progress"), float(s.done) / float(std::max(1, int(s.total))), px(130),
                             px(8));
        } else if (s.copying && s.copy_total > 0) {
            ui::progress_bar(ImGui::GetID("guest_copy_progress"),
                             float(double(s.copy_done) / double(s.copy_total)), px(130), px(8));
        } else if (ui::button(built ? "Rebuild##guests" : "Build##guests", Kind::Primary, 130.0f, can_build)) {
            start_build(xbox_source(game_file), discs, output);
        }
    });
    card.end();
    if (game_file.empty()) {
        ui::gap(10);
        ui::text("Choose your Xbox disc on the Game page first.", ui::Font::Regular, ui::size::kDetail, p.secondary);
    }
}

void GuestDrop(launcher::PageContext &page, const char *path) {
    take(page, guest_of_disc(path), path);
}

bool GuestBusy() {
    return state().building || state().dialog;
}

launcher::Notice GuestNotice(const std::filesystem::path &folder) {
    if (state().building || state().copying) return {};
    bool earlier = false;   // (either release's file)
    for (bool european : {false, true}) {
        const std::filesystem::path file = built_file(folder, european);
        earlier = earlier || (is_file(file) && guest::guests_version(file.u8string()) != guest::GUESTS_VERSION);
    }
    if (!earlier) return {};
    return {"REBUILD NEEDED", "Rebuild the guests",
            "Link and Heihachi were built by an earlier release. Rebuild them on the Guest page to get this release's "
            "fixes; online play also needs both players' guests built by the same release.",
            "Guest page", "Guest"};
}

std::vector<std::string> GuestArguments(const std::filesystem::path &folder, const std::string &game_file) {
    const std::filesystem::path file = built_file(folder, IsEuropeanDisc(game_file));
    if (!is_file(file)) return {};
    return {"--guests", file.u8string()};
}
