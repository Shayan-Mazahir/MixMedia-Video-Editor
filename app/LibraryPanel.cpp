// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#include "LibraryPanel.h"
#include "EffectNames.h"
#include "MediaBin.h"
#include "TitleRenderer.h"

#include <QDataStream>
#include <QIODevice>
#include <QLinearGradient>
#include <QMimeData>
#include <QPainter>
#include <QRadialGradient>

namespace {

constexpr int IconW = 112, IconH = 63;
const QColor From(0x2a, 0x5d, 0x7a); // the clip before a transition
const QColor To(0xe8, 0x8a, 0x3c);   // the clip after

// A little colourful scene, so effects have something to show off on
QImage sampleScene()
{
    QImage img(IconW, IconH, QImage::Format_RGBA8888);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    QLinearGradient sky(0, 0, 0, IconH);
    sky.setColorAt(0, QColor(0x3d, 0x8b, 0xd6));
    sky.setColorAt(1, QColor(0xf5, 0xb0, 0x6b));
    p.fillRect(img.rect(), sky);
    QRadialGradient sun(QPointF(IconW * 0.72, IconH * 0.38), 13);
    sun.setColorAt(0, QColor(0xff, 0xf3, 0xa0));
    sun.setColorAt(1, QColor(0xff, 0xc2, 0x4a));
    p.setBrush(sun);
    p.setPen(Qt::NoPen);
    p.drawEllipse(QPointF(IconW * 0.72, IconH * 0.38), 11, 11);
    p.setBrush(QColor(0x2f, 0x8a, 0x4f));
    p.drawEllipse(QRectF(-20, IconH * 0.62, IconW * 0.8, IconH));
    p.setBrush(QColor(0x23, 0x6b, 0x3c));
    p.drawEllipse(QRectF(IconW * 0.35, IconH * 0.7, IconW, IconH));
    return img;
}

} // namespace

QPixmap LibraryPanel::effectIcon(const TimelineClip& c)
{
    // The real thing: the engine applies the effect to the sample scene
    QImage img = sampleScene();
    ve_clip settings {};
    settings.look = c.look;
    settings.brightness = c.brightness;
    settings.contrast = c.contrast;
    settings.saturation = c.saturation;
    settings.temperature = c.temperature;
    settings.blur = c.blur;
    settings.sharpen = c.sharpen;
    settings.vignette = c.vignette;
    ve_apply_effects(&settings, img.bits(), img.width(), img.height());
    return QPixmap::fromImage(img);
}

QPixmap LibraryPanel::titleIcon(const TimelineClip& c)
{
    // The title drawn exactly like it will be, over a dimmed scene
    QImage img = sampleScene().convertToFormat(QImage::Format_ARGB32);
    QPainter p(&img);
    p.fillRect(img.rect(), QColor(0, 0, 0, 120));
    p.drawImage(0, 0, TitleRenderer::render(c.title, img.size()));
    return QPixmap::fromImage(img);
}

