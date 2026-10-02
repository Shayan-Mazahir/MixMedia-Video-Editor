#include "TimelineWidget.h"
#include "FilmstripCache.h"
#include "MediaBin.h"
#include "WaveformCache.h"

#include <QDataStream>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
const QColor Background(0x17, 0x18, 0x1a);
const QColor RulerBg(0x22, 0x23, 0x26);
const QColor TrackBg(0x1c, 0x1d, 0x20);
const QColor HeaderBg(0x26, 0x27, 0x2b);
const QColor Lines(0x33, 0x35, 0x39);
const QColor Dim(0x80, 0x82, 0x86);
const QColor Accent(0x2f, 0xc6, 0xb4);
const QColor VideoClip(0x2a, 0x5d, 0x7a);
const QColor AudioClip(0x2e, 0x6b, 0x4f);
const QColor TitleClip(0x6a, 0x4c, 0x9c);
const QColor Playhead(0xff, 0x4d, 0x4d);

constexpr double MinClipSeconds = 0.1;
constexpr double MinZoom = 0.02;   // pixels per second, zoomed way out
constexpr double MaxZoom = 600.0;  // zoomed way in
constexpr int SnapPixels = 8;
constexpr int EdgeGrabPixels = 6;
constexpr int MaxUndo = 100;
constexpr double Infinity = std::numeric_limits<double>::infinity();

QString formatTime(double seconds)
{
    int total = int(seconds);
    if (total >= 3600)
        return QString::asprintf("%d:%02d:%02d", total / 3600, (total / 60) % 60, total % 60);
    return QString::asprintf("%02d:%02d", total / 60, total % 60);
}

bool sameLayout(const QList<TimelineClip>& a, const QList<TimelineClip>& b)
{
    if (a.size() != b.size())
        return false;
    for (int i = 0; i < a.size(); ++i) {
        if (a[i].start != b[i].start || a[i].in != b[i].in
            || a[i].duration != b[i].duration || a[i].track != b[i].track)
            return false;
    }
    return true;
}
} // namespace

TimelineWidget::TimelineWidget(QWidget* parent)
    : QWidget(parent)
    , m_filmstrip(new FilmstripCache(this))
    , m_waveforms(new WaveformCache(this))
{
    setMinimumHeight(RulerHeight + TrackHeight * 2);
    setAcceptDrops(true);
    setMouseTracking(true); // so the cursor can change over clip edges
    setFocusPolicy(Qt::ClickFocus);
    connect(m_filmstrip, &FilmstripCache::tileReady, this, qOverload<>(&QWidget::update));
    connect(m_waveforms, &WaveformCache::ready, this, qOverload<>(&QWidget::update));
}

QSize TimelineWidget::sizeHint() const
{
    return { 800, RulerHeight + TrackHeight * int(m_tracks.size()) + 20 };
}

QList<RenderClip> TimelineWidget::renderClips(const std::function<QString(const TimelineClip&)>& titleImage) const
{
    QList<RenderClip> out;
    for (const TimelineClip& c : m_clips) {
        RenderClip r;
        r.path = c.isTitle() && titleImage ? titleImage(c) : c.path;
        r.start = c.start;
        r.in = c.isTitle() ? 0.0 : c.in;
        r.duration = c.duration;
        r.video = c.showsVideo() && !m_tracks[c.track].audio;
        r.audio = c.playsAudio();
        r.volume = c.volume;
        r.fadeIn = c.fadeIn;
        r.fadeOut = c.fadeOut;
        // Tracks higher up the list sit on top in the picture
        r.layer = int(m_tracks.size()) - c.track;
        if (!r.path.isEmpty())
            out << r;
    }
    return out;
}

void TimelineWidget::setClips(const QList<TimelineClip>& clips)
{
    m_clips = clips;
    m_undo.clear();
    m_redo.clear();
    select(-1);
    zoomToFit();
    changed();
}

void TimelineWidget::select(int index)
{
    if (index == m_selected)
        return;
    m_selected = index;
    emit selectionChanged(index);
}

void TimelineWidget::updateClip(int index, const TimelineClip& clip, const QString& what)
{
    if (index < 0 || index >= m_clips.size())
        return;

    // Lots of little changes to the same thing (like dragging a slider) = one undo step
    QString key = QString::number(index) + ':' + what;
    bool sameEdit = key == m_lastEdit && m_lastEditClock.isValid() && m_lastEditClock.elapsed() < 1500;
    if (!sameEdit)
        pushUndo(m_clips);
    m_lastEdit = key;
    m_lastEditClock.restart();

    m_clips[index] = clip;
    changed();
}

