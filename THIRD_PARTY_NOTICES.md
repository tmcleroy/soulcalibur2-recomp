# Third-party notices

Soulcalibur II Recomp is GPL-3.0-or-later (`LICENSE`). It contains, builds on or ships the work below, each under
its own license. The full texts are in [docs/licenses](docs/licenses), and every release archive carries this file
and that folder.

Nothing of Soulcalibur II is in this repository or its releases' data: the player's own disc is read on their
machine, and the guests (Link, Heihachi) are converted there from the player's own GameCube and PlayStation 2
discs. The HD texture import (the Textures page) holds only a map of texture names (hashes), and reads the pack
the player downloaded themselves.

## In this repository

| What | License | Where, and what for |
| --- | --- | --- |
| [recomp-common](https://github.com/tmcleroy/recomp-common) | GPL-3.0 | `recomp-common/`, a synced copy: the launcher, the in-game menu and the settings. Its launcher derives from the launcher of [wiiparty-recomp](https://github.com/arelkair/wiiparty-recomp) by arelkair (GPL-3.0-or-later), as its files say |
| musl's math (sin, cos, atan2, log2, expm1) | MIT ([docs/licenses/musl.txt](docs/licenses/musl.txt)) | `runtime/x87_math/`, so the game's x87 arithmetic gives the same results on every system |
| The online service | GPL-3.0-or-later | `online/`, from cps2recomp's `online/` (the same author) |
| [xxHash](https://github.com/Cyan4973/xxHash) 0.8.2 (Yann Collet) | BSD-2-Clause ([xxHash.txt](docs/licenses/xxHash.txt)) | `third_party/doa2u-re/recomp-runtime/xxhash.h`: texture packs' names (the game) |
| [stb_image_write](https://github.com/nothings/stb) 1.16 (Sean Barrett) | Public domain or MIT (its end) | `runtime/stb/stb_image_write.h`: the PNG files of the menu's Export pictures |

## Submodules

| What | License | Use |
| --- | --- | --- |
| [doa2u-re](https://github.com/tmcleroy/doa2u-re) (branch `soulcalibur2-runtime`), NoRain211's DOA runtime (from doa3-re and doaxvb-re) | GPL-3.0-or-later (its `LICENSE` and `NOTICE`) | `third_party/doa2u-re`: the runtime the game runs on (kernel, Direct3D 8 on SDL_GPU, DirectSound, input). In every build |
| [xboxrecomp](https://github.com/NoRain211/xboxrecomp) (sp00nznet's, NoRain211's fork) | MIT (its `LICENSE`; its `NOTICE` lists files of its own under other licenses) | `tools/xboxrecomp`: the translator that turns the player's `Default.xbe` into C on their machine. Only its Python tools run (`tools.xbe_parser`, `tools.disasm`, `tools.recomp`); none of its code is built into the programs. Its `src/apu` and `src/nv2a/nv2a_regs.h` are LGPL-2.1 files from xemu that this project does not use |

## Downloaded at build time, and in the release programs

| What | License | In |
| --- | --- | --- |
| [SDL 3](https://github.com/libsdl-org/SDL) 3.4.16 | zlib ([SDL3.txt](docs/licenses/SDL3.txt)) | The game and the launcher: `libSDL3` beside them (the app's `Frameworks`, the Linux folder, `SDL3.dll`) |
| [Dear ImGui](https://github.com/ocornut/imgui) 1.92.9b | MIT ([DearImGui.txt](docs/licenses/DearImGui.txt)) | The launcher, and the free camera's panel in the game |
| [Inter](https://github.com/rsms/inter) | SIL Open Font License 1.1 ([Inter-OFL.txt](docs/licenses/Inter-OFL.txt)) | Compiled into the launcher |
| [SMAA](https://github.com/iryoku/smaa) (`71c806a`) | MIT ([SMAA.txt](docs/licenses/SMAA.txt)) | The game's anti-aliasing |
| [shaderc](https://github.com/google/shaderc) (with glslang and SPIRV-Tools inside) | Apache-2.0 ([shaderc.txt](docs/licenses/shaderc.txt)) | Compiles the renderer's shaders: `libshaderc_shared` in the macOS app and the Windows and Linux folders |
| [SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross) | Apache-2.0 ([SPIRV-Cross.txt](docs/licenses/SPIRV-Cross.txt)) | The macOS game: its shaders turned into Metal |
| [slang-shaders](https://github.com/libretro/slang-shaders) (commit `2b0530b`) | Each file its own, stated at its top: crt-geom-mini (after cgwg's crt-geom), CRT Royale (TroggleMonkey), CRT Guest (guest(r), Dr. Venom) and zfast-crt, CRT Light (Greg Hogan) GPL-2.0-or-later; CRT Lottes (Timothy Lottes), scanline-fract (hunterk) and sharp-bilinear-scanlines (rsn8887) public domain; xBRZ (Hyllian, Zenju) and ScaleFX (Sp00kyFox) and the blur and gamma includes MIT; the stock pass (libretro), ossc (DariusG) and integer-scaling-scanlines with no notice | The Graphics page's shader presets, in `shaders/` beside the game (`tools/shaders.sh`), each file with its header |
| LLVM's libc++ and libunwind (from [llvm-mingw](https://github.com/mstorsjo/llvm-mingw)) | Apache-2.0 with LLVM exceptions ([llvm-mingw.txt](docs/licenses/llvm-mingw.txt)) | `libc++.dll` and `libunwind.dll` in the Windows folder |

## Used while developing (nothing of it here or in the releases)

- [Cxbx-Reloaded](https://github.com/Cxbx-Reloaded/Cxbx-Reloaded)'s XbSymbolDatabase: its signatures, read from a
  local checkout by `tools/xdk_symbols.py`, name the Xbox library functions in the player's executable.
- xemu, Cxbx-Reloaded and xboxdevwiki, as references for how the Xbox behaves.

## Names

Soulcalibur is a trademark of Bandai Namco Entertainment; Link and The Legend of Zelda of Nintendo; Heihachi Mishima
and Tekken of Bandai Namco Entertainment; Spawn of Todd McFarlane Productions. They are used only to say what this
program works with. This project is not affiliated with or endorsed by any of them.
