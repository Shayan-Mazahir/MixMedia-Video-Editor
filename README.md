# MixMedia Video Editor
 just a small project im working on since i hate editors not being friendly and not working on Linux

## What it can do

- **Timeline** with 2 video tracks and 2 audio tracks: drag clips in, move, trim edges, split (`S`), snap
- **Ripple delete**: deleting a clip closes the gap (`Shift+Delete` leaves it)
- **Live preview** and playback with sound
- **Detach audio** into its own track, with a waveform
- **Volume and fades** per clip (fade the top clip in over another for a cross-dissolve)
- **Titles** with your own text, size, colour and position
- **Projects**: save and open `.mixmedia` files
- **Export to MP4**
  - *Instant*: copies simple cuts without re-encoding, so it takes seconds
  - *Normal*: re-encodes everything, on your graphics card if it can (Intel, AMD or NVIDIA), otherwise on the CPU

## Building

It's C++20 with Qt 6 for the window and FFmpeg for the video work. The engine (`engine/`) has a plain C interface, so other languages can plug into it later.

### Linux (Fedora)

You'll want FFmpeg from [RPM Fusion](https://rpmfusion.org/) for the full set of codecs:

```bash
sudo dnf install gcc-c++ cmake ninja-build qt6-qtbase-devel qt6-qtmultimedia-devel ffmpeg-devel
cmake -S . -B build -G Ninja
cmake --build build
./build/app/mixmedia
```

For GPU export on Intel graphics, also install `intel-media-driver` (from RPM Fusion nonfree).

### Windows

Install [MSYS2](https://www.msys2.org/), open the **UCRT64** terminal and run:

```bash
pacman -S mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,pkgconf,qt6-base,qt6-multimedia,qt6-tools,ffmpeg}
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --install build --prefix dist/MixMedia
```

`dist/MixMedia/bin` then has `mixmedia.exe` with every DLL it needs next to it, so you can zip that folder up and run it on any Windows PC.

Every push also gets built on GitHub (see `.github/workflows/build.yml`). The Windows zip is under **Artifacts** on each run's page.

## Tests

```bash
QT_QPA_PLATFORM=offscreen ctest --test-dir build
```

There's also a little command-line tool for poking at the engine: `./build/tools/ve-cli` (see the top of `tools/ve_cli.cpp`).
