// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#pragma once

#include "exporter.h"

#include <string>
#include <vector>

namespace ve {

// One piece of the source file to copy, in timeline order.
struct CopySegment {
    double in;       // where it starts in the file
    double duration;
};

// Copies the pieces straight into a new file without decoding or encoding anything.
// Super fast, but each cut starts at the keyframe just before `in`.
int copyStreams(const std::string& source, const std::vector<CopySegment>& segments,
                const std::string& outPath, ProgressFn progress, void* user);

} // namespace ve
