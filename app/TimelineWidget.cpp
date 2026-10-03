// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#include "TimelineWidget.h"
#include "FilmstripCache.h"
#include "ClipPresets.h"
#include "MediaBin.h"
#include "WaveformCache.h"

#include <QDataStream>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QContextMenuEvent>
#include <QMenu>
#include <QScrollBar>

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
const QColor EffectClip(0x8a, 0x66, 0x1c);
const QColor TransitionClip(0x3d, 0x6f, 0xa8);
const QColor FxTrackBg(0x1f, 0x1c, 0x19);
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
    connect(m_filmstrip, &FilmstripCache::tileReady, this, [this] { invalidate(); });
    connect(m_waveforms, &WaveformCache::ready, this, [this] { invalidate(); });

    // Scrollbar along the bottom, for people who'd rather drag than scroll
    m_scrollBar = new QScrollBar(Qt::Horizontal, this);
    connect(m_scrollBar, &QScrollBar::valueChanged, this, [this](int value) {
        if (value != int(m_scrollX)) {
            m_scrollX = value;
            invalidate();
        }
    });
}

QSize TimelineWidget::sizeHint() const
{
    return { 800, tracksBottom() + 20 };
}

QList<TimelineTrack> TimelineWidget::defaultTracks()
{
    using K = TimelineTrack::Kind;
    return { { "FX 2", K::Fx }, { "FX 1", K::Fx }, { "Video 2", K::Video },
             { "Video 1", K::Video }, { "Audio 1", K::Audio }, { "Audio 2", K::Audio } };
}

QList<RenderClip> TimelineWidget::renderClips(const std::function<QString(const TimelineClip&)>& titleImage) const
{
    // Transition blocks aren't drawn themselves: they tell the clip overlapping the end of the
    // one before to blend in from it (and its detached sound to crossfade)
    QHash<int, const TimelineClip*> transitionInto;
    for (const TimelineClip& t : m_clips) {
        int before = -1, after = -1;
        if (t.isTransition() && transitionPair(t, &before, &after))
            for (int i : partnersOf(after))
                transitionInto.insert(i, &t);
    }

    QList<RenderClip> out;
    for (int i = 0; i < m_clips.size(); ++i) {
        const TimelineClip& c = m_clips[i];
        if (c.isTransition()) {
            // Not on a cut: it plays on everything below it instead, like an effect block
            int part = transitionPart(c);
            if (part < 0)
                continue;
            RenderClip r;
            r.kind = VE_CLIP_TRANSITION;
            r.start = c.start;
            r.duration = c.duration;
            r.audio = false;
            r.transition = c.transition;
            r.part = part;
            r.layer = int(m_tracks.size()) - c.track;
            out << r;
            continue;
        }
        RenderClip r;
        r.kind = c.isEffect() ? VE_CLIP_ADJUSTMENT : VE_CLIP_MEDIA;
        r.path = c.isTitle() && titleImage ? titleImage(c) : c.path;
        r.start = c.start;
        r.in = c.isTitle() || c.isEffect() ? 0.0 : c.in;
        r.duration = c.duration;
        r.video = c.isEffect() || (c.showsVideo() && m_tracks[c.track].kind != TimelineTrack::Kind::Audio);
        r.audio = c.playsAudio();
        r.volume = c.volume;
        r.fadeIn = c.fadeIn;
        r.fadeOut = c.fadeOut;
        r.speed = c.speed;
        r.opacity = c.opacity;
        r.scale = c.scale;
        r.posX = c.posX;
        r.posY = c.posY;
        r.look = c.look;
        r.brightness = c.brightness;
        r.contrast = c.contrast;
        r.saturation = c.saturation;
        r.temperature = c.temperature;
        r.blur = c.blur;
        r.sharpen = c.sharpen;
        r.vignette = c.vignette;
        if (const TimelineClip* t = transitionInto.value(i)) {
            r.transition = t->transition;
            r.transitionDuration = t->duration;
        }
        r.animIn = c.animIn;
        r.animInDuration = c.animInDuration;
        r.animOut = c.animOut;
        r.animOutDuration = c.animOutDuration;
        // Tracks higher up the list sit on top in the picture (and effects apply in that order)
        r.layer = int(m_tracks.size()) - c.track;
        if (!r.path.isEmpty() || c.isEffect())
            out << r;
    }
    return out;
}

void TimelineWidget::setClips(const QList<TimelineClip>& clips, const QList<TimelineTrack>& tracks)
{
    m_clips = clips;
    if (tracks.isEmpty()) {
        upgradeOldProject();
    } else {
        m_tracks = tracks;
    }
    m_undo.clear();
    m_redo.clear();
    select(-1);
    zoomToFit();
    changed();
}

