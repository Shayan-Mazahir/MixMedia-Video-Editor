#pragma once

#include "RenderClip.h"

#include <QElapsedTimer>
#include <QMainWindow>

class AudioPlayer;
class ClipInspector;
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

    void importFiles(const QStringList& paths, bool quiet = false);
    void openFiles(const QStringList& paths); // projects get opened, media gets imported
    bool loadProject(const QString& path);

    // For testing: load files, put them on the timeline, then save a screenshot and quit
    void runDemo(const QStringList& paths, const QString& screenshotPath);

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void importMedia();
    void exportVideo();
    void togglePlay();
    void goToStart();
    void onClipsChanged();
    void onPlayheadMoved(double sec);
    void onTick();
    void refreshInspector();

    void newProject();
    void openProject();
    bool saveProject();
    bool saveProjectAs();

private:
    struct Project {
        int width = 1920;
        int height = 1080;
        double fps = 30.0;
    };

    QWidget* buildMediaPanel();
    QWidget* buildPreviewPanel();
    void buildActions();
    bool addMediaItem(const QString& path, QString* error);

    Project project() const;
    QList<RenderClip> renderClips(QSize titleSize) const;
    void requestPreview();
    void updateTimeLabel();
    void startPlayback();
    void stopPlayback();

    bool maybeSave(); // false = the user cancelled
    void setDirty(bool dirty);
    void updateWindowTitle();
    QString projectFolder() const;

    MediaBin* m_mediaBin = nullptr;
    PreviewWidget* m_preview = nullptr;
    TimelineWidget* m_timeline = nullptr;
    ClipInspector* m_inspector = nullptr;
    QLabel* m_timeLabel = nullptr;
    QToolButton* m_playButton = nullptr;

    PreviewRenderer* m_renderer = nullptr;
    AudioPlayer* m_audio = nullptr;

    QTimer* m_playTimer = nullptr;
    QElapsedTimer m_clock;
    double m_playFrom = 0.0;
    bool m_playing = false;

    QString m_projectPath;
    bool m_dirty = false;
    bool m_loadingProject = false;
};
