// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#pragma once

#include <QDialog>

class NumberSlider;
class QCheckBox;
class QLabel;
class QRadioButton;

// Edit → Settings: how much of the computer MixMedia may use, and update checks.
class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget* parent = nullptr);

private:
    void refresh();
    void save();

    QCheckBox* m_advanced;
    QWidget* m_details;
    NumberSlider* m_threads;
    QLabel* m_threadsNote;
    QCheckBox* m_gpuExport;
    QCheckBox* m_gpuCaptions;
    QRadioButton* m_updatesReleases;
    QRadioButton* m_updatesPre;
    QRadioButton* m_updatesOff;
    QRadioButton* m_themeDark;
    QRadioButton* m_themeLight;
    QRadioButton* m_themeSystem;
};
