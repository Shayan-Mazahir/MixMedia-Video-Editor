#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds the Linux downloads: MixMedia-Linux-x86_64.AppImage and MixMedia-Linux-x86_64.tar.gz.
#
# Runs on Ubuntu 22.04 (old enough that the result works on pretty much any newer Linux).
# GitHub runs it, and so does build-in-container.sh on your own machine.
#
# Needs: QT_ROOT_DIR = an official Qt 6 install (with Qt Multimedia).
# Optional: FFMPEG_PREFIX = where FFmpeg gets built (kept between runs, it's slow to build)
set -euo pipefail

: "${QT_ROOT_DIR:?point QT_ROOT_DIR at a Qt 6 install}"
source "$(dirname "$0")/ffmpeg-versions.sh"
FFMPEG_PREFIX="${FFMPEG_PREFIX:-$PWD/ffmpeg}"
BUILD_DIR="${BUILD_DIR:-build-package}"
export CC="${CC:-gcc-12}" CXX="${CXX:-g++-12}"
export PKG_CONFIG_PATH="$FFMPEG_PREFIX/lib/pkgconfig"
# Our FFmpeg and Qt, so the build, the tests and linuxdeploy can all find them
export LD_LIBRARY_PATH="$FFMPEG_PREFIX/lib:$QT_ROOT_DIR/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export PATH="$QT_ROOT_DIR/bin:$PATH"
# AppImages normally need FUSE, which containers don't have. This makes them unpack and run instead.
export APPIMAGE_EXTRACT_AND_RUN=1

step() { echo; echo "==== $* ===="; }

# ---- FFmpeg with just what a video editor needs (no network stuff dragging in Samba and friends) ----
if [ ! -e "$FFMPEG_PREFIX/lib/libavcodec.so" ]; then
    step "Building FFmpeg $FFMPEG_VERSION (only the first time, takes a few minutes)"
    work=$(mktemp -d)
    # Ubuntu 22.04's dav1d (AV1 decoding) is too old for FFmpeg 8, so build a newer one,
    # baked straight into FFmpeg (static) so there's nothing extra to ship
    curl -fsSL "https://code.videolan.org/videolan/dav1d/-/archive/$DAV1D_VERSION/dav1d-$DAV1D_VERSION.tar.gz" | tar xz -C "$work"
    meson setup "$work/dav1d-build" "$work/dav1d-$DAV1D_VERSION" --prefix="$FFMPEG_PREFIX" --libdir=lib \
        --buildtype=release --default-library=static -Denable_tools=false -Denable_tests=false
    ninja -C "$work/dav1d-build" install

    curl -fsSL "https://ffmpeg.org/releases/ffmpeg-$FFMPEG_VERSION.tar.xz" | tar xJ -C "$work"
    (
        cd "$work/ffmpeg-$FFMPEG_VERSION"
        ./configure --prefix="$FFMPEG_PREFIX" --cc="$CC" --cxx="$CXX" --enable-shared --disable-static \
            --enable-gpl --enable-libx264 --enable-libmp3lame --enable-libdav1d --enable-vaapi --enable-libdrm --enable-zlib \
            --disable-programs --disable-doc --disable-network --disable-autodetect \
            --pkg-config-flags=--static
        make -j"$(nproc)"
        make install
    )
    rm -r "$work"
fi

# ---- The app ----
step "Building MixMedia"
cmake -S . -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
    -DCMAKE_PREFIX_PATH="$QT_ROOT_DIR"
cmake --build "$BUILD_DIR"

step "Running the tests"
QT_QPA_PLATFORM=offscreen ctest --test-dir "$BUILD_DIR" --output-on-failure

# ---- AppImage = one file you run. The tar.gz is the same thing as a plain folder. ----
step "Packaging"
rm -rf AppDir MixMedia MixMedia-Linux-x86_64.AppImage MixMedia-Linux-x86_64.tar.gz squashfs-root
DESTDIR=AppDir cmake --install "$BUILD_DIR"

tools="${TOOLS_DIR:-.linuxdeploy}"
mkdir -p "$tools"
base=https://github.com/linuxdeploy
[ -x "$tools/linuxdeploy" ] || curl -fsSL -o "$tools/linuxdeploy" \
    "$base/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage"
[ -x "$tools/linuxdeploy-plugin-qt" ] || curl -fsSL -o "$tools/linuxdeploy-plugin-qt" \
    "$base/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage"
chmod +x "$tools/linuxdeploy" "$tools/linuxdeploy-plugin-qt"

export QMAKE="$QT_ROOT_DIR/bin/qmake"
export LDAI_OUTPUT=MixMedia-Linux-x86_64.AppImage
export EXTRA_PLATFORM_PLUGINS=libqoffscreen.so # lets the app start without a screen (used by the smoke test)
PATH="$PWD/$tools:$PATH" linuxdeploy --appdir AppDir \
    --executable AppDir/usr/bin/mixmedia \
    --desktop-file packaging/mixmedia.desktop \
    --icon-file packaging/mixmedia.png \
    --plugin qt --output appimage

# Same files as a folder, with a "mixmedia" to double-click at the top
mv AppDir MixMedia
ln -s usr/bin/mixmedia MixMedia/mixmedia
cp packaging/linux/README.txt MixMedia/
tar czf MixMedia-Linux-x86_64.tar.gz MixMedia
rm -r MixMedia

# ---- Make sure what we just made actually starts (no screen needed for this) ----
step "Smoke test"
./MixMedia-Linux-x86_64.AppImage --appimage-extract >/dev/null
# Only what's inside the AppImage this time, so a missing library can't hide behind ours
LD_LIBRARY_PATH="" QT_QPA_PLATFORM=offscreen timeout 10 ./squashfs-root/AppRun & pid=$!
sleep 6
if kill -0 "$pid" 2>/dev/null; then
    echo "AppImage starts fine"
    kill "$pid" || true
    rm -r squashfs-root
else
    wait "$pid" || true
    echo "The AppImage quit straight away, something's missing from it" >&2
    exit 1
fi

step "Done: MixMedia-Linux-x86_64.AppImage and MixMedia-Linux-x86_64.tar.gz"
