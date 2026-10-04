// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#include "Icons.h"
#include "Theme.h"

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

namespace Icons {

namespace {

// Everything's drawn on a 24 x 24 grid, with round 2-unit lines
void draw(QPainter& p, const QString& name, const QColor& c)
{
    QPen pen(c, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    auto line = [&](qreal x1, qreal y1, qreal x2, qreal y2) { p.drawLine(QPointF(x1, y1), QPointF(x2, y2)); };
    auto poly = [&](std::initializer_list<QPointF> pts) { p.drawPolyline(QPolygonF(QList<QPointF>(pts))); };

    if (name == "import" || name == "export") {
        // A tray with an arrow going in (or out)
        poly({ { 4, 15 }, { 4, 20 }, { 20, 20 }, { 20, 15 } });
        if (name == "import") {
            line(12, 3, 12, 14);
            poly({ { 7.5, 9.5 }, { 12, 14 }, { 16.5, 9.5 } });
        } else {
            line(12, 14, 12, 3);
            poly({ { 7.5, 7.5 }, { 12, 3 }, { 16.5, 7.5 } });
        }
    } else if (name == "undo" || name == "redo") {
        QPainterPath path;
        bool undo = name == "undo";
        p.save();
        if (!undo) { // redo is undo, mirrored
            p.translate(24, 0);
            p.scale(-1, 1);
        }
        path.moveTo(5, 10);
        path.lineTo(14.5, 10);
        path.cubicTo(18.5, 10, 20, 12.5, 20, 15);
        path.cubicTo(20, 17.5, 18.5, 20, 14.5, 20);
        path.lineTo(10, 20);
        p.drawPath(path);
        poly({ { 9, 6 }, { 5, 10 }, { 9, 14 } });
        p.restore();
    } else if (name == "split") {
        // Scissors
        p.drawEllipse(QPointF(6.5, 17.5), 3, 3);
        p.drawEllipse(QPointF(17.5, 17.5), 3, 3);
        line(8.6, 15.4, 18, 4);
        line(15.4, 15.4, 6, 4);
    } else if (name == "delete") {
        // A bin
        line(4, 6.5, 20, 6.5);
        poly({ { 9, 6.5 }, { 9.5, 3.5 }, { 14.5, 3.5 }, { 15, 6.5 } });
        poly({ { 6, 6.5 }, { 7, 20.5 }, { 17, 20.5 }, { 18, 6.5 } });
        line(10.5, 10, 10.5, 17);
        line(13.5, 10, 13.5, 17);
    } else if (name == "detach") {
        // A speaker, split off
        poly({ { 3, 9.5 }, { 7, 9.5 }, { 11.5, 5 }, { 11.5, 19 }, { 7, 14.5 }, { 3, 14.5 }, { 3, 9.5 } });
        p.drawArc(QRectF(10, 7.5, 9, 9), -50 * 16, 100 * 16);
        line(16.5, 3, 21, 7.5);
        line(16.5, 21, 21, 16.5);
    } else if (name == "title") {
        line(5, 5, 19, 5);
        line(12, 5, 12, 20);
        line(9, 20, 15, 20);
    } else if (name == "zoom-in" || name == "zoom-out") {
        p.drawEllipse(QPointF(10.5, 10.5), 6.5, 6.5);
        line(15.3, 15.3, 20.5, 20.5);
        line(7.5, 10.5, 13.5, 10.5);
        if (name == "zoom-in")
            line(10.5, 7.5, 10.5, 13.5);
    } else if (name == "fit") {
        // Arrows out to the corners
        poly({ { 4, 9 }, { 4, 4 }, { 9, 4 } });
        poly({ { 15, 4 }, { 20, 4 }, { 20, 9 } });
        poly({ { 20, 15 }, { 20, 20 }, { 15, 20 } });
        poly({ { 9, 20 }, { 4, 20 }, { 4, 15 } });
    } else if (name == "play") {
        p.setBrush(c);
        QPainterPath tri;
        tri.moveTo(8, 5);
        tri.lineTo(19, 12);
        tri.lineTo(8, 19);
        tri.closeSubpath();
        p.drawPath(tri);
    } else if (name == "pause") {
        p.setBrush(c);
        p.drawRoundedRect(QRectF(6.5, 5, 3.5, 14), 1, 1);
        p.drawRoundedRect(QRectF(14, 5, 3.5, 14), 1, 1);
    } else if (name == "start") {
        line(6, 5, 6, 19);
        p.setBrush(c);
        QPainterPath tri;
        tri.moveTo(18, 5.5);
        tri.lineTo(9, 12);
        tri.lineTo(18, 18.5);
        tri.closeSubpath();
        p.drawPath(tri);
    } else if (name == "new") {
        p.drawRoundedRect(QRectF(4, 4, 16, 16), 4, 4);
        line(12, 8.5, 12, 15.5);
        line(8.5, 12, 15.5, 12);
    } else if (name == "open") {
        // A folder
        poly({ { 3, 18.5 }, { 3, 6 }, { 9, 6 }, { 11, 8.5 }, { 21, 8.5 }, { 21, 18.5 }, { 3, 18.5 } });
        line(3, 11.5, 21, 11.5);
    } else if (name == "captions") {
        p.drawRoundedRect(QRectF(3, 5, 18, 14), 3, 3);
        line(6.5, 12.5, 11, 12.5);
        line(13, 12.5, 17.5, 12.5);
        line(6.5, 15.5, 15, 15.5);
    }
}

} // namespace

QIcon get(const QString& name, const QColor& wanted)
{
    const QColor colour = wanted.isValid() ? wanted : Theme::colours().icon;
    QIcon icon;
    for (int size : { 16, 24, 32, 48 }) {
        QPixmap pm(size, size);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(size / 24.0, size / 24.0);
        draw(p, name, colour);
        p.end();
        icon.addPixmap(pm);
        // Greyed out when it can't be used
        QPixmap off(size, size);
        off.fill(Qt::transparent);
        QPainter q(&off);
        q.setRenderHint(QPainter::Antialiasing);
        q.scale(size / 24.0, size / 24.0);
        draw(q, name, Theme::colours().dark ? QColor(0x5a, 0x5f, 0x68) : QColor(0xb0, 0xb6, 0xbf));
        q.end();
        icon.addPixmap(off, QIcon::Disabled);
    }
    return icon;
}

} // namespace Icons
