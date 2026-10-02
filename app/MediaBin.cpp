// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#include "MediaBin.h"

#include <QDataStream>
#include <QFileInfo>
#include <QMimeData>

namespace {
constexpr double StillImageSeconds = 5.0; // pictures have no length, so give them one
}

MediaBin::MediaBin(QWidget* parent)
    : QListWidget(parent)
{
    setViewMode(QListView::IconMode);
    setMovement(QListView::Static);

    // Careful: Static movement secretly turns dragging off, so this has to come after it.
    setDragEnabled(true);
    setDragDropMode(QAbstractItemView::DragOnly);
}

TimelineClip MediaBin::clipFor(const QListWidgetItem* item) const
{
    TimelineClip c;
    c.path = item->data(PathRole).toString();
    c.name = QFileInfo(c.path).fileName();
    c.sourceDuration = item->data(DurationRole).toDouble();
    c.duration = c.sourceDuration > 0 ? c.sourceDuration : StillImageSeconds;
    c.hasVideo = item->data(HasVideoRole).toBool();
    c.hasAudio = item->data(HasAudioRole).toBool();
    c.width = item->data(WidthRole).toInt();
    c.height = item->data(HeightRole).toInt();
    c.fps = item->data(FpsRole).toDouble();
    c.thumb = item->icon().pixmap(iconSize());
    return c;
}

QStringList MediaBin::mimeTypes() const
{
    return { MimeType };
}

QMimeData* MediaBin::mimeData(const QList<QListWidgetItem*>& items) const
{
    QByteArray data;
    QDataStream out(&data, QIODevice::WriteOnly);
    out << qint32(items.size());
    for (const QListWidgetItem* item : items)
        out << clipFor(item);

    auto* mime = new QMimeData;
    mime->setData(MimeType, data);
    return mime;
}
