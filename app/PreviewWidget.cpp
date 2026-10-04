// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#include "PreviewWidget.h"

#include <QMouseEvent>
#include <QPainter>

PreviewWidget::PreviewWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(320, 180);
    setAttribute(Qt::WA_OpaquePaintEvent);
}

void PreviewWidget::setFrame(const QImage& frame)
{
    m_frame = frame;
    update();
}

void PreviewWidget::setAspect(double aspect)
{
    if (aspect > 0 && aspect != m_aspect) {
        m_aspect = aspect;
        update();
        emit resized();
    }
}

QRect PreviewWidget::frameRect() const
{
    int w = width();
    int h = int(w / m_aspect);
    if (h > height()) {
        h = height();
        w = int(h * m_aspect);
    }
    return QRect((width() - w) / 2, (height() - h) / 2, w, h);
}

QSize PreviewWidget::renderSize() const
{
    return frameRect().size() * devicePixelRatioF();
}

void PreviewWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(0x0b, 0x0b, 0x0c));

    QRect r = frameRect();
    if (m_frame.isNull()) {
        p.fillRect(r, Qt::black);
        p.setPen(QColor(0x55, 0x55, 0x55));
        p.drawText(r, Qt::AlignCenter, "Drop something on the timeline to see it here");
        return;
    }
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(r, m_frame);
    if (m_picking) {
        // A hint along the top while it's waiting for the click
        QRect banner(r.left(), r.top(), r.width(), 26);
        p.fillRect(banner, QColor(0, 0, 0, 170));
        p.setPen(QColor(0x2f, 0xc6, 0xb4));
        p.drawText(banner, Qt::AlignCenter, m_pickingColor ? "Click the colour to remove" : "Click the spot to zoom into");
    }
}

void PreviewWidget::pickColor()
{
    pickSpot();
    m_pickingColor = true;
}

void PreviewWidget::pickSpot()
{
    m_picking = true;
    m_pickingColor = false;
    setCursor(Qt::CrossCursor);
    update();
}

void PreviewWidget::mousePressEvent(QMouseEvent* event)
{
    if (!m_picking || event->button() != Qt::LeftButton)
        return QWidget::mousePressEvent(event);
    QRectF r = frameRect();
    QPointF at = event->position();
    if (!r.contains(at))
        return; // missed the picture, keep waiting
    m_picking = false;
    unsetCursor();
    update();
    if (m_pickingColor) {
        // The colour of the frame right under the click (averaged over a few pixels, to ignore noise)
        QPointF f((at.x() - r.left()) / r.width() * m_frame.width(), (at.y() - r.top()) / r.height() * m_frame.height());
        int red = 0, green = 0, blue = 0, n = 0;
        for (int dy = -2; dy <= 2; ++dy)
            for (int dx = -2; dx <= 2; ++dx) {
                QPoint p(int(f.x()) + dx, int(f.y()) + dy);
                if (!m_frame.rect().contains(p))
                    continue;
                QColor c = m_frame.pixelColor(p);
                red += c.red();
                green += c.green();
                blue += c.blue();
                ++n;
            }
        if (n > 0)
            emit colorPicked(QColor(red / n, green / n, blue / n));
        return;
    }
    emit picked(QPointF((at.x() - r.center().x()) / r.width(), (at.y() - r.center().y()) / r.height()));
}

void PreviewWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    emit resized();
}
