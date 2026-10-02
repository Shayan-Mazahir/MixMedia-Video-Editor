// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#pragma once

#include <QColor>
#include <QDataStream>
#include <QPixmap>
#include <QString>

// What a title looks like. Titles get drawn into a see-through picture that sits on top of the video.
struct TitleStyle {
    QString text = "Your text here";
    double size = 8.0;            // as a % of the picture height, so it looks the same at any resolution
    QColor color = Qt::white;
    double y = 0.82;              // where it sits, 0 = top, 1 = bottom
    bool box = true;              // dark box behind the text so it's readable on anything
    bool bold = true;

    bool operator==(const TitleStyle&) const = default;
};

struct TimelineClip {
    enum class Kind { Media, Title };

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

    // Effects (all 0 = untouched)
    int look = 0;         // VE_LOOK_*
    float brightness = 0, contrast = 0, saturation = 0, temperature = 0;
    float blur = 0, sharpen = 0, vignette = 0;

    TitleStyle title;     // only for Kind::Title

    double end() const { return start + duration; }
    bool isTitle() const { return kind == Kind::Title; }
    bool showsVideo() const { return (hasVideo || isTitle()) && videoOn; }
    bool playsAudio() const { return hasAudio && audioOn; }
    bool audioOnly() const { return playsAudio() && !showsVideo(); }
    bool isStill() const { return sourceDuration <= 0.0; }
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
    return out << int(c.kind) << c.path << c.name << c.start << c.in << c.duration << c.sourceDuration << c.track
               << c.hasVideo << c.hasAudio << c.width << c.height << c.fps << c.thumb
               << c.videoOn << c.audioOn << c.volume << c.fadeIn << c.fadeOut
               << c.title.text << c.title.size << c.title.color << c.title.y << c.title.box << c.title.bold
               << c.speed << c.opacity << c.scale << c.posX << c.posY << c.look << c.brightness << c.contrast
               << c.saturation << c.temperature << c.blur << c.sharpen << c.vignette;
}

inline QDataStream& operator>>(QDataStream& in, TimelineClip& c)
{
    int kind = 0;
    in >> kind >> c.path >> c.name >> c.start >> c.in >> c.duration >> c.sourceDuration >> c.track
       >> c.hasVideo >> c.hasAudio >> c.width >> c.height >> c.fps >> c.thumb
       >> c.videoOn >> c.audioOn >> c.volume >> c.fadeIn >> c.fadeOut
       >> c.title.text >> c.title.size >> c.title.color >> c.title.y >> c.title.box >> c.title.bold
       >> c.speed >> c.opacity >> c.scale >> c.posX >> c.posY >> c.look >> c.brightness >> c.contrast
       >> c.saturation >> c.temperature >> c.blur >> c.sharpen >> c.vignette;
    c.kind = TimelineClip::Kind(kind);
    return in;
}