void TimelineWidget::upgradeOldProject()
{
    // Older projects had no FX tracks: add them on top, and move every clip down to match
    m_tracks = defaultTracks();
    const int added = 2;
    for (TimelineClip& c : m_clips)
        c.track = std::min<int>(c.track + added, m_tracks.size() - 1);

    // ...and kept transitions on the clips themselves. They become overlap transitions now.
    struct Old { double cut; int type; double length; };
    QList<Old> old;
    for (TimelineClip& c : m_clips) {
        if (c.kind == TimelineClip::Kind::Media && c.transition != 0
            && m_tracks[c.track].kind == TimelineTrack::Kind::Video)
            old << Old { c.start, c.transition, c.transitionDuration };
        c.transition = 0;
    }
    // Last cut first, so the earlier cuts don't move while we work
    std::sort(old.begin(), old.end(), [](const Old& x, const Old& y) { return x.cut > y.cut; });
    for (const Old& o : old) {
        double cut = 0;
        if (nearestCut(o.cut, &cut) && std::abs(cut - o.cut) < 0.01)
            applyTransition(makeTransitionClip(o.type, o.length), cut, 1);
    }
}

void TimelineWidget::addTrack(TimelineTrack::Kind kind, int at)
{
    m_undo << Snapshot { m_clips, m_tracks };
    m_redo.clear();
    insertTrack(kind, at);
    changed();
}

int TimelineWidget::insertTrack(TimelineTrack::Kind kind, int at)
{
    using K = TimelineTrack::Kind;
    // Count how many of this kind there are, so it gets the next number
    int count = 0;
    for (const TimelineTrack& t : m_tracks)
        count += t.kind == kind;
    QString base = kind == K::Fx ? "FX" : kind == K::Video ? "Video" : "Audio";
    TimelineTrack track { QString("%1 %2").arg(base).arg(count + 1), kind };

    // New FX and video tracks go on top of their group, audio at the bottom of its group
    if (at < 0 || at > m_tracks.size()) {
        int first = -1, last = -1, lastFx = -1;
        for (int i = 0; i < m_tracks.size(); ++i) {
            if (m_tracks[i].kind == kind) {
                if (first < 0)
                    first = i;
                last = i;
            }
            if (m_tracks[i].kind == K::Fx)
                lastFx = i;
        }
        if (kind == K::Audio)
            at = last >= 0 ? last + 1 : int(m_tracks.size());
        else if (first >= 0)
            at = first;
        else
            at = kind == K::Fx ? 0 : lastFx + 1;
    }

    m_tracks.insert(at, track);
    for (TimelineClip& c : m_clips)
        if (c.track >= at)
            ++c.track;
    return at;
}

bool TimelineWidget::removeTrack(int index)
{
    if (index < 0 || index >= m_tracks.size())
        return false;
    for (const TimelineClip& c : m_clips)
        if (c.track == index)
            return false; // only empty tracks
    int sameKind = 0;
    for (const TimelineTrack& t : m_tracks)
        sameKind += t.kind == m_tracks[index].kind;
    if (sameKind <= 1)
        return false; // always keep at least one of each

    m_undo << Snapshot { m_clips, m_tracks };
    m_redo.clear();
    m_tracks.removeAt(index);
    for (TimelineClip& c : m_clips)
        if (c.track > index)
            --c.track;
    changed();
    return true;
}

void TimelineWidget::selectClip(int index)
{
    select(index >= 0 && index < m_clips.size() ? index : -1);
    invalidate();
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

    if (clip.isTransition() && m_clips[index].isTransition()) {
        // Its place comes from the overlap; a new length slides the clips to match
        double length = clip.duration;
        TimelineClip& block = m_clips[index];
        block.transition = clip.transition;
        block.name = clip.name;
        if (std::abs(length - block.duration) > 1e-6)
            resizeTransition(index, length);
    } else {
        m_clips[index] = clip;
    }
    changed();
}

void TimelineWidget::setTransition(int index, int type)
{
    if (index < 0 || index >= m_clips.size())
        return;

    // Already a transition into this clip? Change it (or take it away with "None")
    for (int i = 0; i < m_clips.size(); ++i) {
        int before = -1, after = -1;
        if (!m_clips[i].isTransition() || !transitionPair(m_clips[i], &before, &after) || after != index)
            continue;
        pushUndo(m_clips);
        if (type == 0) {
            undoOverlap(i);
            m_clips.removeAt(i);
            select(-1);
        } else {
            m_clips[i].transition = type;
            m_clips[i].name = transitionName(type);
            select(i);
        }
        changed();
        return;
    }
    if (type == 0 || !hasClipBefore(index))
        return;

    const QList<TimelineClip> before = m_clips;
    const QList<TimelineTrack> tracksBefore = m_tracks;
    if (!applyTransition(makeTransitionClip(type), m_clips[index].start, -1))
        return;
    m_undo << Snapshot { before, tracksBefore };
    m_redo.clear();
    select(int(m_clips.size()) - 1);
    changed();
}

bool TimelineWidget::isOnCut(const TimelineClip& transition) const
{
    int before = -1, after = -1;
    return transitionPair(transition, &before, &after);
}

