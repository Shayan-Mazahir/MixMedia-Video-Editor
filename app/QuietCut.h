// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#pragma once

#include "RenderClip.h"

#include <QList>
#include <QPair>
#include <QVector>

class QWidget;

// "Cut out the quiet bits": finds the pauses in the talking, so they can be cut out
// (the jump-cut style). Works from how loud the finished sound is, every 1/100th of a second.
namespace QuietCut {

constexpr double Step = 0.01; // seconds per loudness value

// A good "this counts as quiet" level for these loudness values (in dB): somewhere between the
// background noise and the talking, worked out from how loud things usually are
float autoThreshold(const QVector<float>& levels);

// The stretches to cut, in seconds: quiet for at least `minQuiet`, leaving `keep` of breathing
// room either side (not at the very start and end though: those go completely)
QList<QPair<double, double>> quietRanges(const QVector<float>& levels, float threshold, double minQuiet, double keep);

// The whole thing, with its windows: listens to the timeline, then asks how keen to be.
// Returns the stretches to cut (empty = cancelled or nothing quiet).
QList<QPair<double, double>> run(QWidget* parent, const QList<RenderClip>& clips);

} // namespace QuietCut
