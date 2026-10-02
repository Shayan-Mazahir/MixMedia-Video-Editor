// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#include "PreviewRenderer.h"
#include "ThreadName.h"

PreviewRenderer::PreviewRenderer(QObject* parent)
    : QObject(parent)
    , m_worker([this] { run(); })
{
}

PreviewRenderer::~PreviewRenderer()
{
    {
        std::lock_guard lock(m_mutex);
        m_quit = true;
    }
    m_wake.notify_all();
    m_worker.join();
}

void PreviewRenderer::setClips(const QList<RenderClip>& clips)
{
    {
        std::lock_guard lock(m_mutex);
        m_clips = clips;
        m_clipsChanged = true;
    }
    m_wake.notify_one();
}

void PreviewRenderer::request(double t, QSize size)
{
    if (size.width() < 2 || size.height() < 2)
        return;
    {
        std::lock_guard lock(m_mutex);
        m_time = t;
        m_size = size;
        m_hasRequest = true;
    }
    m_wake.notify_one();
}

void PreviewRenderer::run()
{
    nameThisThread("mm-preview");
    ve_timeline* tl = ve_timeline_create();
    ve_timeline_use_preview_settings(tl);

    while (true) {
        double t;
        QSize size;
        {
            std::unique_lock lock(m_mutex);
            m_wake.wait(lock, [this] { return m_quit || m_hasRequest || m_clipsChanged; });
            if (m_quit)
                break;
            if (m_clipsChanged) {
                applyClips(tl, m_clips);
                m_clipsChanged = false;
            }
            if (!m_hasRequest)
                continue;
            t = m_time;
            size = m_size;
            m_hasRequest = false;
        }

        // RGB32 is the screen's own format, so Qt can show it without converting every pixel
        QImage frame(size, QImage::Format_RGB32);
        ve_timeline_render_video_bgra(tl, t, size.width(), size.height(), frame.bits());
        emit frameReady(frame, t);
    }

    ve_timeline_destroy(tl);
}
