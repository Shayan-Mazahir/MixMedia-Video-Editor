// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#pragma once

#include <QWidget>

#include <functional>

class QTextBrowser;
class QToolButton;

// The help: the pages in docs/ (packed inside the app, so it works offline and always
// matches this version), with back/forward and links between pages.
class HelpWindow : public QWidget {
    Q_OBJECT

public:
    // Opens the help at a page, e.g. "subtitles.md" (one window, reused)
    static void open(const QString& page = "README.md");

    // A little round "?" button that opens a page (or whichever page `page()` says when clicked)
    static QToolButton* button(const QString& page, QWidget* parent = nullptr);
    static QToolButton* button(std::function<QString()> page, QWidget* parent = nullptr);

    QString currentPage() const;

private:
    explicit HelpWindow(QWidget* parent = nullptr);
    void show(const QString& page);

    QTextBrowser* m_browser;
};
