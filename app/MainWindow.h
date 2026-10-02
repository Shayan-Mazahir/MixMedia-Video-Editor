#pragma once

#include <QElapsedTimer>
#include <QMainWindow>

class AudioPlayer;
class MediaBin;
class PreviewRenderer;
class PreviewWidget;
class QLabel;
class QToolButton;
class QTimer;
class TimelineWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    void importFiles(const QStringList& paths);

    // For testing: load files, put them on the timeline, then save a screenshot and quit
    void runDemo(const QStringList& paths, const QString& screenshotPath);

private slots:
    void importMedia();
    void exportVideo();
    void togglePlay();
    void goToStart();
    void onClipsChanged();
    void onPlayheadMoved(double sec);
    void onTick();

private:
    struct Project {
        int width = 1920;
        int height = 1080;
        double fps = 30.0;
    };

    QWidget* buildMediaPanel();
    QWidget* buildPreviewPanel();
    void buildToolbar();
    bool addMediaItem(const QString& path, QString* error);

    Project project() const;
    void requestPreview();
    void updateTimeLabel();
    void startPlayback();
    void stopPlayback();

    MediaBin* m_mediaBin = nullptr;
    PreviewWidget* m_preview = nullptr;
    TimelineWidget* m_timeline = nullptr;
    QLabel* m_timeLabel = nullptr;
    QToolButton* m_playButton = nullptr;

    PreviewRenderer* m_renderer = nullptr;
    AudioPlayer* m_audio = nullptr;

    QTimer* m_playTimer = nullptr;
    QElapsedTimer m_clock;
    double m_playFrom = 0.0;
    bool m_playing = false;
};
