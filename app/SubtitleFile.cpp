// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#include "SubtitleFile.h"

#include <QFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStringConverter>

#include <cmath>

namespace SubtitleFile {

double parseTime(const QString& text)
{
    // [[h:]m:]s[.,fraction]
    static const QRegularExpression re("^\\s*(?:(\\d+):)?(?:(\\d+):)?(\\d+)(?:[.,](\\d+))?\\s*$");
    QRegularExpressionMatch m = re.match(text);
    if (!m.hasMatch())
        return -1;
    double seconds = m.captured(3).toDouble();
    QString fraction = m.captured(4);
    if (!fraction.isEmpty())
        seconds += fraction.toDouble() / std::pow(10.0, fraction.size());
    // One colon = m:s, two = h:m:s
    if (!m.captured(2).isEmpty())
        seconds += m.captured(1).toDouble() * 3600 + m.captured(2).toDouble() * 60;
    else if (!m.captured(1).isEmpty())
        seconds += m.captured(1).toDouble() * 60;
    return seconds;
}

QString formatTime(double seconds, bool srt)
{
    qint64 ms = std::llround(std::max(0.0, seconds) * 1000);
    qint64 h = ms / 3600000, m = (ms / 60000) % 60, s = (ms / 1000) % 60, rest = ms % 1000;
    if (srt)
        return QString::asprintf("%02lld:%02lld:%02lld,%03lld", h, m, s, rest);
    if (h > 0)
        return QString::asprintf("%lld:%02lld:%02lld.%03lld", h, m, s, rest);
    return QString::asprintf("%lld:%02lld.%03lld", m, s, rest);
}

QList<Line> parse(const QString& contents)
{
    QString text = contents;
    text.replace("\r\n", "\n").replace('\r', '\n');
    if (text.startsWith(QChar(0xFEFF)))
        text.remove(0, 1);

    static const QRegularExpression arrow("^\\s*(\\S+)\\s*-->\\s*(\\S+)");
    // <i>, <b>, <font ...>, <c.yellow>, {\an8} and friends: we do our own styling
    static const QRegularExpression tags("<[^>]*>|\\{\\\\[^}]*\\}");

    QList<Line> lines;
    const QStringList blocks = text.split(QRegularExpression("\\n\\s*\\n"), Qt::SkipEmptyParts);
    for (const QString& block : blocks) {
        QStringList rows = block.split('\n');
        int at = 0;
        while (at < rows.size() && !rows[at].contains("-->"))
            ++at; // (skips the line number in .srt, and cue names in .vtt)
        if (at >= rows.size())
            continue; // "WEBVTT", NOTE blocks and the like
        QRegularExpressionMatch m = arrow.match(rows[at]);
        if (!m.hasMatch())
            continue;
        Line line { parseTime(m.captured(1)), parseTime(m.captured(2)), {} };
        if (line.start < 0 || line.end <= line.start)
            continue;
        QStringList words;
        for (int i = at + 1; i < rows.size(); ++i) {
            QString row = rows[i];
            row.remove(tags);
            if (!row.trimmed().isEmpty())
                words << row.trimmed();
        }
        line.text = words.join('\n');
        if (!line.text.isEmpty())
            lines << line;
    }
    std::sort(lines.begin(), lines.end(), [](const Line& a, const Line& b) { return a.start < b.start; });
    return lines;
}

bool load(const QString& path, QList<Line>* lines, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = file.errorString();
        return false;
    }
    QByteArray raw = file.readAll();
    // Most are UTF-8; really old ones might be Windows-1252 (and won't decode cleanly as UTF-8)
    auto utf8 = QStringDecoder(QStringConverter::Utf8);
    QString text = utf8(raw);
    if (utf8.hasError())
        text = QString::fromLatin1(raw);
    *lines = parse(text);
    if (lines->isEmpty()) {
        *error = "Couldn't find any subtitles in that file.";
        return false;
    }
    return true;
}

QString toSrt(const QList<Line>& lines)
{
    QString out;
    int n = 1;
    for (const Line& l : lines)
        out += QString("%1\n%2 --> %3\n%4\n\n").arg(n++).arg(formatTime(l.start, true), formatTime(l.end, true), l.text);
    return out;
}

bool save(const QString& path, const QList<Line>& lines, QString* error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        *error = file.errorString();
        return false;
    }
    file.write(toSrt(lines).toUtf8());
    if (!file.commit()) {
        *error = file.errorString();
        return false;
    }
    return true;
}

} // namespace SubtitleFile