int TimelineWidget::transitionPart(const TimelineClip& block) const
{
    if (isOnCut(block))
        return -1;
    // Lined up with the start of some footage (with nothing right before it)? It brings it in.
    // With the end (nothing right after)? It takes it out. Anywhere else it plays on the spot.
    auto footage = [this](const TimelineClip& c) {
        return c.kind == TimelineClip::Kind::Media && m_tracks[c.track].kind == TimelineTrack::Kind::Video;
    };
    auto touching = [&](int track, double at, bool endingThere) {
        for (const TimelineClip& o : m_clips)
            if (footage(o) && o.track == track && std::abs((endingThere ? o.end() : o.start) - at) < 0.01)
                return true;
        return false;
    };
    constexpr double near = 0.02;
    for (const TimelineClip& c : m_clips)
        if (footage(c) && std::abs(c.start - block.start) < near && !touching(c.track, c.start, true))
            return VE_PART_IN;
    for (const TimelineClip& c : m_clips)
        if (footage(c) && std::abs(c.end() - block.end()) < near && !touching(c.track, c.end(), false))
            return VE_PART_OUT;
    return VE_PART_THROUGH;
}

bool TimelineWidget::cutNear(double sec) const
{
    // Close enough on screen that it's obviously meant for that cut
    double cut = 0;
    return nearestCut(sec, &cut) && std::abs(cut - sec) * m_pixelsPerSecond < 40;
}

bool TimelineWidget::hasClipBefore(int index) const
{
    const TimelineClip& me = m_clips[index];
    for (int i = 0; i < m_clips.size(); ++i)
        if (i != index && m_clips[i].track == me.track && std::abs(m_clips[i].end() - me.start) < 0.01)
            return true;
    return false;
}

void TimelineWidget::setClipSpeed(int index, double speed)
{
    if (index < 0 || index >= m_clips.size())
        return;
    speed = std::clamp(speed, 0.1, 10.0);
    const TimelineClip before = m_clips[index];
    if (before.isStill() || before.isTitle() || std::abs(before.speed - speed) < 1e-9)
        return;

    // Same bit of the file, played faster or slower, so the clip gets shorter or longer
    double newDuration = std::min(before.sourceSpan() / speed, (before.sourceDuration - before.in) / speed);
    double change = newDuration - before.duration;

    pushUndo(m_clips);
    QList<int> group = partnersOf(index);
    QList<int> tracks;
    for (int i : group) {
        TimelineClip& c = m_clips[i];
        c.speed = speed;
        c.duration = newDuration;
        c.fadeIn = std::min(c.fadeIn, newDuration);
        c.fadeOut = std::min(c.fadeOut, newDuration);
        tracks << c.track;
    }
    // Slide whatever comes after along, so nothing overlaps (or leaves a gap)
    for (int i = 0; i < m_clips.size(); ++i) {
        TimelineClip& c = m_clips[i];
        if (!group.contains(i) && tracks.contains(c.track) && c.start >= before.end() - 1e-6)
            c.start = std::max(0.0, c.start + change);
    }
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
        if (m_tracks[t].kind != TimelineTrack::Kind::Audio)
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
    addAtPlayhead(title);
}

void TimelineWidget::addAtPlayhead(const TimelineClip& prototype)
{
    TimelineClip clip = prototype;
    const Snapshot before { m_clips, m_tracks };

    if (clip.isTransition() && cutNear(m_playhead)) {
        // Playhead on a cut: the clips overlap and blend across it. (Anywhere else it's
        // placed like an effect block and plays on the spot.)
        if (!applyTransition(clip, m_playhead, -1))
            return;
        m_undo << before;
        m_redo.clear();
        select(int(m_clips.size()) - 1);
        changed();
        return;
    }
    {
        // Right at the playhead, on the closest track of the right kind that's free there
        int home = pickTrack(homeTrack(clip), clip);
        if (home < 0)
            return;
        int best = -1;
        for (int i = 0; i < m_tracks.size(); ++i) {
            if (m_tracks[i].kind != m_tracks[home].kind || freeStart(m_clips, i, m_playhead, clip.duration) != m_playhead)
                continue;
            if (best < 0 || std::abs(i - home) < std::abs(best - home))
                best = i;
        }
        if (best < 0 && clip.belongsOnFx()) {
            // Things already stacked up there: a fresh FX track on top keeps it at the playhead
            best = insertTrack(TimelineTrack::Kind::Fx, -1);
        }
        if (best >= 0) {
            clip.track = best;
            clip.start = m_playhead;
        } else {
            clip.track = home;
            clip.start = freeStart(m_clips, home, m_playhead, clip.duration); // footage: next free spot
        }
    }
    m_undo << before; // one undo step, even if a track got added
    if (m_undo.size() > MaxUndo)
        m_undo.removeFirst();
    m_redo.clear();
    m_clips << clip;
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
    double scrollBefore = m_scrollX;
    keepPlayheadVisible();
    if (m_scrollX != scrollBefore)
        invalidate(); // the view moved, so everything shifts
    else
        update(); // just the playhead, the cached picture is still good
}

void TimelineWidget::invalidate()
{
    m_cacheDirty = true;
    syncScrollBar();
    update();
}

void TimelineWidget::syncScrollBar()
{
    double visible = width() - HeaderWidth;
    int maxScroll = int(std::max(0.0, duration() * m_pixelsPerSecond - visible * 0.5));
    QSignalBlocker quiet(m_scrollBar);
    m_scrollBar->setRange(0, maxScroll);
    m_scrollBar->setPageStep(int(visible));
    m_scrollBar->setSingleStep(int(visible / 20) + 1);
    m_scrollBar->setValue(int(m_scrollX));
    m_scrollBar->setGeometry(HeaderWidth, height() - 12, width() - HeaderWidth, 12);
    m_scrollBar->setVisible(maxScroll > 0);
}

