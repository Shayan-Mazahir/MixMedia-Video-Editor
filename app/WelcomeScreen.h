// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#pragma once

#include <QDialog>
#include <QStringList>

// What you see when MixMedia opens: start something new, open something, or pick up a
// recent project where you left off.
class WelcomeScreen : public QDialog {
    Q_OBJECT

public:
    enum class Choice { Nothing, New, Open, Import, Recent };

    explicit WelcomeScreen(const QStringList& recent, QWidget* parent = nullptr);

    Choice choice() const { return m_choice; }
    QString recentPath() const { return m_path; }

    // Shown at startup unless you've turned it off
    static bool showAtStartup();

private:
    void pick(Choice c, const QString& path = {});

    Choice m_choice = Choice::Nothing;
    QString m_path;
};
