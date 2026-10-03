#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds the Linux AppImage + tar.gz on your own machine, the exact same way GitHub does:
# inside a throwaway Ubuntu 22.04 container (needs podman, which Fedora comes with).
#
#   packaging/linux/build-in-container.sh
#
# The first run downloads Qt and builds FFmpeg, so it takes a while. Those get kept in
# .package-cache/ so later runs only rebuild MixMedia itself. The results land in the repo folder.
set -euo pipefail

repo="$(cd "$(dirname "$0")/../.." && pwd)"
qt_version="${QT_VERSION:-6.11}"
mkdir -p "$repo/.package-cache"

# (-t only when there's a terminal, so it works from scripts too)
podman run --rm -i $( [ -t 1 ] && echo -t ) \
    --security-opt label=disable \
    -v "$repo:/src" \
    -w /src \
    -e QT_VERSION="$qt_version" \
    docker.io/library/ubuntu:22.04 \
    bash -c '
        set -euo pipefail
        packaging/linux/install-ubuntu-deps.sh | tail -1
        cache=/src/.package-cache

        # The official Qt build (Ubuntu 22.04 only has an old Qt). aqtinstall is the tool
        # GitHub uses for this too; it lives in this container only, nothing touches your system.
        if [ ! -d "$cache/qt" ]; then
            apt-get install -y --no-install-recommends python3-pip >/dev/null
            pip3 install --quiet aqtinstall
            version=$(aqt list-qt linux desktop --spec "$QT_VERSION" --latest-version)
            echo "Getting Qt $version..."
            aqt install-qt linux desktop "$version" linux_gcc_64 -m qtmultimedia -O "$cache/qt-download"
            mv "$cache/qt-download/$version/gcc_64" "$cache/qt"
            rm -r "$cache/qt-download"
        fi

        QT_ROOT_DIR="$cache/qt" FFMPEG_PREFIX="$cache/ffmpeg" TOOLS_DIR=.package-cache/linuxdeploy \
            BUILD_DIR=.package-cache/build packaging/linux/make-package.sh
    '
