// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#include "MediaBin.h"
#include "CardDelegate.h"
#include "Icons.h"
#include "Theme.h"

#include <QDataStream>
#include <QFileInfo>
#include <QFontMetricsF>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>

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

    setItemDelegate(new CardDelegate(this)); // files as little cards
    setMouseTracking(true);                  // (so they light up when hovered)
}

void MediaBin::mousePressEvent(QMouseEvent* event)
{
    // Nothing here yet: the whole drop zone is a big Import button
    if (count() == 0 && event->button() == Qt::LeftButton) {
        emit importRequested();
        return;
    }
    QListWidget::mousePressEvent(event);
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

void MediaBin::paintEvent(QPaintEvent* event)
{
    QListWidget::paintEvent(event);
    if (count() > 0)
        return;
    // Nothing imported yet? A big friendly drop zone.
    QPainter p(viewport());
    p.setRenderHint(QPainter::Antialiasing);
    QRectF zone = QRectF(viewport()->rect()).adjusted(14, 14, -14, -14);
    p.setPen(QPen(QColor(0x3d, 0x43, 0x4d), 1.5, Qt::DashLine));
    p.setBrush(QColor(255, 255, 255, 6));
    p.drawRoundedRect(zone, 12, 12);
    // Icon, heading and hint stacked in the middle, wrapping when the panel's narrow
    const QString heading = "Drop your files here", hint = "Videos, music and pictures.\nOr click here to pick them (Ctrl+I).";
    const int flags = Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap;
    const QRectF text = zone.adjusted(12, 0, -12, 0);
    const QFont big = Theme::font(1.25, true), small = Theme::font(0.95);
    const qreal headingH = QFontMetricsF(big).boundingRect(text, flags, heading).height();
    const qreal hintH = QFontMetricsF(small).boundingRect(text, flags, hint).height();
    qreal y = zone.center().y() - (44 + 10 + headingH + 6 + hintH) / 2;
    Icons::get("import", Theme::colours().accent).paint(&p, QRect(int(zone.center().x()) - 22, int(y), 44, 44));
    y += 54;
    p.setFont(big);
    p.setPen(Theme::colours().text);
    p.drawText(QRectF(text.left(), y, text.width(), headingH), flags, heading);
    y += headingH + 6;
    p.setFont(small);
    p.setPen(Theme::colours().dim);
    p.drawText(QRectF(text.left(), y, text.width(), hintH), flags, hint);
}
