#pragma once

#include <QHash>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QVector>

#include <condition_variable>
#include <mutex>
#include <thread>

// Loudness overviews for drawing waveforms. Worked out in the background, one file at a time.
class WaveformCache : public QObject {
    Q_OBJECT

public:
    static constexpr int PerSecond = 50; // peaks per second of sound

    explicit WaveformCache(QObject* parent = nullptr);
    ~WaveformCache() override;

    // The peaks if we've got them, otherwise empty (and it goes on the to-do list).
    QVector<float> peaks(const QString& path);

signals:
    void ready();

private:
    void run();

    std::mutex m_mutex;
    std::condition_variable m_wake;
    QHash<QString, QVector<float>> m_done;
    QStringList m_todo;
    QSet<QString> m_asked;
    bool m_quit = false;
    std::thread m_worker;
};
