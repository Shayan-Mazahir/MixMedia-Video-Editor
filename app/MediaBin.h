// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#pragma once

#include "TimelineClip.h"

#include <QListWidget>

// The media panel. Mostly a normal list, but it knows how to pack
// clips up so the timeline can catch them when you drag them over.
class MediaBin : public QListWidget {
    Q_OBJECT

public:
    explicit MediaBin(QWidget* parent = nullptr);

    static constexpr const char* MimeType = "application/x-ve-media";

    enum Role {
        PathRole = Qt::UserRole,
        DurationRole,
        HasVideoRole,
        HasAudioRole,
        WidthRole,
        HeightRole,
        FpsRole,
    };

    // A fresh timeline clip made from one of our items (covering the whole file)
    TimelineClip clipFor(const QListWidgetItem* item) const;

signals:
    void importRequested(); // the empty drop zone got clicked

protected:
    QStringList mimeTypes() const override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    QMimeData* mimeData(const QList<QListWidgetItem*>& items) const override;
};
