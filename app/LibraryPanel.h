// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#pragma once

#include "TimelineClip.h"

#include <QListWidget>

// One of the library tabs (Titles, Effects, Transitions): ready-made blocks to drag onto
// the timeline, or double-click to drop one at the playhead.
class LibraryPanel : public QListWidget {
    Q_OBJECT

public:
    explicit LibraryPanel(const QList<TimelineClip>& items, QWidget* parent = nullptr);

    // Little pictures of what each one does
    static QPixmap titleIcon(const TimelineClip& clip);
    static QPixmap effectIcon(const TimelineClip& clip);
    static QPixmap transitionIcon(int type);

signals:
    void addAtPlayhead(const TimelineClip& clip);

protected:
    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QList<QListWidgetItem*>& items) const override;

private:
    QList<TimelineClip> m_items;
};
