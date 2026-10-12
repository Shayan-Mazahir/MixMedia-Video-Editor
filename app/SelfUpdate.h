// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#pragma once

#include "UpdateChecker.h"

#include <QString>

class QWidget;

// "Update now": downloads the new version, checks it's exactly what GitHub says it should be,
// puts it in place of this one, and restarts into it. Linux only for now (the AppImage and the
// folder download). On Windows a running program can't replace itself without help, so it's
// still "download it yourself" there.
namespace SelfUpdate {

enum class Kind {
    None,     // can't update itself (Windows, a system install, a build of your own...)
    AppImage, // one .AppImage file
    Folder,   // the tar.gz folder
};
Kind kind(); // how this copy of MixMedia was installed
QString assetName(Kind kind); // which download it needs, e.g. "MixMedia-Linux-x86_64.AppImage"

// Gets the update and puts it in place (with a progress bar). Returns the program to start
// next time, or empty if it was cancelled or didn't work (after saying why).
QString install(QWidget* parent, const UpdateChecker::Asset& asset);

// Closes MixMedia (asking to save first, as usual) and opens the new one.
// False = you kept it open (so the new version starts next time instead).
bool restartInto(const QString& program);

// The pieces, separately (for the tests). `file` is the download, already checked.
bool replaceAppImage(const QString& file, const QString& appImage);
bool replaceFolder(const QString& tarball, const QString& folder, QString* error);

} // namespace SelfUpdate
