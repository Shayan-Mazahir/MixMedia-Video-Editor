#include "PreviewRenderer.h"

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
    ve_timeline* tl = ve_timeline_create();

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

        QImage frame(size, QImage::Format_RGBA8888);
        ve_timeline_render_video(tl, t, size.width(), size.height(), frame.bits());
        emit frameReady(frame, t);
    }

    ve_timeline_destroy(tl);
}
