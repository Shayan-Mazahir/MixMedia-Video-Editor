// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#pragma once

#include "RenderClip.h"

#include <QObject>
#include <QThread>

class AudioWorker;

// Plays the timeline's sound. All the real work happens on its own thread
// so a busy window never makes the audio stutter.
class AudioPlayer : public QObject {
    Q_OBJECT

public:
    explicit AudioPlayer(QObject* parent = nullptr);
    ~AudioPlayer() override;

    void setClips(const QList<RenderClip>& clips);
    void play(double from);
    void stop();

private:
    QThread m_thread;
    AudioWorker* m_worker = nullptr;
};
