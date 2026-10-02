// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#pragma once

#include "timeline.h"

#include <string>

namespace ve {

struct ExportSettings {
    std::string path;
    int width = 1920;
    int height = 1080;
    double fps = 30.0;
    int crf = 20;          // quality: lower = better looking + bigger file
    bool hardware = true;  // try the graphics card first
};

// Return non-zero from the callback to cancel.
using ProgressFn = int (*)(double done, void* user);

// Returns one of the VE_ codes from engine.h. encoderUsed (optional) gets the encoder's name.
int exportTimeline(Timeline& timeline, const ExportSettings& settings, ProgressFn progress, void* user,
                   std::string* encoderUsed = nullptr);

} // namespace ve
