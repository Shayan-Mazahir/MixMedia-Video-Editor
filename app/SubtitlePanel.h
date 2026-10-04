// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#pragma once

#include <QWidget>

class QPushButton;
class QTableWidget;
class TimelineWidget;

// The Subtitles tab: every line in a table you can type straight into, plus the buttons for
// adding, importing and exporting them. Changes go straight onto the timeline.
class SubtitlePanel : public QWidget {
    Q_OBJECT

public:
    explicit SubtitlePanel(TimelineWidget* timeline, QWidget* parent = nullptr);

    void refresh();               // the timeline changed
    void setPlayhead(double sec); // lights up the line being shown

signals:
    void seekRequested(double sec);
    void importClicked();
    void exportClicked();
    void autoCaptionsClicked();

private:
    void edited(int row, int column);
    void addLine();
    void deleteSelected();
    void shiftAll();

    TimelineWidget* m_timeline;
    QTableWidget* m_table;
    QList<int> m_lines; // timeline clip index for each row
    QPushButton* m_export;
    QPushButton* m_delete;
    QPushButton* m_shift;
    QPushButton* m_look;
    bool m_filling = false;
    int m_current = -1;
};
