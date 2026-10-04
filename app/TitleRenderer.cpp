// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#include "TitleRenderer.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QPainter>
#include <QPainterPath>
#include <QStandardPaths>
#include <QTextLayout>

#include <cmath>

namespace TitleRenderer {

namespace {

QFont fontFor(const TitleStyle& style, QSize size)
{
    QFont font;
    if (!style.font.isEmpty())
        font.setFamily(style.font);
    font.setPixelSize(std::max(4, int(std::lround(size.height() * style.size / 100))));
    font.setBold(style.bold);
    font.setItalic(style.italic);
    // Letters placed exactly (not snapped to whole pixels), or extra spacing comes out uneven
    font.setHintingPreference(QFont::PreferNoHinting);
    if (style.spacing != 0) // (% of a typical letter's width, about half the text size)
        font.setLetterSpacing(QFont::AbsoluteSpacing, font.pixelSize() * 0.5 * style.spacing / 100);
    return font;
}

// The text as outlines, wrapped to fit, lined up the way the style says.
// The highlighted word (if any) goes in `lit` instead.
QPainterPath layoutText(const TitleStyle& style, const QFont& font, QSize size, QRectF* bounds,
                        int highlightWord = -1, QPainterPath* lit = nullptr)
{
    const double maxWidth = size.width() * 0.9;
    QString text = style.text;
    text.replace('\n', QChar::LineSeparator); // (QTextLayout's idea of a line break)
    QTextLayout layout(text, font);
    QTextOption option;
    option.setWrapMode(QTextOption::WordWrap);
    layout.setTextOption(option);

    struct Line {
        QString text;
        double width, y;
    };
    QList<Line> lines;
    QFontMetricsF metrics(font);
    double y = 0, widest = 0;
    layout.beginLayout();
    for (QTextLine line = layout.createLine(); line.isValid(); line = layout.createLine()) {
        line.setLineWidth(maxWidth);
        QString part = text.mid(line.textStart(), line.textLength()).remove(QChar::LineSeparator).trimmed();
        double w = metrics.horizontalAdvance(part);
        lines << Line { part, w, y };
        widest = std::max(widest, w);
        y += metrics.lineSpacing();
    }
    layout.endLayout();
    const double height = std::max(0.0, y - metrics.leading());

    // Where the block goes: style.y is its middle, style.x the side it's aligned to
    double left = style.align == 0 ? size.width() * style.x
                : style.align == 2 ? size.width() * style.x - widest
                                   : size.width() * style.x - widest / 2;
    double top = size.height() * style.y - height / 2;
    top = std::clamp(top, 0.0, std::max(0.0, size.height() - height)); // don't fall off the top or bottom

    QPainterPath path;
    int word = 0;
    for (const Line& line : lines) {
        double x = style.align == 0 ? left : style.align == 2 ? left + widest - line.width : left + (widest - line.width) / 2;
        double baseline = top + line.y + metrics.ascent();
        if (highlightWord < 0 || !lit) {
            path.addText(QPointF(x, baseline), font, line.text);
            continue;
        }
        // Word by word, so one of them can be drawn differently
        int at = 0;
        while (at < line.text.size()) {
            while (at < line.text.size() && line.text[at].isSpace())
                ++at;
            int end = at;
            while (end < line.text.size() && !line.text[end].isSpace())
                ++end;
            if (end == at)
                break;
            QPointF where(x + metrics.horizontalAdvance(line.text.left(at)), baseline);
            (word == highlightWord ? *lit : path).addText(where, font, line.text.mid(at, end - at));
            ++word;
            at = end;
        }
    }
    *bounds = QRectF(left, top, widest, height);
    return path;
}

} // namespace

QImage render(const TitleStyle& style, QSize size, int highlightWord)
{
    QImage img(size, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    if (style.text.trimmed().isEmpty())
        return img;

    const QFont font = fontFor(style, size);
    QRectF bounds;
    QPainterPath lit;
    QPainterPath text = layoutText(style, font, size, &bounds, highlightWord, &lit);
    QPainterPath all = text;
    all.addPath(lit);
    const double px = font.pixelSize();

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);

    if (style.box) {
        double pad = px / 3;
        p.setPen(Qt::NoPen);
        p.setBrush(style.boxColor);
        p.drawRoundedRect(bounds.adjusted(-pad * 2, -pad, pad * 2, pad), pad, pad);
    }

    if (style.shadow) {
        // Drawn small and stretched back up = a soft blur, cheaply. Softer = smaller.
        double soft = std::clamp(style.shadowSoftness, 0.0, 100.0) / 100;
        int shrink = 1 + int(std::lround(soft * 10));
        QImage shadow(size / shrink + QSize(1, 1), QImage::Format_ARGB32_Premultiplied);
        shadow.fill(Qt::transparent);
        {
            QPainter s(&shadow);
            s.setRenderHint(QPainter::Antialiasing);
            s.scale(1.0 / shrink, 1.0 / shrink);
            double off = px * style.shadowDistance / 100;
            s.translate(off, off);
            s.fillPath(all, style.shadowColor);
            if (style.outline > 0)
                s.strokePath(all, QPen(style.shadowColor, px * style.outline / 100 * 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        }
        p.save();
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawImage(QRectF(0, 0, shadow.width() * shrink, shadow.height() * shrink), shadow);
        p.restore();
    }

    if (style.outline > 0) // drawn twice as thick, then the letters go on top covering the inner half
        p.strokePath(all, QPen(style.outlineColor, px * style.outline / 100 * 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.fillPath(text, style.color);
    if (!lit.isEmpty())
        p.fillPath(lit, style.highlight);
    p.end();
    return img.convertToFormat(QImage::Format_ARGB32);
}

QString imageFile(const TitleStyle& style, QSize size, int highlightWord)
{
    // Name the file after everything that affects how it looks, so any change makes a new one
    QByteArray key;
    QDataStream stream(&key, QIODevice::WriteOnly);
    stream << style << size << highlightWord << 4; // (bump the number when the drawing itself changes)
    QString name = QCryptographicHash::hash(key, QCryptographicHash::Sha1).toHex().left(20) + ".png";

    QDir dir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/titles");
    dir.mkpath(".");
    QString path = dir.filePath(name);
    if (!QFileInfo::exists(path))
        render(style, size, highlightWord).save(path);
    return path;
}

} // namespace TitleRenderer
