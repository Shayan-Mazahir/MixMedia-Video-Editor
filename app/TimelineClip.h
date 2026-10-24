// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#pragma once

#include <ve/engine.h>

#include <QColor>
#include <QDataStream>
#include <QList>
#include <QPixmap>
#include <QString>

#include <algorithm>
#include <array>

// What a title looks like. Titles get drawn into a see-through picture that sits on top of the video.
struct TitleStyle {
    QString text = "Your text here";
    double size = 8.0;            // as a % of the picture height, so it looks the same at any resolution
    QColor color = Qt::white;
    double y = 0.82;              // where it sits, 0 = top, 1 = bottom
    bool box = true;              // a box behind the text so it's readable on anything
    bool bold = true;

    QString font;                 // empty = the usual one
    bool italic = false;
    int align = 1;                // 0 = left, 1 = centre, 2 = right
    double x = 0.5;               // across: 0 = left edge, 1 = right edge (the side it's aligned to)
    double spacing = 0.0;         // extra space between letters, % of normal (can be negative)
    double outline = 0.0;         // outline thickness, % of the text size (0 = none)
    QColor outlineColor = Qt::black;
    bool shadow = false;
    QColor shadowColor = QColor(0, 0, 0, 170);
    double shadowDistance = 6.0;  // % of the text size (0 + a bright colour = a glow)
    double shadowSoftness = 20.0; // 0..100
    QColor boxColor = QColor(0, 0, 0, 160);

    // Subtitles only: TikTok-style, a few words at a time with the one being said lit up
    bool wordByWord = false;
    QColor highlight = QColor(0xff, 0xd6, 0x00);
    int wordsAtOnce = 3;

    bool operator==(const TitleStyle&) const = default;

    // How subtitle tracks look until you change them
    static TitleStyle subtitles()
    {
        TitleStyle s;
        s.text.clear();
        s.size = 5.0;
        s.y = 0.88;
        s.bold = false;
        s.boxColor = QColor(0, 0, 0, 170);
        return s;
    }
};

// One word of a subtitle, and when it's said (seconds from the start of the line)
struct WordTime {
    QString word;
    double start = 0.0, end = 0.0;
    bool operator==(const WordTime&) const = default;
};

inline QDataStream& operator<<(QDataStream& out, const TitleStyle& t)
{
    return out << t.text << t.size << t.color << t.y << t.box << t.bold << t.font << t.italic << t.align << t.x
               << t.spacing << t.outline << t.outlineColor << t.shadow << t.shadowColor << t.shadowDistance
               << t.shadowSoftness << t.boxColor << t.wordByWord << t.highlight << t.wordsAtOnce;
}
inline QDataStream& operator>>(QDataStream& in, TitleStyle& t)
{
    return in >> t.text >> t.size >> t.color >> t.y >> t.box >> t.bold >> t.font >> t.italic >> t.align >> t.x
           >> t.spacing >> t.outline >> t.outlineColor >> t.shadow >> t.shadowColor >> t.shadowDistance
           >> t.shadowSoftness >> t.boxColor >> t.wordByWord >> t.highlight >> t.wordsAtOnce;
}

// A spot on the timeline you've marked (press M), with an optional note
struct TimelineMarker {
    double time = 0.0;
    QString note;
    bool operator==(const TimelineMarker&) const = default;
};

// A row on the timeline. FX tracks hold titles, effects and transitions; subtitle tracks hold
// subtitle lines (and how they all look); the rest hold footage and sound.
struct TimelineTrack {
    enum class Kind { Fx, Video, Audio, Subtitles };
    QString name;
    Kind kind = Kind::Video;
    TitleStyle style = TitleStyle::subtitles(); // (subtitle tracks only)

    bool operator==(const TimelineTrack&) const = default;
};

// A setting's value at one moment of a clip. Settings glide from one to the next.
struct Keyframe {
    double time = 0.0; // seconds from the start of the clip
    float value = 0.0f;
    int ease = VE_EASE_SMOOTH; // how it gets here from the keyframe before

