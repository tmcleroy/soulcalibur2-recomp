#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# The Linux (x86_64, SteamOS) build: the game and the launcher in build/release-linux, packed as
# build/Soulcalibur2Recomp-linux-x86_64.tar.gz. Run on the Mac in Docker, with the repo mounted at its own
# path (build/src links by absolute path):
#   docker run --rm --platform linux/amd64 -v "$PWD":"$PWD" -w "$PWD" doaxvb-build tools/release/linux.sh
# The lift (private/program) is made on the Mac first; it is the same for every system. shaderc is fetched on the
# Mac first too (tools/release/fetch_shaderc_linux.sh).
set -e
root=$(cd "$(dirname "$0")/../.." && pwd)
out="$root/build/release-linux"
export SC2_SRC_DIR=build/src-linux SC2_RUNNER_DIR=build/linux-runner SC2_LAUNCHER_DIR=build/linux-launcher
# (the program finds the SDL3 beside it)
export SC2_CMAKE_ARGS="-DRECOMP_FETCH_SDL3=ON -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON -DCMAKE_INSTALL_RPATH=\$ORIGIN"
python3 "$root/tools/build_game.py" build
# the European disc's program, which the US one hands a European disc to (runner.cpp handToOtherRelease, #60): its
# lift (private/program-pal) is made on the Mac first too, from the European disc (tools/build_game.py --pal lift)
if [ ! -d "$root/private/program-pal" ]; then
    echo "private/program-pal is missing: lift the European disc first (tools/build_game.py --pal lift)" >&2
    exit 1
fi
python3 "$root/tools/build_game.py" --pal build
python3 "$root/tools/build_game.py" launcher
rm -rf "$out"
mkdir -p "$out/Soulcalibur2Recomp"
pkg="$out/Soulcalibur2Recomp"
cp "$root/build/linux-runner/recomp_program_runner" "$pkg/soulcalibur2"
cp "$root/build/linux-runner-pal/recomp_program_runner" "$pkg/soulcalibur2-pal"
cp "$root/build/linux-launcher/soulcalibur2-launcher" "$pkg/"
# the runtime's shared SDL3, found beside the program
# (as its soname, a plain file: the launcher's updates list files, not links)
cp -L "$root/build/linux-runner/_deps/sdl3-build/libSDL3.so.0" "$pkg/libSDL3.so.0"
# shaderc, found the same way: the renderer can't start without it, and not every distribution has it (#30, #40)
cp "$root/private/toolchains/shaderc-linux/libshaderc_shared.so.1" "$pkg/"
cp "$root/LICENSE" "$root/README.md" "$root/THIRD_PARTY_NOTICES.md" "$pkg/"
cp -R "$root/docs/licenses" "$pkg/licenses"
# (the Shader feature's presets, beside the game: tools/shaders.sh)
cp -R "$("$root/tools/shaders.sh")" "$pkg/shaders"
tar czf "$root/build/Soulcalibur2Recomp-linux-x86_64.tar.gz" -C "$out" Soulcalibur2Recomp
echo "built $root/build/Soulcalibur2Recomp-linux-x86_64.tar.gz"
"$root/tools/release/update_manifest.sh" linux-x86_64 "$pkg" "$root/build/Soulcalibur2Recomp-linux-x86_64.tar.gz"