void TimelineWidget::detachAudio()
{
    if (m_selected < 0)
        return;
    TimelineClip& video = m_clips[m_selected];
    if (video.isTitle() || !video.hasAudio || !video.showsVideo() || !video.audioOn)
        return;

    // The sound gets its own clip on the first audio track with room for it
    TimelineClip sound = video;
    sound.videoOn = false;
    sound.thumb = QPixmap();
    sound.track = -1;
    for (int t = 0; t < m_tracks.size() && sound.track < 0; ++t) {
        if (!m_tracks[t].audio)
            continue;
        if (freeStart(m_clips, t, video.start, video.duration) == video.start)
            sound.track = t;
    }
    if (sound.track < 0)
        return; // no room anywhere, leave things alone

    pushUndo(m_clips);
    m_clips[m_selected].audioOn = false;
    m_clips << sound;
    select(int(m_clips.size()) - 1);
    changed();
}

void TimelineWidget::addTitle()
{
    TimelineClip title;
    title.kind = TimelineClip::Kind::Title;
    title.name = "Title";
    title.duration = 5.0;
    title.hasVideo = true;
    title.track = 0; // top video track, so it sits over everything
    title.start = freeStart(m_clips, title.track, m_playhead, title.duration);

    pushUndo(m_clips);
    m_clips << title;
    select(int(m_clips.size()) - 1);
    changed();
}

double TimelineWidget::duration() const
{
    double end = 0.0;
    for (const TimelineClip& c : m_clips)
        end = std::max(end, c.end());
    return end;
}

void TimelineWidget::setPlayhead(double sec)
{
    m_playhead = std::max(0.0, sec);
    keepPlayheadVisible();
    update();
}

void TimelineWidget::appendClips(const QList<TimelineClip>& clips)
{
    if (clips.isEmpty())
        return;
    pushUndo(m_clips);
    bool wasEmpty = m_clips.isEmpty();

    for (TimelineClip clip : clips) {
        clip.track = pickTrack(1, clip.audioOnly());
        if (clip.track < 0)
            continue;
        double end = 0.0;
        for (const TimelineClip& c : m_clips)
            if (c.track == clip.track)
                end = std::max(end, c.end());
        clip.start = end;
        m_clips << clip;
    }
    select(int(m_clips.size()) - 1);
    if (wasEmpty)
        zoomToFit();
    changed();
}

// ---- Coordinates ----

double TimelineWidget::secToX(double sec) const
{
    return HeaderWidth + sec * m_pixelsPerSecond - m_scrollX;
}

double TimelineWidget::xToSec(double x) const
{
    return (x - HeaderWidth + m_scrollX) / m_pixelsPerSecond;
}

QRect TimelineWidget::contentRect() const
{
    return QRect(HeaderWidth, 0, width() - HeaderWidth, height());
}

QRect TimelineWidget::trackRect(int track) const
{
    return QRect(HeaderWidth, RulerHeight + track * TrackHeight, width() - HeaderWidth, TrackHeight);
}

QRectF TimelineWidget::clipRect(const TimelineClip& clip) const
{
    QRect t = trackRect(clip.track);
    return QRectF(secToX(clip.start), t.top() + 4, clip.duration * m_pixelsPerSecond, t.height() - 8);
}

int TimelineWidget::trackAt(int y) const
{
    int i = (y - RulerHeight) / TrackHeight;
    if (y < RulerHeight || i >= m_tracks.size())
        return -1;
    return i;
}

int TimelineWidget::clipAt(const QPoint& pos) const
{
    for (int i = int(m_clips.size()) - 1; i >= 0; --i)
        if (clipRect(m_clips[i]).contains(pos))
            return i;
    return -1;
}

TimelineWidget::Edge TimelineWidget::edgeAt(int clip, const QPoint& pos) const
{
    if (clip < 0)
        return Edge::None;
    QRectF r = clipRect(m_clips[clip]);
    // Tiny clips: keep the middle grabbable so you can still move them
    double grab = std::min<double>(EdgeGrabPixels, r.width() / 3);
    if (pos.x() <= r.left() + grab)
        return Edge::Left;
    if (pos.x() >= r.right() - grab)
        return Edge::Right;
    return Edge::None;
}

