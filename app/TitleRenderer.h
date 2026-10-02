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
