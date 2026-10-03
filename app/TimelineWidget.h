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
    const QList<TimelineTrack>& tracks() const { return m_tracks; }
    static QList<TimelineTrack> defaultTracks();

    // e.g. opening a project (clears undo). No tracks = an older project, which gets tidied up.
    void setClips(const QList<TimelineClip>& clips, const QList<TimelineTrack>& tracks = {});

    // What the engine needs. titleImage turns a title into the picture file to show.
    QList<RenderClip> renderClips(const std::function<QString(const TimelineClip&)>& titleImage = {}) const;
    double duration() const;

    double playhead() const { return m_playhead; }
    void setPlayhead(double sec); // moves it without firing playheadMoved

    // Drops clips onto the end of the right track (e.g. double-click in the media panel)
    void appendClips(const QList<TimelineClip>& clips);
    // Drops clips as if they'd been dragged to `pos` (used for files dragged in from outside)
    void dropClips(const QList<TimelineClip>& clips, const QPoint& pos);
    // Puts a title/effect at the playhead (or a transition on the nearest cut)
    void addAtPlayhead(const TimelineClip& clip);

    // Where clips start and end, for jumping between cuts
    QList<double> cutPoints() const;

    int selectedIndex() const { return m_selected; }
    void selectClip(int index);
    // Swap in a changed version of a clip (from the properties panel). Edits with the same
    // `what` in quick succession (dragging a slider) become a single undo step.
    void updateClip(int index, const TimelineClip& clip, const QString& what);
    // Changes how fast a clip plays, and slides everything after it along to make room
    void setClipSpeed(int index, double speed);
    // Puts a transition on the cut where this clip starts
    void setTransition(int index, int type);
    bool hasClipBefore(int index) const; // does another clip end right where this one starts?
    bool isOnCut(const TimelineClip& transition) const; // is a transition block joining two clips on a cut?
    // A transition block that isn't on a cut: VE_PART_IN, _OUT or _THROUGH (-1 = it's on a cut)
    int transitionPart(const TimelineClip& transition) const;

    // Tracks
    void addTrack(TimelineTrack::Kind kind, int at = -1); // at = index, -1 = the usual spot
    bool removeTrack(int index);                          // only empty ones

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
    enum class Drag { None, Playhead, Move, TrimLeft, TrimRight };
    enum class Edge { None, Left, Right };

    // Undo keeps the tracks too, since adding one shifts every clip below it
    struct Snapshot {
        QList<TimelineClip> clips;
        QList<TimelineTrack> tracks;
    };

    // Turning seconds into pixels and back
    double secToX(double sec) const;
    double xToSec(double x) const;
    QRect contentRect() const;
    int trackHeight(int track) const;
    int trackTop(int track) const;
    int tracksBottom() const;
    QRect trackRect(int track) const;
    QRectF clipRect(const TimelineClip& clip) const;
    int trackAt(int y) const;
    int clipAt(const QPoint& pos) const;
    Edge edgeAt(int clip, const QPoint& pos) const;

    // Placement rules
    bool accepts(int track, const TimelineClip& clip) const;
    int pickTrack(int wanted, const TimelineClip& clip) const;
    int mainVideoTrack() const;
    int homeTrack(const TimelineClip& clip) const;
    static double freeStart(const QList<TimelineClip>& clips, int track,
                            double start, double duration, int ignore = -1);
    // Cuts = one clip ending exactly where the next starts, on a video track
    bool nearestCut(double sec, double* cut, int* before = nullptr, int* after = nullptr) const;
    // The two clips a transition block blends (the second overlapping the end of the first)
    bool transitionPair(const TimelineClip& block, int* before, int* after) const;
    bool cutNear(double sec) const; // is there a cut within a few pixels of here?
    void slideAfter(int track, double from, double delta, int skip); // that track, its sound and the FX above
    bool applyTransition(TimelineClip block, double near, int wantedTrack); // overlaps the clips, adds the block
    void undoOverlap(int blockIndex);
    void resizeTransition(int blockIndex, double length);
    bool placeTransition(TimelineClip& t, int wantedTrack, double near) const; // just for the drag preview
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
    void upgradeOldProject(); // older projects had transitions stored on clips, and no FX tracks
    int insertTrack(TimelineTrack::Kind kind, int at); // adds a track (no undo step), returns where it went

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

    QList<TimelineTrack> m_tracks = defaultTracks();
    QList<TimelineClip> m_clips;
    QList<TimelineClip> m_ghosts; // where a drop would land
    int m_selected = -1;

    QList<Snapshot> m_undo;
    QList<Snapshot> m_redo;
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
    static constexpr int FxTrackHeight = 40;
    static constexpr int HeaderWidth = 90;
};