    bool operator==(const Keyframe&) const = default;
};

struct TimelineClip {
    // Media = a file. Title = text. Effect = a filter over everything below it.
    // Transition = a little block sitting on a cut, blending the clips either side.
    // Subtitle = one line of subtitles (its text is title.text), on a subtitle track.
    enum class Kind { Media, Title, Effect, Transition, Subtitle };

    Kind kind = Kind::Media;
    QString path;
    QString name;
    double start = 0.0;          // where it sits on the timeline (seconds)
    double in = 0.0;             // where in the file it starts playing from
    double duration = 0.0;       // how long it plays for
    double sourceDuration = 0.0; // full length of the file, 0 = a picture (any length you like)
    int track = 0;
    bool hasVideo = false;
    bool hasAudio = false;
    int width = 0;
    int height = 0;
    double fps = 0.0;
    QPixmap thumb;

    // Things you can tweak per clip
    bool videoOn = true;  // false = only use this clip's sound
    bool audioOn = true;  // false = its sound was detached (or muted)
    float volume = 1.0f;
    double fadeIn = 0.0;  // seconds
    double fadeOut = 0.0;
    double speed = 1.0;   // 2 = twice as fast, 0.5 = slow motion

    // Picture-in-picture
    float opacity = 1.0f;
    float scale = 1.0f;   // 1 = fills the frame
    float posX = 0.0f;    // shift as a fraction of the frame, 0 = centred
    float posY = 0.0f;

    // Crop (fraction cut off each edge), turning and mirroring
    float cropLeft = 0, cropRight = 0, cropTop = 0, cropBottom = 0;
    float rotation = 0;   // degrees, clockwise
    bool flipH = false, flipV = false;
    bool fill = false;    // cover the whole frame (cutting off what hangs over) instead of fitting inside it
    bool blurFill = false; // the bars around it show a blurred copy of it instead of black
    // Its frame (handy for a facecam): shape, border and shadow
    int shape = 0;           // VE_SHAPE_*
    float cornerRadius = 0.15f, borderWidth = 0.0f, shadow = 0.0f;
    QColor borderColor = Qt::white;
    float shake = 0.0f; // 0..1, camera shake

    bool reverse = false; // plays backwards
    bool freeze = false;  // holds one frame (the one at `in`) the whole time

    // Green screen: everything close to keyColor turns see-through
    bool chromaKey = false;
    QColor keyColor = QColor(0, 200, 60);
    float keyStrength = 0.4f, keySoftness = 0.2f, keySpill = 0.5f;
    float keyEdges = 0.3f; // pull in and soften the edge
    bool keyMatte = false; // showing what's see-through, while tuning (never saved, never exported)

    // Sound tools
    bool keepPitch = true;    // speeding up/slowing down keeps voices sounding normal
    float denoise = 0.0f;     // 0..1, background noise taken out
    bool duck = false;        // turns itself down while there's talking on other clips (for music)
    float duckAmount = 0.7f;  // 0..1, how far down

    // Effects (all 0 = untouched)
    int look = 0;         // VE_LOOK_*
    QString lutPath;      // a look from a .cube file ("" = none)
    float lutStrength = 1.0f; // 0..1, how much of it
    float brightness = 0, contrast = 0, saturation = 0, temperature = 0;
    float blur = 0, sharpen = 0, vignette = 0;

    // Transition from the clip ending right where this one starts (VE_TRANSITION_*)
    int transition = 0;
    double transitionDuration = 1.0;
    // How it arrives and leaves (VE_ANIM_*)
    int animIn = 0, animOut = 0;
    double animInDuration = 0.5, animOutDuration = 0.5;

    TitleStyle title;     // Kind::Title, and the text of a Kind::Subtitle line
    bool ownStyle = false; // subtitles: this line has its own look instead of the track's
    QList<WordTime> words; // subtitles: when each word is said (empty = spread evenly)