QPixmap LibraryPanel::transitionIcon(int type)
{
    QPixmap pm(IconW, IconH);
    pm.fill(From);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF all(0, 0, IconW, IconH);

    switch (type) {
    case VE_TRANSITION_DISSOLVE: {
        QLinearGradient g(all.topLeft(), all.topRight());
        g.setColorAt(0, From);
        g.setColorAt(1, To);
        p.fillRect(all, g);
        break;
    }
    case VE_TRANSITION_FADE_BLACK: {
        QLinearGradient g(all.topLeft(), all.topRight());
        g.setColorAt(0, From);
        g.setColorAt(0.5, Qt::black);
        g.setColorAt(1, To);
        p.fillRect(all, g);
        break;
    }
    case VE_TRANSITION_WIPE_LEFT: p.fillRect(QRectF(IconW / 2.0, 0, IconW / 2.0, IconH), To); break;
    case VE_TRANSITION_WIPE_RIGHT: p.fillRect(QRectF(0, 0, IconW / 2.0, IconH), To); break;
    case VE_TRANSITION_WIPE_UP: p.fillRect(QRectF(0, IconH / 2.0, IconW, IconH / 2.0), To); break;
    case VE_TRANSITION_WIPE_DOWN: p.fillRect(QRectF(0, 0, IconW, IconH / 2.0), To); break;
    case VE_TRANSITION_SLIDE_LEFT:
    case VE_TRANSITION_SLIDE_RIGHT: {
        bool left = type == VE_TRANSITION_SLIDE_LEFT;
        p.fillRect(all, QColor(0x17, 0x18, 0x1a));
        p.fillRect(QRectF(left ? -IconW * 0.45 : IconW * 0.45, 0, IconW, IconH).adjusted(2, 2, -2, -2), From);
        p.fillRect(QRectF(left ? IconW * 0.55 : -IconW * 0.55, 0, IconW, IconH).adjusted(2, 2, -2, -2), To);
        break;
    }
    case VE_TRANSITION_ZOOM: {
        QColor to = To;
        to.setAlpha(200);
        p.fillRect(all.adjusted(IconW * 0.22, IconH * 0.22, -IconW * 0.22, -IconH * 0.22), to);
        break;
    }
    default:
        break;
    }

    // An arrow for the ones that move
    static const char* arrows[VE_TRANSITION_COUNT] = { "", "", "", "←", "→", "↑", "↓", "←", "→", "" };
    if (type > 0 && type < VE_TRANSITION_COUNT && arrows[type][0]) {
        QFont f = p.font();
        f.setPixelSize(26);
        f.setBold(true);
        p.setFont(f);
        p.setPen(Qt::white);
        p.drawText(all, Qt::AlignCenter, QString::fromUtf8(arrows[type]));
    }
    return pm;
}

LibraryPanel::LibraryPanel(const QList<TimelineClip>& items, QWidget* parent)
    : QListWidget(parent)
    , m_items(items)
{
    setViewMode(QListView::IconMode);
    setMovement(QListView::Static);
    setIconSize(QSize(IconW, IconH));
    setGridSize(QSize(IconW + 20, IconH + 36));
    setResizeMode(QListView::Adjust);
    setWordWrap(true);
    setFocusPolicy(Qt::ClickFocus);
    // (Static movement turns dragging off, so this has to come after it)
    setDragEnabled(true);
    setDragDropMode(QAbstractItemView::DragOnly);

    for (int i = 0; i < m_items.size(); ++i) {
        const TimelineClip& c = m_items[i];
        QPixmap icon = c.isTransition() ? transitionIcon(c.transition) : c.isTitle() ? titleIcon(c) : effectIcon(c);
        auto* item = new QListWidgetItem(QIcon(icon), c.name);
        item->setData(Qt::UserRole, i);
        item->setToolTip(c.isTransition()
                             ? "Drop it on a cut to blend the two clips, at the start or end of a clip\nto bring it in or out, or anywhere else to play it on the spot.\nDouble-click to add it at the playhead."
                             : "Drag onto an FX track, or double-click to add it at the playhead.");
        addItem(item);
    }
    connect(this, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        emit addAtPlayhead(m_items[item->data(Qt::UserRole).toInt()]);
    });
}

QStringList LibraryPanel::mimeTypes() const
{
    return { MediaBin::MimeType };
}

QMimeData* LibraryPanel::mimeData(const QList<QListWidgetItem*>& items) const
{
    // Same format as the media panel, so the timeline treats them like any other clip
    QByteArray data;
    QDataStream out(&data, QIODevice::WriteOnly);
    out << qint32(items.size());
    for (const QListWidgetItem* item : items)
        out << m_items[item->data(Qt::UserRole).toInt()];
    auto* mime = new QMimeData;
    mime->setData(MediaBin::MimeType, data);
    return mime;
}
