/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "sc2_settings.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <filesystem>

namespace sc2 {

namespace {

enum : unsigned { kScaleChoices = 7 };
constexpr unsigned kScales[kScaleChoices] = {1u, 2u, 3u, 4u, 5u, 6u, 8u};

/* The controls (the stick clicks left out); sc2_input.cpp gives each to the
   runtime by its name. */
struct Control {
    const char *name, *title;
    SDL_Scancode key;          /* player 1's usual key */
    recomp::PadInput pad;      /* each player's usual button or trigger */
};

/* The game's default button setup: X horizontal, Y vertical, B kick, A
   guard. The usual keys are the runtime's built-in ones. */
const Control kControls[] = {
    {"up", "Up", SDL_SCANCODE_UP, {SDL_GAMEPAD_BUTTON_DPAD_UP, -1}},
    {"down", "Down", SDL_SCANCODE_DOWN, {SDL_GAMEPAD_BUTTON_DPAD_DOWN, -1}},
    {"left", "Left", SDL_SCANCODE_LEFT, {SDL_GAMEPAD_BUTTON_DPAD_LEFT, -1}},
    {"right", "Right", SDL_SCANCODE_RIGHT, {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, -1}},
    {"x", "X: horizontal attack", SDL_SCANCODE_A, {SDL_GAMEPAD_BUTTON_WEST, -1}},
    {"y", "Y: vertical attack", SDL_SCANCODE_S, {SDL_GAMEPAD_BUTTON_NORTH, -1}},
    {"b", "B: kick", SDL_SCANCODE_X, {SDL_GAMEPAD_BUTTON_EAST, -1}},
    {"a", "A: guard", SDL_SCANCODE_Z, {SDL_GAMEPAD_BUTTON_SOUTH, -1}},
    {"black", "Black", SDL_SCANCODE_W, {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, -1}},
    {"white", "White", SDL_SCANCODE_Q, {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, -1}},
    {"ltrigger", "Left trigger", SDL_SCANCODE_E, {-1, SDL_GAMEPAD_AXIS_LEFT_TRIGGER}},
    {"rtrigger", "Right trigger", SDL_SCANCODE_R, {-1, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER}},
    {"start", "Start", SDL_SCANCODE_RETURN, {SDL_GAMEPAD_BUTTON_START, -1}},
    /* Unbound unless chosen: a gamepad's Back opens the in-game menu (sc2_menu.cpp). */
    {"back", "Back", SDL_SCANCODE_UNKNOWN, {-1, -1}},
};
constexpr int kControlCount = static_cast<int>(sizeof kControls / sizeof kControls[0]);

/* The Shader feature's presets, after Off, from RetroArch's slang-shaders (cps2recomp's Display page's): in shaders/
   beside the game (tools/shaders.sh copies them and what they use, tools/shaders.py reads this table). */
const char *const kShaderPresets[] = {
    "crt/crt-geom-mini.slangp", "crt/crt-lottes.slangp", "crt/crt-royale.slangp", "crt/crt-guest-advanced-fast.slangp",
    "crt/zfast-crt.slangp", "scanlines/scanline-fract.slangp", "pixel-art-scaling/sharp-bilinear-scanlines.slangp",
    "scanlines/ossc.slangp", "edge-smoothing/xbrz/xbrz-freescale-multipass.slangp",
    "edge-smoothing/scalefx/scalefx.slangp",
};
constexpr int kPresetCount = static_cast<int>(sizeof kShaderPresets / sizeof kShaderPresets[0]);
constexpr int kPlayers = 2;

} // namespace

const std::vector<recomp::FeatureInfo> &Features()
{
    static const std::vector<recomp::FeatureInfo> features = {
        {"resolution", "Graphics", "Resolution", "Draws the game at a multiple of the Xbox's 640x480. Above 4x needs a fast graphics card: 6x and 8x are "
            "for a 4K screen.",
            recomp::kFeatureEnhanced, {"Original (640x480)", "2x (1280x960)", "3x (1920x1440)", "4x (2560x1920)",
                "5x (3200x2400)", "6x (3840x2880)", "8x (5120x3840)"}},
        {"shader", "Graphics", "Shader",
            "The picture drawn through one of RetroArch's shaders, at the Xbox's 480 lines whatever the resolution. "
            "CRT: a curved monitor. CRT Lottes and CRT Guest: flat monitors, sharp and bright. CRT Royale: the closest "
            "to a real monitor's glass and the slowest. CRT Light: lines and a faint mask, for a slow computer. "
            "Scanlines: the lines alone, light, strong or as an OSSC draws them. xBRZ and ScaleFX: edges made smooth "
            "lines and curves. Your own: the first .slangp preset in the shaders folder beside the game's settings. "
            "The in-game menu is drawn over it without the shader.",
            0, {"Off", "CRT", "CRT Lottes", "CRT Royale", "CRT Guest", "CRT Light", "Scanlines: light",
                "Scanlines: strong", "Scanlines: OSSC", "xBRZ", "ScaleFX", "Your own"}},
#ifdef RECOMP_SDL_GPU_PRESENTER
#ifdef _WIN32
        {"frame_interpolation", "Rendering", "Frame interpolation",
            "Smoother motion while gameplay stays at 60 Hz. Uses a frame of visual history, plus rendering/display delay. "
            "Set a custom rate on the Rendering page. Changes apply next launch.",
            0, {"Off (original 60 Hz)", "Match monitor", "Custom rate"}},
#endif
        {"graphics_prepare", "Rendering", "Shader preparation",
            "Prepare available shader combinations before playing to reduce compilation hitches. "
            "Cached shaders are reused. Unseen combinations may still compile during play. Changes apply next launch.",
            recomp::kFeatureEnhanced, {"During play", "Before playing"}},
#endif
        {"fps_counter", "Graphics", "FPS counter",
            "The frames shown each second, in the top right corner. The game runs at 60.", 0, {}},
        {"health_refill", "Training", "Health refill",
            "A fighter's health fills again once a combo is over (when it has stayed put for a moment).", 0, {}},
        {"infinite_time", "Training", "Infinite time", "The round's clock stays where it started.", 0, {}},
        {"input_display", "Training", "Input display",
            "What each fighter holds, newest at the top, down their side of the picture: for how many frames, the "
            "direction in numpad notation from their own side (6 towards the other fighter, 2 down), and the buttons "
            "(A horizontal, B vertical, K kick, G guard).",
            0, {}},
        {"combo_readout", "Training", "Combo readout",
            "Under each fighter's life bar: the hits and damage of their combo (a hit counts in it while the one hit "
            "is still reeling from the last), the last hit's damage, and the round's best. Health is 240 at full.",
            0, {}},
        {"hit_boxes", "Training", "Hit boxes",
            "The fighters' own collision shapes, as the game tests them: where each can be hit (blue circles), its "
            "pushing sphere (green), and the weapon and limbs that hit (lines: red while an attack is live).",
            0, {}},
        {"frame_data", "Training", "Frame data",
            "Under each fighter's life bar, its last move's startup (to its first live frame), active and recovery "
            "frames and the advantage after it on hit or guard; and a frame meter above the bottom of the picture, a "
            "cell a frame for each fighter: startup green, active red, recovery blue, stun yellow.",
            0, {}},
        {"dummy", "Training", "Dummy",
            "Record: your controls work player 2 (up to 20 seconds) while player 1 stands. Replay: player 2 does it "
            "again and again. In Practice (its dummy is player 2 while this is on) and VS Battle.",
            recomp::kFeatureSession, {"Off", "Record", "Replay"}},
        {"endless", "Modes", "Endless battle",
            "In any fight: the game plays both fighters, for as long as you like, with no knockouts, no clock and no "
            "bars. Touch a fighter's controls to take it over (for five seconds after you let go).",
            recomp::kFeatureSession, {}},
        {"no_damage_p1", "Cheats", "Player 1 takes no damage", "Player 1's health stays full.", 0, {}},
        {"no_damage_p2", "Cheats", "Player 2 takes no damage", "Player 2's health stays full.", 0, {}},
        {"one_hit", "Cheats", "One-hit knockouts", "Any hit empties the health of whoever it hits.", 0, {}},
        {"online_ping", "Online", "Ping",
            "During an online match, the time a packet takes to the other player and back, at the bottom of the picture "
            "(\"PING 42 ms\").",
            recomp::kFeatureEnhanced, {}},
        {"online_delay", "Online", "Input delay",
            "During an online match, how many frames each player's own controls are held back, at the bottom of the "
            "picture (\"DELAY 2\").",
            0, {}},
        {"online_hide_names", "Online", "Hide names",
            "During an online match, the two players' names (each gave theirs on the launcher's Online page) are at the "
            "bottom of the picture: this hides them.",
            0, {}},
    };
    return features;
}

const recomp::BindTable &BindingTable()
{
    static const struct Table {
        recomp::BindAction actions[kControlCount];
        recomp::BindTable table;
        Table()
        {
            for (int i = 0; i < kControlCount; ++i) {
                actions[i] = {kControls[i].name, kControls[i].title, {kControls[i].key, SDL_SCANCODE_UNKNOWN},
                    {kControls[i].pad, kControls[i].pad}};
            }
            table = {actions, kControlCount, kPlayers};
        }
    } table;
    return table.table;
}

std::string ShaderPreset(int choice, const std::string &settings_path)
{
    if (choice >= 1 && choice <= kPresetCount) {
        // (Android: the app unpacks its presets into a folder of its own and names it, GameActivity.java)
        if (const char *unpacked = std::getenv("SC2_SHADERS"); unpacked != nullptr && *unpacked)
            return std::string(unpacked) + "/" + kShaderPresets[choice - 1];
        const char *base = SDL_GetBasePath();
        return std::string(base != nullptr ? base : "") + "shaders/" + kShaderPresets[choice - 1];
    }
    if (choice != kPresetCount + 1 || settings_path.empty()) return {};
    // Your own: the first .slangp, by name, in shaders/ beside settings.ini
    std::vector<std::filesystem::path> found;
    std::error_code error;
    const std::filesystem::path folder = std::filesystem::path(settings_path).parent_path() / "shaders";
    for (const auto &entry : std::filesystem::directory_iterator(folder, error))
        if (entry.path().extension() == ".slangp") found.push_back(entry.path());
    std::sort(found.begin(), found.end());
    return found.empty() ? std::string() : found.front().string();
}

bool ValidRenderRate(const std::string &text)
{
    char *end = nullptr;
    const double rate = std::strtod(text.c_str(), &end);
    return end != text.c_str() && *end == '\0' && std::isfinite(rate) && rate >= 30.0 && rate <= 1000.0;
}

void MigrateRenderingSettings(recomp::Ini &ini)
{
    const std::string legacy = ini.get("rendering", "fps");
    if (legacy.empty()) return;
    if (ValidRenderRate(legacy)) ini.set("rendering", "custom_fps", legacy);
    if (recomp::Preset(ini) != "pure") {
        const bool had_prepare = !ini.get("features", "graphics_prepare").empty();
        const auto &features = Features();
        for (size_t i = 0; i < features.size(); ++i) {
            if (features[i].name == "graphics_prepare" && !had_prepare) {
                recomp::SetFeature(ini, features, i, 1);
                continue;
            }
            if (!ini.get("features", features[i].name).empty()) continue;
            if (features[i].name == "frame_interpolation")
                recomp::SetFeature(ini, features, i, legacy == "monitor" ? 1 : ValidRenderRate(legacy) ? 2 : 0);
            else if (features[i].name == "graphics_prepare")
                recomp::SetFeature(ini, features, i, 1);
        }
    }
    ini.set("rendering", "fps", "");
    ini.save();
}

Settings LoadSettings(const std::string &path)
{
    recomp::Ini ini(path);
    ini.load();
    MigrateRenderingSettings(ini);
    const auto &features = Features();
    const std::vector<int> values = recomp::FeatureValues(ini, features);
    Settings settings;
    settings.render_fps = "off";
    for (size_t i = 0; i < features.size(); ++i) {
        if (features[i].name == "frame_interpolation") {
            const std::string rate = ini.get("rendering", "custom_fps");
            settings.render_fps = values[i] == 1 ? "monitor" : values[i] == 2 ?
                (ValidRenderRate(rate) ? rate : "144") : "off";
        }
        else if (features[i].name == "graphics_prepare") settings.graphics_prepare = values[i] != 0;
        else if (features[i].name == "health_refill") settings.health_refill = values[i] != 0;
        else if (features[i].name == "fps_counter") settings.fps_counter = values[i] != 0;
        else if (features[i].name == "infinite_time") settings.infinite_time = values[i] != 0;
        else if (features[i].name == "input_display") settings.input_display = values[i] != 0;
        else if (features[i].name == "combo_readout") settings.combo_readout = values[i] != 0;
        else if (features[i].name == "hit_boxes") settings.hit_boxes = values[i] != 0;
        else if (features[i].name == "frame_data") settings.frame_data = values[i] != 0;
        else if (features[i].name == "no_damage_p1") settings.no_damage[0] = values[i] != 0;
        else if (features[i].name == "no_damage_p2") settings.no_damage[1] = values[i] != 0;
        else if (features[i].name == "shader") settings.shader = values[i];
        else if (features[i].name == "one_hit") settings.one_hit = values[i] != 0;
        else if (features[i].name == "online_ping") settings.online_ping = values[i] != 0;
        else if (features[i].name == "online_delay") settings.online_delay = values[i] != 0;
        else if (features[i].name == "online_hide_names") settings.online_hide_names = values[i] != 0;
        else if (features[i].name == "resolution" && values[i] >= 0 && values[i] < static_cast<int>(kScaleChoices))
            settings.scale = kScales[values[i]];
    }
    return settings;
}

} // namespace sc2
