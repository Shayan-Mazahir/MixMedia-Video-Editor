#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# What the Linux package build needs on Ubuntu 22.04 (used on GitHub and in the local container).
set -euo pipefail

SUDO=""
[ "$(id -u)" -ne 0 ] && SUDO=sudo

$SUDO apt-get update
DEBIAN_FRONTEND=noninteractive $SUDO apt-get install -y --no-install-recommends \
    ca-certificates curl xz-utils git ninja-build meson nasm pkg-config make gcc-12 g++-12 \
    libx264-dev libmp3lame-dev libva-dev libdrm-dev zlib1g-dev \
    libgl-dev libegl-dev libxkbcommon-dev libxkbcommon-x11-0 libpulse-dev libfuse2 file \
    libfontconfig1 libdbus-1-3 libglib2.0-0 libxrandr2 \
    libxcb-cursor0 libxcb-icccm4 libxcb-image0 libxcb-keysyms1 libxcb-randr0 \
    libxcb-render-util0 libxcb-shape0 libxcb-xinerama0 libxcb-xkb1

# Ubuntu 22.04's CMake is too old for us, so use the official one if there isn't a new enough one
# already (GitHub's machines come with one)
need=3.25
have=$(cmake --version 2>/dev/null | head -1 | grep -oE '[0-9]+\.[0-9]+' || echo 0)
if [ "$(printf '%s\n' "$need" "$have" | sort -V | head -1)" != "$need" ]; then
    version=3.31.6
    curl -fsSL "https://github.com/Kitware/CMake/releases/download/v$version/cmake-$version-linux-x86_64.tar.gz" \
        | $SUDO tar xz -C /opt
    $SUDO ln -sf "/opt/cmake-$version-linux-x86_64/bin/"* /usr/local/bin/
fi
cmake --version | head -1
