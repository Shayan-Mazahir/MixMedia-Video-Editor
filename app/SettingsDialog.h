// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#pragma once

#include <QDialog>

class NumberSlider;
class QCheckBox;
class QLabel;

// Edit → Settings. For now: how much of the computer MixMedia may use.
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
};