// ---- Placement ----

int TimelineWidget::pickTrack(int wanted, bool audioOnly) const
{
    if (wanted >= 0 && wanted < m_tracks.size() && m_tracks[wanted].audio == audioOnly)
        return wanted;

    // Wrong kind of track - find the closest one that fits
    int best = -1;
    for (int i = 0; i < m_tracks.size(); ++i) {
        if (m_tracks[i].audio != audioOnly)
            continue;
        if (best < 0 || std::abs(i - wanted) < std::abs(best - wanted))
            best = i;
    }
    return best;
}

double TimelineWidget::freeStart(const QList<TimelineClip>& clips, int track,
                                 double start, double duration, int ignore)
{
    // Keep nudging right until we stop bumping into other clips
    bool moved = true;
    while (moved) {
        moved = false;
        for (int i = 0; i < clips.size(); ++i) {
            const TimelineClip& c = clips[i];
            if (i == ignore || c.track != track)
                continue;
            if (start < c.end() - 1e-9 && start + duration > c.start + 1e-9) {
                start = c.end();
                moved = true;
            }
        }
    }
    return start;
}

QList<TimelineClip> TimelineWidget::layoutDrop(const QMimeData* mime, const QPoint& pos) const
{
    QList<TimelineClip> placed;
    if (!mime->hasFormat(MediaBin::MimeType))
        return placed;

    QByteArray data = mime->data(MediaBin::MimeType);
    QDataStream in(&data, QIODevice::ReadOnly);
    qint32 count = 0;
    in >> count;

    QList<TimelineClip> all = m_clips;
    int wantedTrack = trackAt(pos.y());
    double start = std::max(0.0, xToSec(pos.x()));

    for (int n = 0; n < count && in.status() == QDataStream::Ok; ++n) {
        TimelineClip clip;
        in >> clip;
        clip.track = pickTrack(wantedTrack, clip.audioOnly());
        if (clip.track < 0)
            continue;
        clip.start = freeStart(all, clip.track, start, clip.duration);

        all << clip;
        placed << clip;
        start = clip.end(); // several clips at once? line them up one after another
    }
    return placed;
}

double TimelineWidget::snapped(double sec, int ignore, double* moved) const
{
    // Things worth sticking to: the start, the playhead, and other clips' edges
    double best = sec;
    double bestDist = SnapPixels / m_pixelsPerSecond;
    auto consider = [&](double point) {
        double d = std::abs(point - sec);
        if (d < bestDist) {
            bestDist = d;
            best = point;
        }
    };
    consider(0.0);
    consider(m_playhead);
    for (int i = 0; i < m_clips.size(); ++i) {
        if (i == ignore || m_dragPartners.contains(i))
            continue;
        consider(m_clips[i].start);
        consider(m_clips[i].end());
    }
    if (moved)
        *moved = std::abs(best - sec);
    return best;
}

double TimelineWidget::neighbourBefore(int clip) const
{
    const TimelineClip& me = m_clips[clip];
    double limit = 0.0;
    for (int i = 0; i < m_clips.size(); ++i)
        if (i != clip && m_clips[i].track == me.track && m_clips[i].start < me.start)
            limit = std::max(limit, m_clips[i].end());
    return limit;
}

double TimelineWidget::neighbourAfter(int clip) const
{
    const TimelineClip& me = m_clips[clip];
    double limit = Infinity;
    for (int i = 0; i < m_clips.size(); ++i)
        if (i != clip && m_clips[i].track == me.track && m_clips[i].start > me.start)
            limit = std::min(limit, m_clips[i].start);
    return limit;
}

// ---- Edits ----

void TimelineWidget::dragMove(const QPoint& pos)
{
    TimelineClip& clip = m_clips[m_selected];
    double start = std::max(0.0, xToSec(pos.x()) - m_grabOffset);

    // Snap whichever end of the clip is closest to something
    double startDist = 0, endDist = 0;
    double snapStart = snapped(start, m_selected, &startDist);
    double snapEnd = snapped(start + clip.duration, m_selected, &endDist);
    bool startSnaps = snapStart != start;
    bool endSnaps = snapEnd != start + clip.duration;
    if (startSnaps && (!endSnaps || startDist <= endDist))
        start = snapStart;
    else if (endSnaps)
        start = snapEnd - clip.duration;
    clip.start = std::max(0.0, start);

    int wanted = trackAt(pos.y());
    if (wanted >= 0) {
        int track = pickTrack(wanted, clip.audioOnly());
        if (track >= 0)
            clip.track = track;
    }
    syncPartners();
}

