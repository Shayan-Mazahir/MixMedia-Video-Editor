// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#pragma once

#include "RenderClip.h"
#include "TimelineClip.h"

#include <QElapsedTimer>
#include <QList>
#include <QWidget>

#include <functional>

class FilmstripCache;
class WaveformCache;
class QMimeData;
class QScrollBar;

class TimelineWidget : public QWidget {
    Q_OBJECT

public:
    explicit TimelineWidget(QWidget* parent = nullptr);

    QSize sizeHint() const override;

    const QList<TimelineClip>& clips() const { return m_clips; }
    void setClips(const QList<TimelineClip>& clips); // e.g. opening a project (clears undo)

    // What the engine needs. titleImage turns a title into the picture file to show.
    QList<RenderClip> renderClips(const std::function<QString(const TimelineClip&)>& titleImage = {}) const;
    double duration() const;

    double playhead() const { return m_playhead; }
    void setPlayhead(double sec); // moves it without firing playheadMoved

    // Drops clips onto the end of the right track (e.g. double-click in the media panel)
    void appendClips(const QList<TimelineClip>& clips);
    // Drops clips as if they'd been dragged to `pos` (used for files dragged in from outside)
    void dropClips(const QList<TimelineClip>& clips, const QPoint& pos);

    // Where clips start and end, for jumping between cuts
    QList<double> cutPoints() const;

    int selectedIndex() const { return m_selected; }
    void selectClip(int index);
    // Swap in a changed version of a clip (from the properties panel). Edits with the same
    // `what` in quick succession (dragging a slider) become a single undo step.
    void updateClip(int index, const TimelineClip& clip, const QString& what);
    // Changes how fast a clip plays, and slides everything after it along to make room
    void setClipSpeed(int index, double speed);

public slots:
    void splitAtPlayhead();
    void deleteSelected(bool closeGap = true); // closing the gap = "ripple delete"
    void deleteSelectedKeepGap();
    void undo();
    void redo();
    void zoomToFit();
    void zoomBy(double factor);
    void detachAudio();
    void addTitle();

signals:
    void playheadMoved(double seconds); // you moved it by hand
    void clipsChanged();
    void selectionChanged(int index);
    void filesDropped(const QStringList& files, const QPoint& pos);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    struct Track {
        QString name;
        bool audio;
    };
    enum class Drag { None, Playhead, Move, TrimLeft, TrimRight };
    enum class Edge { None, Left, Right };

    // Turning seconds into pixels and back
    double secToX(double sec) const;
    double xToSec(double x) const;
    QRect contentRect() const;
    QRect trackRect(int track) const;
    QRectF clipRect(const TimelineClip& clip) const;
    int trackAt(int y) const;
    int clipAt(const QPoint& pos) const;
    Edge edgeAt(int clip, const QPoint& pos) const;

    // Placement rules
    int pickTrack(int wanted, bool audioOnly) const;
    static double freeStart(const QList<TimelineClip>& clips, int track,
                            double start, double duration, int ignore = -1);
    QList<TimelineClip> layoutDrop(const QList<TimelineClip>& clips, const QPoint& pos) const;
    static QList<TimelineClip> clipsFromMime(const QMimeData* mime);
    static QStringList filesFromMime(const QMimeData* mime);
    double snapped(double sec, int ignore, double* moved = nullptr) const;
    double neighbourBefore(int clip) const;
    double neighbourAfter(int clip) const;

    // Edits
    void dragMove(const QPoint& pos);
    void dragTrim(const QPoint& pos, bool left);
    void pushUndo(const QList<TimelineClip>& state);
    QList<int> partnersOf(int index) const; // itself + its detached sound (or the video it came from)
    void syncPartners();
    void changed();
    void invalidate(); // something changed, redraw the timeline properly next time
    void select(int index);

    void clampScroll();
    void keepPlayheadVisible();
    void syncScrollBar();

    void drawRuler(QPainter& p);
    void drawTracks(QPainter& p);
    void drawClip(QPainter& p, const TimelineClip& clip, bool selected, bool ghost);
    void drawFilmstrip(QPainter& p, const TimelineClip& clip, const QRectF& r);
    void drawWaveform(QPainter& p, const TimelineClip& clip, const QRectF& r);
    void drawFades(QPainter& p, const TimelineClip& clip, const QRectF& r);
    void drawPlayhead(QPainter& p);

    QList<Track> m_tracks { { "Video 2", false }, { "Video 1", false }, { "Audio 1", true }, { "Audio 2", true } };
    QList<TimelineClip> m_clips;
    QList<TimelineClip> m_ghosts; // where a drop would land
    int m_selected = -1;

    QList<QList<TimelineClip>> m_undo;
    QList<QList<TimelineClip>> m_redo;
    QList<TimelineClip> m_beforeDrag;
    QList<int> m_dragPartners;
    QString m_lastEdit;
    QElapsedTimer m_lastEditClock;

    double m_pixelsPerSecond = 50.0;
    double m_scrollX = 0.0;
    double m_playhead = 0.0;

    Drag m_drag = Drag::None;
    double m_grabOffset = 0.0; // where on the clip you grabbed it (seconds)

    QScrollBar* m_scrollBar;
    QPixmap m_cache; // the timeline minus the playhead
    bool m_cacheDirty = true;

    FilmstripCache* m_filmstrip;
    WaveformCache* m_waveforms;

    static constexpr int RulerHeight = 28;
    static constexpr int TrackHeight = 58;
    static constexpr int HeaderWidth = 90;
};
