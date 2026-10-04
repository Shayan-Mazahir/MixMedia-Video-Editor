// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#pragma once

#include "timeline.h"

#include <string>
#include <vector>

namespace ve {

enum class ExportFormat { Mp4, Gif, Mp3, M4a };

struct ExportSettings {
    ExportFormat format = ExportFormat::Mp4;
    std::string path;
    int width = 1920;
    int height = 1080;
    double fps = 30.0;
    int crf = 20;          // quality: lower = better looking + bigger file
    bool hardware = true;  // try the graphics card first

    struct Subtitle {
        double start, end;
        std::string text;
    };
    std::vector<Subtitle> subtitles; // added as a track that can be switched on and off
    std::string subtitleLanguage = "und";
};

// Return non-zero from the callback to cancel.
using ProgressFn = int (*)(double done, void* user);

// Returns one of the VE_ codes from engine.h. encoderUsed (optional) gets the encoder's name.
int exportTimeline(Timeline& timeline, const ExportSettings& settings, ProgressFn progress, void* user,
                   std::string* encoderUsed = nullptr);

// An animated GIF (no sound, 256 colours)
int exportGif(Timeline& timeline, const ExportSettings& settings, ProgressFn progress, void* user);

// Just the sound, as MP3 or M4A
int exportSound(Timeline& timeline, const ExportSettings& settings, ProgressFn progress, void* user);

// Can this FFmpeg make it? (MP3 needs LAME, which some builds leave out)
bool formatAvailable(ExportFormat format);

} // namespace ve
