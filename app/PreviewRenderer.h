#pragma once

#include "RenderClip.h"

#include <QImage>
#include <QObject>
#include <QSize>

#include <condition_variable>
#include <mutex>
#include <thread>

// Draws timeline frames on a background thread. If you ask for lots of frames
// quickly (scrubbing, playback), it skips straight to the newest request.
class PreviewRenderer : public QObject {
    Q_OBJECT

public:
    explicit PreviewRenderer(QObject* parent = nullptr);
    ~PreviewRenderer() override;

    void setClips(const QList<RenderClip>& clips);
    void request(double t, QSize size);

signals:
    void frameReady(const QImage& frame, double t);

private:
    void run();

    std::mutex m_mutex;
    std::condition_variable m_wake;
    QList<RenderClip> m_clips;
    bool m_clipsChanged = false;
    bool m_hasRequest = false;
    double m_time = 0.0;
    QSize m_size;
    bool m_quit = false;
    std::thread m_worker;
};
