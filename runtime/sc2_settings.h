/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Soulcalibur II's settings, shared by the runner and the launcher: the
   features in settings.ini's [features] (recomp-common/settings.h) and the
   keys and buttons for each Xbox control ([keyboard1], [gamepad1], ...;
   recomp-common/launcher/src/bindings.h). */
#ifndef SC2_SETTINGS_H
#define SC2_SETTINGS_H

#include "../recomp-common/settings.h"
#include "../recomp-common/launcher/src/bindings.h"

#include <string>
#include <vector>

namespace sc2 {

/* Every feature, in the order the launcher shows them. */
const std::vector<recomp::FeatureInfo> &Features();

/* The Xbox controller's controls, each bindable to a key and a gamepad
   button or trigger, for two players. The sticks are not bindable: each
   player's gamepad drives them. */
const recomp::BindTable &BindingTable();

/* Move the preview's old rendering key into the shared feature settings once. */
void MigrateRenderingSettings(recomp::Ini &ini);
bool ValidRenderRate(const std::string &text);

/* What settings.ini chooses, as the runner uses it. */
struct Settings {
    std::string render_fps; // empty/off, monitor, or a numeric presentation rate
    bool graphics_prepare = false;
    unsigned scale = 1;   /* internal resolution: N times 640x480 */
    int shader = 0;       /* the Shader feature's choice (ShaderPreset) */
    bool health_refill = false;
    bool infinite_time = false;
    bool input_display = false, combo_readout = false, hit_boxes = false, frame_data = false;
    bool no_damage[2] = {false, false}, one_hit = false;   /* the cheats */
    bool fps_counter = false;
    bool online_ping = true, online_delay = false, online_hide_names = false;   /* (sc2_online.h) */
};

Settings LoadSettings(const std::string &path);

/* The preset (a .slangp) for the Shader feature's choice, "" for Off (or Your own with none there): the game's
   shaders/ folder, or Your own's beside settings.ini. */
std::string ShaderPreset(int choice, const std::string &settings_path);

/* Gives the runtime's host input the player's layout from settings.ini (the
   usual one where it says nothing). The runner's alone (sc2_input.cpp). */
void ApplyBindings(const std::string &path);

} // namespace sc2

#endif
