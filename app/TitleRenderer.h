// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#pragma once

#include "TimelineClip.h"

#include <QImage>
#include <QSize>
#include <QString>

namespace TitleRenderer {

// Draws a title onto a see-through picture of the given size.
QImage render(const TitleStyle& style, QSize size);

// Same picture, saved as a PNG in the cache folder (reused if it's already there).
// That file is what the engine layers over the video.
QString imageFile(const TitleStyle& style, QSize size);

} // namespace TitleRenderer
