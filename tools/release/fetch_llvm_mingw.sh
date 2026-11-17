#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# llvm-mingw for the Windows build (tools/release/windows.sh), the compiler the shared runtime is built with on
# Windows, for the system of the Docker container it runs in (aarch64 on Apple silicon, x86_64 elsewhere):
#   tools/release/fetch_llvm_mingw.sh aarch64|x86_64
# Unpacked into private/toolchains/llvm-mingw-<version>-ucrt-ubuntu-22.04-<arch>. Needs curl and xz.
set -e
root=$(cd "$(dirname "$0")/../.." && pwd)
ver=20261006
arch=${1:?usage: fetch_llvm_mingw.sh aarch64|x86_64}
case "$arch" in
    aarch64|arm64) arch=aarch64 sha=e77c4acfa44184f6b0c52b814d724ad63b2ecebb3c3f6f842c0846e717cbd23c ;;
    x86_64|amd64) arch=x86_64 sha=5f9c6ed95b2d4bdb2869a488c5fd5857fbdabcf288a0aa3eb1da43f6a08d8ab4 ;;
    *) echo "error: no llvm-mingw for $arch" >&2; exit 1 ;;
esac
name=llvm-mingw-$ver-ucrt-ubuntu-22.04-$arch
out="$root/private/toolchains"
[ -x "$out/$name/bin/clang" ] && exit 0
tmp=$(mktemp -d)
curl -fsSL -o "$tmp/$name.tar.xz" "https://github.com/mstorsjo/llvm-mingw/releases/download/$ver/$name.tar.xz"
echo "$sha  $tmp/$name.tar.xz" | shasum -a 256 -c - >/dev/null
mkdir -p "$out"
tar -xJf "$tmp/$name.tar.xz" -C "$out"
rm -rf "$tmp"
echo "llvm-mingw in $out/$name"
