// SPDX-License-Identifier: GPL-3.0-or-later
#include "sc2_settings.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <fstream>
int main()
{
#ifdef RECOMP_SDL_GPU_PRESENTER
    constexpr bool supported = true;
    const std::string path = "graphics-settings-test.ini";
#else
    constexpr bool supported = false;
    const std::string path = "graphics-settings-unsupported-test.ini";
    for (const auto &f : sc2::Features()) assert(f.name != "graphics_prepare");
#endif
    auto write = [&](const char *value) { std::ofstream(path) << value; };
    for (const auto &f : sc2::Features()) assert(f.name != "shader_workers");
    write("[features]\npreset=pure\n"); assert(!sc2::LoadSettings(path).graphics_prepare);
    write("[features]\npreset=enhanced\n"); assert(sc2::LoadSettings(path).graphics_prepare == supported);
    write("[features]\npreset=custom\ngraphics_prepare=0\n"); assert(!sc2::LoadSettings(path).graphics_prepare);
    write("[features]\npreset=custom\ngraphics_prepare=1\n"); assert(sc2::LoadSettings(path).graphics_prepare == supported);
    std::remove(path.c_str());
}