    // Keyframes for size, position, opacity, rotation, volume and shake (indexed by VE_KEY_*), in time order.
    // A setting with none just uses its normal value above.
    std::array<QList<Keyframe>, VE_KEY_COUNT> keys;

    bool hasKeys() const
    {
        return std::any_of(keys.begin(), keys.end(), [](const QList<Keyframe>& k) { return !k.isEmpty(); });
    }
    // The setting's normal (not keyframed) value
    float baseValue(int param) const
    {
        switch (param) {
        case VE_KEY_SIZE: return scale;
        case VE_KEY_POS_X: return posX;
        case VE_KEY_POS_Y: return posY;
        case VE_KEY_OPACITY: return opacity;
        case VE_KEY_ROTATION: return rotation;
        case VE_KEY_VOLUME: return volume;
        case VE_KEY_SHAKE: return shake;
        default: return 0.0f;
        }
    }
    void setBaseValue(int param, float v)
    {
        switch (param) {
        case VE_KEY_SIZE: scale = v; break;
        case VE_KEY_POS_X: posX = v; break;
        case VE_KEY_POS_Y: posY = v; break;
        case VE_KEY_OPACITY: opacity = v; break;
        case VE_KEY_ROTATION: rotation = v; break;
        case VE_KEY_VOLUME: volume = v; break;
        case VE_KEY_SHAKE: shake = v; break;
        default: break;
        }
    }
    // What a setting is at `local` seconds into the clip (same easing as the engine)
    float valueAt(int param, double local) const
    {
        const QList<Keyframe>& k = keys[size_t(param)];
        if (k.isEmpty())
            return baseValue(param);
        if (local <= k.first().time)
            return k.first().value;
        if (local >= k.last().time)
            return k.last().value;
        int i = 1;
        while (k[i].time < local)
            ++i;
        const Keyframe& a = k[i - 1];
        const Keyframe& b = k[i];
        double p = b.time > a.time ? (local - a.time) / (b.time - a.time) : 1.0;
        return float(a.value + (b.value - a.value) * ve_ease(b.ease, p));
    }
    // Adds a keyframe, or changes the one that's already there (within half a frame).
    // `ease` -1 = leave how it arrives alone (smooth for a new one).
    void setKey(int param, double local, float value, int ease = -1)
    {
        QList<Keyframe>& k = keys[size_t(param)];
        for (Keyframe& f : k)
            if (std::abs(f.time - local) < KeySnap) {
                f.value = value;
                if (ease >= 0)
                    f.ease = ease;
                return;
            }
        auto at = std::lower_bound(k.begin(), k.end(), local, [](const Keyframe& f, double t) { return f.time < t; });
        k.insert(at, Keyframe { local, value, std::max(ease, 0) });
    }
    // Moves every keyframe along (when the start of the clip gets trimmed or split off)
    void shiftKeys(double by)
    {
        for (QList<Keyframe>& k : keys)
            for (Keyframe& f : k)
                f.time += by;
    }
    static constexpr double KeySnap = 1.0 / 60; // closer than this counts as "the same moment"

    double end() const { return start + duration; }
    bool isTitle() const { return kind == Kind::Title; }
    bool isEffect() const { return kind == Kind::Effect; }
    bool isTransition() const { return kind == Kind::Transition; }
    bool isSubtitle() const { return kind == Kind::Subtitle; }
    bool belongsOnFx() const { return isTitle() || isEffect() || isTransition(); }
    bool showsVideo() const { return (hasVideo || isTitle()) && videoOn; }
    bool playsAudio() const { return hasAudio && audioOn; }
    bool audioOnly() const { return playsAudio() && !showsVideo(); }
    bool isStill() const { return sourceDuration <= 0.0 || freeze; } // a picture, or a frozen frame
    // Seconds of the file this clip uses (more than its duration when sped up)
    double sourceSpan() const { return duration * speed; }
    bool hasEffects() const
    {
        return look != 0 || brightness != 0 || contrast != 0 || saturation != 0 || temperature != 0
               || blur != 0 || sharpen != 0 || vignette != 0;
    }
    bool isMoved() const { return opacity < 1.0f || scale != 1.0f || posX != 0.0f || posY != 0.0f; }
};

