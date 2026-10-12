// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#pragma once

#include <QSettings>
#include <QString>

class QWidget;

// Where MixMedia keeps its things: a "MixMedia data" folder right next to the program (next to
// the .AppImage file on Linux), so it's all in one place, easy to find, and deleting the
// MixMedia folder removes everything. If that can't be written to (say it's installed for
// everyone in Program Files, or in /usr), it uses the usual per-user place instead.
//
//   MixMedia data/
//     settings.ini   your settings
//     models/        speech models for auto-captions (drop your own in here too)
//     fonts/         extra fonts for titles and subtitles (.ttf, .otf)
//     autosave/      backups, in case it crashes
//     cache/         pictures it can make again (safe to delete)
namespace AppPaths {

// Call once, straight after the QApplication: makes the folders, brings anything over from
// older versions, and loads the extra fonts
void setUp();

// The folder you'd think of as "where MixMedia is" (for the AppImage: where the .AppImage file is)
QString appFolder();
QString data();
bool portable(); // true = next to the program, false = the usual per-user place
QString models();
QString fonts();
QString autosave();
QString cache();

// The settings (use like a QSettings: AppPaths::settings().value(...))
QSettings settings();

// Opens a folder in the file manager. If there isn't one to open it with, shows where it is instead.
void openFolder(QWidget* parent, const QString& folder);

} // namespace AppPaths
