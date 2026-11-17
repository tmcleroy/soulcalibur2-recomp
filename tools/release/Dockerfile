# SPDX-License-Identifier: GPL-3.0-or-later
# The build environment for the Linux and Windows release builds (Ubuntu 24.04). tools/release/release.sh builds it
# twice: for linux/amd64 (the Linux x86_64 build; emulated on Apple silicon) and for the machine's own system (the
# Windows build, cross-compiled with the llvm-mingw in private/toolchains for that system):
#   docker build --platform linux/amd64 -t soulcalibur2-build:amd64 tools/release
#   docker build -t soulcalibur2-build:native tools/release
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates g++ cmake ninja-build python3 curl file zip git zlib1g-dev \
    libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev libxtst-dev libxkbcommon-dev \
    libwayland-dev wayland-protocols libdecor-0-dev libegl-dev libgl-dev \
    libpulse-dev libasound2-dev libudev-dev libdbus-1-dev libpipewire-0.3-dev \
    && rm -rf /var/lib/apt/lists/*
