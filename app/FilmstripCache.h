// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#pragma once

#include <QCache>
#include <QHash>
#include <QImage>
#include <QList>
#include <QObject>

#include <condition_variable>
#include <mutex>
#include <thread>

// Little frames for the strips on timeline clips. They get made in the
// background, so the timeline just asks, and repaints when tileReady fires.
class FilmstripCache : public QObject {
    Q_OBJECT

public:
    static constexpr int TileHeight = 56;

    explicit FilmstripCache(QObject* parent = nullptr);
    ~FilmstripCache() override;

    // The tile if we've got it, otherwise a null image (and it goes on the to-do list).
    QImage tile(const QString& path, double sec);

signals:
    void tileReady();

private:
    struct Request {
        QString path;
        double sec;
        QString key;
    };

    static QString keyFor(const QString& path, double sec);
    void run();

    std::mutex m_mutex;
    std::condition_variable m_wake;
    QCache<QString, QImage> m_tiles; // forgets the least recently used tiles first
    QList<Request> m_todo;  // newest at the back, those get done first
    QHash<QString, bool> m_queued;
    bool m_quit = false;
    std::thread m_worker;
};
