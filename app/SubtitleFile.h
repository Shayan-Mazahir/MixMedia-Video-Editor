// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#pragma once

#include <QList>
#include <QString>

// Reading and writing subtitle files: .srt (the common one) and .vtt (the web one)
namespace SubtitleFile {

struct Line {
    double start = 0.0, end = 0.0; // seconds
    QString text;                  // can be more than one line
};

// Either format. Bits it doesn't understand (styling tags, notes) get skipped.
QList<Line> parse(const QString& contents);
bool load(const QString& path, QList<Line>* lines, QString* error);

QString toSrt(const QList<Line>& lines);
bool save(const QString& path, const QList<Line>& lines, QString* error);

// "01:02:03,456" (srt) or "1:02.456" (short, for showing in the app)
QString formatTime(double seconds, bool srt);
// Understands both of those, plus "62.5" and "1:02". Returns -1 if it can't read it.
double parseTime(const QString& text);

} // namespace SubtitleFile
