// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#include "FilmstripCache.h"
#include "ThreadName.h"

#include <ve/engine.h>

#include <unordered_map>

namespace {
constexpr int MaxTileWidth = FilmstripCache::TileHeight * 3;
constexpr int MaxQueued = 96;     // older requests are probably scrolled away by now
constexpr int MaxCachedKB = 40 * 1024; // ~40MB of tiles, then the oldest go
}

FilmstripCache::FilmstripCache(QObject* parent)
    : QObject(parent)
    , m_worker([this] { run(); })
{
    std::lock_guard lock(m_mutex);
    m_tiles.setMaxCost(MaxCachedKB);
}

FilmstripCache::~FilmstripCache()
{
    {
        std::lock_guard lock(m_mutex);
        m_quit = true;
    }
    m_wake.notify_all();
    m_worker.join();
}

QString FilmstripCache::keyFor(const QString& path, double sec)
{
    return path + '@' + QString::number(qint64(sec * 1000));
}

QImage FilmstripCache::tile(const QString& path, double sec)
{
    QString key = keyFor(path, sec);
    std::lock_guard lock(m_mutex);

    if (const QImage* cached = m_tiles.object(key))
        return *cached;

    if (!m_queued.contains(key)) {
        m_todo.append({ path, sec, key });
        m_queued.insert(key, true);
        while (m_todo.size() > MaxQueued)
            m_queued.remove(m_todo.takeFirst().key);
        m_wake.notify_one();
    }
    return {};
}

void FilmstripCache::run()
{
    nameThisThread("mm-filmstrip");
    std::unordered_map<std::string, ve_reader*> readers;

    while (true) {
        Request req;
        {
            std::unique_lock lock(m_mutex);
            m_wake.wait(lock, [this] { return m_quit || !m_todo.isEmpty(); });
            if (m_quit)
                break;
            req = m_todo.takeLast();
        }

        std::string path = req.path.toStdString();
        ve_reader*& reader = readers[path];
        if (!reader)
            reader = ve_reader_open(path.c_str());

        QImage img;
        if (reader) {
            QImage buf(MaxTileWidth, TileHeight, QImage::Format_RGBA8888);
            int w = 0, h = 0;
            if (ve_reader_frame(reader, req.sec, 1, MaxTileWidth, TileHeight, buf.bits(), &w, &h) == VE_OK)
                img = QImage(buf.bits(), w, h, w * 4, QImage::Format_RGBA8888).convertToFormat(QImage::Format_RGB888);
        }

        {
            std::lock_guard lock(m_mutex);
            m_queued.remove(req.key);
            // Store even a failed (null) one as a black tile so we don't keep retrying
            if (img.isNull()) {
                img = QImage(16, 9, QImage::Format_RGBA8888);
                img.fill(Qt::black);
            }
            m_tiles.insert(req.key, new QImage(img), std::max<qsizetype>(1, img.sizeInBytes() / 1024));
        }
        emit tileReady();
    }

    for (auto& [path, reader] : readers)
        ve_reader_close(reader);
}