QList<double> TimelineWidget::cutPoints() const
{
    QList<double> points { 0.0 };
    for (const TimelineClip& c : m_clips)
        points << c.start << c.end();
    std::sort(points.begin(), points.end());
    points.erase(std::unique(points.begin(), points.end(), [](double a, double b) { return std::abs(a - b) < 1e-6; }),
                 points.end());
    return points;
}

void TimelineWidget::appendClips(const QList<TimelineClip>& clips)
{
    if (clips.isEmpty())
        return;
    pushUndo(m_clips);
    bool wasEmpty = m_clips.isEmpty();

    for (TimelineClip clip : clips) {
        clip.track = pickTrack(homeTrack(clip), clip);
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

int TimelineWidget::trackHeight(int track) const
{
    return m_tracks[track].kind == TimelineTrack::Kind::Fx ? FxTrackHeight : TrackHeight;
}

int TimelineWidget::trackTop(int track) const
{
    int y = RulerHeight;
    for (int i = 0; i < track; ++i)
        y += trackHeight(i);
    return y;
}

int TimelineWidget::tracksBottom() const
{
    return trackTop(int(m_tracks.size()));
}

QRect TimelineWidget::trackRect(int track) const
{
    return QRect(HeaderWidth, trackTop(track), width() - HeaderWidth, trackHeight(track));
}

QRectF TimelineWidget::clipRect(const TimelineClip& clip) const
{
    QRect t = trackRect(clip.track);
    return QRectF(secToX(clip.start), t.top() + 4, clip.duration * m_pixelsPerSecond, t.height() - 8);
}

int TimelineWidget::trackAt(int y) const
{
    if (y < RulerHeight)
        return -1;
    for (int i = 0; i < m_tracks.size(); ++i)
        if (y < trackTop(i) + trackHeight(i))
            return i;
    return -1;
}

int TimelineWidget::homeTrack(const TimelineClip& clip) const
{
    // Where things go when you don't say: titles/effects on the lowest FX track, the rest by the main video
    if (clip.belongsOnFx()) {
        for (int i = int(m_tracks.size()) - 1; i >= 0; --i)
            if (m_tracks[i].kind == TimelineTrack::Kind::Fx)
                return i;
    }
    return mainVideoTrack();
}

int TimelineWidget::mainVideoTrack() const
{
    // The bottom video track: where footage goes by default
    int found = 0;
    for (int i = 0; i < m_tracks.size(); ++i)
        if (m_tracks[i].kind == TimelineTrack::Kind::Video)
            found = i;
    return found;
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

bool TimelineWidget::accepts(int track, const TimelineClip& clip) const
{
    using K = TimelineTrack::Kind;
    if (track < 0 || track >= m_tracks.size())
        return false;
    switch (m_tracks[track].kind) {
    case K::Fx: return clip.belongsOnFx();
    case K::Video: return (clip.showsVideo() && !clip.belongsOnFx()) || clip.isTitle(); // titles are fine here too
    case K::Audio: return clip.audioOnly();
    }
    return false;
}

int TimelineWidget::pickTrack(int wanted, const TimelineClip& clip) const
{
    if (accepts(wanted, clip))
        return wanted;

    // Wrong kind of track - find the closest one that fits
    int best = -1;
    for (int i = 0; i < m_tracks.size(); ++i) {
        if (!accepts(i, clip) || (clip.isTitle() && m_tracks[i].kind != TimelineTrack::Kind::Fx))
            continue; // (titles only go on video tracks if you put them there on purpose)
        if (best < 0 || std::abs(i - wanted) < std::abs(best - wanted))
            best = i;
    }
    return best;
}

bool TimelineWidget::nearestCut(double sec, double* cut, int* before, int* after) const
{
    // A cut = one clip ending exactly where another starts, on the same video track
    bool found = false;
    for (int j = 0; j < m_clips.size(); ++j) {
        const TimelineClip& b = m_clips[j];
        if (b.kind != TimelineClip::Kind::Media || m_tracks[b.track].kind != TimelineTrack::Kind::Video)
            continue;
        for (int i = 0; i < m_clips.size(); ++i) {
            const TimelineClip& a = m_clips[i];
            if (i == j || a.track != b.track || std::abs(a.end() - b.start) > 0.01)
                continue;
            if (!found || std::abs(b.start - sec) < std::abs(*cut - sec)) {
                *cut = b.start;
                if (before)
                    *before = i;
                if (after)
                    *after = j;
                found = true;
            }
        }
    }
    return found;
}

bool TimelineWidget::transitionPair(const TimelineClip& block, int* before, int* after) const
{
    // The clip starting where the block starts, overlapping one that ends where the block ends
    for (int j = 0; j < m_clips.size(); ++j) {
        const TimelineClip& b = m_clips[j];
        if (b.kind != TimelineClip::Kind::Media || m_tracks[b.track].kind != TimelineTrack::Kind::Video
            || std::abs(b.start - block.start) > 0.01)
            continue;
        for (int i = 0; i < m_clips.size(); ++i) {
            const TimelineClip& a = m_clips[i];
            if (i == j || a.track != b.track || a.start >= b.start - 0.001 || std::abs(a.end() - block.end()) > 0.01)
                continue;
            if (before)
                *before = i;
            if (after)
                *after = j;
            return true;
        }
    }
    return false;
}

void TimelineWidget::slideAfter(int track, double from, double delta, int skip)
{
    // Everything on that track from `from` on, the sound detached from it, and the FX blocks above,
    // so titles and effects stay over the bits of video they belong to
    QSet<int> moving;
    for (int i = 0; i < m_clips.size(); ++i)
        if (m_clips[i].track == track && m_clips[i].start >= from - 1e-6)
            moving.insert(i);
    for (int i : QSet<int>(moving))
        for (int p : partnersOf(i))
            moving.insert(p);
    for (int i = 0; i < m_clips.size(); ++i)
        if (m_tracks[m_clips[i].track].kind == TimelineTrack::Kind::Fx && m_clips[i].start >= from - 1e-6)
            moving.insert(i);
    moving.remove(skip);
    for (int i : moving)
        m_clips[i].start = std::max(0.0, m_clips[i].start + delta);
}

bool TimelineWidget::applyTransition(TimelineClip block, double near, int wantedTrack)
{
    // Like Filmora: the clip after the cut slides back to overlap the one before by the
    // transition's length, and the two blend across that overlap
    double cut = 0;
    int a = -1, b = -1;
    if (!nearestCut(near, &cut, &a, &b))
        return false;
    double length = std::min({ block.duration, m_clips[a].duration - 0.1, m_clips[b].duration - 0.1 });
    if (length < 0.1)
        return false; // clips too short to overlap
    const int track = m_clips[b].track;
    slideAfter(track, cut, -length, -1);
    block.start = cut - length;
    block.duration = length;

    // An FX track with room for it (the one asked for, or else the one nearest the video), or a new one
    int best = -1;
    for (int i = 0; i < m_tracks.size(); ++i) {
        if (m_tracks[i].kind != TimelineTrack::Kind::Fx || freeStart(m_clips, i, block.start, length) != block.start)
            continue;
        bool better = best < 0 || (wantedTrack >= 0 ? std::abs(i - wantedTrack) < std::abs(best - wantedTrack) : i > best);
        if (better)
            best = i;
    }
    block.track = best >= 0 ? best : insertTrack(TimelineTrack::Kind::Fx, -1);
    m_clips << block;
    return true;
}

void TimelineWidget::undoOverlap(int blockIndex)
{
    // Taking a transition away: slide the clip after it back to where it was
    int a = -1, b = -1;
    if (!transitionPair(m_clips[blockIndex], &a, &b))
        return;
    double overlap = m_clips[a].end() - m_clips[b].start;
    slideAfter(m_clips[b].track, m_clips[b].start, overlap, blockIndex);
}

void TimelineWidget::resizeTransition(int blockIndex, double length)
{
    int a = -1, b = -1;
    TimelineClip& block = m_clips[blockIndex];
    if (!transitionPair(block, &a, &b)) {
        block.duration = std::max(0.1, length);
        return;
    }
    // How long the clips were before they overlapped, so the overlap can't swallow either one
    double overlap = m_clips[a].end() - m_clips[b].start;
    double aLength = m_clips[a].duration, bLength = m_clips[b].duration;
    length = std::clamp(length, 0.1, std::min(aLength, bLength) - 0.1);
    double change = length - overlap;
    slideAfter(m_clips[b].track, m_clips[b].start, -change, blockIndex);
    block.start -= change;
    block.duration = length;
}

bool TimelineWidget::placeTransition(TimelineClip& t, int wantedTrack, double near) const
{
    // Just a preview of where a dragged transition would go (the real thing happens on drop)
    double cut = 0;
    int a = -1, b = -1;
    if (!nearestCut(near, &cut, &a, &b) || !cutNear(near))
        return false;
    double length = std::min({ t.duration, m_clips[a].duration - 0.1, m_clips[b].duration - 0.1 });
    if (length < 0.1)
        return false;
    t.start = cut - length;
    t.duration = length;
    t.track = -1;
    for (int i = 0; i < m_tracks.size(); ++i)
        if (m_tracks[i].kind == TimelineTrack::Kind::Fx
            && (t.track < 0 || std::abs(i - wantedTrack) < std::abs(t.track - wantedTrack)))
            t.track = i;
    return t.track >= 0;
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

QList<TimelineClip> TimelineWidget::clipsFromMime(const QMimeData* mime)
{
    QList<TimelineClip> clips;
    if (!mime->hasFormat(MediaBin::MimeType))
        return clips;
    QByteArray data = mime->data(MediaBin::MimeType);
    QDataStream in(&data, QIODevice::ReadOnly);
    qint32 count = 0;
    in >> count;
    for (int n = 0; n < count && in.status() == QDataStream::Ok; ++n) {
        TimelineClip clip;
        in >> clip;
        clips << clip;
    }
    return clips;
}

QStringList TimelineWidget::filesFromMime(const QMimeData* mime)
{
    QStringList files;
    for (const QUrl& url : mime->urls())
        if (url.isLocalFile())
            files << url.toLocalFile();
    return files;
}

QList<TimelineClip> TimelineWidget::layoutDrop(const QList<TimelineClip>& clips, const QPoint& pos) const
{
    QList<TimelineClip> placed;
    QList<TimelineClip> all = m_clips;
    int wantedTrack = trackAt(pos.y());
    double start = std::max(0.0, xToSec(pos.x()));

    for (TimelineClip clip : clips) {
        if (clip.isTransition() && placeTransition(clip, wantedTrack, start)) {
            // (just the preview: on drop, the clips slide into an overlap for it)
            all << clip;
            placed << clip;
            continue;
        }
        clip.track = pickTrack(wantedTrack, clip);
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
        int track = pickTrack(wanted, clip);
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
            lo = std::max(lo, c.start - c.in / c.speed); // (the file runs `speed` times faster than the timeline)
        double hi = c.end() - MinClipSeconds;
        t = std::max(lo, std::min(t, hi));

        double delta = t - c.start;
        if (!c.isStill())
            c.in += delta * c.speed;
        c.start = t;
        c.duration -= delta;
    } else {
        // Can't go past the next clip, or past the end of the file
        double lo = c.start + MinClipSeconds;
        double hi = neighbourAfter(m_selected);
        if (!c.isStill())
            hi = std::min(hi, c.start + (c.sourceDuration - c.in) / c.speed);
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
        m_clips[i].speed = me.speed;
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
        right.in = left.isStill() ? 0.0 : left.in + (m_playhead - left.start) * left.speed;
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
    if (me.kind != TimelineClip::Kind::Media)
        return out;
    for (int i = 0; i < m_clips.size(); ++i) {
        const TimelineClip& c = m_clips[i];
        if (i != index && c.path == me.path && std::abs(c.start - me.start) < 1e-6
            && std::abs(c.in - me.in) < 1e-6 && std::abs(c.duration - me.duration) < 1e-6 && c.speed == me.speed)
            out << i;
    }
    return out;
}

void TimelineWidget::deleteSelected(bool closeGap)
{
    if (m_selected < 0)
        return;
    pushUndo(m_clips);

    if (m_clips[m_selected].isTransition()) {
        // Slide the clip after it back out of the overlap, then drop the block
        undoOverlap(m_selected);
        m_clips.removeAt(m_selected);
        select(-1);
        changed();
        return;
    }

    QList<int> doomed = partnersOf(m_selected);
    std::sort(doomed.begin(), doomed.end(), std::greater<int>()); // back to front so indexes stay valid
    for (int i : doomed) {
        TimelineClip gone = m_clips.takeAt(i);
        if (!closeGap)
            continue;
        // Slide everything after it on the same track left, so there's no hole to fix by hand.
        // Cutting footage also slides the titles, effects and transitions above it, so they stay
        // lined up with the bits of video they belong to.
        bool footage = m_tracks[gone.track].kind == TimelineTrack::Kind::Video;
        for (TimelineClip& c : m_clips) {
            bool fx = footage && m_tracks[c.track].kind == TimelineTrack::Kind::Fx;
            if ((c.track == gone.track || fx) && c.start >= gone.end() - 1e-6)
                c.start = std::max(0.0, c.start - gone.duration);
        }
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
    m_redo << Snapshot { m_clips, m_tracks };
    Snapshot s = m_undo.takeLast();
    m_clips = s.clips;
    m_tracks = s.tracks;
    select(-1);
    changed();
}

void TimelineWidget::redo()
{
    if (m_redo.isEmpty())
        return;
    m_undo << Snapshot { m_clips, m_tracks };
    Snapshot s = m_redo.takeLast();
    m_clips = s.clips;
    m_tracks = s.tracks;
    select(-1);
    changed();
}

void TimelineWidget::pushUndo(const QList<TimelineClip>& state)
{
    m_undo << Snapshot { state, m_tracks };
    if (m_undo.size() > MaxUndo)
        m_undo.removeFirst();
    m_redo.clear();
}

void TimelineWidget::changed()
{
    invalidate();
    emit clipsChanged();
}

// ---- Zoom & scroll ----

void TimelineWidget::zoomToFit()
{
    double total = duration() > 0 ? duration() : 60.0;
    double visible = std::max(100, width() - HeaderWidth - 30);
    m_pixelsPerSecond = std::clamp(visible / total, MinZoom, MaxZoom);
    m_scrollX = 0;
    invalidate();
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
    invalidate();
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
    // Clips (media, titles, effects, transitions) from the panels, or files from the file manager
    const QMimeData* mime = event->mimeData();
    if (mime->hasFormat(MediaBin::MimeType) || !filesFromMime(mime).isEmpty())
        event->acceptProposedAction();
}

void TimelineWidget::dragMoveEvent(QDragMoveEvent* event)
{
    if (!filesFromMime(event->mimeData()).isEmpty()) {
        event->acceptProposedAction(); // we don't know how long they are until they're imported
        return;
    }
    m_ghosts = layoutDrop(clipsFromMime(event->mimeData()), event->position().toPoint());
    if (m_ghosts.isEmpty())
        event->ignore();
    else
        event->acceptProposedAction();
    invalidate();
}

void TimelineWidget::dragLeaveEvent(QDragLeaveEvent*)
{
    m_ghosts.clear();
    invalidate();
}

void TimelineWidget::dropEvent(QDropEvent* event)
{
    m_ghosts.clear();
    QPoint pos = event->position().toPoint();
    QStringList files = filesFromMime(event->mimeData());
    event->acceptProposedAction();
    if (!files.isEmpty())
        emit filesDropped(files, pos); // the main window imports them, then calls dropClips
    else
        dropClips(clipsFromMime(event->mimeData()), pos);
    invalidate();
}

void TimelineWidget::dropClips(const QList<TimelineClip>& clips, const QPoint& pos)
{
    const Snapshot before { m_clips, m_tracks };
    bool wasEmpty = m_clips.isEmpty();

    // Transitions slide the clips into an overlap; everything else lands where it was dropped
    QList<TimelineClip> others;
    bool added = false;
    for (const TimelineClip& c : clips) {
        if (c.isTransition() && cutNear(xToSec(pos.x())))
            added |= applyTransition(c, std::max(0.0, xToSec(pos.x())), trackAt(pos.y()));
        else
            others << c;
    }
    QList<TimelineClip> placed = layoutDrop(others, pos);
    m_clips << placed;
    if (!added && placed.isEmpty())
        return;

    m_undo << before;
    m_redo.clear();
    select(int(m_clips.size()) - 1);
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
    if (hit >= 0 && m_clips[hit].isTransition() && isOnCut(m_clips[hit])) {
        select(hit); // locked to its overlap: change its length in Properties instead
    } else if (hit >= 0) {
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
    invalidate();
}

void TimelineWidget::mouseMoveEvent(QMouseEvent* event)
{
    QPoint pos = event->position().toPoint();

    switch (m_drag) {
    case Drag::None: {
        // Just hovering: show the resize arrows over clip edges
        int hit = pos.y() >= RulerHeight && pos.x() >= HeaderWidth ? clipAt(pos) : -1;
        if (hit >= 0 && !(m_clips[hit].isTransition() && isOnCut(m_clips[hit])) && edgeAt(hit, pos) != Edge::None)
            setCursor(Qt::SizeHorCursor);
        else
            unsetCursor();
        return;
    }
    case Drag::Playhead:
        m_playhead = std::max(0.0, xToSec(pos.x()));
        emit playheadMoved(m_playhead);
        update(); // only the playhead moved
        return;
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
    invalidate();
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
        invalidate();
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
    invalidate();
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
    syncScrollBar();
}

void TimelineWidget::contextMenuEvent(QContextMenuEvent* event)
{
    QPoint pos = event->pos();
    if (pos.x() < HeaderWidth) {
        // Right-click a track name: add or remove tracks
        int track = trackAt(pos.y());
        QMenu menu(this);
        using K = TimelineTrack::Kind;
        menu.addAction("Add an FX track (titles, effects, transitions)", this, [this] { addTrack(K::Fx); });
        menu.addAction("Add a video track", this, [this] { addTrack(K::Video); });
        menu.addAction("Add an audio track", this, [this] { addTrack(K::Audio); });
        if (track >= 0) {
            menu.addSeparator();
            QAction* remove = menu.addAction(QString("Remove \"%1\"").arg(m_tracks[track].name), this,
                                             [this, track] { removeTrack(track); });
            bool empty = std::none_of(m_clips.begin(), m_clips.end(), [track](const TimelineClip& c) { return c.track == track; });
            remove->setEnabled(empty);
            if (!empty)
                remove->setText(remove->text() + " (move its clips off first)");
        }
        menu.exec(event->globalPos());
        return;
    }
    int hit = pos.y() >= RulerHeight ? clipAt(pos) : -1;
    double at = std::max(0.0, xToSec(pos.x()));

    QMenu menu(this);
    if (hit >= 0) {
        select(hit);
        invalidate();
        const TimelineClip& clip = m_clips[hit];
        menu.addAction("Split here", this, [this, at] {
            setPlayhead(at);
            emit playheadMoved(at);
            splitAtPlayhead();
        });
        menu.addAction("Delete", this, [this] { deleteSelected(); });
        menu.addAction("Delete, leaving a gap", this, [this] { deleteSelectedKeepGap(); });
        if (clip.showsVideo() && clip.playsAudio())
            menu.addAction("Detach audio", this, &TimelineWidget::detachAudio);
        QMenu* transitions = menu.addMenu("Transition into this clip");
        for (int type = 0; type < VE_TRANSITION_COUNT; ++type) {
            QAction* a = transitions->addAction(transitionName(type), this, [this, hit, type] { setTransition(hit, type); });
            a->setCheckable(true);
            a->setChecked(clip.transition == type);
        }
        if (!hasClipBefore(hit))
            transitions->setToolTip("Needs another clip ending right where this one starts");
        menu.addSeparator();
    }
    menu.addAction("Add a title here", this, [this, at] {
        setPlayhead(at);
        emit playheadMoved(at);
        addTitle();
    });
    menu.exec(event->globalPos());
}

// ---- Drawing ----

void TimelineWidget::paintEvent(QPaintEvent*)
{
    // Everything except the playhead gets drawn once and kept, so during playback
    // (when only the playhead moves) we're just copying a picture, not redrawing the lot
    const qreal dpr = devicePixelRatioF();
    if (m_cacheDirty || m_cache.size() != size() * dpr) {
        m_cache = QPixmap(size() * dpr);
        m_cache.setDevicePixelRatio(dpr);
        QPainter c(&m_cache);
        c.setRenderHint(QPainter::Antialiasing);
        c.fillRect(rect(), Background);
        drawTracks(c);
        c.save();
        c.setClipRect(contentRect());
        for (int i = 0; i < m_clips.size(); ++i)
            drawClip(c, m_clips[i], i == m_selected, false);
        for (const TimelineClip& ghost : m_ghosts)
            drawClip(c, ghost, false, true);
        c.restore();
        drawRuler(c);
        m_cacheDirty = false;
    }

    QPainter p(this);
    p.drawPixmap(0, 0, m_cache);
    p.setRenderHint(QPainter::Antialiasing);
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
        bool fx = m_tracks[i].kind == TimelineTrack::Kind::Fx;
        p.fillRect(t, fx ? FxTrackBg : TrackBg);
        p.fillRect(0, t.top(), HeaderWidth, t.height(), HeaderBg);
        // A coloured strip on the left says what kind of track it is
        QColor strip = fx ? EffectClip : m_tracks[i].kind == TimelineTrack::Kind::Video ? VideoClip : AudioClip;
        p.fillRect(0, t.top(), 3, t.height(), strip);

        p.setPen(Lines);
        p.drawLine(0, t.bottom(), width(), t.bottom());
        p.drawLine(HeaderWidth - 1, t.top(), HeaderWidth - 1, t.bottom());

        p.setPen(Dim);
        p.drawText(QRect(10, t.top(), HeaderWidth - 10, t.height()), Qt::AlignVCenter, m_tracks[i].name);
    }

    int bottom = tracksBottom();
    if (m_clips.isEmpty() && bottom < height()) {
        p.setPen(Dim);
        p.drawText(QRect(HeaderWidth, bottom, width() - HeaderWidth, height() - bottom),
                   Qt::AlignCenter, "Drag clips here (or double-click them in Media) to start editing.\nRight-click a track name to add more tracks.");
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
        if (g >= secPerTile * clip.speed * 0.5) {
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

        double src = clip.isStill() ? 0.0 : std::floor((clip.in + i * secPerTile * clip.speed) / q) * q;
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
    QColor fill = clip.isTitle()        ? TitleClip
                  : clip.isEffect()     ? EffectClip
                  : clip.isTransition() ? TransitionClip
                  : clip.audioOnly()    ? AudioClip
                                        : VideoClip;
    p.fillPath(shape, fill);
    p.setClipPath(shape, Qt::IntersectClip);

    if (clip.isTransition()) {
        // On a cut: a bow-tie, the two clips crossing. Otherwise a ramp up (in), down (out),
        // or a V (out and back in on the spot).
        p.setPen(QPen(QColor(255, 255, 255, 150), 1.2));
        int part = ghost ? -1 : transitionPart(clip);
        if (part == VE_PART_IN) {
            p.drawLine(r.bottomLeft(), r.topRight());
        } else if (part == VE_PART_OUT) {
            p.drawLine(r.topLeft(), r.bottomRight());
        } else if (part == VE_PART_THROUGH) {
            const QPointF vee[] = { r.topLeft(), QPointF(r.center().x(), r.bottom()), r.topRight() };
            p.drawPolyline(vee, 3);
        } else {
            const QPointF bowtie[] = { r.topLeft(), r.bottomRight(), r.topRight(), r.bottomLeft(), r.topLeft() };
            p.drawPolyline(bowtie, 5);
        }
    } else if (clip.isTitle()) {
        // Show the actual words, so you can tell titles apart at a glance
        QFont big = p.font();
        big.setPixelSize(int(r.height() * 0.38));
        big.setBold(true);
        p.setFont(big);
        p.setPen(QColor(255, 255, 255, 200));
        double left = std::max(r.left(), double(HeaderWidth)) + 8;
        p.drawText(QRectF(left, r.top() + 14, r.right() - left, r.height() - 14),
                   Qt::AlignLeft | Qt::AlignVCenter, clip.title.text.simplified());
    } else if (clip.showsVideo() && m_tracks[clip.track].kind != TimelineTrack::Kind::Audio) {
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
    QString icon = clip.isTitle()       ? QStringLiteral("T  ")
                   : clip.isEffect()  ? QStringLiteral("fx  ")
                   : clip.audioOnly() ? QStringLiteral("♪ ")
                                      : QString();
    QString muted = clip.hasAudio && !clip.audioOn && clip.showsVideo() ? QStringLiteral("  ·  no sound") : QString();
    if (clip.isTransition() && !ghost) {
        int part = transitionPart(clip);
        muted = part == VE_PART_IN ? QStringLiteral("  ·  in") : part == VE_PART_OUT ? QStringLiteral("  ·  out")
              : part == VE_PART_THROUGH ? QStringLiteral("  ·  on the spot") : QString();
    }
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
        double src = clip.in + (x - r.left()) * secPerPixel * clip.speed;
        int a = int(src * WaveformCache::PerSecond);
        int b = std::max(a + 1, int((src + secPerPixel * clip.speed) * WaveformCache::PerSecond));
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