void TimelineWidget::dragTrim(const QPoint& pos, bool left)
{
    TimelineClip& c = m_clips[m_selected];
    double t = snapped(xToSec(pos.x()), m_selected);

    if (left) {
        // Can't go past the clip before it, or before the start of the file
        double lo = std::max(0.0, neighbourBefore(m_selected));
        if (!c.isStill())
            lo = std::max(lo, c.start - c.in);
        double hi = c.end() - MinClipSeconds;
        t = std::max(lo, std::min(t, hi));

        double delta = t - c.start;
        if (!c.isStill())
            c.in += delta;
        c.start = t;
        c.duration -= delta;
    } else {
        // Can't go past the next clip, or past the end of the file
        double lo = c.start + MinClipSeconds;
        double hi = neighbourAfter(m_selected);
        if (!c.isStill())
            hi = std::min(hi, c.start + c.sourceDuration - c.in);
        t = std::max(lo, std::min(t, hi));
        c.duration = t - c.start;
    }
    syncPartners();
}

void TimelineWidget::syncPartners()
{
    // Whatever happened to the clip you're dragging happens to its partners too
    const TimelineClip& me = m_clips[m_selected];
    for (int i : m_dragPartners) {
        m_clips[i].start = me.start;
        m_clips[i].in = me.in;
        m_clips[i].duration = me.duration;
    }
}

void TimelineWidget::splitAtPlayhead()
{
    auto inside = [this](const TimelineClip& c) {
        return m_playhead > c.start + 0.01 && m_playhead < c.end() - 0.01;
    };

    // The selected clip (and its partner) if the playhead's on it, otherwise everything under the playhead
    QList<int> targets;
    if (m_selected >= 0 && inside(m_clips[m_selected])) {
        targets = partnersOf(m_selected);
    } else {
        for (int i = 0; i < m_clips.size(); ++i)
            if (inside(m_clips[i]))
                targets << i;
    }
    if (targets.isEmpty())
        return;

    pushUndo(m_clips);
    int firstNew = int(m_clips.size());
    for (int i : targets) {
        TimelineClip& left = m_clips[i];
        TimelineClip right = left;
        right.start = m_playhead;
        right.in = left.isStill() ? 0.0 : left.in + (m_playhead - left.start);
        right.duration = left.end() - m_playhead;
        left.duration = m_playhead - left.start;
        // Fades belong to the outer ends, not the new cut in the middle
        right.fadeIn = 0.0;
        left.fadeOut = 0.0;
        m_clips << right;
    }
    select(firstNew); // the right-hand piece, ready to delete or move
    changed();
}

QList<int> TimelineWidget::partnersOf(int index) const
{
    // A clip and its detached sound share a file and line up exactly. They travel together.
    QList<int> out { index };
    const TimelineClip& me = m_clips[index];
    if (me.isTitle())
        return out;
    for (int i = 0; i < m_clips.size(); ++i) {
        const TimelineClip& c = m_clips[i];
        if (i != index && c.path == me.path && std::abs(c.start - me.start) < 1e-6
            && std::abs(c.in - me.in) < 1e-6 && std::abs(c.duration - me.duration) < 1e-6)
            out << i;
    }
    return out;
}

void TimelineWidget::deleteSelected(bool closeGap)
{
    if (m_selected < 0)
        return;
    pushUndo(m_clips);

    QList<int> doomed = partnersOf(m_selected);
    std::sort(doomed.begin(), doomed.end(), std::greater<int>()); // back to front so indexes stay valid
    for (int i : doomed) {
        TimelineClip gone = m_clips.takeAt(i);
        if (!closeGap)
            continue;
        // Slide everything after it on the same track left, so there's no hole to fix by hand
        for (TimelineClip& c : m_clips)
            if (c.track == gone.track && c.start >= gone.end() - 1e-6)
                c.start = std::max(0.0, c.start - gone.duration);
    }
    select(-1);
    changed();
}

