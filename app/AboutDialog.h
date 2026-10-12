// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#pragma once

#include <QDialog>

// Help → About MixMedia: who made it, where to get help, the licence, and what it's built with.
// (Keep the credit if you share a modified version, see NOTICE.)
class AboutDialog : public QDialog {
    Q_OBJECT

public:
    explicit AboutDialog(QWidget* parent = nullptr);

private:
    // "help:page.md" opens a help page in MixMedia, "folder:" the data folder, anything else the browser
    void follow(const QString& link);
};
