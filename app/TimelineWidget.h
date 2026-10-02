#pragma once

#include "RenderClip.h"
#include "TimelineClip.h"

#include <QList>
#include <QWidget>

class FilmstripCache;
class QMimeData;

class TimelineWidget : public QWidget {
    Q_OBJECT

public:
    explicit TimelineWidget(QWidget* parent = nullptr);

    QSize sizeHint() const override;

    const QList<TimelineClip>& clips() const { return m_clips; }
    QList<RenderClip> renderClips() const;
    double duration() const;

    double playhead() const { return m_playhead; }
    void setPlayhead(double sec); // moves it without firing playheadMoved

    // Drops clips onto the end of the right track (e.g. double-click in the media panel)
    void appendClips(const QList<TimelineClip>& clips);

public slots:
    void splitAtPlayhead();
    void deleteSelected();
    void undo();
    void redo();
    void zoomToFit();
    void zoomBy(double factor);

signals:
    void playheadMoved(double seconds); // you moved it by hand
    void clipsChanged();

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
    QList<TimelineClip> layoutDrop(const QMimeData* mime, const QPoint& pos) const;
    double snapped(double sec, int ignore, double* moved = nullptr) const;
    double neighbourBefore(int clip) const;
    double neighbourAfter(int clip) const;

    // Edits
    void dragMove(const QPoint& pos);
    void dragTrim(const QPoint& pos, bool left);
    void pushUndo(const QList<TimelineClip>& state);
    void changed();

    void clampScroll();
    void keepPlayheadVisible();

    void drawRuler(QPainter& p);
    void drawTracks(QPainter& p);
    void drawClip(QPainter& p, const TimelineClip& clip, bool selected, bool ghost);
    void drawFilmstrip(QPainter& p, const TimelineClip& clip, const QRectF& r);
    void drawPlayhead(QPainter& p);

    QList<Track> m_tracks { { "Video 2", false }, { "Video 1", false }, { "Audio 1", true } };
    QList<TimelineClip> m_clips;
    QList<TimelineClip> m_ghosts; // where a drop would land
    int m_selected = -1;

    QList<QList<TimelineClip>> m_undo;
    QList<QList<TimelineClip>> m_redo;
    QList<TimelineClip> m_beforeDrag;

    double m_pixelsPerSecond = 50.0;
    double m_scrollX = 0.0;
    double m_playhead = 0.0;

    Drag m_drag = Drag::None;
    double m_grabOffset = 0.0; // where on the clip you grabbed it (seconds)

    FilmstripCache* m_filmstrip;

    static constexpr int RulerHeight = 28;
    static constexpr int TrackHeight = 64;
    static constexpr int HeaderWidth = 90;
};