void TimelineWidget::deleteSelectedKeepGap()
{
    deleteSelected(false);
}

void TimelineWidget::undo()
{
    if (m_undo.isEmpty())
        return;
    m_redo << m_clips;
    m_clips = m_undo.takeLast();
    select(-1);
    changed();
}

void TimelineWidget::redo()
{
    if (m_redo.isEmpty())
        return;
    m_undo << m_clips;
    m_clips = m_redo.takeLast();
    select(-1);
    changed();
}

void TimelineWidget::pushUndo(const QList<TimelineClip>& state)
{
    m_undo << state;
    if (m_undo.size() > MaxUndo)
        m_undo.removeFirst();
    m_redo.clear();
}

void TimelineWidget::changed()
{
    update();
    emit clipsChanged();
}

// ---- Zoom & scroll ----

void TimelineWidget::zoomToFit()
{
    double total = duration() > 0 ? duration() : 60.0;
    double visible = std::max(100, width() - HeaderWidth - 30);
    m_pixelsPerSecond = std::clamp(visible / total, MinZoom, MaxZoom);
    m_scrollX = 0;
    update();
}

void TimelineWidget::zoomBy(double factor)
{
    // Keep the playhead in the same spot if we can see it, otherwise the left edge
    double anchorX = secToX(m_playhead);
    if (anchorX < HeaderWidth || anchorX > width())
        anchorX = HeaderWidth;
    double anchor = xToSec(anchorX);
    m_pixelsPerSecond = std::clamp(m_pixelsPerSecond * factor, MinZoom, MaxZoom);
    m_scrollX = anchor * m_pixelsPerSecond - (anchorX - HeaderWidth);
    clampScroll();
    update();
}

void TimelineWidget::clampScroll()
{
    double visible = width() - HeaderWidth;
    double maxScroll = std::max(0.0, duration() * m_pixelsPerSecond - visible * 0.5);
    m_scrollX = std::clamp(m_scrollX, 0.0, maxScroll);
}

void TimelineWidget::keepPlayheadVisible()
{
    double x = secToX(m_playhead);
    double visible = width() - HeaderWidth;
    if (x > width() - 20 || x < HeaderWidth)
        m_scrollX = std::max(0.0, m_playhead * m_pixelsPerSecond - visible * 0.1);
}

// ---- Drag & drop from the media panel ----

void TimelineWidget::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasFormat(MediaBin::MimeType))
        event->acceptProposedAction();
}

void TimelineWidget::dragMoveEvent(QDragMoveEvent* event)
{
    m_ghosts = layoutDrop(event->mimeData(), event->position().toPoint());
    if (m_ghosts.isEmpty())
        event->ignore();
    else
        event->acceptProposedAction();
    update();
}

void TimelineWidget::dragLeaveEvent(QDragLeaveEvent*)
{
    m_ghosts.clear();
    update();
}

void TimelineWidget::dropEvent(QDropEvent* event)
{
    m_ghosts.clear();
    QList<TimelineClip> placed = layoutDrop(event->mimeData(), event->position().toPoint());
    if (placed.isEmpty()) {
        update();
        return;
    }

    pushUndo(m_clips);
    bool wasEmpty = m_clips.isEmpty();
    m_clips << placed;
    select(int(m_clips.size()) - 1);
    event->acceptProposedAction();
    if (wasEmpty)
        zoomToFit(); // first clip in? show the whole thing
    changed();
}

// ---- Mouse & keyboard ----

void TimelineWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;
    QPoint pos = event->position().toPoint();
    if (pos.x() < HeaderWidth)
        return;

    int hit = pos.y() >= RulerHeight ? clipAt(pos) : -1;
    if (hit >= 0) {
        select(hit);
        m_beforeDrag = m_clips;
        m_dragPartners = partnersOf(hit);
        m_dragPartners.removeOne(hit);
        switch (edgeAt(hit, pos)) {
        case Edge::Left: m_drag = Drag::TrimLeft; break;
        case Edge::Right: m_drag = Drag::TrimRight; break;
        case Edge::None:
            m_drag = Drag::Move;
            m_grabOffset = xToSec(pos.x()) - m_clips[hit].start;
            setCursor(Qt::ClosedHandCursor);
            break;
        }
    } else {
        select(-1);
        m_drag = Drag::Playhead;
        m_playhead = std::max(0.0, xToSec(pos.x()));
        emit playheadMoved(m_playhead);
    }
    update();
}

