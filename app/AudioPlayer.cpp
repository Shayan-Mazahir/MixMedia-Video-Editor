#include "AudioPlayer.h"

#include <QAudioFormat>
#include <QAudioSink>
#include <QIODevice>
#include <QMediaDevices>

#include <algorithm>
#include <cstring>
#include <vector>

namespace {

// The speaker pulls sound out of this whenever it's hungry, and we mix it fresh from the timeline.
class TimelineSource : public QIODevice {
public:
    TimelineSource(bool int16, QObject* parent)
        : QIODevice(parent)
        , m_int16(int16)
        , m_tl(ve_timeline_create())
    {
    }
    ~TimelineSource() override { ve_timeline_destroy(m_tl); }

    void setClips(const QList<RenderClip>& clips) { applyClips(m_tl, clips); }
    void seekTo(double t) { m_pos = t; }

    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override { return (1 << 20) + QIODevice::bytesAvailable(); }

protected:
    qint64 readData(char* data, qint64 maxSize) override
    {
        const int bytesPerFrame = m_int16 ? 4 : 8;
        int frames = int(std::min<qint64>(maxSize / bytesPerFrame, VE_AUDIO_RATE / 10));
        if (frames <= 0)
            return 0;

        m_mix.resize(size_t(frames) * 2);
        ve_timeline_render_audio(m_tl, m_pos, frames, m_mix.data());
        m_pos += double(frames) / VE_AUDIO_RATE;

        if (m_int16) {
            auto* out = reinterpret_cast<int16_t*>(data);
            for (size_t i = 0; i < m_mix.size(); ++i)
                out[i] = int16_t(std::clamp(m_mix[i], -1.0f, 1.0f) * 32767.0f);
        } else {
            std::memcpy(data, m_mix.data(), m_mix.size() * sizeof(float));
        }
        return qint64(frames) * bytesPerFrame;
    }

    qint64 writeData(const char*, qint64) override { return -1; }

private:
    bool m_int16;
    ve_timeline* m_tl;
    double m_pos = 0.0;
    std::vector<float> m_mix;
};

} // namespace

// Lives on the audio thread
class AudioWorker : public QObject {
public:
    void init()
    {
        QAudioDevice device = QMediaDevices::defaultAudioOutput();
        QAudioFormat format;
        format.setSampleRate(VE_AUDIO_RATE);
        format.setChannelCount(VE_AUDIO_CHANNELS);
        format.setSampleFormat(QAudioFormat::Float);
        bool int16 = false;
        if (!device.isFormatSupported(format)) {
            format.setSampleFormat(QAudioFormat::Int16);
            int16 = true;
        }

        m_source = new TimelineSource(int16, this);
        m_source->open(QIODevice::ReadOnly);
        m_sink = new QAudioSink(device, format, this);
        m_sink->setBufferSize(format.bytesForDuration(100'000)); // ~100ms, keeps it snappy
    }

    void setClips(const QList<RenderClip>& clips) { m_source->setClips(clips); }

    void play(double from)
    {
        m_sink->stop();
        m_source->seekTo(from);
        if (!m_source->isOpen())
            m_source->open(QIODevice::ReadOnly);
        m_sink->start(m_source);
    }

    void stop() { m_sink->stop(); }

private:
    TimelineSource* m_source = nullptr;
    QAudioSink* m_sink = nullptr;
};

AudioPlayer::AudioPlayer(QObject* parent)
    : QObject(parent)
    , m_worker(new AudioWorker)
{
    m_worker->moveToThread(&m_thread);
    m_thread.setObjectName("audio");
    m_thread.start(QThread::HighPriority);
    QMetaObject::invokeMethod(m_worker, [w = m_worker] { w->init(); }, Qt::BlockingQueuedConnection);
}

AudioPlayer::~AudioPlayer()
{
    QMetaObject::invokeMethod(m_worker, [w = m_worker] { delete w; }, Qt::BlockingQueuedConnection);
    m_thread.quit();
    m_thread.wait();
}

void AudioPlayer::setClips(const QList<RenderClip>& clips)
{
    QMetaObject::invokeMethod(m_worker, [w = m_worker, clips] { w->setClips(clips); });
}

void AudioPlayer::play(double from)
{
    QMetaObject::invokeMethod(m_worker, [w = m_worker, from] { w->play(from); });
}

void AudioPlayer::stop()
{
    QMetaObject::invokeMethod(m_worker, [w = m_worker] { w->stop(); });
}
