// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#pragma once

#include "TimelineClip.h"

#include <QList>
#include <QStringList>

// Saving and opening .mixmedia project files (plain JSON, so it's easy to peek inside).
namespace ProjectFile {

constexpr const char* Extension = "mixmedia";

struct Data {
    QStringList media;          // everything in the media panel
    QList<TimelineClip> clips;  // thumbnails aren't saved, they get remade on open
    QList<TimelineTrack> tracks; // empty = an older project (from before FX tracks)
    double playhead = 0.0;
};

bool save(const QString& path, const Data& data, QString* error);

// missing (optional) gets any media files that couldn't be found
bool load(const QString& path, Data* data, QStringList* missing, QString* error);

} // namespace ProjectFile