// So clips can be packed up for drag & drop
inline QDataStream& operator<<(QDataStream& out, const TimelineClip& c)
{
    out << int(c.kind) << c.path << c.name << c.start << c.in << c.duration << c.sourceDuration << c.track
        << c.hasVideo << c.hasAudio << c.width << c.height << c.fps << c.thumb
        << c.videoOn << c.audioOn << c.volume << c.fadeIn << c.fadeOut
        << c.title
        << c.speed << c.opacity << c.scale << c.posX << c.posY << c.look << c.brightness << c.contrast
        << c.saturation << c.temperature << c.blur << c.sharpen << c.vignette
        << c.transition << c.transitionDuration << c.animIn << c.animInDuration << c.animOut << c.animOutDuration
        << c.cropLeft << c.cropRight << c.cropTop << c.cropBottom << c.rotation << c.flipH << c.flipV << c.fill << c.blurFill << c.lutPath << c.lutStrength
        << c.shape << c.cornerRadius << c.borderWidth << c.shadow << c.borderColor << c.shake
        << c.reverse << c.freeze << c.chromaKey << c.keyColor << c.keyStrength << c.keySoftness << c.keySpill << c.keyEdges
        << c.keepPitch << c.denoise << c.duck << c.duckAmount << c.ownStyle << qint32(c.words.size());
    for (const WordTime& w : c.words)
        out << w.word << w.start << w.end;
    for (const QList<Keyframe>& k : c.keys) {
        out << qint32(k.size());
        for (const Keyframe& f : k)
            out << f.time << f.value << qint32(f.ease);
    }
    return out;
}

inline QDataStream& operator>>(QDataStream& in, TimelineClip& c)
{
    int kind = 0;
    in >> kind >> c.path >> c.name >> c.start >> c.in >> c.duration >> c.sourceDuration >> c.track
       >> c.hasVideo >> c.hasAudio >> c.width >> c.height >> c.fps >> c.thumb
       >> c.videoOn >> c.audioOn >> c.volume >> c.fadeIn >> c.fadeOut
       >> c.title
       >> c.speed >> c.opacity >> c.scale >> c.posX >> c.posY >> c.look >> c.brightness >> c.contrast
       >> c.saturation >> c.temperature >> c.blur >> c.sharpen >> c.vignette
       >> c.transition >> c.transitionDuration >> c.animIn >> c.animInDuration >> c.animOut >> c.animOutDuration
       >> c.cropLeft >> c.cropRight >> c.cropTop >> c.cropBottom >> c.rotation >> c.flipH >> c.flipV >> c.fill >> c.blurFill >> c.lutPath >> c.lutStrength
       >> c.shape >> c.cornerRadius >> c.borderWidth >> c.shadow >> c.borderColor >> c.shake
       >> c.reverse >> c.freeze >> c.chromaKey >> c.keyColor >> c.keyStrength >> c.keySoftness >> c.keySpill >> c.keyEdges
       >> c.keepPitch >> c.denoise >> c.duck >> c.duckAmount >> c.ownStyle;
    qint32 words = 0;
    in >> words;
    c.words.clear();
    for (qint32 i = 0; i < words && in.status() == QDataStream::Ok; ++i) {
        WordTime w;
        in >> w.word >> w.start >> w.end;
        c.words << w;
    }
    for (QList<Keyframe>& k : c.keys) {
        qint32 n = 0;
        in >> n;
        k.clear();
        for (qint32 i = 0; i < n && in.status() == QDataStream::Ok; ++i) {
            Keyframe f;
            qint32 ease = 0;
            in >> f.time >> f.value >> ease;
            f.ease = ease;
            k << f;
        }
    }
    c.kind = TimelineClip::Kind(kind);
    return in;
}
