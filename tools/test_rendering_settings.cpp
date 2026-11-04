// SPDX-License-Identifier: GPL-3.0-or-later
#include "../runtime/sc2_settings.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <fstream>
#include <cstdio>
int main(int argc, char **argv) {
    assert(argc == 2);
    for (const auto &feature : sc2::Features()) assert(feature.name != "shader_workers");
    const std::string path=argv[1];
    auto write=[&](const char *s){std::ofstream(path)<<s;};
    write("[features]\npreset=custom\nresolution=3\n[rendering]\nfps=143.98\n[other]\nkeep=yes\n");
    auto s=sc2::LoadSettings(path);
    assert(s.render_fps=="143.98" && s.graphics_prepare && s.scale==4);
    recomp::Ini ini(path);ini.load();
    assert(ini.get("rendering","fps").empty() && ini.get("other","keep")=="yes");
    recomp::SetPreset(ini,sc2::Features(),"pure");ini.save();
    s=sc2::LoadSettings(path);assert(s.render_fps=="off" && !s.graphics_prepare);
    ini.load();recomp::SetPreset(ini,sc2::Features(),"enhanced");ini.save();
    s=sc2::LoadSettings(path);assert(s.render_fps=="off" && s.graphics_prepare);
    write("[features]\npreset=custom\nframe_interpolation=1\ngraphics_prepare=1\nshader_workers=5\n");
    s=sc2::LoadSettings(path);assert(s.render_fps=="monitor");
    write("[features]\npreset=custom\nframe_interpolation=2\nshader_workers=999\n[rendering]\ncustom_fps=nan\n");
    s=sc2::LoadSettings(path);assert(s.render_fps=="144");
    for(const char *bad:{"", "nan", "inf", "144oops", "29", "1001"})assert(!sc2::ValidRenderRate(bad));
    for(const char *good:{"30", "1000", "143.98"})assert(sc2::ValidRenderRate(good));
    std::remove(path.c_str());
    std::puts("PASS: migration, preservation, presets, monitor/custom rate, invalid inputs and ignored legacy worker preference");
}