void TimelineWidget::mouseMoveEvent(QMouseEvent* event)
{
    QPoint pos = event->position().toPoint();

    switch (m_drag) {
    case Drag::None: {
        // Just hovering: show the resize arrows over clip edges
        int hit = pos.y() >= RulerHeight && pos.x() >= HeaderWidth ? clipAt(pos) : -1;
        if (edgeAt(hit, pos) != Edge::None)
            setCursor(Qt::SizeHorCursor);
        else
            unsetCursor();
        return;
    }
    case Drag::Playhead:
        m_playhead = std::max(0.0, xToSec(pos.x()));
        emit playheadMoved(m_playhead);
        break;
    case Drag::Move:
        dragMove(pos);
        emit clipsChanged();
        break;
    case Drag::TrimLeft:
    case Drag::TrimRight:
        dragTrim(pos, m_drag == Drag::TrimLeft);
        emit clipsChanged();
        break;
    }
    update();
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent*)
{
    if (m_drag == Drag::Move && m_selected >= 0) {
        // Landed on top of another clip? Slide over to the next free spot.
        TimelineClip& clip = m_clips[m_selected];
        clip.start = freeStart(m_clips, clip.track, clip.start, clip.duration, m_selected);
    }
    if (m_selected >= 0 && m_drag != Drag::None && m_drag != Drag::Playhead)
        syncPartners();
    m_dragPartners.clear();

    bool edited = m_drag == Drag::Move || m_drag == Drag::TrimLeft || m_drag == Drag::TrimRight;
    m_drag = Drag::None;
    unsetCursor();

    if (edited && !sameLayout(m_clips, m_beforeDrag)) {
        pushUndo(m_beforeDrag);
        changed();
    } else {
        update();
    }
}

void TimelineWidget::wheelEvent(QWheelEvent* event)
{
    double x = event->position().x();

    if (event->modifiers() & Qt::ControlModifier) {
        // Zoom around the mouse so the spot under it stays put
        double anchor = xToSec(x);
        double factor = event->angleDelta().y() > 0 ? 1.25 : 0.8;
        m_pixelsPerSecond = std::clamp(m_pixelsPerSecond * factor, MinZoom, MaxZoom);
        m_scrollX = anchor * m_pixelsPerSecond - (x - HeaderWidth);
    } else {
        int delta = event->angleDelta().x() != 0 ? event->angleDelta().x() : event->angleDelta().y();
        m_scrollX -= delta;
    }
    clampScroll();
    update();
    event->accept();
}

void TimelineWidget::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        // Shift = leave the gap where the clip was
        deleteSelected(!(event->modifiers() & Qt::ShiftModifier));
        return;
    }
    QWidget::keyPressEvent(event);
}

void TimelineWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    clampScroll();
}

// ---- Drawing ----

void TimelineWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), Background);

    drawTracks(p);

    p.save();
    p.setClipRect(contentRect());
    for (int i = 0; i < m_clips.size(); ++i)
        drawClip(p, m_clips[i], i == m_selected, false);
    for (const TimelineClip& ghost : m_ghosts)
        drawClip(p, ghost, false, true);
    p.restore();

    drawRuler(p);
    drawPlayhead(p);
}

void TimelineWidget::drawRuler(QPainter& p)
{
    p.fillRect(0, 0, width(), RulerHeight, RulerBg);
    p.setPen(Lines);
    p.drawLine(0, RulerHeight - 1, width(), RulerHeight - 1);

    // Pick a label spacing that doesn't get cramped when zoomed out
    static const double steps[] = { 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 900, 1800, 3600 };
    double major = steps[std::size(steps) - 1];
    for (double s : steps) {
        if (s * m_pixelsPerSecond >= 80) {
            major = s;
            break;
        }
    }
    double minor = major / 5.0;

    QFont font = p.font();
    font.setPixelSize(10);
    p.setFont(font);

    long first = std::max(0L, long(std::floor(xToSec(HeaderWidth) / minor)));
    long last = long(std::ceil(xToSec(width()) / minor));
    for (long n = first; n <= last; ++n) {
        double s = n * minor;
        int x = int(secToX(s));
        bool isMajor = n % 5 == 0;
        p.setPen(isMajor ? Dim : Lines);
        p.drawLine(x, RulerHeight - (isMajor ? 12 : 6), x, RulerHeight - 1);
        if (isMajor)
            p.drawText(x + 3, RulerHeight - 14, formatTime(s));
    }

    p.fillRect(0, 0, HeaderWidth, RulerHeight, RulerBg);
}

