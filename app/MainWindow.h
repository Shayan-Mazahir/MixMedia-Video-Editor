// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#pragma once

#include "ProjectFile.h"
#include "RenderClip.h"

#include <QElapsedTimer>
#include <QMainWindow>

#include <memory>

class QLockFile;

class AudioPlayer;
class ClipInspector;
class MediaBin;
class PreviewRenderer;
class PreviewWidget;
class SubtitlePanel;
class QComboBox;
class QLabel;
class QListWidgetItem;
class QToolButton;
class QTimer;
class TimelineWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    QList<class QListWidgetItem*> importFiles(const QStringList& paths, bool quiet = false); // returns what got added
    void openFiles(const QStringList& paths); // projects get opened, media gets imported
    bool loadProject(const QString& path);

    // Crashed last time? Offers to bring back what was auto-saved. Call once at startup.
    void offerRecovery();

    // For testing: load files, put them on the timeline, then save a screenshot and quit
    void runDemo(const QStringList& paths, const QString& screenshotPath);

protected:
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    void importMedia();
    void exportVideo();
    void togglePlay();
    void goToStart();
    void onClipsChanged();
    void onPlayheadMoved(double sec);
    void onTick();
    void refreshInspector();
    void onFilesDroppedOnTimeline(const QStringList& files, const QPoint& pos);

    void newProject();
    void openProject();
    bool saveProject();
    bool saveProjectAs();
    void showAbout();
    void editProjectSettings();
    void evenOutVolume(int index);
    void importSubtitles();
    void autoCaptions();
    void exportSubtitles();

private:
    struct Project {
        int width = 1920;
        int height = 1080;
        double fps = 30.0;
    };

    QWidget* buildMediaPanel();
    QWidget* buildPreviewPanel();
    void buildActions();
    QListWidgetItem* addMediaItem(const QString& path, QString* error);

    Project project() const { return project(m_settings); }
    Project project(const ProjectSettings& settings) const; // what it'd be with these settings
    void setProjectSettings(const ProjectSettings& settings);
    QList<RenderClip> renderClips(QSize titleSize, bool subtitles = true) const;
    void requestPreview();
    void updateTimeLabel();
    void startPlayback();
    void stopPlayback();
    void seekTo(double sec);
    void seekBy(double seconds);
    void stepFrames(int frames);
    void jumpToCut(int direction); // -1 = previous, +1 = next

    bool maybeSave(); // false = the user cancelled
    ProjectFile::Data projectData() const;

    // Auto-save: a copy of unsaved work every minute, in our own folder (your project file is
    // only ever written when you save). It's deleted again when you save or close normally.
    static QString autoSaveFolder();
    void autoSave();
    void clearAutoSave();
    void setDirty(bool dirty);
    void updateWindowTitle();
    QString projectFolder() const;

    MediaBin* m_mediaBin = nullptr;
    PreviewWidget* m_preview = nullptr;
    TimelineWidget* m_timeline = nullptr;
    ClipInspector* m_inspector = nullptr;
    SubtitlePanel* m_subtitles = nullptr;
    QLabel* m_timeLabel = nullptr;
    QToolButton* m_playButton = nullptr;
    QComboBox* m_shapeBox = nullptr;

    PreviewRenderer* m_renderer = nullptr;
    AudioPlayer* m_audio = nullptr;

    QTimer* m_playTimer = nullptr;
    QElapsedTimer m_clock;
    double m_playFrom = 0.0;
    bool m_playing = false;

    ProjectSettings m_settings;
    QString m_projectPath;
    bool m_dirty = false;
    bool m_loadingProject = false;

    QTimer* m_autoSaveTimer = nullptr;
    QString m_autoSaveId;                   // this window's auto-save files are named after it
    std::unique_ptr<QLockFile> m_autoSaveLock; // held while we run, so a crash leaves it stale
};
