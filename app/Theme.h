// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#pragma once

#include <QColor>
#include <QFont>

class QApplication;
class QWidget;

// MixMedia's look: dark or light, with a teal accent, used everywhere
namespace Theme {

enum class Mode { Dark, Light, System };

struct Colours {
    bool dark;
    QColor window, panel, control, hover, border, text, dim, accent, accent2;
    // For the timeline, which draws itself
    QColor track, fxTrack, header, ruler, lines, headerText, rulerText, ticks;
    QColor icon; // line icons on buttons
};

// The colours in use right now
const Colours& colours();

// The app's normal font, scaled (1.0 = normal size). Everything sizes off this, so text follows
// whatever size your system uses instead of fixed pixels.
QFont font(double scale = 1.0, bool bold = false);

// Makes a window big enough for everything in it, at whatever font size the system uses
// (call just before showing it). Never smaller than it would be anyway.
void fit(QWidget& window);

// Sets (or switches) the look. Everything redraws to match.
void apply(QApplication& app, Mode mode);
Mode savedMode();
void saveMode(Mode mode);

} // namespace Theme
