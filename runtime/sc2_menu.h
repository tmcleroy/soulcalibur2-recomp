/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The in-game menu (recomp-common/menu.h) over a paused game. */
#ifndef SC2_MENU_H
#define SC2_MENU_H

#include <string>

namespace sc2 {

/* Installs the menu at each Swap (recomp_d3d_present_hook). The keys and
   buttons the player gave the game (settings.ini) never open it. `shot`, for
   tests, is PRESENT:KEYS:FILE.bmp: at that Swap the menu opens, takes KEYS one
   a frame (u d l r: directions, c: confirm, b: back), saves its picture to
   FILE.bmp and closes. */
void InstallMenu(const std::string &settingsPath, const std::string &shot);

/* The Shader feature's choice (sc2_settings.h, ShaderPreset): the presenter draws the picture through it from its
   next frame. After InstallMenu (Your own's folder is beside its settings). */
void SetShader(int choice);

/* The speed chosen (the menu's Speed, - and =): game frames to a frame shown (1 at full speed; online, always 1) */
double GameSpeed();

/* Tests (--export-pictures PRESENT[:FOLDER]): at that Swap the menu's Export pictures runs, to FOLDER or the pictures
   folder beside settings.ini; it says how many on stderr and the game ends (stop "export-pictures", 1 on a failure). */
void ExportPicturesAt(const std::string &spec);

/* Reset game (the menu's): this copy of the game replaced by a new one with the same arguments, as if the Xbox were
   switched off and on (runner.cpp, as joining a match from the menu starts it again) */
[[noreturn]] void StartGameAgain();

/* Tests (SC2_TEST_KEYS SWAP:start): reads of controller 1 still to be given Start, counted down by the runner */
extern int test_start_reads;

/* Texture packs (the runtime's d3d_texture_replace.h): textures/ and textures-dump/ beside settings.ini, on or off,
   while playing too (the presenter makes its textures again). */
void ApplyTextures(const std::string &settings_path, bool hd, bool dump);
bool TexturesHd();
bool TexturesDump();

} // namespace sc2

#endif
