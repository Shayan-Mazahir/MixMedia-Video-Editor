// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#include "CardDelegate.h"
#include "Theme.h"

#include <QAbstractItemView>
#include <QPainter>
#include <QPainterPath>

#include <cmath>

namespace {

QString shortTime(double seconds)
{
    int s = int(std::lround(seconds));
    return s >= 3600 ? QString::asprintf("%d:%02d:%02d", s / 3600, (s / 60) % 60, s % 60)
                     : QString::asprintf("%d:%02d", s / 60, s % 60);
}

// A dark pill with a bit of text on it
void badge(QPainter* p, const QRectF& picture, const QString& text, bool left)
{
    QFont f = Theme::font(0.78, true);
    p->setFont(f);
    QFontMetricsF m(f);
    QRectF r(0, 0, m.horizontalAdvance(text) + 10, 16);
    r.moveBottom(picture.bottom() - 4);
    if (left)
        r.moveLeft(picture.left() + 4);
    else
        r.moveRight(picture.right() - 4);
    p->setPen(Qt::NoPen);
    p->setBrush(QColor(8, 10, 14, 200));
    p->drawRoundedRect(r, 5, 5);
    p->setPen(QColor(0xee, 0xf0, 0xf3));
    p->drawText(r, Qt::AlignCenter, text);
}

} // namespace

QSize CardDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex&) const
{
    // The picture, plus two lines for the name
    return option.decorationSize + QSize(16, 44);
}

void CardDelegate::paint(QPainter* p, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    p->setRenderHint(QPainter::SmoothPixmapTransform);

    const QRectF cell = QRectF(option.rect).adjusted(3, 3, -3, -3);
    const bool selected = option.state & QStyle::State_Selected;
    const bool hover = option.state & QStyle::State_MouseOver;

    // The card behind it all
    if (selected || hover) {
        QColor back = selected ? QColor(47, 198, 180, 38) : QColor(255, 255, 255, 12);
        p->setPen(selected ? QPen(Theme::colours().accent, 1.5) : Qt::NoPen);
        p->setBrush(back);
        p->drawRoundedRect(cell, 9, 9);
    }

    // The picture, with rounded corners
    const QSize deco = option.decorationSize;
    QRectF picture(cell.left() + (cell.width() - deco.width()) / 2, cell.top() + 5, deco.width(), deco.height());
    QPixmap pm = index.data(Qt::DecorationRole).value<QIcon>().pixmap(deco * p->device()->devicePixelRatioF());
    QPainterPath rounded;
    rounded.addRoundedRect(picture, 6, 6);
    p->save();
    p->setClipPath(rounded);
    p->fillRect(picture, QColor(0x0d, 0x0e, 0x11));
    if (!pm.isNull()) {
        // Fill the box keeping the shape (cropping a little if it has to)
        QSizeF size = QSizeF(pm.size()).scaled(picture.size(), Qt::KeepAspectRatioByExpanding);
        QRectF target(QPointF(0, 0), size);
        target.moveCenter(picture.center());
        p->drawPixmap(target, pm, QRectF(pm.rect()));
    }
    p->restore();
    p->setPen(QPen(QColor(255, 255, 255, 25), 1));
    p->setBrush(Qt::NoBrush);
    p->drawRoundedRect(picture.adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);

    // Badges: what kind of file, and how long
    const QString kind = index.data(KindRole).toString();
    if (!kind.isEmpty())
        badge(p, picture, kind == "audio" ? QStringLiteral("♪ Sound") : kind == "picture" ? QStringLiteral("Picture") : QStringLiteral("Video"), true);
    const QVariant duration = index.data(DurationRole);
    if (duration.isValid() && duration.toDouble() > 0)
        badge(p, picture, shortTime(duration.toDouble()), false);

    // The name, up to two lines
    QFont f = Theme::font(0.95);
    p->setFont(f);
    p->setPen(selected ? Theme::colours().text : Theme::colours().dim);
    QRectF text(cell.left() + 4, picture.bottom() + 5, cell.width() - 8, cell.bottom() - picture.bottom() - 6);
    QString name = index.data(Qt::DisplayRole).toString();
    QFontMetrics m(f);
    // Wrap to two lines, then trim the end of the second if it's still too long
    QString first = name, second;
    if (m.horizontalAdvance(name) > text.width()) {
        int cut = name.size();
        while (cut > 1 && m.horizontalAdvance(name.left(cut)) > text.width())
            --cut;
        int space = name.left(cut).lastIndexOf(' ');
        if (space > cut / 2)
            cut = space;
        first = name.left(cut).trimmed();
        second = m.elidedText(name.mid(cut).trimmed(), Qt::ElideMiddle, int(text.width()));
    }
    p->drawText(text, Qt::AlignHCenter | Qt::AlignTop, second.isEmpty() ? first : first + "\n" + second);
    p->restore();
}
