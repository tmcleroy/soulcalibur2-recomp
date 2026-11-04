/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The in-game menu. Esc or F1, or a gamepad's Back, open it after a Swap; the
   game stays paused there (its next frame waits) and its sound with it, while
   the menu draws itself over a copy of the frame the window showed
   (recomp_d3d_presenter_read_frame) and the presenter shows that picture
   instead. A key or button the player gave the game does only that. */
#include "sc2_menu.h"
#include "sc2_touch.h"

#include "sc2_settings.h"
#include "sc2_online.h"
#include "sc2_state.h"
#include "sc2_version.h"
#include "sc2_training.h"
#include "../recomp-common/menu.h"

#include "audio_output.h"
#include "d3d8_adapter.h"
#include "d3d_shader_preset.h"
#include "d3d_vblank.h"
#include "input_host_win32.h"
#include "input_keyboard_win32.h"
extern "C" {
#include "scene_freeze_adapter.h"
#include "program_manual.h"
#include "sc2_hooks.h"
#include "stop_report.h"
#include "runtime.h"
}
#include "xbox_memory_layout.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

namespace sc2 {

namespace {

recomp::Menu menu;
recomp::Bindings bindings;
std::string settings_path;
/* The Shader feature's choice (SetShader) */
int shader_choice;
/* The features the menu offers (those that work while playing): their places in Features() */
std::vector<size_t> menu_features;

/* The features the menu changes while playing: training's, and what an online match shows (sc2_online.h). -1: not
   one of them. */
int FeatureValue(const std::string &name)
{
    Training &training = TrainingState();
    OnlineShown &online = OnlineDisplay();
    if (name == "health_refill") return training.health_refill;
    if (name == "infinite_time") return training.infinite_time;
    if (name == "input_display") return training.input_display;
    if (name == "combo_readout") return training.combo_readout;
    if (name == "hit_boxes") return training.hit_boxes;
    if (name == "frame_data") return training.frame_data;
    if (name == "endless") return training.endless;
    if (name == "no_damage_p1") return training.no_damage[0];
    if (name == "no_damage_p2") return training.no_damage[1];
    if (name == "one_hit") return training.one_hit;
    if (name == "fps_counter") return training.fps_counter;
    if (name == "stretch") return recomp_d3d_present_stretch != 0;
    if (name == "shader") return shader_choice;
    if (name == "dummy") return static_cast<int>(training.dummy);
    if (name == "online_ping") return online.ping;
    if (name == "online_delay") return online.delay;
    if (name == "online_hide_names") return online.hide_names;
    return -1;
}

void SetFeatureValue(const std::string &name, int value)
{
    Training &training = TrainingState();
    OnlineShown &online = OnlineDisplay();
    if (name == "health_refill") training.health_refill = value != 0;
    else if (name == "infinite_time") training.infinite_time = value != 0;
    else if (name == "input_display") training.input_display = value != 0;
    else if (name == "combo_readout") training.combo_readout = value != 0;
    else if (name == "hit_boxes") training.hit_boxes = value != 0;
    else if (name == "frame_data") training.frame_data = value != 0;
    else if (name == "endless") training.endless = value != 0;
    else if (name == "no_damage_p1") training.no_damage[0] = value != 0;
    else if (name == "no_damage_p2") training.no_damage[1] = value != 0;
    else if (name == "one_hit") training.one_hit = value != 0;
    else if (name == "fps_counter") training.fps_counter = value != 0;
    else if (name == "stretch") recomp_d3d_present_stretch = value != 0;
    else if (name == "shader") SetShader(value);
    else if (name == "dummy") training.dummy = static_cast<Dummy>(value);
    else if (name == "online_ping") online.ping = value != 0;
    else if (name == "online_delay") online.delay = value != 0;
    else if (name == "online_hide_names") online.hide_names = value != 0;
}

/* The speeds the menu offers (- and = too), slowest first, as cps2recomp's: the game's frames to the display's */
struct Speed {
    const char *name;
    int frames, ticks;
};
constexpr Speed kSpeedTable[] = {{"1/8", 1, 8}, {"1/6", 1, 6}, {"1/4", 1, 4}, {"1/3", 1, 3}, {"1/2", 1, 2}, {"3/4", 3, 4},
    {"Full", 1, 1}, {"1.5x", 3, 2}, {"2x", 2, 1}, {"3x", 3, 1}, {"4x", 4, 1}, {"6x", 6, 1}, {"8x", 8, 1}};
constexpr int kFullSpeed = 6;
constexpr int kSpeedCount = static_cast<int>(sizeof kSpeedTable / sizeof kSpeedTable[0]);
const std::vector<std::string> kSpeeds = [] {
    std::vector<std::string> names;
    for (const Speed &one : kSpeedTable) names.push_back(one.name);
    return names;
}();
int speed = kFullSpeed;

/* The menu's commands, by their place in MenuConfig::commands */
enum Command { kHost, kStartOver, kUnlock, kJoinCode, kJoinListed };

/* Joining from the Online page: the lobby's matches shown (by their place after kJoinListed), and the code being
   typed for "Join with a code" (shown in the menu's note) */
std::vector<OpenMatch> listed;
bool typing_code;
std::string typed_code;

bool escape_free = true, back_free = true;

struct Shot {
    uint32_t present = 0;
    std::string keys, path;
} shot;

void Configure()
{
    recomp::MenuConfig config;
    config.groups = {
        {recomp::MenuRow(recomp::MenuRow::Resume)},
        {recomp::MenuRow(recomp::MenuRow::Save), recomp::MenuRow(recomp::MenuRow::Load),
            recomp::MenuRow(recomp::MenuRow::Slot), recomp::MenuRow(recomp::MenuRow::Speed)},
        {recomp::MenuRow(recomp::MenuRow::Pages)},
        {recomp::MenuRow(recomp::MenuRow::Mute)},
        {recomp::MenuRow(recomp::MenuRow::Quit)},
    };
    const auto &features = Features();
    menu_features.clear();
    for (size_t i = 0; i < features.size(); ++i) {
        if (FeatureValue(features[i].name) < 0) continue;
        menu_features.push_back(i);
        config.features.push_back({features[i].category, features[i].title, features[i].description,
            features[i].choices});
    }
    config.value = [](size_t feature) { return FeatureValue(Features()[menu_features[feature]].name); };
    config.setValue = [](size_t feature, int value) {
        const size_t index = menu_features[feature];
        SetFeatureValue(Features()[index].name, value);
        /* Kept for the next start, as the launcher keeps it (not the dummy: this run's) */
        if (!settings_path.empty() && !(Features()[index].flags & recomp::kFeatureSession)) {
            recomp::Ini ini(settings_path);
            ini.load();
            recomp::SetFeature(ini, Features(), index, value);
            ini.save();
        }
    };
    /* Online's page: hosting a match from here (cps2recomp's menu has it), or stopping */
    if (OnlineHosting())
        config.commands.push_back({"Online", "Stop hosting",
            "Others can no longer join: the match's code stops working. Your game goes on as it is."});
    else
        config.commands.push_back({"Online", "Host a match",
            "Others can join you online while you play on as you like: the match's code is on the screen to give "
            "them, and with your name from the launcher's Online page it's in the lobby too. When someone joins, "
            "the game starts over for the match. This row again stops hosting."});
    listed.clear();
    config.commands.push_back({"Training", "Start over",
        "The round again as its fighting began (Backspace while playing). Kept once a round's clock starts moving, "
        "or in Practice as its fighting begins."});
    config.commands.push_back({"Cheats", "Unlock everything",
        "Every character, stage, mode, weapon and Museum page the game unlocks, had now. The game keeps them in your "
        "save the next time it saves."});
    if (OnlineCanHost()) {
        config.commands.push_back({"Online", "Join with a code",
            "Type the code the host's game shows (or the host's address) and press Return: the game starts again and "
            "joins their match."});
        std::vector<OpenMatch> now;
        const bool came = OnlineLobby(now);
        listed = now;
        if (!came) config.commands.push_back({"Online", "Open matches: looking...", "The lobby's open matches show here."});
        else if (now.empty())
            config.commands.push_back({"Online", "Open matches: none right now",
                "Matches players host in the lobby show here (the list is asked for again each time the menu opens)."});
        for (const OpenMatch &one : now) {
            const bool same = one.version == SC2_VERSION;
            config.commands.push_back({"Online", (same ? "Join " : "(") + (one.name.empty() ? one.code : one.name) +
                    (same ? "" : ": version " + one.version + ")"),
                same ? "Their match in the lobby (code " + one.code + "): the game starts again and joins it."
                     : "Their game is version " + one.version + " and yours " SC2_VERSION ": both need the same to play."});
        }
    }
    config.speeds = kSpeeds;
    config.normalSpeed = kFullSpeed;
    config.speedDescription = "Slows the game down or speeds it up (- and = while playing). P pauses it, and . goes on a "
                              "frame.";
    config.muted = [] { return recomp_audio_output_muted() != 0; };
    config.setMuted = [](bool muted) { recomp_audio_output_set_muted(muted ? 1 : 0); };
    config.confirmName = "A or Return";
    config.closeName = escape_free ? "Esc or Back" : "F1 or Back";
    menu.Configure(std::move(config));
}

/* The menu's buttons held now: the arrows, Return or Space, Esc or Backspace;
   a gamepad's d-pad or left stick, South, East, Back or Start. */
unsigned Held()
{
    const auto key = [](SDL_Scancode code) { return recomp_input_scancode_held(code); };
    const uint64_t pad = recomp_input_host_pad_buttons();
    const auto button = [&](SDL_GamepadButton b) { return (pad >> b & 1u) != 0u; };
    unsigned held = 0;
    if (key(SDL_SCANCODE_UP) || button(SDL_GAMEPAD_BUTTON_DPAD_UP)) held |= recomp::kMenuUp;
    if (key(SDL_SCANCODE_DOWN) || button(SDL_GAMEPAD_BUTTON_DPAD_DOWN)) held |= recomp::kMenuDown;
    if (key(SDL_SCANCODE_LEFT) || button(SDL_GAMEPAD_BUTTON_DPAD_LEFT)) held |= recomp::kMenuLeft;
    if (key(SDL_SCANCODE_RIGHT) || button(SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) held |= recomp::kMenuRight;
    if (key(SDL_SCANCODE_RETURN) || key(SDL_SCANCODE_SPACE) || button(SDL_GAMEPAD_BUTTON_SOUTH))
        held |= recomp::kMenuConfirm;
    if (key(SDL_SCANCODE_ESCAPE) || key(SDL_SCANCODE_BACKSPACE) || key(SDL_SCANCODE_F1) || key(SDL_SCANCODE_AC_BACK) ||
        button(SDL_GAMEPAD_BUTTON_EAST) || button(SDL_GAMEPAD_BUTTON_BACK) || button(SDL_GAMEPAD_BUTTON_START))
        held |= recomp::kMenuBack;
    return held;
}

/* What opens it: F1, Esc and a gamepad's Back while the game has not got them, and Android's Back. */
bool OpenHeld()
{
    if (OnlineInMatch()) return false; /* (the match would wait for the menu: Esc asks to leave, runner.cpp) */
    const uint64_t pad = recomp_input_host_pad_buttons();
    return recomp_input_scancode_held(SDL_SCANCODE_F1) || recomp_input_scancode_held(SDL_SCANCODE_AC_BACK) ||
        (escape_free && recomp_input_scancode_held(SDL_SCANCODE_ESCAPE)) ||
        (back_free && (pad >> SDL_GAMEPAD_BUTTON_BACK & 1u) != 0u);
}

void SaveBmp(const std::string &path, const std::vector<uint32_t> &pixels, uint32_t width, uint32_t height)
{
    std::FILE *file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) return;
    const uint32_t row = width * 3u, padded = (row + 3u) & ~3u, size = 54u + padded * height;
    uint8_t header[54] = {'B', 'M'};
    const auto put = [&](int at, uint32_t value) { for (int i = 0; i < 4; ++i) header[at + i] = uint8_t(value >> (8 * i)); };
    put(2, size);
    put(10, 54u);
    put(14, 40u);
    put(18, width);
    put(22, height);
    header[26] = 1;
    header[28] = 24;
    put(34, padded * height);
    std::fwrite(header, 1, sizeof header, file);
    std::vector<uint8_t> line(padded, 0u);
    for (uint32_t y = height; y-- > 0u;) {
        for (uint32_t x = 0u; x < width; ++x) {
            const uint32_t p = pixels[y * width + x];
            line[x * 3u] = uint8_t(p >> 16);
            line[x * 3u + 1u] = uint8_t(p >> 8);
            line[x * 3u + 2u] = uint8_t(p);
        }
        std::fwrite(line.data(), 1, padded, file);
    }
    std::fclose(file);
}

/* A key typed (its character: letters as capitals, digits, '-', '.', ':'; '\n' Return, '\b' Backspace, 27 Esc), or 0 */
int TypedKey()
{
    static bool held[SDL_SCANCODE_COUNT];
    int typed = 0;
    for (int code = SDL_SCANCODE_A; code <= SDL_SCANCODE_PERIOD && typed == 0; ++code) {
        const bool now = recomp_input_scancode_held(static_cast<uint32_t>(code));
        if (now && !held[code]) {
            if (code <= SDL_SCANCODE_Z) typed = 'A' + (code - SDL_SCANCODE_A);
            else if (code >= SDL_SCANCODE_1 && code <= SDL_SCANCODE_9) typed = '1' + (code - SDL_SCANCODE_1);
            else if (code == SDL_SCANCODE_0) typed = '0';
            else if (code == SDL_SCANCODE_RETURN) typed = '\n';
            else if (code == SDL_SCANCODE_BACKSPACE) typed = '\b';
            else if (code == SDL_SCANCODE_ESCAPE) typed = 27;
            else if (code == SDL_SCANCODE_MINUS) typed = '-';
            else if (code == SDL_SCANCODE_PERIOD) typed = '.';
            else if (code == SDL_SCANCODE_SEMICOLON) typed = ':';
        }
        held[code] = now;
    }
    return typed;
}

void Run(bool scripted)
{
    RecompD3dPresenter *presenter = recomp_d3d8_presenter();
    uint32_t width = 0u, height = 0u;
    if (presenter == nullptr ||
        recomp_d3d_presenter_read_frame(presenter, nullptr, 0u, &width, &height) != RECOMP_D3D_PRESENTER_OK)
        return;
    std::vector<uint32_t> frame(static_cast<size_t>(width) * height), picture;
    if (recomp_d3d_presenter_read_frame(presenter, frame.data(), static_cast<uint32_t>(frame.size()), &width, &height) !=
        RECOMP_D3D_PRESENTER_OK) {
        std::fprintf(stderr, "recomp sc2: menu unavailable (no frame readback)\n");
        return;
    }
    recomp_audio_output_set_paused(1);
    if (OnlineCanHost()) OnlineLobbyFetch();
    typing_code = false;
    Configure();
    menu.SetSpeed(speed);
    menu.Open();
    sc2_touch_set_menu(true);
    unsigned frames_open = 0;
    bool lobby_shown = false;
    unsigned previous = scripted ? 0u : Held(); /* the press that opened it is not a menu press */
    size_t script = 0u;
    while (menu.IsOpen()) {
        unsigned pressed = 0u;
        if (scripted) {
            if (script < shot.keys.size()) {
                switch (shot.keys[script++]) {
                case 'u': pressed = recomp::kMenuUp; break;
                case 'd': pressed = recomp::kMenuDown; break;
                case 'l': pressed = recomp::kMenuLeft; break;
                case 'r': pressed = recomp::kMenuRight; break;
                case 'c': pressed = recomp::kMenuConfirm; break;
                case 'b': pressed = recomp::kMenuBack; break;
                default: break;
                }
            }
        } else {
            const unsigned held = Held();
            pressed = held & ~previous;
            previous = held;
        }
        /* (typing a code to join: the keys are the code's; Return joins, Esc goes back to the menu) */
        if (typing_code) {
            pressed = 0u;
            const int typed = TypedKey();
            if (typed == '\n' && !typed_code.empty()) OnlineJoin(typed_code);
            if (typed == 27) {
                typing_code = false;
                menu.Note("");
            } else {
                if (typed == '\b' && !typed_code.empty()) typed_code.pop_back();
                else if (typed > ' ' && typed_code.size() < 40u) typed_code += static_cast<char>(typed);
                menu.Note("Code: " + typed_code + "_   (Return joins, Esc goes back)");
            }
            previous = Held();
        }
        /* (the lobby's matches, once they have come) */
        std::vector<OpenMatch> now;
        if (!typing_code && !lobby_shown && OnlineCanHost() && ++frames_open % 15u == 0u && OnlineLobby(now)) {
            lobby_shown = true;
            Configure();
        }
        const recomp::Menu::Chosen chosen = menu.Buttons(pressed);
        if (chosen.kind == recomp::Menu::Chosen::Command && chosen.id == kJoinCode) {
            typing_code = true;
            typed_code.clear();
            menu.Note("Code: _   (Return joins, Esc goes back)");
            while (TypedKey() != 0) {}
        } else if (chosen.kind == recomp::Menu::Chosen::Command && chosen.id >= kJoinListed) {
            const size_t which = static_cast<size_t>(chosen.id - kJoinListed);
            if (which < listed.size() && listed[which].version != SC2_VERSION)
                menu.Note("That match is on version " + listed[which].version + "; yours is " SC2_VERSION ".");
            else if (which < listed.size()) OnlineJoin(listed[which].code);
        }
        if (chosen.kind == recomp::Menu::Chosen::Quit) recomp_stop(0, "menu-quit");
        if (chosen.kind == recomp::Menu::Chosen::Save || chosen.kind == recomp::Menu::Chosen::Load) {
            /* (done at the start of the game's next frame) */
            if (chosen.kind == recomp::Menu::Chosen::Save) AskSaveState(menu.Slot());
            else if (StateSaved(menu.Slot())) AskLoadState(menu.Slot());
            if (chosen.kind == recomp::Menu::Chosen::Load && !StateSaved(menu.Slot())) menu.Note("That slot is empty.");
            else menu.Close();
        }
        if (chosen.kind == recomp::Menu::Chosen::Command && chosen.id == kUnlock) {
            TrainingUnlockAll();
            menu.Close();
        } else if (chosen.kind == recomp::Menu::Chosen::Command && chosen.id == kStartOver) {
            if (TrainingStartOver()) menu.Close();
            else menu.Note("Start over: in a fight, once its round has begun.");
        } else if (chosen.kind == recomp::Menu::Chosen::Command && chosen.id == kHost) {
            /* (hosting, or stopping it; the menu closes to the game, the code on the screen) */
            if (OnlineHosting()) OnlineStopHosting();
            else OnlineHost();
            if (*OnlineNote() && !OnlineHosting()) {
                Configure();
                menu.Note(OnlineNote());
            } else {
                menu.Close();
            }
        }
        picture = frame;
        if (menu.IsOpen()) menu.Draw(picture.data(), static_cast<int>(width), static_cast<int>(height));
        const RecompD3dPresenterError shown = recomp_d3d_presenter_show_image(presenter, picture.data(), width, height);
        if (shown == RECOMP_D3D_PRESENTER_CLOSED) recomp_stop(0, "host-window-closed");
        if (scripted && script >= shot.keys.size()) {
            SaveBmp(shot.path, picture, width, height);
            std::fprintf(stderr, "recomp sc2: menu picture %s\n", shot.path.c_str());
            menu.Close();
        }
        /* (a frame's time where the swapchain does not wait, a hidden window) */
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    sc2_touch_set_menu(false);
    speed = menu.Speed() < 0 ? kFullSpeed : menu.Speed();
    /* The press that closed it does not reach the game. */
    for (int frames = 0; !scripted && Held() != 0u && frames < 120; ++frames) {
        recomp_d3d_presenter_show_image(presenter, frame.data(), width, height);
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    recomp_audio_output_set_paused(0);
}

/* For finding the game's state: SC2_RAM_DUMP=DIR and SC2_RAM_DUMP_AT=N,N,... save
   all of RAM at those Swaps as DIR/ram-N.bin. */
void DumpRam(uint32_t swap_counter)
{
    static const char *dir = std::getenv("SC2_RAM_DUMP");
    static const char *at = std::getenv("SC2_RAM_DUMP_AT");
    if (dir == nullptr || at == nullptr) return;
    for (const char *p = at; *p != '\0';) {
        char *end;
        const unsigned long n = std::strtoul(p, &end, 10);
        if (end == p) break;
        if (n == swap_counter) {
            const std::string path = std::string(dir) + "/ram-" + std::to_string(n) + ".bin";
            if (std::FILE *file = std::fopen(path.c_str(), "wb")) {
                std::fwrite(recomp_memory(0u, RECOMP_XBOX_RAM_SIZE), 1, RECOMP_XBOX_RAM_SIZE, file);
                std::fclose(file);
                std::fprintf(stderr, "recomp sc2: RAM %s\n", path.c_str());
            }
        }
        p = *end == ',' ? end + 1 : end;
    }
}

/* Whether the player has given a key to the game (then it does only that) */
bool KeyBound(SDL_Scancode code)
{
    const recomp::BindTable &table = BindingTable();
    for (int player = 0; player < table.players; ++player)
        for (int action = 0; action < table.count; ++action)
            if (bindings.key[player][action] == code) return true;
    return false;
}

/* A key's press (held now, not at the last Swap) that the game has not been given */
bool Pressed(SDL_Scancode code)
{
    static bool held[SDL_SCANCODE_COUNT];
    const bool now = recomp_input_scancode_held(code) && !KeyBound(code);
    const bool pressed = now && !held[code];
    held[code] = now;
    return pressed;
}

/* The keys while playing (not in an online match): F5 saves a state in the slot, F9 loads it, F6 and F7 choose the
   slot before or after; Backspace starts the round over; - and = slow the game down and speed it up again. */
void PlayKeys()
{
    if (OnlineInMatch() || OnlineHosting()) return;
    char note[64];
    if (Pressed(SDL_SCANCODE_F5)) AskSaveState(menu.Slot());
    if (Pressed(SDL_SCANCODE_F9)) {
        if (StateSaved(menu.Slot())) AskLoadState(menu.Slot());
        else {
            std::snprintf(note, sizeof note, "State %d is empty", menu.Slot());
            TrainingNote(note);
        }
    }
    const bool before = Pressed(SDL_SCANCODE_F6), after = Pressed(SDL_SCANCODE_F7);
    if (before || after) {
        menu.SetSlot(menu.Slot() + (after ? 1 : -1));
        std::snprintf(note, sizeof note, "Slot %d%s", menu.Slot(), StateSaved(menu.Slot()) ? "" : " (empty)");
        TrainingNote(note);
    }
    if (Pressed(SDL_SCANCODE_BACKSPACE)) TrainingStartOver();
    const bool slower = Pressed(SDL_SCANCODE_MINUS), faster = Pressed(SDL_SCANCODE_EQUALS);
    if (slower || faster) {
        speed = std::max(0, std::min(kSpeedCount - 1, speed + (faster ? 1 : -1)));
        std::snprintf(note, sizeof note, "Speed: %s", kSpeeds[speed].c_str());
        TrainingNote(note);
    }
}

/* The opening skipped by a press (Start, A, B, Return, Space), or by --skip-intro (tests, online play). The movie
   (scene 11) ends as a finished one does (sc2_hooks.c, movie_scene_update). Each logo scene (1, then 29) has an
   update (at +0x18 of the scene's task, [0x39871C]) that steps through its loading (+0x5C), then shows its logo for
   so many frames counted at +0x44 and goes on: the count is put past its end once the loading is done (logos_skipped
   keeps a press made while it loads, until it has). */
bool test_skip; /* (SC2_TEST_KEYS skip: a press) */
bool logos_skipped;

void SkipOpening()
{
    enum : uint32_t { kMovie = 11u };
    /* (the two logo scenes: their update, the step at +0x5C from which the logo shows, the frames it shows) */
    static const struct { uint32_t update, ready, frames; } kLogos[] = {{0x0004fef0u, 10u, 0x78u}, {0x00050190u, 1u, 0xb4u}};
    const uint64_t pad = recomp_input_host_pad_buttons();
    const bool held = recomp_input_scancode_held(SDL_SCANCODE_RETURN) || recomp_input_scancode_held(SDL_SCANCODE_SPACE) ||
        (pad >> SDL_GAMEPAD_BUTTON_START & 1u) || (pad >> SDL_GAMEPAD_BUTTON_SOUTH & 1u) ||
        (pad >> SDL_GAMEPAD_BUTTON_EAST & 1u);
    static bool held_before = true;
    const bool pressed = (held && !held_before) || test_skip;
    held_before = held;
    test_skip = false;
    const uint32_t current = *recomp_memory_u32(0x398700u);
    if (current == kMovie && pressed) sc2_movie_skip = 1;
    const uint32_t task = *recomp_memory_u32(0x39871Cu);
    const uint32_t update = task != 0u && (current == 1u || current == 29u) ? *recomp_memory_u32(task + 0x18u) : 0u;
    for (const auto &logo : kLogos) {
        if (update != logo.update) continue;
        if (pressed || recomp_skip_intro) logos_skipped = true;
        uint32_t *shown = recomp_memory_u32(task + 0x44u);
        if (logos_skipped && *recomp_memory_u32(task + 0x5cu) >= logo.ready && *shown <= logo.frames) {
            *shown = logo.frames + 1u;
            std::fprintf(stderr, "recomp sc2: the logos skipped (scene %u)\n", current);
        }
        return;
    }
    logos_skipped = false;
}

void OnPresent(uint32_t swap_counter)
{
    DumpRam(swap_counter);
    SkipOpening();
    ApplyTraining(swap_counter);
    PlayKeys();
    /* (tests: SC2_TEST_KEYS=SWAP:WHAT,... does at those Swaps what a key or the menu would: save, load (slot 1),
       over (start over), record, replay, slow, fast (2x), pause, unlock, endless, skip: a press, hud: the HUD hidden or shown) */
    if (const char *keys = std::getenv("SC2_TEST_KEYS")) {
        for (const char *p = keys; *p;) {
            char *end;
            const unsigned long at = std::strtoul(p, &end, 10);
            if (*end != ':') break;
            const char *what = end + 1;
            const char *next = std::strchr(what, ',');
            const std::string action(what, next ? next - what : std::strlen(what));
            if (at == swap_counter) {
                std::fprintf(stderr, "recomp sc2: test key %u %s\n", swap_counter, action.c_str());
                if (action == "save") AskSaveState(menu.Slot());
                else if (action == "load") AskLoadState(menu.Slot());
                else if (action == "over") TrainingStartOver();
                else if (action == "record") TrainingState().dummy = Dummy::Record;
                else if (action == "replay") TrainingState().dummy = Dummy::Replay;
                else if (action == "slow") speed = 1;
                else if (action == "fast") speed = 8;
                else if (action == "pause") recomp_scene_freeze_set(true);
                else if (action == "unlock") TrainingUnlockAll();
                else if (action == "endless") TrainingState().endless = !TrainingState().endless;
                else if (action == "skip") test_skip = true;
                else if (action == "hud") recomp_scene_freeze_set_overlay_hidden(!recomp_scene_freeze_overlay_hidden());
            }
            p = next ? next + 1 : end + std::strlen(end);
        }
    }
    /* (slower: each frame shown for longer; faster: some frames not shown, GameSpeed) */
    if (speed < kFullSpeed && !OnlineInMatch()) {
        const Speed &one = kSpeedTable[speed];
        std::this_thread::sleep_for(std::chrono::microseconds(16667 * (one.ticks - one.frames) / one.frames));
    }
    static bool held_before = true; /* (a key held at the start does not open it) */
    if (shot.present != 0u && swap_counter == shot.present) {
        Run(true);
        return;
    }
    const bool held = OpenHeld();
    if (held && !held_before) Run(false);
    held_before = OpenHeld();
}

} // namespace

double GameSpeed()
{
    const Speed &one = kSpeedTable[speed];
    return OnlineInMatch() ? 1.0 : static_cast<double>(one.frames) / one.ticks;
}

void SetShader(int choice)
{
    shader_choice = choice;
    recomp_d3d_set_shader_preset(ShaderPreset(choice, settings_path).c_str());
}

void InstallMenu(const std::string &settingsPath, const std::string &shotSpec)
{
    settings_path = settingsPath;
    const recomp::BindTable &table = BindingTable();
    bindings = settingsPath.empty() ? recomp::DefaultBindings(table) : recomp::LoadBindings(settingsPath, table);
    for (int player = 0; player < table.players; ++player) {
        for (int action = 0; action < table.count; ++action) {
            if (bindings.key[player][action] == SDL_SCANCODE_ESCAPE) escape_free = false;
            if (bindings.pad[player][action].button == SDL_GAMEPAD_BUTTON_BACK) back_free = false;
        }
    }
    if (!shotSpec.empty()) {
        const size_t first = shotSpec.find(':'), second = shotSpec.find(':', first + 1u);
        if (first != std::string::npos && second != std::string::npos) {
            shot.present = static_cast<uint32_t>(std::strtoul(shotSpec.substr(0, first).c_str(), nullptr, 10));
            shot.keys = shotSpec.substr(first + 1u, second - first - 1u);
            shot.path = shotSpec.substr(second + 1u);
        }
    }
    recomp_d3d_present_hook = OnPresent;
}

} // namespace sc2
