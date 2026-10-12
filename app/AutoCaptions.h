// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#pragma once

#include "RenderClip.h"
#include "TimelineClip.h"

#include <QList>
#include <QString>

class QWidget;

// Auto-captions: pick the options, get the speech model if it's not here yet, listen to the
// timeline, and turn what's heard into subtitle lines.
namespace AutoCaptions {

// The ones we offer to download (squeezed "quantized" versions: a third of the size and the
// memory, and they hear just about as well)
// (No English-only ".en" ones: they're a touch better at pure English, but if anything else
// gets said they go round in circles making things up.)
struct Model {
    QString id;        // e.g. "small-q5_1" (the file is ggml-<id>.bin)
    QString name;      // what people see
    int megabytes;
};
QList<Model> models();
QString modelFolder();
QString modelPath(const QString& id);

// The speech detector (tiny), so only the talking gets listened to
QString detectorPath();

// Your own models: files picked with "Add a model file…", and any ggml-*.bin in the models
// folder that isn't one of ours
QStringList ownModels();
void addOwnModel(const QString& path);

struct Word {
    double start, end;
    QString text;
    bool startsSentence;
};

// Groups words into subtitle lines: a new line at the end of a sentence, after a pause, or when
// it gets too long to read comfortably. Each line keeps its words' timings.
QList<TimelineClip> linesFromWords(const QList<Word>& words, int maxChars = 42, double maxSeconds = 6.0);

// The whole thing, with its windows. Returns the lines (empty if cancelled or nothing heard),
// and sets *replace to whether they should replace the subtitles already there.
QList<TimelineClip> run(QWidget* parent, const QList<RenderClip>& clips, bool haveSubtitles, bool* replace);

} // namespace AutoCaptions
