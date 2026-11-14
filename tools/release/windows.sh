#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# The Windows x64 build, cross-compiled with llvm-mingw (the compiler the shared runtime is built with on
# Windows): build/Soulcalibur2Recomp-windows-x64.zip. Run on the Mac in Docker, the repo mounted at its own
# path, with llvm-mingw for the container's system extracted in private/toolchains and shaderc fetched
# (tools/release/fetch_shaderc_windows.sh, on the Mac):
#   docker run --rm -v "$PWD":"$PWD" -w "$PWD" cps2recomp-build-arm64 tools/release/windows.sh
set -e
root=$(cd "$(dirname "$0")/../.." && pwd)
export LLVM_MINGW=$(ls -d "$root"/private/toolchains/llvm-mingw-*-"$(uname -m)" | head -1)
out="$root/build/release-windows"
export SC2_SRC_DIR=build/src-windows SC2_RUNNER_DIR=build/windows-runner SC2_LAUNCHER_DIR=build/windows-launcher
export SC2_CMAKE_ARGS="-DCMAKE_TOOLCHAIN_FILE=$root/tools/release/llvm-mingw.cmake -DRECOMP_WINDOWS_SDL_GPU=ON -DRECOMP_GAME_LIBS=ws2_32;iphlpapi"
python3 "$root/tools/build_game.py" build
# the European disc's program, which the US one hands a European disc to (runner.cpp handToOtherRelease, #60): its
# lift (private/program-pal) is made on the Mac first too, from the European disc (tools/build_game.py --pal lift)
if [ ! -d "$root/private/program-pal" ]; then
    echo "private/program-pal is missing: lift the European disc first (tools/build_game.py --pal lift)" >&2
    exit 1
fi
python3 "$root/tools/build_game.py" --pal build
python3 "$root/tools/build_game.py" launcher
pkg="$out/Soulcalibur2Recomp"
rm -rf "$out" && mkdir -p "$pkg"
cp "$root/build/windows-runner/recomp_program_runner.exe" "$pkg/soulcalibur2.exe"
cp "$root/build/windows-runner-pal/recomp_program_runner.exe" "$pkg/soulcalibur2-pal.exe"
cp "$root/build/windows-launcher/soulcalibur2-launcher.exe" "$pkg/"
cp "$root"/build/windows-runner/*.dll "$pkg/" 2>/dev/null || true
for dll in libc++.dll libunwind.dll; do cp "$LLVM_MINGW/x86_64-w64-mingw32/bin/$dll" "$pkg/"; done
# (the SDL_GPU presenter compiles its shaders with it)
cp "$root/private/toolchains/shaderc-windows/libshaderc_shared.dll" \
    "$root/private/toolchains/shaderc-windows/LICENSE.shaderc.txt" "$pkg/"
cp "$root/LICENSE" "$root/README.md" "$root/THIRD_PARTY_NOTICES.md" "$pkg/"
cp -R "$root/docs/licenses" "$pkg/licenses"
# (the Shader feature's presets, beside the game: tools/shaders.sh)
cp -R "$("$root/tools/shaders.sh")" "$pkg/shaders"
(cd "$out" && rm -f "$root/build/Soulcalibur2Recomp-windows-x64.zip" && python3 -c "
import shutil; shutil.make_archive('$root/build/Soulcalibur2Recomp-windows-x64', 'zip', '.', 'Soulcalibur2Recomp')")
echo "built $root/build/Soulcalibur2Recomp-windows-x64.zip"
"$root/tools/release/update_manifest.sh" windows-x64 "$pkg" "$root/build/Soulcalibur2Recomp-windows-x64.zip"
