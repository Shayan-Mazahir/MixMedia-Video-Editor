// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#pragma once

#include <QDialog>
#include <QHash>
#include <QKeySequence>
#include <QList>

class NumberSlider;
class QCheckBox;
class QLabel;
class QRadioButton;
class QTableWidget;
class QKeySequenceEdit;

// Edit -> Settings: how much of the computer MixMedia may use, and update checks.
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

    // Shortcuts: changes wait here until OK (Cancel = forget them)
    QWidget* buildShortcuts();
    void showKeys(); // fills the table from what they'd be
    QList<QKeySequence> keysFor(int row) const;
    QTableWidget* m_keys = nullptr;
    QKeySequenceEdit* m_keyEdit = nullptr;
    QLabel* m_keyNote = nullptr;
    QHash<QString, QList<QKeySequence>> m_pendingKeys; // by action id
    bool m_resetAllKeys = false;
};
