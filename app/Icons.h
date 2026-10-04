// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

// Little line icons for the toolbar and buttons, drawn in code (so there are no image files,
// and they're sharp at any size). Names: import, export, undo, redo, split, delete, detach,
// title, zoom-in, zoom-out, fit, play, pause, start, captions, new, open.
namespace Icons {

// No colour = the right one for the current theme
QIcon get(const QString& name, const QColor& colour = QColor());

} // namespace Icons
