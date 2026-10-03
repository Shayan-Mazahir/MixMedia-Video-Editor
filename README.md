<p align="center"><img src="Mix%20Media.png" alt="MixMedia logo" width="150"></p>

# MixMedia Video Editor
just a small project im working on since i hate editors not being friendly and not working on Linux

# ⚠️ Disclaimer
This project, for majority is vibe coded, it serves 2 purpose for me:

1. Just me seeing how good Claude can be

2. Having a personal video editor that suits my requirnments and is easy to use

## Download

Grab the latest from the [Releases page](https://github.com/Shayan-Mazahir/MixMedia-Video-Editor/releases/latest):

| | Easiest | Portable (no install) |
|---|---|---|
| **Windows** | `MixMedia-Setup-x64.exe`: installs with Start menu and desktop shortcuts | `MixMedia-Windows-x64.zip`: unzip and run `mixmedia.exe` |
| **Linux** | `MixMedia-Linux-x86_64.AppImage`: make it executable and run it | `MixMedia-Linux-x86_64.tar.gz`: unpack and run `mixmedia` |

Everything it needs is included, so there's nothing else to install.

## What it can do

- **Timeline** in the Filmora style: FX tracks on top for titles, effects and transitions, then video and audio tracks. Drag clips in, move, trim edges, split (`S`), snap. Right-click a track name to add more.
- **Ripple delete**: deleting a clip closes the gap (`Shift+Delete` leaves it)
- **Live preview** and playback with sound
- **Detach audio** into its own track, with a waveform
- **Volume and fades** per clip (fade the top clip in over another for a cross-dissolve)
- **Titles** with your own text, size, colour and position
- **Effects**: one-click looks (black & white, sepia, vintage, vivid, cool, warm, faded, dramatic) plus brightness, contrast, colour, warmth, blur, sharpen and vignette
- **Transitions** like Filmora: drop one on a cut and the clip after it slides back to overlap the one before, so both keep playing while they blend (the video gets shorter by the transition's length). Dissolve, fade through black, wipes, slides and zoom, and the sound crossfades too. Change the length in Properties, or delete it to slide everything back. No cut needed either: put one at the start of a clip to bring it in from black, at the end to take it out, or anywhere in the middle to play it on the spot (like an effect, it works on everything below it).
- **Effect blocks**: drop a look or effect on an FX track and it applies to everything below it, for as long as the block lasts. Stack several on different FX tracks.
- **Animations**: clips and titles can fade, slide, zoom or wipe in and out
- **Picture-in-picture**: resize, move and fade any clip over another
- **Speed**: slow motion and fast forward, from 0.1× to 10×
- **Precise controls**: every slider has a box for typing exact numbers
- **Drag files in** straight from your file manager, plus right-click menus and keyboard shortcuts (arrows to step frames, ↑ ↓ to jump between cuts)
- **Projects**: save and open `.mixmedia` files
- **Auto-save**: unsaved work gets a backup copy every minute. If MixMedia crashes, it offers to bring it back next time (your project file is only written when you save).
- **Export**
  - *Presets*: YouTube 1080p and 4K, Shorts / TikTok / Reels (tall), Instagram square, small file to send, GIF, and sound only
  - *Formats*: MP4, animated GIF, MP3 or M4A
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

## Licence

MixMedia Video Editor is made by **Shayan Mazahir** and is free and open source under the [GNU GPL v3](LICENSE) (or any later version).

You're welcome to use it, change it and share it. If you share it or a modified version:

- **keep it open source** under the same licence
- **credit the original**: keep the [NOTICE](NOTICE) file and the copyright lines at the top of the source files, say it was originally made by Shayan Mazahir with a link back to this repo, and leave that credit on the app's About screen

The release downloads include Qt (LGPL v3) and FFmpeg with x264 (GPL). See [NOTICE](NOTICE) for details.
