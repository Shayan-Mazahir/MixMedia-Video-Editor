// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#include "WaveformCache.h"
#include "ThreadName.h"

#include <ve/engine.h>

WaveformCache::WaveformCache(QObject* parent)
    : QObject(parent)
    , m_worker([this] { run(); })
{
}

WaveformCache::~WaveformCache()
{
    {
        std::lock_guard lock(m_mutex);
        m_quit = true;
    }
    m_wake.notify_all();
    m_worker.join();
}

QVector<float> WaveformCache::peaks(const QString& path)
{
    std::lock_guard lock(m_mutex);
    auto it = m_done.constFind(path);
    if (it != m_done.constEnd())
        return *it;
    if (!m_asked.contains(path)) {
        m_asked.insert(path);
        m_todo.append(path);
        m_wake.notify_one();
    }
    return {};
}

void WaveformCache::run()
{
    nameThisThread("mm-waveform");
    while (true) {
        QString path;
        {
            std::unique_lock lock(m_mutex);
            m_wake.wait(lock, [this] { return m_quit || !m_todo.isEmpty(); });
            if (m_quit)
                return;
            path = m_todo.takeFirst();
        }

        QByteArray file = path.toUtf8();
        QVector<float> result;
        ve_media_info info;
        if (ve_probe(file.constData(), &info) == VE_OK && info.has_audio && info.duration_sec > 0) {
            result.resize(int(info.duration_sec * PerSecond) + PerSecond);
            int n = ve_audio_peaks(file.constData(), PerSecond, result.data(), int(result.size()));
            result.resize(std::max(n, 0));
        }
        if (result.isEmpty())
            result = { 0.0f }; // nothing to show, but don't keep asking

        {
            std::lock_guard lock(m_mutex);
            m_done.insert(path, result);
        }
        emit ready();
    }
}
