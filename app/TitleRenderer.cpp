// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#include "TitleRenderer.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QPainter>
#include <QPainterPath>
#include <QStandardPaths>

#include <cmath>

namespace TitleRenderer {

QImage render(const TitleStyle& style, QSize size)
{
    QImage img(size, QImage::Format_ARGB32);
    img.fill(Qt::transparent);
    if (style.text.trimmed().isEmpty())
        return img;

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);

    QFont font;
    font.setPixelSize(std::max(4, int(std::lround(size.height() * style.size / 100))));
    font.setBold(style.bold);
    p.setFont(font);

    // Wrap long lines so they stay inside the picture, then centre the block on style.y
    int maxWidth = int(size.width() * 0.9);
    QRect bounds = p.fontMetrics().boundingRect(QRect(0, 0, maxWidth, size.height()),
                                                Qt::AlignHCenter | Qt::TextWordWrap, style.text);
    int centreY = int(size.height() * style.y);
    QRect textRect((size.width() - bounds.width()) / 2, centreY - bounds.height() / 2,
                   bounds.width(), bounds.height());
    // Don't let it fall off the top or bottom
    textRect.moveTop(std::clamp(textRect.top(), 0, std::max(0, size.height() - textRect.height())));

    if (style.box) {
        int pad = font.pixelSize() / 3;
        QRectF box = textRect.adjusted(-pad * 2, -pad, pad * 2, pad);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 160));
        p.drawRoundedRect(box, pad, pad);
    } else {
        // No box? A soft shadow keeps it readable on bright footage
        p.setPen(QColor(0, 0, 0, 170));
        int off = std::max(1, font.pixelSize() / 18);
        p.drawText(textRect.translated(off, off), Qt::AlignHCenter | Qt::TextWordWrap, style.text);
    }

    p.setPen(style.color);
    p.drawText(textRect, Qt::AlignHCenter | Qt::TextWordWrap, style.text);
    return img;
}

QString imageFile(const TitleStyle& style, QSize size)
{
    // Name the file after everything that affects how it looks, so any change makes a new one
    QByteArray key;
    QDataStream stream(&key, QIODevice::WriteOnly);
    stream << style.text << style.size << style.color << style.y << style.box << style.bold << size;
    QString name = QCryptographicHash::hash(key, QCryptographicHash::Sha1).toHex().left(20) + ".png";

    QDir dir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/titles");
    dir.mkpath(".");
    QString path = dir.filePath(name);
    if (!QFileInfo::exists(path))
        render(style, size).save(path);
    return path;
}

} // namespace TitleRenderer