void TimelineWidget::drawTracks(QPainter& p)
{
    QFont font = p.font();
    font.setPixelSize(11);
    p.setFont(font);

    for (int i = 0; i < m_tracks.size(); ++i) {
        QRect t = trackRect(i);
        p.fillRect(t, TrackBg);
        p.fillRect(0, t.top(), HeaderWidth, TrackHeight, HeaderBg);

        p.setPen(Lines);
        p.drawLine(0, t.bottom(), width(), t.bottom());
        p.drawLine(HeaderWidth - 1, t.top(), HeaderWidth - 1, t.bottom());

        p.setPen(Dim);
        p.drawText(QRect(10, t.top(), HeaderWidth - 10, TrackHeight), Qt::AlignVCenter, m_tracks[i].name);
    }

    int bottom = RulerHeight + int(m_tracks.size()) * TrackHeight;
    if (m_clips.isEmpty() && bottom < height()) {
        p.setPen(Dim);
        p.drawText(QRect(HeaderWidth, bottom, width() - HeaderWidth, height() - bottom),
                   Qt::AlignCenter, "Drag clips here (or double-click them in Media) to start editing");
    }
}

void TimelineWidget::drawFilmstrip(QPainter& p, const TimelineClip& clip, const QRectF& r)
{
    double aspect = clip.width > 0 && clip.height > 0 ? double(clip.width) / clip.height : 16.0 / 9.0;
    double tileW = r.height() * aspect;
    double secPerTile = tileW / m_pixelsPerSecond;

    // Round the times off a bit so scrolling and trimming can reuse tiles we already have
    static const double grid[] = { 0.25, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 1200, 1800 };
    double q = grid[std::size(grid) - 1];
    for (double g : grid) {
        if (g >= secPerTile * 0.5) {
            q = g;
            break;
        }
    }

    int first = std::max(0, int(std::floor((HeaderWidth - r.left()) / tileW)));
    for (int i = first;; ++i) {
        double x = r.left() + i * tileW;
        if (x >= r.right() || x > width())
            break;
        QRectF tileRect(x, r.top(), tileW, r.height());

        double src = clip.isStill() ? 0.0 : std::floor((clip.in + i * secPerTile) / q) * q;
        QImage img = m_filmstrip->tile(clip.path, src);
        if (!img.isNull())
            p.drawImage(tileRect, img);
        else if (!clip.thumb.isNull())
            p.drawPixmap(tileRect, clip.thumb, QRectF(clip.thumb.rect()));
    }
}

void TimelineWidget::drawClip(QPainter& p, const TimelineClip& clip, bool selected, bool ghost)
{
    QRectF r = clipRect(clip);
    if (r.right() < HeaderWidth || r.left() > width())
        return;

    p.save();
    if (ghost)
        p.setOpacity(0.45);

    QPainterPath shape;
    shape.addRoundedRect(r, 4, 4);
    p.fillPath(shape, clip.isTitle() ? TitleClip : clip.audioOnly() ? AudioClip : VideoClip);
    p.setClipPath(shape, Qt::IntersectClip);

    if (clip.isTitle()) {
        // Show the actual words, so you can tell titles apart at a glance
        QFont big = p.font();
        big.setPixelSize(int(r.height() * 0.38));
        big.setBold(true);
        p.setFont(big);
        p.setPen(QColor(255, 255, 255, 200));
        double left = std::max(r.left(), double(HeaderWidth)) + 8;
        p.drawText(QRectF(left, r.top() + 14, r.right() - left, r.height() - 14),
                   Qt::AlignLeft | Qt::AlignVCenter, clip.title.text.simplified());
    } else if (clip.showsVideo() && !m_tracks[clip.track].audio) {
        drawFilmstrip(p, clip, r);
    } else if (clip.playsAudio()) {
        drawWaveform(p, clip, r);
    }
    drawFades(p, clip, r);

    // Name tag in the corner, on a dark pill so it's readable over any picture
    QFont font = p.font();
    font.setPixelSize(11);
    font.setBold(false);
    p.setFont(font);
    QString icon = clip.isTitle() ? QStringLiteral("T  ") : clip.audioOnly() ? QStringLiteral("♪ ") : QString();
    QString muted = clip.hasAudio && !clip.audioOn && clip.showsVideo() ? QStringLiteral("  ·  no sound") : QString();
    QString label = icon + clip.name + QStringLiteral("  ·  ") + formatTime(clip.duration) + muted;
    double visibleLeft = std::max(r.left(), double(HeaderWidth));
    int maxText = int(r.right() - visibleLeft - 14);
    if (maxText > 20) {
        QString text = p.fontMetrics().elidedText(label, Qt::ElideRight, maxText);
        QRectF tag(visibleLeft + 4, r.top() + 4, p.fontMetrics().horizontalAdvance(text) + 8, 16);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 150));
        p.drawRoundedRect(tag, 3, 3);
        p.setPen(Qt::white);
        p.drawText(tag, Qt::AlignCenter, text);
    }

    p.setClipping(false);
    p.setBrush(Qt::NoBrush);
    if (selected || ghost) {
        p.setPen(QPen(ghost ? Qt::white : Accent, 2));
        p.drawRoundedRect(r.adjusted(1, 1, -1, -1), 4, 4);
        if (selected) {
            // Little handles on the edges, so it's obvious you can trim
            p.setBrush(Accent);
            p.setPen(Qt::NoPen);
            p.drawRoundedRect(QRectF(r.left(), r.top() + r.height() / 2 - 10, 4, 20), 2, 2);
            p.drawRoundedRect(QRectF(r.right() - 4, r.top() + r.height() / 2 - 10, 4, 20), 2, 2);
        }
    } else {
        p.setPen(QPen(QColor(0, 0, 0, 120), 1));
        p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
    }
    p.restore();
}

void TimelineWidget::drawWaveform(QPainter& p, const TimelineClip& clip, const QRectF& r)
{
    QVector<float> peaks = m_waveforms->peaks(clip.path);
    if (peaks.size() < 2)
        return; // still working it out

    // One line per pixel column, mirrored around the middle like most editors do
    double mid = r.center().y() + 6; // leave the top for the name tag
    double half = (r.height() - 14) / 2;
    int from = int(std::max(r.left(), double(HeaderWidth)));
    int to = int(std::min(r.right(), double(width())));
    double secPerPixel = 1.0 / m_pixelsPerSecond;

    p.setPen(QColor(170, 235, 200, 190));
    for (int x = from; x < to; ++x) {
        double src = clip.in + (x - r.left()) * secPerPixel;
        int a = int(src * WaveformCache::PerSecond);
        int b = std::max(a + 1, int((src + secPerPixel) * WaveformCache::PerSecond));
        float peak = 0.0f;
        for (int i = std::max(a, 0); i < std::min<int>(b, peaks.size()); ++i)
            peak = std::max(peak, peaks[i]);
        double hgt = std::max(1.0, peak * clip.volume * half);
        p.drawLine(QPointF(x, mid - hgt), QPointF(x, mid + hgt));
    }
}

void TimelineWidget::drawFades(QPainter& p, const TimelineClip& clip, const QRectF& r)
{
    // Shaded ramps in the corners, like Filmora shows them
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 120));
    if (clip.fadeIn > 0) {
        double w = std::min(clip.fadeIn * m_pixelsPerSecond, r.width());
        const QPointF ramp[] = { r.topLeft(), r.bottomLeft(), { r.left() + w, r.top() } };
        p.drawPolygon(ramp, 3);
    }
    if (clip.fadeOut > 0) {
        double w = std::min(clip.fadeOut * m_pixelsPerSecond, r.width());
        const QPointF ramp[] = { r.topRight(), r.bottomRight(), { r.right() - w, r.top() } };
        p.drawPolygon(ramp, 3);
    }
}

void TimelineWidget::drawPlayhead(QPainter& p)
{
    double x = secToX(m_playhead);
    if (x < HeaderWidth || x > width())
        return;

    p.setPen(QPen(Playhead, 2));
    p.drawLine(QPointF(x, 0), QPointF(x, height()));
    p.setBrush(Playhead);
    p.setPen(Qt::NoPen);
    const QPointF head[] = { { x - 6, 0 }, { x + 6, 0 }, { x, 8 } };
    p.drawPolygon(head, 3);
}
